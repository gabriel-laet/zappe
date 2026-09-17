//! Terrestrial OTA (ISDB-Tb / DVB) playback via `dvbv5-zap` and `mpv`.
//!
//! No scraping, no DRM — local RF through the Linux DVB stack (`smsusb` / `smsdvb`).

use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::mpsc::{self, Receiver, Sender};
use std::thread;
use std::time::Duration;

use anyhow::{Context, Result};

/// Parsed channel name from a dvbv5 `channels.conf` (`[Name]` sections).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OtaChannel {
    pub name: String,
}

#[derive(Clone, Debug)]
pub struct OtaConfig {
    pub channels_conf: PathBuf,
    pub adapter: u32,
    pub dvbv5_zap: PathBuf,
    pub mpv: PathBuf,
    pub zap_log: PathBuf,
}

impl OtaConfig {
    pub fn resolve(channels_override: Option<PathBuf>) -> Option<Self> {
        let channels_conf = channels_override
            .or_else(|| {
                std::env::var("ZAPPE_OTA_CHANNELS")
                    .ok()
                    .map(PathBuf::from)
            })
            .or_else(default_channels_conf)?;
        if !channels_conf.is_file() {
            log::info!(
                "OTA: no channels.conf at {} (set ZAPPE_OTA_CHANNELS)",
                channels_conf.display()
            );
            return None;
        }
        Some(Self {
            channels_conf,
            adapter: std::env::var("ZAPPE_DVB_ADAPTER")
                .ok()
                .and_then(|s| s.parse().ok())
                .unwrap_or(0),
            dvbv5_zap: std::env::var("ZAPPE_DVBV5_ZAP")
                .map(PathBuf::from)
                .unwrap_or_else(|_| PathBuf::from("dvbv5-zap")),
            mpv: std::env::var("ZAPPE_MPV")
                .map(PathBuf::from)
                .unwrap_or_else(|_| PathBuf::from("mpv")),
            zap_log: std::env::var("ZAPPE_OTA_ZAP_LOG")
                .map(PathBuf::from)
                .unwrap_or_else(|_| PathBuf::from("/tmp/zappe-zap.log")),
        })
    }
}

fn default_channels_conf() -> Option<PathBuf> {
    dirs::home_dir().map(|h| h.join("tv/channels.conf"))
}

/// Read channel names from a dvbv5-style `channels.conf`.
pub fn parse_channels_conf(path: &Path) -> Result<Vec<OtaChannel>> {
    let file = std::fs::File::open(path)
        .with_context(|| format!("open channels.conf {}", path.display()))?;
    let reader = BufReader::new(file);
    let mut out = Vec::new();
    for line in reader.lines() {
        let line = line?;
        let line = line.trim();
        if !line.starts_with('[') || !line.ends_with(']') || line.len() < 3 {
            continue;
        }
        let name = line[1..line.len() - 1].trim();
        if name.is_empty() {
            continue;
        }
        out.push(OtaChannel {
            name: name.to_string(),
        });
    }
    Ok(out)
}

#[derive(Debug)]
enum OtaCmd {
    Play { channel: String },
    Stop,
    Shutdown,
}

#[derive(Clone, Debug)]
pub enum OtaEvent {
    Playing { channel: String },
    Stopped,
    Failed(String),
}

struct Playback {
    zap: Child,
    mpv: Child,
}

impl Playback {
    fn stop(&mut self) {
        let _ = self.mpv.kill();
        let _ = self.mpv.wait();
        let _ = self.zap.kill();
        let _ = self.zap.wait();
    }
}

pub struct OtaHandle {
    pub enabled: bool,
    tx: Option<Sender<OtaCmd>>,
    events: Receiver<OtaEvent>,
}

impl OtaHandle {
    pub fn start(config: Option<OtaConfig>) -> Self {
        let Some(config) = config else {
            let (_tx, rx) = mpsc::channel();
            return Self {
                enabled: false,
                tx: None,
                events: rx,
            };
        };

        log::info!(
            "OTA: enabled — {} (adapter {})",
            config.channels_conf.display(),
            config.adapter
        );

        let (cmd_tx, cmd_rx) = mpsc::channel();
        let (ev_tx, ev_rx) = mpsc::channel();
        thread::Builder::new()
            .name("zappe-ota".into())
            .spawn(move || ota_worker(config, cmd_rx, ev_tx))
            .expect("ota thread");

        Self {
            enabled: true,
            tx: Some(cmd_tx),
            events: ev_rx,
        }
    }

