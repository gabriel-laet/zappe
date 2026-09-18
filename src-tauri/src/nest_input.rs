//! Route ATONGX keys and the air-mouse into the Chrome nest.
//!
//! Living-room stack:
//! ```text
//! ATONGX HID ──► outer gamescope (DRM / kiosk) ──► zappe | nest gamescope
//!                                                      └── google-chrome-stable
//! ```
//!
//! The nest is its own compositor. Host `wtype` / `xdotool` talk to the *outer*
//! display unless we copy `DISPLAY` / `WAYLAND_DISPLAY` from the Chrome
//! process inside gamescope. Chrome in the nest is almost always an Xwayland
//! client, so **xdotool on that DISPLAY** is the reliable path.
//!
//! `wtype space` types the letters s-p-a-c-e (text mode). Named keys must use
//! `wtype -k <keysym>`. Play/Pause / D-pad / OK all go through [`inject_key`].
//!
//! Back / Home stay in `hid.rs` / `return_to_guide`. This module only
//! injects navigation, play/pause, and pointer clicks.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::nest::{chrome_pids_for_profile, nest_size, nest_window_pids, which};
use crate::paths::profile_dir;
use crate::wm;

static POINTER_ON: AtomicBool = AtomicBool::new(false);
static LAST_POINTER_MS: AtomicU64 = AtomicU64::new(0);

const POINTER_DEBOUNCE_MS: u64 = 250;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NestInputTarget {
    pub wayland_display: Option<String>,
    pub x11_display: Option<String>,
    pub pid: Option<u32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct KeySpec {
    pub name: &'static str,
    /// xkb keysym for `wtype -k` and `xdotool key`.
    pub xkb: &'static str,
    /// Linux evdev KEY_* code for ydotool / uinput.
    pub evdev: u16,
}

pub fn key_spec(name: &str) -> Option<KeySpec> {
    match name.trim().to_ascii_lowercase().as_str() {
        "up" | "arrowup" | "arrow-up" => Some(KeySpec {
            name: "up",
            xkb: "Up",
            evdev: 103,
        }),
        "down" | "arrowdown" | "arrow-down" => Some(KeySpec {
            name: "down",
            xkb: "Down",
            evdev: 108,
        }),
        "left" | "arrowleft" | "arrow-left" => Some(KeySpec {
            name: "left",
            xkb: "Left",
            evdev: 105,
        }),
        "right" | "arrowright" | "arrow-right" => Some(KeySpec {
            name: "right",
            xkb: "Right",
            evdev: 106,
        }),
        "ok" | "enter" | "return" | "select" | "numpadenter" => Some(KeySpec {
            name: "ok",
            xkb: "Return",
            evdev: 28,
        }),
        "space" | "playpause" | "play-pause" => Some(KeySpec {
            name: "space",
            xkb: "space",
            evdev: 57,
        }),
        "escape" | "esc" | "back" => Some(KeySpec {
            name: "escape",
            xkb: "Escape",
            evdev: 1,
        }),
        "pageup" | "page_up" | "page-up" => Some(KeySpec {
            name: "pageup",
            xkb: "Page_Up",
            evdev: 104,
        }),
        "pagedown" | "page_down" | "page-down" => Some(KeySpec {
            name: "pagedown",
            xkb: "Page_Down",
            evdev: 109,
        }),
        _ => None,
    }
}

pub fn wtype_key_args(spec: KeySpec) -> Vec<String> {
    // Named keys: `-k` (press+release). Bare `wtype space` types the word.
    vec!["-k".into(), spec.xkb.into()]
}

pub fn xdotool_key_args(spec: KeySpec) -> Vec<String> {
    vec!["key".into(), "--clearmodifiers".into(), spec.xkb.into()]
}

pub fn ydotool_key_args(spec: KeySpec) -> Vec<String> {
    vec![
        "key".into(),
        format!("{}:1", spec.evdev),
        format!("{}:0", spec.evdev),
    ]
}

pub fn pointer_mode() -> bool {
    POINTER_ON.load(Ordering::Relaxed)
}

/// Toggle air-mouse cursor mode. `None` if this press is a duplicate of the
/// guide keydown + global-shortcut pair (both fire on X11).
pub fn toggle_pointer_debounced() -> Option<bool> {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0);
    let last = LAST_POINTER_MS.load(Ordering::Relaxed);
    if now.saturating_sub(last) < POINTER_DEBOUNCE_MS {
        return None;
    }
    LAST_POINTER_MS.store(now, Ordering::Relaxed);
    let next = !POINTER_ON.load(Ordering::Relaxed);
    POINTER_ON.store(next, Ordering::Relaxed);
    Some(next)
}

