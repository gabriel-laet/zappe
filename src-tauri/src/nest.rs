//! Player / harvest nest: `gamescope` wrapping Google Chrome.
//!
//! This is a real browser session — no CDP, no `--enable-automation`, and no
//! remote-debugging flags. Chrome is a silent player + logged-in harvester.

use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::Arc;

use anyhow::{anyhow, Context, Result};
use tokio::process::{Child, Command};
use tokio::sync::Mutex;

use crate::paths::{ensure_data_dir, profile_dir};
use crate::wm;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NestMode {
    /// Windowed nest for background harvest; hide afterwards.
    Harvest,
    /// Fullscreen nest for watching.
    Play,
}

#[derive(Clone, Debug, serde::Serialize)]
pub struct NestStatus {
    pub available: bool,
    pub chrome_path: Option<String>,
    pub gamescope_path: Option<String>,
    pub profile: String,
    pub ready: bool,
    pub launched_by_zappe: bool,
    pub using_gamescope: bool,
    pub pids: Vec<u32>,
}

struct NestInner {
    child: Option<Child>,
    launched_by_zappe: bool,
}

#[derive(Clone)]
pub struct NestManager {
    inner: Arc<Mutex<NestInner>>,
}

#[allow(dead_code)]
impl NestManager {
    pub fn start() -> Self {
        Self {
            inner: Arc::new(Mutex::new(NestInner {
                child: None,
                launched_by_zappe: false,
            })),
        }
    }

    pub async fn is_ready(&self) -> bool {
        let inner = self.inner.lock().await;
        inner.launched_by_zappe || !chrome_pids_for_profile(&profile_dir()).is_empty()
    }

    pub async fn launched_by_zappe(&self) -> bool {
        self.inner.lock().await.launched_by_zappe
    }

    pub async fn status(&self) -> NestStatus {
        let chrome = find_chrome();
        let gamescope = find_gamescope();
        let profile = profile_dir();
        let inner = self.inner.lock().await;
        NestStatus {
            available: chrome.is_some(),
            chrome_path: chrome.as_ref().map(|p| p.display().to_string()),
            gamescope_path: gamescope.as_ref().map(|p| p.display().to_string()),
            profile: profile.display().to_string(),
            ready: inner.launched_by_zappe || !chrome_pids_for_profile(&profile).is_empty(),
            launched_by_zappe: inner.launched_by_zappe,
            using_gamescope: prefer_gamescope() && gamescope.is_some(),
            pids: chrome_pids_for_profile(&profile),
        }
    }

    /// Open `url` in the nest. Failures are logged; they must not panic.
    pub async fn open_url(&self, url: &str, mode: NestMode) -> Result<()> {
        if let Err(err) = open_url_inner(&self.inner, url, mode).await {
            log::warn!("nest open failed (skill/play continues without crash): {err:#}");
            return Err(err);
        }
        Ok(())
    }

    pub fn open_url_detached(&self, url: String, mode: NestMode) {
        let inner = self.inner.clone();
        tauri::async_runtime::spawn(async move {
            if let Err(err) = open_url_inner(&inner, &url, mode).await {
                log::warn!("nest open failed: {err:#}");
            }
        });
    }

    pub async fn hide(&self) {
        let pids = nest_window_pids();
        wm::nudge_nest_hide(pids.first().copied());
    }

    pub async fn show_fullscreen(&self) {
        let pids = nest_window_pids();
        wm::nudge_nest_show(pids.first().copied());
    }

    /// Best-effort HID into the nest (Space / Escape). Teach-mode will record this later.
    pub async fn send_key(&self, key: &str) {
        let pids = nest_window_pids();
        wm::nudge_nest_show(pids.first().copied());
        send_key_best_effort(key);
    }

    pub async fn shutdown_if_owned(&self) {
        let mut inner = self.inner.lock().await;
        if !inner.launched_by_zappe {
            return;
        }
        if let Some(mut child) = inner.child.take() {
            let _ = child.start_kill();
            let _ = child.wait().await;
        }
        inner.launched_by_zappe = false;
    }
}

async fn open_url_inner(inner: &Arc<Mutex<NestInner>>, url: &str, mode: NestMode) -> Result<()> {
    let chrome = find_chrome().ok_or_else(|| {
        anyhow!(
            "Google Chrome is required. On Arch/Omarchy: sudo pacman -S google-chrome (or set CHROME_PATH)."
        )
    })?;
    let profile = profile_dir();
    ensure_data_dir().context("create zappe data dir")?;
    std::fs::create_dir_all(&profile)
        .with_context(|| format!("create profile dir {}", profile.display()))?;

    let already_running = !chrome_pids_for_profile(&profile).is_empty();
    if already_running {
        // Existing Chrome singleton: open the URL in that session (no second nest).
        navigate_existing(&chrome, &profile, url)?;
        apply_mode(mode);
        return Ok(());
    }

    let mut guard = inner.lock().await;
    if guard.child.is_some() {
        drop(guard);
        navigate_existing(&chrome, &profile, url)?;
        apply_mode(mode);
        return Ok(());
    }

    let child = spawn_nest(&chrome, &profile, url, mode)?;
    guard.child = Some(child);
    guard.launched_by_zappe = true;
    drop(guard);

    tokio::time::sleep(std::time::Duration::from_millis(400)).await;
    apply_mode(mode);
    Ok(())
}

