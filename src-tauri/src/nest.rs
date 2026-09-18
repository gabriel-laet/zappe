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
    /// PID of the nest gamescope we spawned (never the outer kiosk).
    nest_pid: Option<u32>,
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
                nest_pid: None,
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
        let pids = self.play_window_pids().await;
        wm::nudge_nest_hide(pids.first().copied());
    }

    pub async fn show_fullscreen(&self) {
        let pids = self.play_window_pids().await;
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
            kill_child_and_profile_chrome(&mut child).await;
        }
        inner.nest_pid = None;
        inner.launched_by_zappe = false;
    }

    async fn play_window_pids(&self) -> Vec<u32> {
        let inner = self.inner.lock().await;
        if let Some(pid) = inner
            .nest_pid
            .filter(|p| pid_alive(*p) && is_safe_nest_pid(*p))
        {
            return vec![pid];
        }
        drop(inner);
        nest_window_pids()
    }

    /// Hide, then kill the nest gamescope (and leftover profile Chrome).
    /// Never signals the outer kiosk gamescope (`gamescope -- zappe`).
    pub async fn exit_play(&self) {
        let mut inner = self.inner.lock().await;
        let owned_pid = inner.nest_pid;
        if let Some(pid) = owned_pid.filter(|p| is_safe_nest_pid(*p)) {
            log::info!("ATONGX nest exit — hide/kill child gamescope pid={pid}");
            wm::nudge_nest_hide(Some(pid));
        }
        if let Some(mut child) = inner.child.take() {
            kill_child_and_profile_chrome(&mut child).await;
            inner.nest_pid = None;
            inner.launched_by_zappe = false;
            return;
        }
        inner.nest_pid = None;
        drop(inner);

        let mut signaled = false;
        for pid in nest_gamescope_pids() {
            if !is_safe_nest_pid(pid) {
                log::warn!("refusing to signal gamescope pid {pid} (self/kiosk/ancestor)");
                continue;
            }
            log::info!("ATONGX nest exit — hide/kill nest gamescope pid={pid}");
            wm::nudge_nest_hide(Some(pid));
            signal_term(pid);
            signaled = true;
        }
        if !signaled {
            self.hide().await;
        }
        let ancestors = ancestor_pids();
        let self_pid = std::process::id();
        for pid in chrome_pids_for_profile(&profile_dir()) {
            if is_safe_pid(pid, self_pid, &ancestors) {
                signal_term(pid);
            }
        }
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
    guard.nest_pid = child.id();
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
    let plan = NestLaunch::new(
        chrome.to_path_buf(),
        profile.to_path_buf(),
        Some(url.to_string()),
        mode,
    );
    if !plan.has_no_cdp_flags() {
        return Err(anyhow!(
            "refusing to launch Chrome with automation/CDP flags"
        ));
    }
    let argv = plan.argv();
    log::info!(
        "starting nest chrome={} gamescope={}: {}",
        chrome.display(),
        plan.gamescope
            .as_ref()
            .map(|p| p.display().to_string())
            .unwrap_or_else(|| "none".into()),
        argv.join(" ")
    );

    let mut cmd = Command::new(&argv[0]);
    cmd.args(&argv[1..])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .kill_on_drop(false)
        .env("ACCESSIBILITY_ENABLED", "1")
        .env("GTK_A11Y", "atspi");

    #[cfg(unix)]
    {
        cmd.process_group(0);
    }

    cmd.spawn()
        .with_context(|| format!("spawn nest {}", argv[0]))
}

