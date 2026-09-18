use std::fs;
use std::fs::OpenOptions;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::time::Instant;

use anyhow::{anyhow, Context, Result};
use serde::Serialize;
use tokio::process::{Child, Command};
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout, Duration};

use crate::paths::{data_dir, ensure_data_dir};

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct OtaChannel {
    pub name: String,
    pub source: String,
}

#[derive(Clone, Debug, Serialize)]
pub struct OtaStatus {
    pub enabled: bool,
    pub adapter: bool,
    pub channels: Vec<OtaChannel>,
    pub error: Option<String>,
}

/// What a retune must do before `dvbv5-zap` may touch the stick.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SwitchPlan {
    pub stop_previous: bool,
    pub channel: String,
}

/// Config file paths used for OTA. `ZAPPE_OTA_CHANNELS` overrides defaults (colon-separated).
pub fn channel_config_paths() -> Vec<PathBuf> {
    if let Ok(raw) = std::env::var("ZAPPE_OTA_CHANNELS") {
        return raw
            .split(':')
            .map(|s| s.trim())
            .filter(|s| !s.is_empty())
            .map(expand_user_path)
            .collect();
    }
    default_channel_paths()
}

/// Expand a leading `~/` so appliance env like `ZAPPE_OTA_CHANNELS=~/tv/channels.conf`
/// still finds the file. Bare `~` other users (`~glaet/…`) are left alone.
fn expand_user_path(raw: &str) -> PathBuf {
    let trimmed = raw.trim();
    if trimmed == "~" {
        return home_dir().unwrap_or_else(|| PathBuf::from("~"));
    }
    if let Some(rest) = trimmed.strip_prefix("~/") {
        if let Some(home) = home_dir() {
            return home.join(rest);
        }
    }
    PathBuf::from(trimmed)
}

fn home_dir() -> Option<PathBuf> {
    dirs::home_dir().or_else(|| std::env::var_os("HOME").map(PathBuf::from))
}