fn apply_mode(mode: NestMode) {
    let pids = nest_window_pids();
    match mode {
        NestMode::Harvest => wm::nudge_nest_hide(pids.first().copied()),
        NestMode::Play => wm::nudge_nest_show(pids.first().copied()),
    }
}

fn spawn_nest(chrome: &Path, profile: &Path, url: &str, mode: NestMode) -> Result<Child> {
    let plan = NestLaunch::new(chrome.to_path_buf(), profile.to_path_buf(), Some(url.to_string()), mode);
    if !plan.has_no_cdp_flags() {
        return Err(anyhow!("refusing to launch Chrome with automation/CDP flags"));
    }
    let argv = plan.argv();
    log::info!("starting nest: {}", argv.join(" "));

    let mut cmd = Command::new(&argv[0]);
    cmd.args(&argv[1..])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .kill_on_drop(false)
        .env("ACCESSIBILITY_ENABLED", "1")
        .env("GTK_A11Y", "atspi");

    cmd.spawn().with_context(|| format!("spawn nest {}", argv[0]))
}

fn navigate_existing(chrome: &Path, profile: &Path, url: &str) -> Result<()> {
    let args = chrome_args(profile, Some(url));
    if !has_no_cdp_flags(&args) {
        return Err(anyhow!("refusing to launch Chrome with automation/CDP flags"));
    }
    let status = std::process::Command::new(chrome)
        .args(&args)
        .env("ACCESSIBILITY_ENABLED", "1")
        .env("GTK_A11Y", "atspi")
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .context("open URL in existing Chrome")?;
    if !status.success() {
        log::warn!("Chrome singleton navigate exited {}", status);
    }
    Ok(())
}

pub struct NestLaunch {
    pub gamescope: Option<PathBuf>,
    pub chrome: PathBuf,
    pub profile: PathBuf,
    pub url: Option<String>,
    pub mode: NestMode,
    pub width: u32,
    pub height: u32,
}

impl NestLaunch {
    pub fn new(chrome: PathBuf, profile: PathBuf, url: Option<String>, mode: NestMode) -> Self {
        let (width, height) = nest_size();
        Self {
            gamescope: if prefer_gamescope() {
                find_gamescope()
            } else {
                None
            },
            chrome,
            profile,
            url,
            mode,
            width,
            height,
        }
    }

    pub fn argv(&self) -> Vec<String> {
        let mut chrome_cmd = vec![self.chrome.display().to_string()];
        chrome_cmd.extend(chrome_args(&self.profile, self.url.as_deref()));

        if let Some(gs) = &self.gamescope {
            let mut args = vec![
                gs.display().to_string(),
                "-W".into(),
                self.width.to_string(),
                "-H".into(),
                self.height.to_string(),
            ];
            if self.mode == NestMode::Play {
                args.push("-f".into());
            }
            args.push("--".into());
            args.extend(chrome_cmd);
            args
        } else {
            if prefer_gamescope() {
                log::warn!(
                    "gamescope not found; launching Chrome directly. Install gamescope for the living-room nest."
                );
            }
            chrome_cmd
        }
    }

    pub fn has_no_cdp_flags(&self) -> bool {
        has_no_cdp_flags(&self.argv())
    }
}

pub fn chrome_args(profile: &Path, url: Option<&str>) -> Vec<String> {
    let mut args = vec![
        format!("--user-data-dir={}", profile.display()),
        "--no-first-run".into(),
        "--no-default-browser-check".into(),
        "--disable-session-crashed-bubble".into(),
        "--force-renderer-accessibility".into(),
    ];
    if let Some(url) = url {
        args.push(url.to_string());
    }
    args
}

pub fn has_no_cdp_flags(args: &[String]) -> bool {
    args.iter().all(|a| {
        let l = a.to_ascii_lowercase();
        !l.contains("enable-automation")
            && !l.contains("remote-debugging")
            && !l.contains("remote-debug-port")
            && !l.contains("--headless")
    })
}