pub fn on_pointer_toggled(on: bool) {
    focus_nest_on_host();
    if on {
        inject_mouse_center();
    }
}

/// D-pad / OK while the nest is up. Pointer mode turns OK into a real click.
pub fn inject_action(action: &str) {
    if action == "ok" && pointer_mode() {
        inject_click();
        return;
    }
    inject_key(action);
}

pub fn inject_key(name: &str) {
    let Some(spec) = key_spec(name) else {
        log::warn!("nest key '{name}' is not mapped");
        return;
    };
    focus_nest_on_host();
    let target = discover_nest_target();

    if target.x11_display.is_some() {
        activate_chrome_x11(Some(&target));
        if try_run("xdotool", &xdotool_key_args(spec), Some(&target)) {
            log::info!(
                "nest key {} via xdotool DISPLAY={}",
                spec.name,
                target.x11_display.as_deref().unwrap_or("?")
            );
            return;
        }
    }

    if target.wayland_display.is_some() {
        if try_run("wtype", &wtype_key_args(spec), Some(&target)) {
            log::info!(
                "nest key {} via wtype WAYLAND_DISPLAY={}",
                spec.name,
                target.wayland_display.as_deref().unwrap_or("?")
            );
            return;
        }
    }

    activate_gamescope_host();
    if try_run("xdotool", &xdotool_key_args(spec), None) {
        log::info!("nest key {} via host xdotool", spec.name);
        return;
    }
    if try_run("wtype", &wtype_key_args(spec), None) {
        log::info!("nest key {} via host wtype", spec.name);
        return;
    }
    if try_run("ydotool", &ydotool_key_args(spec), None) {
        log::info!("nest key {} via ydotool/uinput", spec.name);
        return;
    }
    log::info!(
        "no nest injector reached Chrome; '{}' not sent (need xdotool on nest DISPLAY, or ydotool)",
        spec.name
    );
}

/// Type into the nest *without* raising it on HDMI. Login uses this so the
/// Connect screen stays in front. Do not call [`focus_nest_on_host`].
pub fn inject_text_quiet(text: &str) -> bool {
    if text.is_empty() {
        return true;
    }
    let target = discover_nest_target();
    if target.x11_display.is_some()
        && try_run("xdotool", &xdotool_type_args(text), Some(&target))
    {
        log::info!(
            "nest text via quiet xdotool DISPLAY={}",
            target.x11_display.as_deref().unwrap_or("?")
        );
        return true;
    }
    if target.wayland_display.is_some()
        && try_run("wtype", &wtype_text_args(text), Some(&target))
    {
        log::info!(
            "nest text via quiet wtype WAYLAND_DISPLAY={}",
            target.wayland_display.as_deref().unwrap_or("?")
        );
        return true;
    }
    log::warn!("quiet text inject did not reach the hidden nest");
    false
}

/// Named key into the nest without raising the gamescope window.
pub fn inject_key_quiet(name: &str) -> bool {
    let Some(spec) = key_spec(name) else {
        log::warn!("nest key '{name}' is not mapped");
        return false;
    };
    let target = discover_nest_target();
    if target.x11_display.is_some()
        && try_run("xdotool", &xdotool_key_args(spec), Some(&target))
    {
        return true;
    }
    if target.wayland_display.is_some()
        && try_run("wtype", &wtype_key_args(spec), Some(&target))
    {
        return true;
    }
    false
}