fn default_channel_paths() -> Vec<PathBuf> {
    let mut paths = Vec::new();
    if let Some(home) = home_dir() {
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
    let data = data_dir().join("channels.conf");
    if data.exists() {
        paths.push(data);
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

pub fn ota_status() -> OtaStatus {
    let adapter = dvb_adapter_present();
    let enabled = ota_available();
    match list_channels() {
        Ok(channels) => {
            let error = if !enabled {
                Some(conf_missing_message())
            } else if channels.is_empty() {
                Some(
                    "channels.conf has no playable TV aberta channels (1Seg-only rows are dropped)."
                        .into(),
                )
            } else if !adapter {
                Some(adapter_missing_message())
            } else {
                None
            };
            OtaStatus {
                enabled,
                adapter,
                channels,
                error,
            }
        }
        Err(err) => OtaStatus {
            enabled,
            adapter,
            channels: Vec::new(),
            error: Some(err.to_string()),
        },
    }
}

fn expected_conf_paths() -> Vec<String> {
    let mut paths = Vec::new();
    if let Some(home) = home_dir() {
        paths.push(home.join("tv/channels.conf").display().to_string());
    }
    if let Some(config) = dirs::config_dir() {
        paths.push(
            config
                .join("zappe")
                .join("channels.conf")
                .display()
                .to_string(),
        );
    }
    paths.push(data_dir().join("channels.conf").display().to_string());
    paths
}

fn conf_missing_message() -> String {
    format!(
        "channels.conf not found. Put the Brazilian TV aberta list in {} (or set ZAPPE_OTA_CHANNELS).",
        expected_conf_paths().join(" or ")
    )
}

fn adapter_missing_message() -> String {
    format!(
        "no DVB tuner at {} (missing frontend/demux/dvr). Plug in the stick and check `ls /dev/dvb/`.",
        dvb_adapter_path().display()
    )
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

fn names_in_conf(conf: &Path) -> Result<Vec<String>> {
    let text = fs::read_to_string(conf).with_context(|| format!("read {}", conf.display()))?;
    Ok(parse_channel_names(&text))
}

fn is_1seg(name: &str) -> bool {
    let l = fold_name(name);
    l.contains("1seg") || l.contains("1-seg") || l.contains("one seg") || l.contains("oneseg")
}

fn fold_name(name: &str) -> String {
    name.chars()
        .map(|c| match c {
            'Á' | 'À' | 'Ã' | 'Â' | 'á' | 'à' | 'ã' | 'â' => 'a',
            'É' | 'Ê' | 'é' | 'ê' => 'e',
            'Í' | 'í' => 'i',
            'Ó' | 'Ô' | 'Õ' | 'ó' | 'ô' | 'õ' => 'o',
            'Ú' | 'Ü' | 'ú' | 'ü' => 'u',
            'Ç' | 'ç' => 'c',
            other => other.to_ascii_lowercase(),
        })
        .collect::<String>()
        .split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
}

/// Normalize for duplicate detection (strip quality suffixes and Brazilian `TV`/`Rede`).
fn channel_base_key(name: &str) -> String {
    let mut s = fold_name(name);
    for token in [
        " hdtv", " hd", " fhd", " sd", " uhd", " 4k", " (hd)", " [hd]",
    ] {
        if let Some(idx) = s.rfind(token) {
            if idx + token.len() == s.len() {
                s = s[..idx].trim().to_string();
            }
        }
    }
    for prefix in ["tv ", "rede "] {
        if let Some(rest) = s.strip_prefix(prefix) {
            s = rest.trim().to_string();
        }
    }
    s.trim().to_string()
}

fn hd_score(name: &str) -> i32 {
    let l = fold_name(name);
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
        sa.cmp(&sb).then_with(|| fold_name(a).cmp(&fold_name(b)))
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
            .then_with(|| fold_name(a).cmp(&fold_name(b)))
    });
    out
}

/// Map a guide / voice label onto a real `channels.conf` section.
///
/// Exact and case-insensitive hits win, then Brazilian aliases (`Globo` →
/// `Globo HD` / `TV GLOBO`) among the HD-first filtered list.
pub fn resolve_channel_name(requested: &str, names: &[String]) -> Option<String> {
    let req = requested.trim();
    if req.is_empty() {
        return None;
    }

    // Match the same HD-first list the Canais shelf shows. Raw section
    // names like `TV GLOBO` must not bypass `filter_and_sort_channels`.
    let playable = filter_and_sort_channels(names.to_vec());
    if playable.is_empty() {
        return None;
    }
    if let Some(hit) = playable.iter().find(|n| n.as_str() == req) {
        return Some(hit.clone());
    }

    let req_fold = fold_name(req);
    if let Some(hit) = playable.iter().find(|n| fold_name(n) == req_fold) {
        return Some(hit.clone());
    }

    let req_key = channel_base_key(req);
    let mut ranked: Vec<&String> = playable
        .iter()
        .filter(|n| {
            let key = channel_base_key(n);
            let folded = fold_name(n);
            key == req_key
                || (!req_key.is_empty() && req_key.len() >= 3 && key.contains(&req_key))
                || (!req_fold.is_empty() && folded.contains(&req_fold))
                || (!req_fold.is_empty() && req_fold.contains(&folded))
        })
        .collect();

    if ranked.is_empty() {
        return None;
    }

    ranked.sort_by(|a, b| {
        let a_key = channel_base_key(a);
        let b_key = channel_base_key(b);
        let a_fold = fold_name(a);
        let b_fold = fold_name(b);
        score_name_hit(&a_key, &a_fold, &req_key, &req_fold)
            .cmp(&score_name_hit(&b_key, &b_fold, &req_key, &req_fold))
            .then_with(|| hd_score(a).cmp(&hd_score(b)))
            .then_with(|| a_fold.cmp(&b_fold))
    });
    Some(ranked[0].clone())
}

fn score_name_hit(key: &str, folded: &str, req_key: &str, req_fold: &str) -> i32 {
    if key == req_key {
        return 0;
    }
    if folded == req_fold {
        return 1;
    }
    if folded.starts_with(req_fold) || key.starts_with(req_key) {
        return 2;
    }
    3
}

/// Always stop a live session before starting the next channel.
pub fn plan_switch(active: Option<&str>, requested: &str, names: &[String]) -> Result<SwitchPlan> {
    let channel = resolve_channel_name(requested, names).ok_or_else(|| {
        anyhow!("channel '{requested}' not in channels.conf (no Globo/Record/SBT-style match)")
    })?;
    Ok(SwitchPlan {
        stop_previous: active.is_some(),
        channel,
    })
}

#[derive(Default)]
struct OtaInner {
    pipeline_child: Option<Child>,
    active_channel: Option<String>,
    active_conf: Option<PathBuf>,
    mpv_pid: Option<u32>,
}

pub struct OtaSession {
    inner: Mutex<OtaInner>,
    fake: bool,
}

impl OtaSession {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(OtaInner::default()),
            fake: false,
        }
    }

    #[cfg(test)]
    fn new_fake() -> Self {
        Self {
            inner: Mutex::new(OtaInner::default()),
            fake: true,
        }
    }

    pub fn latest_mpv_pid(&self) -> Option<u32> {
        self.inner
            .try_lock()
            .ok()
            .and_then(|g| g.mpv_pid)
            .or_else(crate::wm::find_mpv_pid)
    }

    pub async fn is_playing(&self) -> bool {
        self.inner.lock().await.pipeline_child.is_some()
    }

    pub async fn play(&self, channel: &str, conf: &Path) -> Result<()> {
        let mut inner = self.inner.lock().await;
        stop_inner(&mut inner, self.fake).await;

        if !conf.exists() {
            return Err(anyhow!(
                "can't open {channel}: channels.conf not found at {}. {}",
                conf.display(),
                conf_missing_message()
            ));
        }

        let names = names_in_conf(conf)?;
        let plan = plan_switch(None, channel, &names)?;
        let channel = plan.channel.as_str();

        if !self.fake {
            if let Err(err) = ensure_dvb_adapter() {
                return Err(anyhow!("can't open {channel}: {err}"));
            }
            if which("dvbv5-zap").is_none() {
                return Err(anyhow!(
                    "dvbv5-zap not found on PATH (install v4l-utils / dvb5-tools on Arch/Omarchy)"
                ));
            }
            if which("mpv").is_none() {
                return Err(anyhow!("mpv not found on PATH"));
            }
        }

        let log_path = if self.fake {
            std::env::temp_dir().join(format!("zappe-ota-fake-{}.log", std::process::id()))
        } else {
            ota_log_path()?
        };
        append_log_header(&log_path, channel, conf)?;
        let log_file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&log_path)
            .with_context(|| format!("open {}", log_path.display()))?;

        let script = if self.fake {
            "exec /bin/sleep 5".into()
        } else {
            pipeline_script(&conf.display().to_string(), channel)
        };
        let mut child = spawn_pipeline(&script, log_file)?;

        match wait_pipeline_ready(&mut child, &log_path, channel, !self.fake).await {
            Ok(mpv_pid) => {
                inner.pipeline_child = Some(child);
                inner.active_channel = Some(channel.to_string());
                inner.active_conf = Some(conf.to_path_buf());
                inner.mpv_pid = mpv_pid;
                Ok(())
            }
            Err(err) => {
                kill_pipeline(&mut child).await;
                Err(err)
            }
        }
    }

    pub async fn stop(&self) {
        let mut inner = self.inner.lock().await;
        stop_inner(&mut inner, self.fake).await;
    }

    #[cfg(test)]
    async fn active_channel(&self) -> Option<String> {
        self.inner.lock().await.active_channel.clone()
    }

    #[cfg(test)]
    async fn pipeline_pid(&self) -> Option<u32> {
        self.inner
            .lock()
            .await
            .pipeline_child
            .as_ref()
            .and_then(|c| c.id())
    }
}

