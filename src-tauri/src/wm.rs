//! Best-effort window-manager nudges (Hyprland 0.56+ Lua API via `hyprctl eval`).

use std::process::Command;

const CHROME_WINDOW: &str = "class:^(chromium|google-chrome|Chromium|chrome)$";
const MPV_WINDOW: &str = "class:^(mpv)$";
const GUIDE_CLASS: &str = "class:^(zappe)$";
const GUIDE_TITLE: &str = "title:^Zappe$";

fn hypr_eval(lua: &str) -> bool {
    match Command::new("hyprctl").arg("eval").arg(lua).output() {
        Ok(out) if out.status.success() => true,
        Ok(out) => {
            let stderr = String::from_utf8_lossy(&out.stderr);
            let stdout = String::from_utf8_lossy(&out.stdout);
            log::warn!(
                "hyprctl eval failed (status {:?}): {}{}",
                out.status.code(),
                stderr.trim(),
                if stdout.trim().is_empty() {
                    String::new()
                } else {
                    format!(" | {}", stdout.trim())
                }
            );
            false
        }
        Err(err) => {
            log::warn!("hyprctl eval unavailable: {err}");
            false
        }
    }
}

fn window_target(pid: Option<u32>, class_or_title: &str) -> String {
    if let Some(p) = pid {
        format!("pid:{p}")
    } else {
        class_or_title.to_string()
    }
}

fn hypr_focus(window: &str) -> bool {
    let lua = format!("hl.dispatch(hl.dsp.focus({{ window = \"{window}\" }}))");
    hypr_eval(&lua)
}

fn hypr_fullscreen(window: &str, set: bool) -> bool {
    let action = if set { "set" } else { "unset" };
    let lua = format!(
        "hl.dispatch(hl.dsp.window.fullscreen({{ mode = \"fullscreen\", action = \"{action}\", window = \"{window}\" }}))"
    );
    hypr_eval(&lua)
}

fn wmctrl_activate(title: &str) {
    let _ = Command::new("wmctrl").args(["-a", title]).status();
}

pub fn find_mpv_pid() -> Option<u32> {
    pgrep_newest("mpv")
}

pub fn nudge_chrome_fullscreen(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        let window = window_target(pid, CHROME_WINDOW);
        let ok = hypr_focus(&window) && hypr_fullscreen(&window, true);
        if !ok {
            let fallback = window_target(None, CHROME_WINDOW);
            let _ = hypr_focus(&fallback);
            let _ = hypr_fullscreen(&fallback, true);
            wmctrl_activate("Google Chrome");
            wmctrl_activate("Chromium");
        }
    }
}

pub fn nudge_chrome_unfullscreen(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        let window = window_target(pid, CHROME_WINDOW);
        let _ = hypr_fullscreen(&window, false);
    }
}

pub fn nudge_mpv_fullscreen(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        let pid = pid.or_else(find_mpv_pid);
        let window = window_target(pid, MPV_WINDOW);
        if !hypr_focus(&window) || !hypr_fullscreen(&window, true) {
            let fallback = window_target(find_mpv_pid(), MPV_WINDOW);
            let _ = hypr_focus(&fallback);
            let _ = hypr_fullscreen(&fallback, true);
        }
    }
}

pub fn nudge_mpv_stop(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        let pid = pid.or_else(find_mpv_pid);
        let window = window_target(pid, MPV_WINDOW);
        let _ = hypr_fullscreen(&window, false);
        let _ = hypr_focus(&window);
    }
}

/// Raise the Tauri guide shell (fullscreen + focus).
pub fn nudge_guide_fullscreen(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        if let Some(p) = pid {
            let window = window_target(Some(p), GUIDE_CLASS);
            if hypr_focus(&window) && hypr_fullscreen(&window, true) {
                return;
            }
        }
        for selector in [GUIDE_CLASS, GUIDE_TITLE] {
            if hypr_focus(selector) && hypr_fullscreen(selector, true) {
                return;
            }
        }
        wmctrl_activate("Zappe");
    }
}

fn pgrep_newest(name: &str) -> Option<u32> {
    let output = Command::new("pgrep")
        .args(["-n", name])
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    stdout.trim().parse().ok()
}