pub fn xdotool_type_args(text: &str) -> Vec<String> {
    vec![
        "type".into(),
        "--clearmodifiers".into(),
        "--delay".into(),
        "12".into(),
        "--".into(),
        text.to_string(),
    ]
}

pub fn wtype_text_args(text: &str) -> Vec<String> {
    vec!["--".into(), text.to_string()]
}

pub fn inject_click() {
    focus_nest_on_host();
    let target = discover_nest_target();
    if target.x11_display.is_some() {
        activate_chrome_x11(Some(&target));
        if try_run(
            "xdotool",
            &["click".into(), "--clearmodifiers".into(), "1".into()],
            Some(&target),
        ) {
            log::info!(
                "nest click via xdotool DISPLAY={}",
                target.x11_display.as_deref().unwrap_or("?")
            );
            return;
        }
    }
    activate_gamescope_host();
    if try_run(
        "xdotool",
        &["click".into(), "--clearmodifiers".into(), "1".into()],
        None,
    ) {
        log::info!("nest click via host xdotool");
        return;
    }
    // ydotool: 0xC0 = left button (newer), `click 1` on older builds.
    if try_run("ydotool", &["click".into(), "0xC0".into()], None)
        || try_run("ydotool", &["click".into(), "1".into()], None)
    {
        log::info!("nest click via ydotool/uinput");
        return;
    }
    log::info!("nest click not sent");
}

fn inject_mouse_center() {
    let (width, height) = nest_size();
    let x = (width / 2).max(1);
    let y = (height / 2).max(1);
    let target = discover_nest_target();
    let args = vec!["mousemove".into(), x.to_string(), y.to_string()];
    if target.x11_display.is_some() && try_run("xdotool", &args, Some(&target)) {
        return;
    }
    let _ = try_run("xdotool", &args, None);
}

/// Raise the nest *window* on the host compositor so the physical ATONGX
/// keyboard (event3) and air-mouse (event4) land in inner gamescope, not the
/// hidden guide. Hyprland is best-effort — the appliance kiosk is gamescope DRM.
pub fn focus_nest_on_host() {
    let pid = nest_window_pids().first().copied();
    wm::nudge_nest_show(pid);
    activate_gamescope_host();
}

fn activate_gamescope_host() {
    if activate_x11_window(None, "--class", "gamescope")
        || activate_x11_window(None, "--class", "gamescope-wl")
        || activate_x11_window(None, "--name", "gamescope")
    {
        return;
    }
    if crate::nest::prefer_gamescope() {
        let _ = std::process::Command::new("wmctrl")
            .args(["-a", "gamescope"])
            .status();
    } else {
        activate_chrome_x11(None);
        let _ = std::process::Command::new("wmctrl")
            .args(["-a", "Google Chrome"])
            .status();
    }
}

fn activate_chrome_x11(target: Option<&NestInputTarget>) {
    for class in [
        "google-chrome",
        "Google-chrome",
        "google-chrome-stable",
        "Chromium",
        "chromium",
        "chrome",
    ] {
        if activate_x11_window(target, "--class", class) {
            return;
        }
    }
    let _ = activate_x11_window(target, "--name", "Chrome");
}

fn activate_x11_window(target: Option<&NestInputTarget>, by: &str, needle: &str) -> bool {
    try_run(
        "xdotool",
        &[
            "search".into(),
            "--onlyvisible".into(),
            by.into(),
            needle.into(),
            "windowactivate".into(),
            "--sync".into(),
        ],
        target,
    )
}

pub fn discover_nest_target() -> NestInputTarget {
    for pid in chrome_pids_for_profile(&profile_dir()) {
        if let Some(env) = read_proc_environ(pid) {
            let target = target_from_environ(pid, &env);
            if target.x11_display.is_some() || target.wayland_display.is_some() {
                return target;
            }
        }
    }
    for pid in nest_gamescope_pids() {
        if let Some(env) = read_proc_environ(pid) {
            let target = target_from_environ(pid, &env);
            if target.x11_display.is_some() || target.wayland_display.is_some() {
                return target;
            }
        }
    }
    NestInputTarget {
        pid: nest_window_pids().first().copied(),
        ..NestInputTarget::default()
    }
}