async fn stop_inner(inner: &mut OtaInner, fake: bool) {
    if let Some(mut child) = inner.pipeline_child.take() {
        kill_pipeline(&mut child).await;
    }
    if !fake {
        sweep_stray_ota_processes(inner.mpv_pid);
    }
    inner.active_channel = None;
    inner.active_conf = None;
    inner.mpv_pid = None;
    // Frontend/demux nodes stay busy for a beat after SIGKILL.
    let settle = if fake {
        Duration::from_millis(20)
    } else {
        Duration::from_millis(200)
    };
    sleep(settle).await;
}

fn spawn_pipeline(script: &str, log_file: std::fs::File) -> Result<Child> {
    let mut cmd = Command::new("sh");
    cmd.arg("-c")
        .arg(script)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(log_file)
        .kill_on_drop(true);

    #[cfg(unix)]
    {
        cmd.process_group(0);
    }

    cmd.spawn().context("spawn OTA pipeline")
}

async fn kill_pipeline(child: &mut Child) {
    if let Some(pid) = child.id() {
        signal_group(pid, "-TERM");
        if timeout(Duration::from_millis(250), child.wait())
            .await
            .is_ok()
        {
            return;
        }
        signal_group(pid, "-KILL");
    }
    let _ = child.start_kill();
    let _ = timeout(Duration::from_millis(800), child.wait()).await;
}

