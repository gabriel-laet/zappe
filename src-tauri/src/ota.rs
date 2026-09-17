use std::fs;
use std::path::{Path, PathBuf};
use std::process::Stdio;

use anyhow::{anyhow, Context, Result};
use serde::Serialize;
use tokio::process::{Child, Command};
use tokio::sync::Mutex;

#[derive(Clone, Debug, Serialize)]
pub struct OtaChannel {
    pub name: String,
    pub source: String,
}

pub fn channel_config_paths() -> Vec<PathBuf> {
    std::env::var("ZAPPE_OTA_CHANNELS")
        .ok()
        .map(|raw| {
            raw.split(':')
                .map(|s| s.trim())
                .filter(|s| !s.is_empty())
                .map(PathBuf::from)
                .collect()
        })
        .unwrap_or_default()
}

pub fn ota_available() -> bool {
    !channel_config_paths().is_empty()
}

pub fn list_channels() -> Result<Vec<OtaChannel>> {
    let mut out = Vec::new();
    for path in channel_config_paths() {
        if !path.exists() {
            continue;
        }
        let text = fs::read_to_string(&path)
            .with_context(|| format!("read {}", path.display()))?;
        for name in parse_channel_names(&text) {
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
    names.sort();
    names.dedup();
    names
}

pub struct OtaSession {
    pipeline_child: Mutex<Option<Child>>,
    active_channel: Mutex<Option<String>>,
    active_conf: Mutex<Option<PathBuf>>,
}

impl OtaSession {
    pub fn new() -> Self {
        Self {
            pipeline_child: Mutex::new(None),
            active_channel: Mutex::new(None),
            active_conf: Mutex::new(None),
        }
    }

    pub async fn is_playing(&self) -> bool {
        self.pipeline_child.lock().await.is_some()
    }

    pub async fn play(&self, channel: &str, conf: &Path) -> Result<()> {
        self.stop().await;

        if which("dvbv5-zap").is_none() {
            return Err(anyhow!(
                "dvbv5-zap not found on PATH (install dvb5-tools on Arch/Omarchy)"
            ));
        }
        if which("mpv").is_none() {
            return Err(anyhow!("mpv not found on PATH"));
        }

        let conf_s = conf.display().to_string();
        let channel_s = channel.replace('\'', "'\\''");
        let script = format!(
            "dvbv5-zap -a 0 -c '{conf_s}' -p '{channel_s}' -r -o - | mpv --hwdec=no --demuxer-lavf-format=mpegts --fs --no-terminal -"
        );

        let child = Command::new("sh")
            .arg("-c")
            .arg(&script)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .kill_on_drop(true)
            .spawn()
            .context("spawn OTA pipeline")?;

        *self.pipeline_child.lock().await = Some(child);
        *self.active_channel.lock().await = Some(channel.to_string());
        *self.active_conf.lock().await = Some(conf.to_path_buf());

        Ok(())
    }

    pub async fn stop(&self) {
        if let Some(mut child) = self.pipeline_child.lock().await.take() {
            let _ = child.start_kill();
            let _ = child.wait().await;
        }
        *self.active_channel.lock().await = None;
        *self.active_conf.lock().await = None;
    }

    pub async fn mpv_pid(&self) -> Option<u32> {
        self.pipeline_child
            .lock()
            .await
            .as_ref()
            .and_then(|c| c.id())
    }
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
}