pub fn target_from_environ(pid: u32, env: &HashMap<String, String>) -> NestInputTarget {
    // Prefer the nest compositor socket when Chrome is a Wayland client.
    // DISPLAY is gamescope's Xwayland — the usual Chrome path.
    let wayland = env
        .get("GAMESCOPE_WAYLAND_DISPLAY")
        .or_else(|| env.get("WAYLAND_DISPLAY"))
        .cloned()
        .filter(|s| !s.is_empty());
    let x11 = env.get("DISPLAY").cloned().filter(|s| !s.is_empty());
    NestInputTarget {
        wayland_display: wayland,
        x11_display: x11,
        pid: Some(pid),
    }
}

/// Inner nest only — never the outer `gamescope -- zappe` kiosk.
pub fn is_nest_gamescope_cmdline(cmd: &str) -> bool {
    let l = cmd.to_ascii_lowercase();
    if !l.contains("gamescope") {
        return false;
    }
    if is_kiosk_gamescope_cmdline(&l) {
        return false;
    }
    l.contains("chrome") || l.contains("chromium")
}

pub fn is_kiosk_gamescope_cmdline(cmd: &str) -> bool {
    let l = cmd.to_ascii_lowercase();
    // Nest cmdlines include `--user-data-dir=.../zappe/chrome-profile` — do not
    // treat that path as the outer kiosk (`gamescope … -- zappe`).
    if l.contains("chrome") || l.contains("chromium") {
        return false;
    }
    let last = l.split_whitespace().last().unwrap_or("");
    last == "zappe" || last.ends_with("/zappe") || l.contains(" -- zappe")
}

pub fn nest_gamescope_pids() -> Vec<u32> {
    crate::nest::pgrep_f("gamescope")
        .into_iter()
        .filter(|pid| {
            read_proc_cmdline(*pid)
                .map(|cmd| is_nest_gamescope_cmdline(&cmd))
                .unwrap_or(false)
        })
        .collect()
}

pub fn parse_environ_bytes(bytes: &[u8]) -> HashMap<String, String> {
    let mut map = HashMap::new();
    for chunk in bytes.split(|b| *b == 0) {
        if chunk.is_empty() {
            continue;
        }
        let Ok(s) = std::str::from_utf8(chunk) else {
            continue;
        };
        if let Some((k, v)) = s.split_once('=') {
            map.insert(k.to_string(), v.to_string());
        }
    }
    map
}

fn read_proc_environ(pid: u32) -> Option<HashMap<String, String>> {
    let bytes = std::fs::read(format!("/proc/{pid}/environ")).ok()?;
    Some(parse_environ_bytes(&bytes))
}

fn read_proc_cmdline(pid: u32) -> Option<String> {
    let bytes = std::fs::read(format!("/proc/{pid}/cmdline")).ok()?;
    Some(String::from_utf8_lossy(&bytes).replace('\0', " "))
}

