use std::fs;
use std::fs::OpenOptions;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::time::Instant;

use anyhow::{anyhow, Context, Result};
use serde::Serialize;
use tokio::process::{Child, Command};
use tokio::sync::Mutex;
use tokio::time::{sleep, Duration};

use crate::paths::{data_dir, ensure_data_dir};

#[derive(Clone, Debug, Serialize)]
pub struct OtaChannel {
    pub name: String,
    pub source: String,
}

/// Config file paths used for OTA. `ZAPPE_OTA_CHANNELS` overrides defaults (colon-separated).
pub fn channel_config_paths() -> Vec<PathBuf> {
    if let Ok(raw) = std::env::var("ZAPPE_OTA_CHANNELS") {
        return raw
            .split(':')
            .map(|s| s.trim())
            .filter(|s| !s.is_empty())
            .map(PathBuf::from)
            .collect();
    }
    default_channel_paths()
}

fn default_channel_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(home) = dirs::home_dir() {
        let tv = home.join("tv").join("channels.conf");
        if tv.exists() {
            paths.push(tv);
        }
    }
    if let Some(config) = dirs::config_dir() {
        let zappe = config.join("zappe").join("channels.conf");
        if zappe.exists() {
            paths.push(zappe);
        }
    }
    paths
}

pub fn ota_available() -> bool {
    channel_config_paths().iter().any(|p| p.exists())
}

pub fn list_channels() -> Result<Vec<OtaChannel>> {
    let mut out = Vec::new();
    for path in channel_config_paths() {
        if !path.exists() {
            continue;
        }
        let text = fs::read_to_string(&path).with_context(|| format!("read {}", path.display()))?;
        for name in filter_and_sort_channels(parse_channel_names(&text)) {
            out.push(OtaChannel {
                name,
                source: path.display().to_string(),
            });
        }
    }
    Ok(out)
}

/// dvbv5 `channels.conf`: `[Channel Name]` sections or legacy `Name:…` zap lines.
fn parse_channel_names(text: &str) -> Vec<String> {
    let mut names = Vec::new();
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') && line.len() > 2 {
            names.push(line[1..line.len() - 1].trim().to_string());
            continue;
        }
        if let Some((name, _)) = line.split_once(':') {
            let name = name.trim();
            if !name.is_empty() && !name.starts_with('[') {
                names.push(name.to_string());
            }
        }
    }
    names
}

fn is_1seg(name: &str) -> bool {
    let l = name.to_lowercase();
    l.contains("1seg") || l.contains("1-seg") || l.contains("one seg") || l.contains("oneseg")
}

/// Normalize for duplicate detection (strip quality suffixes).
fn channel_base_key(name: &str) -> String {
    let mut s = name.to_lowercase();
    for token in [
        " hdtv", " hd", " fhd", " sd", " uhd", " 4k", " (hd)", " [hd]",
    ] {
        if let Some(idx) = s.rfind(token) {
            if idx + token.len() == s.len() {
                s = s[..idx].trim().to_string();
            }
        }
    }
    s.trim().to_string()
}

fn hd_score(name: &str) -> i32 {
    let l = name.to_lowercase();
    if l.contains("hdtv") || l.contains(" fhd") || l.ends_with(" hd") || l.contains(" hd ") {
        return 0;
    }
    if l.contains("hd") {
        return 1;
    }
    if l.contains(" sd") {
        return 3;
    }
    2
}

/// Drop obvious 1Seg rows; prefer HD when multiple names map to the same service.
pub fn filter_and_sort_channels(names: Vec<String>) -> Vec<String> {
    let mut filtered: Vec<String> = names.into_iter().filter(|n| !is_1seg(n)).collect();

    filtered.sort_by(|a, b| {
        let sa = hd_score(a);
        let sb = hd_score(b);
        sa.cmp(&sb)
            .then_with(|| a.to_lowercase().cmp(&b.to_lowercase()))
    });

    let mut seen: std::collections::HashMap<String, String> = std::collections::HashMap::new();
    for name in filtered {
        let key = channel_base_key(&name);
        match seen.get(&key) {
            None => {
                seen.insert(key, name);
            }
            Some(existing) => {
                if hd_score(&name) < hd_score(existing) {
                    seen.insert(key, name);
                }
            }
        }
    }

    let mut out: Vec<String> = seen.into_values().collect();
    out.sort_by(|a, b| {
        hd_score(a)
            .cmp(&hd_score(b))
            .then_with(|| a.to_lowercase().cmp(&b.to_lowercase()))
    });
    out
}