    fn send(&self, cmd: OtaCmd) {
        if let Some(tx) = &self.tx {
            let _ = tx.send(cmd);
        }
    }

    pub fn play(&self, channel: &str) {
        self.send(OtaCmd::Play {
            channel: channel.to_string(),
        });
    }

    pub fn stop(&self) {
        self.send(OtaCmd::Stop);
    }

    pub fn shutdown(&self) {
        self.send(OtaCmd::Shutdown);
    }

    pub fn poll_events(&self) -> Vec<OtaEvent> {
        let mut out = Vec::new();
        while let Ok(ev) = self.events.try_recv() {
            out.push(ev);
        }
        out
    }
}

fn ota_worker(config: OtaConfig, cmd_rx: Receiver<OtaCmd>, ev_tx: Sender<OtaEvent>) {
    let mut playback: Option<Playback> = None;

    loop {
        let timeout = if playback.is_some() {
            Duration::from_millis(250)
        } else {
            Duration::from_secs(3600)
        };

        match cmd_rx.recv_timeout(timeout) {
            Ok(OtaCmd::Shutdown) => {
                if let Some(mut p) = playback.take() {
                    p.stop();
                }
                break;
            }
            Ok(OtaCmd::Stop) => {
                if let Some(mut p) = playback.take() {
                    p.stop();
                    let _ = ev_tx.send(OtaEvent::Stopped);
                }
            }
            Ok(OtaCmd::Play { channel }) => {
                if let Some(mut p) = playback.take() {
                    p.stop();
                }
                match spawn_pipeline(&config, &channel) {
                    Ok(p) => {
                        let _ = ev_tx.send(OtaEvent::Playing {
                            channel: channel.clone(),
                        });
                        playback = Some(p);
                    }
                    Err(err) => {
                        let _ = ev_tx.send(OtaEvent::Failed(format!("{err:#}")));
                    }
                }
            }
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        }

        if let Some(p) = &mut playback {
            if let Ok(Some(status)) = p.mpv.try_wait() {
                log::warn!("mpv exited: {status}");
                let _ = p.zap.kill();
                let _ = p.zap.wait();
                playback = None;
                let _ = ev_tx.send(OtaEvent::Stopped);
            } else if let Ok(Some(status)) = p.zap.try_wait() {
                log::warn!("dvbv5-zap exited: {status}");
                let _ = p.mpv.kill();
                let _ = p.mpv.wait();
                playback = None;
                let _ = ev_tx.send(OtaEvent::Stopped);
            }
        }
    }
}

fn spawn_pipeline(config: &OtaConfig, channel: &str) -> Result<Playback> {
    let log_file = std::fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(&config.zap_log)
        .with_context(|| format!("open zap log {}", config.zap_log.display()))?;

    let mut zap = Command::new(&config.dvbv5_zap)
        .arg("-a")
        .arg(config.adapter.to_string())
        .arg("-c")
        .arg(&config.channels_conf)
        .arg("-p")
        .arg(channel)
        .arg("-r")
        .arg("-o")
        .arg("-")
        .stderr(log_file)
        .stdout(Stdio::piped())
        .spawn()
        .with_context(|| format!("spawn {}", config.dvbv5_zap.display()))?;

    let zap_stdout = zap
        .stdout
        .take()
        .context("dvbv5-zap stdout pipe")?;

    let mpv = Command::new(&config.mpv)
        .args([
            "--hwdec=no",
            "--vo=gpu",
            "--demuxer-lavf-format=mpegts",
            "--demuxer-lavf-analyzeduration=5",
            "--cache=yes",
            "--fullscreen",
            "--title",
            &format!("Zappe OTA — {channel}"),
        ])
        .stdin(zap_stdout)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .with_context(|| format!("spawn {}", config.mpv.display()))?;

    log::info!("OTA: zapping {channel} (see {})", config.zap_log.display());
    Ok(Playback { zap, mpv })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn parse_dvbv5_channel_sections() {
        let dir = std::env::temp_dir().join("zappe-ota-test");
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("channels.conf");
        let mut f = std::fs::File::create(&path).unwrap();
        writeln!(
            f,
            "[Globo HD]
DELIVERY_SYSTEM = ISDBT
SERVICE_ID = 59200
VIDEO_PID = 273

[SBT HD]
DELIVERY_SYSTEM = ISDBT
"
        )
        .unwrap();
        let ch = parse_channels_conf(&path).unwrap();
        assert_eq!(ch.len(), 2);
        assert_eq!(ch[0].name, "Globo HD");
        assert_eq!(ch[1].name, "SBT HD");
    }
}