pub fn find_chrome() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("CHROME_PATH") {
        let path = PathBuf::from(explicit);
        if path.exists() {
            return Some(path);
        }
    }
    const NAMES: &[&str] = &[
        "google-chrome-stable",
        "google-chrome",
        "chromium",
        "chromium-browser",
        "chrome",
    ];
    for name in NAMES {
        if let Some(path) = which(name) {
            return Some(path);
        }
    }
    #[cfg(target_os = "macos")]
    {
        for path in [
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
        ] {
            let p = PathBuf::from(path);
            if p.exists() {
                return Some(p);
            }
        }
    }
    None
}

pub fn find_gamescope() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("GAMESCOPE_PATH") {
        let path = PathBuf::from(explicit);
        if path.exists() {
            return Some(path);
        }
    }
    which("gamescope")
}

pub fn prefer_gamescope() -> bool {
    match std::env::var("ZAPPE_NEST")
        .map(|s| s.to_ascii_lowercase())
        .as_deref()
    {
        Ok("chrome") | Ok("chromium") => false,
        _ => true,
    }
}

pub fn chrome_pids_for_profile(profile: &Path) -> Vec<u32> {
    let needle = profile.to_string_lossy();
    pgrep_f(&format!("user-data-dir={needle}"))
}

pub fn nest_window_pids() -> Vec<u32> {
    let mut pids = pgrep_f("gamescope");
    if pids.is_empty() {
        pids = chrome_pids_for_profile(&profile_dir());
    }
    pids
}

fn nest_size() -> (u32, u32) {
    let width = std::env::var("ZAPPE_NEST_WIDTH")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1920);
    let height = std::env::var("ZAPPE_NEST_HEIGHT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(1080);
    (width, height)
}

fn send_key_best_effort(key: &str) {
    let key = key.trim().to_ascii_lowercase();
    let mapped = match key.as_str() {
        "space" | "playpause" | "play-pause" => "space",
        "escape" | "esc" | "back" => "Escape",
        other => other,
    };
    for (bin, args) in [
        ("wtype", vec![mapped.to_string()]),
        ("ydotool", vec!["key".into(), format!("{mapped}:1"), format!("{mapped}:0")]),
        ("xdotool", vec!["key".into(), mapped.to_string()]),
    ] {
        if which(bin).is_some() {
            match std::process::Command::new(bin).args(&args).status() {
                Ok(status) if status.success() => return,
                Ok(status) => log::warn!("{bin} key send exited {status}"),
                Err(err) => log::warn!("{bin} key send failed: {err}"),
            }
        }
    }
    log::info!("no wtype/ydotool/xdotool; nest key '{mapped}' not sent");
}

pub fn which(name: &str) -> Option<PathBuf> {
    let output = std::process::Command::new("which").arg(name).output().ok()?;
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

fn pgrep_f(needle: &str) -> Vec<u32> {
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chrome_args_never_include_cdp_or_automation() {
        let args = chrome_args(Path::new("/tmp/zappe/chrome-profile"), Some("https://www.netflix.com/browse"));
        assert!(has_no_cdp_flags(&args), "{args:?}");
        assert!(args.iter().any(|a| a.contains("user-data-dir=")));
        assert!(args.iter().any(|a| a == "--force-renderer-accessibility"));
        assert!(!args.iter().any(|a| a.contains("enable-automation")));
        assert!(!args.iter().any(|a| a.contains("remote-debugging")));
    }

    #[test]
    fn gamescope_argv_wraps_chrome() {
        let launch = NestLaunch {
            gamescope: Some(PathBuf::from("/usr/bin/gamescope")),
            chrome: PathBuf::from("/usr/bin/google-chrome-stable"),
            profile: PathBuf::from("/tmp/zappe/chrome-profile"),
            url: Some("https://www.netflix.com/browse".into()),
            mode: NestMode::Play,
            width: 1920,
            height: 1080,
        };
        let argv = launch.argv();
        assert_eq!(argv[0], "/usr/bin/gamescope");
        assert!(argv.contains(&"-f".into()));
        assert!(argv.contains(&"--".into()));
        assert!(argv.iter().any(|a| a.ends_with("google-chrome-stable")));
        assert!(launch.has_no_cdp_flags());
    }

    #[test]
    fn harvest_mode_is_not_fullscreen() {
        let launch = NestLaunch {
            gamescope: Some(PathBuf::from("/usr/bin/gamescope")),
            chrome: PathBuf::from("/usr/bin/google-chrome-stable"),
            profile: PathBuf::from("/tmp/zappe/chrome-profile"),
            url: Some("https://www.netflix.com/browse".into()),
            mode: NestMode::Harvest,
            width: 1280,
            height: 720,
        };
        let argv = launch.argv();
        assert!(!argv.contains(&"-f".into()));
        assert!(launch.has_no_cdp_flags());
    }
}