fn navigate_existing(chrome: &Path, profile: &Path, url: &str) -> Result<()> {
    let args = chrome_args(profile, Some(url));
    if !has_no_cdp_flags(&args) {
        return Err(anyhow!(
            "refusing to launch Chrome with automation/CDP flags"
        ));
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

/// Preferred first. Chromium is last-resort — Omarchy often has both, and
/// `which` alone can miss `/usr/bin/google-chrome-stable` / `/opt/google/chrome`.
const CHROME_BIN_NAMES: &[&str] = &[
    "google-chrome-stable",
    "google-chrome",
    "chromium",
    "chromium-browser",
    "chrome",
];

fn well_known_chrome_paths() -> Vec<PathBuf> {
    #[allow(unused_mut)]
    let mut paths = vec![
        PathBuf::from("/usr/bin/google-chrome-stable"),
        PathBuf::from("/opt/google/chrome/google-chrome"),
        PathBuf::from("/usr/bin/google-chrome"),
        PathBuf::from("/usr/local/bin/google-chrome-stable"),
        PathBuf::from("/usr/local/bin/google-chrome"),
        PathBuf::from("/usr/bin/chromium"),
        PathBuf::from("/usr/bin/chromium-browser"),
        PathBuf::from("/usr/bin/chrome"),
    ];
    #[cfg(target_os = "macos")]
    {
        paths.extend([
            PathBuf::from("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
            PathBuf::from("/Applications/Chromium.app/Contents/MacOS/Chromium"),
        ]);
    }
    paths
}

pub fn find_chrome() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("CHROME_PATH") {
        let path = PathBuf::from(explicit);
        if is_runnable(&path) {
            return Some(path);
        }
    }

    for path in well_known_chrome_paths() {
        if is_preferred_chrome_name(&path) && is_runnable(&path) {
            return Some(path);
        }
    }
    for name in ["google-chrome-stable", "google-chrome"] {
        if let Some(path) = which(name) {
            return Some(path);
        }
    }
    for path in well_known_chrome_paths() {
        if is_runnable(&path) {
            return Some(path);
        }
    }
    for name in CHROME_BIN_NAMES {
        if let Some(path) = which(name) {
            return Some(path);
        }
    }
    None
}

fn is_preferred_chrome_name(path: &Path) -> bool {
    match path.file_name().and_then(|s| s.to_str()) {
        Some("google-chrome-stable") | Some("google-chrome") | Some("Google Chrome") => true,
        _ => false,
    }
}

fn is_runnable(path: &Path) -> bool {
    path.is_file()
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

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GamescopeRole {
    Nest,
    Kiosk,
    Other,
}

/// Distinguish the Chrome nest from the outer kiosk (`gamescope -- zappe`).
/// Nest cmdlines contain Chrome / user-data-dir; check those first because the
/// profile path also includes the word "zappe".
pub fn classify_gamescope_cmdline(cmdline: &str) -> GamescopeRole {
    let joined = cmdline.replace(['\0', '\n'], " ");
    let lower = joined.to_ascii_lowercase();
    if !lower.contains("gamescope") {
        return GamescopeRole::Other;
    }
    if lower.contains("google-chrome")
        || lower.contains("chromium")
        || lower.contains("user-data-dir")
    {
        return GamescopeRole::Nest;
    }
    if looks_like_kiosk_payload(&lower) {
        return GamescopeRole::Kiosk;
    }
    GamescopeRole::Other
}

fn looks_like_kiosk_payload(cmd: &str) -> bool {
    let tokens: Vec<&str> = cmd.split_whitespace().collect();
    if let Some(last) = tokens.last() {
        let base = last.rsplit('/').next().unwrap_or(last);
        if base == "zappe" {
            return true;
        }
    }
    cmd.contains("-- zappe")
}

pub fn nest_window_pids() -> Vec<u32> {
    let mut pids: Vec<u32> = nest_gamescope_pids()
        .into_iter()
        .filter(|p| is_safe_nest_pid(*p))
        .collect();
    if pids.is_empty() {
        let ancestors = ancestor_pids();
        let self_pid = std::process::id();
        pids = chrome_pids_for_profile(&profile_dir())
            .into_iter()
            .filter(|p| is_safe_pid(*p, self_pid, &ancestors))
            .collect();
    }
    pids
}

fn nest_gamescope_pids() -> Vec<u32> {
    pids_matching_cmdline(|cmd| classify_gamescope_cmdline(cmd) == GamescopeRole::Nest)
}

fn pids_matching_cmdline(pred: impl Fn(&str) -> bool) -> Vec<u32> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir("/proc") else {
        return out;
    };
    for entry in entries.flatten() {
        let pid: u32 = match entry.file_name().to_str().and_then(|s| s.parse().ok()) {
            Some(p) => p,
            None => continue,
        };
        let cmdline = match std::fs::read(entry.path().join("cmdline")) {
            Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
            Err(_) => continue,
        };
        if pred(&cmdline) {
            out.push(pid);
        }
    }
    out
}

pub fn ancestor_pids() -> Vec<u32> {
    ancestor_pids_from(std::process::id(), ppid_of)
}

pub fn ancestor_pids_from(start: u32, mut ppid_of: impl FnMut(u32) -> Option<u32>) -> Vec<u32> {
    let mut pids = Vec::new();
    let mut current = start;
    for _ in 0..16 {
        let Some(ppid) = ppid_of(current) else {
            break;
        };
        if ppid == 0 || ppid == 1 || ppid == current || pids.contains(&ppid) {
            break;
        }
        pids.push(ppid);
        current = ppid;
    }
    pids
}

fn ppid_of(pid: u32) -> Option<u32> {
    let status = std::fs::read_to_string(format!("/proc/{pid}/status")).ok()?;
    for line in status.lines() {
        if let Some(rest) = line.strip_prefix("PPid:") {
            return rest.trim().parse().ok();
        }
    }
    None
}

pub fn is_safe_pid(pid: u32, self_pid: u32, ancestors: &[u32]) -> bool {
    pid != 0 && pid != 1 && pid != self_pid && !ancestors.contains(&pid)
}

pub fn is_safe_nest_pid(pid: u32) -> bool {
    is_safe_pid(pid, std::process::id(), &ancestor_pids())
}

fn pid_alive(pid: u32) -> bool {
    std::path::Path::new(&format!("/proc/{pid}")).exists()
}

fn signal_term(pid: u32) {
    let _ = std::process::Command::new("kill")
        .args(["-TERM", &pid.to_string()])
        .status();
}

fn signal_term_group(pid: u32) {
    let _ = std::process::Command::new("kill")
        .args(["-TERM", &format!("-{pid}")])
        .status();
}

async fn kill_child_and_profile_chrome(child: &mut Child) {
    let nest_pid = child.id();
    let chrome = chrome_pids_for_profile(&profile_dir());
    if let Some(pid) = nest_pid.filter(|p| is_safe_nest_pid(*p)) {
        signal_term_group(pid);
        signal_term(pid);
    }
    let _ = child.start_kill();
    match tokio::time::timeout(std::time::Duration::from_millis(1200), child.wait()).await {
        Ok(_) => {}
        Err(_) => {
            let _ = child.start_kill();
            let _ = child.wait().await;
        }
    }
    let ancestors = ancestor_pids();
    let self_pid = std::process::id();
    for pid in chrome {
        if is_safe_pid(pid, self_pid, &ancestors) && pid_alive(pid) {
            signal_term(pid);
        }
    }
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

pub fn send_media_key(key: &str) {
    send_key_best_effort(key);
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
        (
            "ydotool",
            vec!["key".into(), format!("{mapped}:1"), format!("{mapped}:0")],
        ),
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
    if let Some(found) = lookup_on_path(name) {
        return Some(found);
    }
    // Extra dirs GUI sessions sometimes omit from PATH.
    for dir in ["/usr/bin", "/usr/local/bin", "/opt/google/chrome"] {
        let candidate = PathBuf::from(dir).join(name);
        if is_runnable(&candidate) {
            return Some(candidate);
        }
    }
    None
}

fn lookup_on_path(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os("PATH")?;
    for dir in std::env::split_paths(&path) {
        let candidate = dir.join(name);
        if is_runnable(&candidate) {
            return Some(candidate);
        }
    }
    None
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
        let args = chrome_args(
            Path::new("/tmp/zappe/chrome-profile"),
            Some("https://www.netflix.com/browse"),
        );
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
    fn chrome_search_prefers_google_chrome_stable_over_chromium() {
        let names = CHROME_BIN_NAMES;
        let stable = names
            .iter()
            .position(|n| *n == "google-chrome-stable")
            .unwrap();
        let google = names.iter().position(|n| *n == "google-chrome").unwrap();
        let chromium = names.iter().position(|n| *n == "chromium").unwrap();
        assert!(stable < google && google < chromium);

        let known = well_known_chrome_paths();
        let first = known[0].to_string_lossy();
        assert!(
            first.ends_with("google-chrome-stable"),
            "well-known paths must start with google-chrome-stable, got {first}"
        );
        let chromium_i = known
            .iter()
            .position(|p| p.file_name().and_then(|s| s.to_str()) == Some("chromium"))
            .unwrap();
        let stable_i = known
            .iter()
            .position(|p| p.file_name().and_then(|s| s.to_str()) == Some("google-chrome-stable"))
            .unwrap();
        assert!(stable_i < chromium_i);
        assert!(is_preferred_chrome_name(Path::new(
            "/usr/bin/google-chrome-stable"
        )));
        assert!(is_preferred_chrome_name(Path::new(
            "/opt/google/chrome/google-chrome"
        )));
        assert!(!is_preferred_chrome_name(Path::new("/usr/bin/chromium")));
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

    #[test]
    fn nest_cmdline_is_not_the_kiosk() {
        assert_eq!(
            classify_gamescope_cmdline("gamescope -e -f -- zappe"),
            GamescopeRole::Kiosk
        );
        assert_eq!(
            classify_gamescope_cmdline("gamescope\0-e\0-f\0--\0/home/glaet/bin/zappe"),
            GamescopeRole::Kiosk
        );
        assert_eq!(
            classify_gamescope_cmdline(
                "gamescope -W 1920 -H 1080 -f -- /usr/bin/google-chrome-stable --user-data-dir=/home/glaet/.local/share/zappe/chrome-profile"
            ),
            GamescopeRole::Nest
        );
        assert_eq!(classify_gamescope_cmdline("Hyprland"), GamescopeRole::Other);
    }

    #[test]
    fn never_signal_self_or_kiosk_ancestors() {
        let self_pid = 400;
        let ancestors = ancestor_pids_from(400, |pid| match pid {
            400 => Some(300), // outer gamescope
            300 => Some(1),
            _ => None,
        });
        assert_eq!(ancestors, vec![300]);
        assert!(!is_safe_pid(300, self_pid, &ancestors));
        assert!(!is_safe_pid(400, self_pid, &ancestors));
        assert!(!is_safe_pid(1, self_pid, &ancestors));
        assert!(is_safe_pid(501, self_pid, &ancestors));
    }

    #[tokio::test]
    async fn exit_play_without_child_does_not_panic() {
        let nest = NestManager::start();
        nest.exit_play().await;
    }
}