pub struct OtaSession {
    pipeline_child: Mutex<Option<Child>>,
    active_channel: Mutex<Option<String>>,
    active_conf: Mutex<Option<PathBuf>>,
    mpv_pid: Mutex<Option<u32>>,
}

impl OtaSession {
    pub fn new() -> Self {
        Self {
            pipeline_child: Mutex::new(None),
            active_channel: Mutex::new(None),
            active_conf: Mutex::new(None),
            mpv_pid: Mutex::new(None),
        }
    }

    pub fn latest_mpv_pid(&self) -> Option<u32> {
        self.mpv_pid
            .try_lock()
            .ok()
            .and_then(|g| *g)
            .or_else(crate::wm::find_mpv_pid)
    }

    pub async fn is_playing(&self) -> bool {
        self.pipeline_child.lock().await.is_some()
    }

    pub async fn play(&self, channel: &str, conf: &Path) -> Result<()> {
        self.stop().await;

        if let Err(err) = ensure_dvb_adapter() {
            return Err(anyhow!("can't open {channel}: {err}"));
        }
        if which("dvbv5-zap").is_none() {
            return Err(anyhow!(
                "dvbv5-zap not found on PATH (install dvb5-tools on Arch/Omarchy)"
            ));
        }
        if which("mpv").is_none() {
            return Err(anyhow!("mpv not found on PATH"));
        }

        let log_path = ota_log_path()?;
        append_log_header(&log_path, channel, conf)?;
        let log_file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&log_path)
            .with_context(|| format!("open {}", log_path.display()))?;

        let script = pipeline_script(&conf.display().to_string(), channel);
        let mut child = Command::new("sh")
            .arg("-c")
            .arg(&script)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(log_file)
            .kill_on_drop(true)
            .spawn()
            .context("spawn OTA pipeline")?;

        match wait_pipeline_ready(&mut child, &log_path, channel).await {
            Ok(mpv_pid) => {
                *self.pipeline_child.lock().await = Some(child);
                *self.active_channel.lock().await = Some(channel.to_string());
                *self.active_conf.lock().await = Some(conf.to_path_buf());
                *self.mpv_pid.lock().await = mpv_pid;
                Ok(())
            }
            Err(err) => {
                let _ = child.start_kill();
                let _ = child.wait().await;
                Err(err)
            }
        }
    }

    pub async fn stop(&self) {
        if let Some(mut child) = self.pipeline_child.lock().await.take() {
            let _ = child.start_kill();
            let _ = child.wait().await;
        }
        *self.active_channel.lock().await = None;
        *self.active_conf.lock().await = None;
        *self.mpv_pid.lock().await = None;
    }

    pub async fn mpv_pid(&self) -> Option<u32> {
        self.pipeline_child
            .lock()
            .await
            .as_ref()
            .and_then(|c| c.id())
    }
}

pub fn dvb_adapter_path() -> PathBuf {
    PathBuf::from("/dev/dvb/adapter0")
}

/// True when `/dev/dvb/adapter0` or its frontend/demux/dvr nodes exist.
pub fn dvb_adapter_present() -> bool {
    let adapter = dvb_adapter_path();
    adapter.exists()
        || adapter.join("frontend0").exists()
        || adapter.join("demux0").exists()
        || adapter.join("dvr0").exists()
}

fn ensure_dvb_adapter() -> Result<PathBuf> {
    let adapter = dvb_adapter_path();
    if dvb_adapter_present() {
        return Ok(adapter);
    }
    Err(anyhow!(
        "no DVB tuner at {} (missing frontend/demux/dvr). Plug in the stick and check `ls /dev/dvb/`.",
        adapter.display()
    ))
}

fn pipeline_script(conf: &str, channel: &str) -> String {
    let channel_s = channel.replace('\'', "'\\''");
    format!(
        "set -o pipefail; dvbv5-zap -a 0 -c '{conf}' -p '{channel_s}' -r -o - | \
         mpv --hwdec=no --vo=gpu --demuxer-lavf-format=mpegts \
         --demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal -"
    )
}

fn ota_log_path() -> Result<PathBuf> {
    ensure_data_dir()?;
    Ok(data_dir().join("ota-pipeline.log"))
}