fn try_run(bin: &str, args: &[String], target: Option<&NestInputTarget>) -> bool {
    if which(bin).is_none() {
        return false;
    }
    let mut cmd = std::process::Command::new(bin);
    cmd.args(args);
    if let Some(t) = target {
        if let Some(display) = &t.x11_display {
            cmd.env("DISPLAY", display);
        }
        if let Some(wayland) = &t.wayland_display {
            cmd.env("WAYLAND_DISPLAY", wayland);
        }
    }
    match cmd.status() {
        Ok(status) if status.success() => true,
        Ok(status) => {
            log::warn!("{bin} {} exited {status}", args.join(" "));
            false
        }
        Err(err) => {
            log::warn!("{bin} {} failed: {err}", args.join(" "));
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quiet_type_args_do_not_raise_or_automate() {
        let args = xdotool_type_args("user@example.com");
        assert_eq!(args[0], "type");
        assert!(args.contains(&"--".into()));
        assert_eq!(args.last().map(String::as_str), Some("user@example.com"));
        assert_eq!(wtype_text_args("secret"), vec!["--", "secret"]);
        let src = include_str!("nest_input.rs");
        assert!(src.contains("fn inject_text_quiet"));
        assert!(
            !src[src.find("pub fn inject_text_quiet").unwrap()
                ..src.find("pub fn inject_click").unwrap()]
                .contains("focus_nest_on_host"),
            "login typing must not raise the nest on HDMI"
        );
    }

    #[test]
    fn wtype_uses_keysym_dash_k_not_text() {
        let space = key_spec("space").unwrap();
        assert_eq!(wtype_key_args(space), vec!["-k", "space"]);
        let up = key_spec("up").unwrap();
        assert_eq!(wtype_key_args(up), vec!["-k", "Up"]);
        let ok = key_spec("ok").unwrap();
        assert_eq!(wtype_key_args(ok), vec!["-k", "Return"]);
        // Regression: `wtype space` types s-p-a-c-e and "succeeds".
        assert_ne!(wtype_key_args(space), vec!["space"]);
    }

    #[test]
    fn xdotool_and_ydotool_use_real_keys() {
        let spec = key_spec("playpause").unwrap();
        assert_eq!(
            xdotool_key_args(spec),
            vec!["key", "--clearmodifiers", "space"]
        );
        assert_eq!(ydotool_key_args(spec), vec!["key", "57:1", "57:0"]);
        let left = key_spec("left").unwrap();
        assert_eq!(left.evdev, 105);
        assert_eq!(xdotool_key_args(left)[2], "Left");
    }

    #[test]
    fn maps_dpad_ok_and_escape() {
        assert_eq!(key_spec("ArrowUp").unwrap().xkb, "Up");
        assert_eq!(key_spec("ENTER").unwrap().xkb, "Return");
        assert_eq!(key_spec("escape").unwrap().xkb, "Escape");
        assert_eq!(key_spec("PageDown").unwrap().xkb, "Page_Down");
        assert!(key_spec("f2").is_none());
    }

    #[test]
    fn nest_gamescope_cmdline_skips_kiosk() {
        assert!(is_kiosk_gamescope_cmdline(
            "gamescope --backend drm -e -f -- zappe"
        ));
        assert!(is_kiosk_gamescope_cmdline(
            "gamescope -e -f -- /home/tv/bin/zappe"
        ));
        assert!(!is_kiosk_gamescope_cmdline(
            "gamescope -W 1920 -H 1080 -f -- google-chrome-stable --user-data-dir=/home/tv/.local/share/zappe/chrome-profile"
        ));
        assert!(!is_nest_gamescope_cmdline("gamescope -e -f -- zappe"));
        assert!(is_nest_gamescope_cmdline(
            "gamescope -W 1920 -H 1080 -f -- /usr/bin/google-chrome-stable --user-data-dir=/home/tv/.local/share/zappe/chrome-profile"
        ));
        assert!(!is_nest_gamescope_cmdline(
            "google-chrome-stable --user-data-dir=/x"
        ));
    }

    #[test]
    fn target_reads_gamescope_and_xwayland() {
        let mut env = HashMap::new();
        env.insert("DISPLAY".into(), ":1".into());
        env.insert("WAYLAND_DISPLAY".into(), "wayland-0".into());
        env.insert("GAMESCOPE_WAYLAND_DISPLAY".into(), "gamescope-0".into());
        let t = target_from_environ(42, &env);
        assert_eq!(t.x11_display.as_deref(), Some(":1"));
        assert_eq!(t.wayland_display.as_deref(), Some("gamescope-0"));
        assert_eq!(t.pid, Some(42));
    }

    #[test]
    fn parse_proc_environ() {
        let bytes = b"DISPLAY=:1\0WAYLAND_DISPLAY=gamescope-0\0HOME=/home/tv\0";
        let map = parse_environ_bytes(bytes);
        assert_eq!(map.get("DISPLAY").map(String::as_str), Some(":1"));
        assert_eq!(
            map.get("WAYLAND_DISPLAY").map(String::as_str),
            Some("gamescope-0")
        );
    }
}