fn signal_group(pid: u32, sig: &str) {
    let _ = std::process::Command::new("kill")
        .args([sig, &format!("-{pid}")])
        .status();
}

fn sweep_stray_ota_processes(mpv_pid: Option<u32>) {
    if let Some(pid) = mpv_pid {
        let _ = std::process::Command::new("kill")
            .args(["-TERM", &pid.to_string()])
            .status();
    }
    for pid in pgrep_exact("dvbv5-zap") {
        let _ = std::process::Command::new("kill")
            .args(["-TERM", &pid.to_string()])
            .status();
    }
    for pid in pgrep_line_contains("mpv --hwdec=no --vo=gpu --demuxer-lavf-format=mpegts") {
        let _ = std::process::Command::new("kill")
            .args(["-TERM", &pid.to_string()])
            .status();
    }
}

fn pgrep_exact(name: &str) -> Vec<u32> {
    let output = std::process::Command::new("pgrep")
        .args(["-x", name])
        .output();
    match output {
        Ok(out) if out.status.success() => String::from_utf8_lossy(&out.stdout)
            .lines()
            .filter_map(|l| l.trim().parse().ok())
            .collect(),
        _ => Vec::new(),
    }
}

fn pgrep_line_contains(needle: &str) -> Vec<u32> {
    let output = std::process::Command::new("pgrep")
        .args(["-f", needle])
        .output();
    match output {
        Ok(out) if out.status.success() => String::from_utf8_lossy(&out.stdout)
            .lines()
            .filter_map(|l| l.trim().parse().ok())
            .collect(),
        _ => Vec::new(),
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
    Err(anyhow!("{}", adapter_missing_message()))
}

fn shell_single_quote(value: &str) -> String {
    value.replace('\'', "'\\''")
}

fn pipeline_script(conf: &str, channel: &str) -> String {
    let conf_s = shell_single_quote(conf);
    let channel_s = shell_single_quote(channel);
    format!(
        "set -o pipefail; dvbv5-zap -a 0 -c '{conf_s}' -p '{channel_s}' -r -o - | \
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
    look_for_mpv: bool,
) -> Result<Option<u32>> {
    if !look_for_mpv {
        if let Some(status) = child.try_wait().context("poll OTA pipeline")? {
            let tail = read_log_tail(log_path, 400);
            return Err(anyhow!(
                "OTA failed for '{channel}' (pipeline exited {status}).{tail}"
            ));
        }
        return Ok(child.id());
    }

    let deadline = Instant::now() + Duration::from_millis(2000);
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

    // Still alive after the early-exit window: keep the tune. Brazilian ISDB-T
    // lock can outlast a short wait; killing a live zap leaves the stick stuck.
    let group = child.id();
    let mpv_pid = if look_for_mpv {
        find_mpv_in_group(group)
            .or_else(crate::wm::find_mpv_pid)
            .or(group)
    } else {
        group
    };
    Ok(mpv_pid)
}

fn find_mpv_in_group(leader: Option<u32>) -> Option<u32> {
    let pgid = leader?;
    let output = std::process::Command::new("pgrep")
        .args(["-g", &pgid.to_string(), "mpv"])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    String::from_utf8_lossy(&output.stdout)
        .lines()
        .filter_map(|l| l.trim().parse().ok())
        .next()
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

fn pid_alive(pid: u32) -> bool {
    std::path::Path::new(&format!("/proc/{pid}")).exists()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    fn br_conf_text() -> &'static str {
        r#"
# ISDB-T São Paulo-style scan
[TV GLOBO]
DELIVERY_SYSTEM = ISDBT
[Globo HD]
DELIVERY_SYSTEM = ISDBT
[Globo 1Seg]
DELIVERY_SYSTEM = ISDBT
[Record]
DELIVERY_SYSTEM = ISDBT
[Record HD]
DELIVERY_SYSTEM = ISDBT
[SBT]
DELIVERY_SYSTEM = ISDBT
[SBT HD]
DELIVERY_SYSTEM = ISDBT
[Band]
DELIVERY_SYSTEM = ISDBT
[TV Cultura]
DELIVERY_SYSTEM = ISDBT
BBC ONE:8:...
"#
    }

    fn write_temp_conf(tag: &str, text: &str) -> PathBuf {
        let path =
            std::env::temp_dir().join(format!("zappe-ota-{tag}-{}.conf", std::process::id()));
        fs::write(&path, text).expect("write temp channels.conf");
        path
    }

    #[test]
    fn expands_tilde_ota_paths() {
        let home = home_dir().expect("home dir");
        assert_eq!(
            expand_user_path("~/tv/channels.conf"),
            home.join("tv/channels.conf")
        );
        assert_eq!(
            expand_user_path("/abs/tv/channels.conf"),
            PathBuf::from("/abs/tv/channels.conf")
        );
    }

    #[test]
    fn parse_dvbv5_sections() {
        let names = parse_channel_names(br_conf_text());
        assert!(names.contains(&"TV GLOBO".to_string()));
        assert!(names.contains(&"Globo HD".to_string()));
        assert!(names.contains(&"SBT".to_string()));
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
        assert!(!out.contains(&"SBT".to_string()));
    }

    #[test]
    fn filter_collapses_brazilian_tv_rede_aliases() {
        let out = filter_and_sort_channels(parse_channel_names(br_conf_text()));
        assert!(!out.iter().any(|n| n.contains("1Seg")));
        assert!(out.contains(&"Globo HD".to_string()));
        assert!(!out.contains(&"TV GLOBO".to_string()));
        assert!(out.contains(&"Record HD".to_string()));
        assert!(!out.contains(&"Record".to_string()));
        assert!(out.contains(&"SBT HD".to_string()));
        assert!(out.contains(&"Band".to_string()));
        assert!(out.contains(&"TV Cultura".to_string()));
    }

    #[test]
    fn resolve_keeps_globo_record_sbt_working() {
        let names = parse_channel_names(br_conf_text());
        assert_eq!(
            resolve_channel_name("Globo", &names).as_deref(),
            Some("Globo HD")
        );
        assert_eq!(
            resolve_channel_name("globo", &names).as_deref(),
            Some("Globo HD")
        );
        assert_eq!(
            resolve_channel_name("TV GLOBO", &names).as_deref(),
            Some("Globo HD")
        );
        assert_eq!(
            resolve_channel_name("Record", &names).as_deref(),
            Some("Record HD")
        );
        assert_eq!(
            resolve_channel_name("SBT", &names).as_deref(),
            Some("SBT HD")
        );
        assert_eq!(
            resolve_channel_name("cultura", &names).as_deref(),
            Some("TV Cultura")
        );
        assert_eq!(resolve_channel_name("nope", &names), None);
    }

    #[test]
    fn plan_switch_always_stops_a_live_session() {
        let names = parse_channel_names(br_conf_text());
        let first = plan_switch(None, "Globo", &names).unwrap();
        assert!(!first.stop_previous);
        assert_eq!(first.channel, "Globo HD");

        let next = plan_switch(Some("Globo HD"), "Record", &names).unwrap();
        assert!(next.stop_previous);
        assert_eq!(next.channel, "Record HD");

        let same = plan_switch(Some("Record HD"), "Record HD", &names).unwrap();
        assert!(same.stop_previous);
        assert_eq!(same.channel, "Record HD");
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

    #[test]
    fn conf_and_adapter_errors_name_the_fix() {
        let conf = conf_missing_message();
        assert!(conf.contains("channels.conf"), "{conf}");
        assert!(conf.contains("ZAPPE_OTA_CHANNELS"), "{conf}");
        let adapter = adapter_missing_message();
        assert!(adapter.contains("/dev/dvb/adapter0"), "{adapter}");
    }

    #[tokio::test]
    async fn play_errors_before_hiding_when_adapter_missing() {
        if dvb_adapter_present() {
            return;
        }
        let conf = write_temp_conf("missing-adapter", "[Globo HD]\n");
        let session = OtaSession::new();
        let err = session.play("Globo", &conf).await.unwrap_err().to_string();
        let _ = fs::remove_file(&conf);
        assert!(err.contains("Globo"), "{err}");
        assert!(err.contains("/dev/dvb/adapter0"), "{err}");
        assert!(!session.is_playing().await);
    }

    #[tokio::test]
    async fn play_errors_when_conf_missing() {
        let session = OtaSession::new_fake();
        let err = session
            .play("Globo", Path::new("/tmp/zappe-missing-channels.conf"))
            .await
            .unwrap_err()
            .to_string();
        assert!(err.contains("Globo"), "{err}");
        assert!(err.contains("channels.conf"), "{err}");
        assert!(!session.is_playing().await);
    }

    #[tokio::test]
    async fn retune_stops_previous_child_then_starts_next() {
        let conf = write_temp_conf("switch", br_conf_text());
        let session = OtaSession::new_fake();
        session.play("Globo", &conf).await.expect("play Globo");
        assert_eq!(session.active_channel().await.as_deref(), Some("Globo HD"));
        let pid_a = session.pipeline_pid().await.expect("pid A");
        assert!(pid_alive(pid_a), "first pipeline should be live");

        session.play("Record", &conf).await.expect("play Record");
        assert_eq!(session.active_channel().await.as_deref(), Some("Record HD"));
        let pid_b = session.pipeline_pid().await.expect("pid B");
        assert_ne!(pid_a, pid_b);
        assert!(!pid_alive(pid_a), "channel A pipeline must be gone");
        assert!(pid_alive(pid_b), "channel B pipeline must be live");

        session.stop().await;
        assert!(!session.is_playing().await);
        assert!(!pid_alive(pid_b), "stop must reap the live pipeline");
        let _ = fs::remove_file(&conf);
    }

    #[tokio::test(flavor = "multi_thread", worker_threads = 2)]
    async fn concurrent_play_serializes_to_one_pipeline() {
        let conf = write_temp_conf("race", br_conf_text());
        let session = Arc::new(OtaSession::new_fake());
        let a = {
            let s = Arc::clone(&session);
            let conf = conf.clone();
            tokio::spawn(async move { s.play("Globo", &conf).await })
        };
        let b = {
            let s = Arc::clone(&session);
            let conf = conf.clone();
            tokio::spawn(async move { s.play("SBT", &conf).await })
        };
        let (ra, rb) = tokio::join!(a, b);
        ra.expect("join A").expect("play A");
        rb.expect("join B").expect("play B");

        assert!(session.is_playing().await);
        let channel = session.active_channel().await.expect("active");
        assert!(
            channel == "Globo HD" || channel == "SBT HD",
            "unexpected winner {channel}"
        );
        let pid = session.pipeline_pid().await.expect("one child");
        assert!(pid_alive(pid));
        session.stop().await;
        assert!(!pid_alive(pid));
        let _ = fs::remove_file(&conf);
    }
}