fn append_log_header(path: &Path, channel: &str, conf: &Path) -> Result<()> {
    use std::io::Write;
    let mut file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .with_context(|| format!("open {}", path.display()))?;
    writeln!(
        file,
        "\n--- zappe ota {} channel={channel} conf={} ---",
        chrono_like_now(),
        conf.display()
    )?;
    Ok(())
}

fn chrono_like_now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

fn read_log_tail(path: &Path, max_chars: usize) -> String {
    let Ok(text) = fs::read_to_string(path) else {
        return String::new();
    };
    let trimmed = text.trim();
    if trimmed.is_empty() {
        return String::new();
    }
    let tail = if trimmed.len() > max_chars {
        &trimmed[trimmed.len().saturating_sub(max_chars)..]
    } else {
        trimmed
    };
    format!(" log: {}", tail.replace('\n', " | "))
}

async fn wait_pipeline_ready(
    child: &mut Child,
    log_path: &Path,
    channel: &str,
) -> Result<Option<u32>> {
    let deadline = Instant::now() + Duration::from_millis(1200);
    loop {
        if let Some(status) = child.try_wait().context("poll OTA pipeline")? {
            let tail = read_log_tail(log_path, 400);
            return Err(anyhow!(
                "OTA failed for '{channel}' (pipeline exited {status}).{tail} Is the tuner at /dev/dvb/adapter0?"
            ));
        }
        if Instant::now() >= deadline {
            break;
        }
        sleep(Duration::from_millis(100)).await;
    }

    let mpv_pid = crate::wm::find_mpv_pid();
    if mpv_pid.is_none() {
        let tail = read_log_tail(log_path, 400);
        return Err(anyhow!(
            "OTA failed for '{channel}': mpv never started.{tail} Is the tuner at /dev/dvb/adapter0?"
        ));
    }
    Ok(mpv_pid)
}

fn which(name: &str) -> Option<PathBuf> {
    let output = std::process::Command::new("which")
        .arg(name)
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    let line = stdout.trim();
    if line.is_empty() {
        None
    } else {
        Some(PathBuf::from(line))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_dvbv5_sections() {
        let text = r#"
# comment
[France 2]
DELIVERY_SYSTEM = DVBT
[ARTE]
DELIVERY_SYSTEM = DVBT
BBC ONE:8:...
"#;
        let names = parse_channel_names(text);
        assert!(names.contains(&"France 2".to_string()));
        assert!(names.contains(&"ARTE".to_string()));
        assert!(names.contains(&"BBC ONE".to_string()));
    }

    #[test]
    fn filter_prefers_hd_and_drops_1seg() {
        let names = vec![
            "Globo SP 1Seg".into(),
            "Globo SP HD".into(),
            "Globo SP".into(),
            "SBT HD".into(),
            "SBT".into(),
        ];
        let out = filter_and_sort_channels(names);
        assert!(!out.iter().any(|n| n.contains("1Seg")));
        assert!(out.contains(&"Globo SP HD".to_string()));
        assert!(out.contains(&"SBT HD".to_string()));
        assert!(!out.contains(&"Globo SP".to_string()));
    }

    #[test]
    fn pipeline_uses_working_watch_flags_and_pipefail() {
        let script = pipeline_script("/home/me/tv/channels.conf", "Globo");
        assert!(script.contains("set -o pipefail"));
        assert!(script.contains("dvbv5-zap -a 0"));
        assert!(script.contains("--vo=gpu"));
        assert!(script.contains("--demuxer-lavf-analyzeduration=5"));
        assert!(script.contains("--cache=yes"));
        assert!(script.contains("--hwdec=no"));
        assert!(script.contains("--fs"));
        assert!(!script.contains("2>/dev/null"));
    }

    #[test]
    fn missing_adapter_message_is_actionable() {
        if dvb_adapter_present() {
            return;
        }
        let err = ensure_dvb_adapter().unwrap_err().to_string();
        assert!(err.contains("/dev/dvb/adapter0"));
        assert!(err.contains("tuner") || err.contains("DVB") || err.contains("dvr"));
    }

    #[tokio::test]
    async fn play_errors_before_hiding_when_adapter_missing() {
        if dvb_adapter_present() {
            return;
        }
        let session = OtaSession::new();
        let err = session
            .play("Globo", Path::new("/tmp/zappe-missing-channels.conf"))
            .await
            .unwrap_err()
            .to_string();
        assert!(err.contains("Globo"), "{err}");
        assert!(err.contains("/dev/dvb/adapter0"), "{err}");
        assert!(session.pipeline_child.lock().await.is_none());
    }
}
