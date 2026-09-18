//! Best-effort window-manager nudges (Hyprland 0.56+ Lua API via `hyprctl eval`).

use std::process::Command;

const CHROME_WINDOW: &str = "class:^(chromium|google-chrome|Chromium|chrome)$";
const NEST_WINDOW: &str = "class:^(gamescope|gamescope-wl)$";
const MPV_WINDOW: &str = "class:^(mpv)$";
const GUIDE_CLASS: &str = "class:^(zappe)$";
const GUIDE_TITLE: &str = "title:^Zappe$";

/// Hyprland 0.56: only `hyprctl eval` + `hl.dsp.*`. Never `hyprctl dispatch …`
/// (including `dispatch exec`) — that path is rejected on Lua sessions.
/// Processes are spawned from Rust (`nest`, `ota`), not via the compositor.
fn hypr_eval(lua: &str) -> bool {
    debug_assert!(
        !lua.contains("dispatch exec") && !lua.contains("hyprctl dispatch"),
        "legacy hyprctl dispatch is forbidden on Hyprland 0.56"
    );
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

/// Raise the gamescope nest (or Chrome fallback when `ZAPPE_NEST=chrome`).
pub fn nudge_nest_show(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        if try_focus_fullscreen(pid, NEST_WINDOW) {
            return;
        }
        if try_focus_fullscreen(None, NEST_WINDOW) {
            return;
        }
        // Fallback only when Chrome is not nested in gamescope.
        if !try_focus_fullscreen(pid, CHROME_WINDOW) {
            let _ = try_focus_fullscreen(None, CHROME_WINDOW);
            wmctrl_activate("gamescope");
            wmctrl_activate("Google Chrome");
        }
    }
}

pub fn nudge_nest_hide(pid: Option<u32>) {
    #[cfg(target_os = "linux")]
    {
        let nest = window_target(pid, NEST_WINDOW);
        if hypr_fullscreen(&nest, false) {
            let _ = hypr_move_to_workspace(&nest, "special:zappe-nest", false);
            return;
        }
        let chrome = window_target(pid, CHROME_WINDOW);
        let _ = hypr_fullscreen(&chrome, false);
        let _ = hypr_move_to_workspace(&chrome, "special:zappe-nest", false);
    }
}

/// Hyprland 0.56: `hl.dsp.window.move` (legacy `movetoworkspacesilent` / `hl.dsp.window.movetoworkspacesilent` is nil).
fn hypr_move_to_workspace(window: &str, workspace: &str, follow: bool) -> bool {
    hypr_eval(&hypr_move_to_workspace_lua(window, workspace, follow))
}

fn hypr_move_to_workspace_lua(window: &str, workspace: &str, follow: bool) -> String {
    format!(
        "hl.dispatch(hl.dsp.window.move({{ workspace = \"{workspace}\", follow = {follow}, window = \"{window}\" }}))"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hide_nudge_uses_056_window_move_not_movetoworkspacesilent() {
        let lua = hypr_move_to_workspace_lua(
            "class:^(gamescope|gamescope-wl)$",
            "special:zappe-nest",
            false,
        );
        assert!(lua.contains("hl.dsp.window.move"));
        assert!(lua.contains("follow = false"));
        assert!(lua.contains("special:zappe-nest"));
        assert!(!lua.contains("movetoworkspacesilent"));
        assert!(!lua.contains("hl.dsp.window.movetoworkspacesilent"));
    }

    #[test]
    fn never_uses_legacy_hyprctl_dispatch_exec() {
        let src = include_str!("wm.rs");
        assert!(
            !src.contains(".arg(\"dispatch\")"),
            "do not invoke `hyprctl dispatch` (0.56 breaks); use eval + hl.dsp.*"
        );
        assert!(src.contains(".arg(\"eval\")"));
        let lua = hypr_move_to_workspace_lua("pid:1", "special:zappe-nest", false);
        assert!(lua.starts_with("hl.dispatch(hl.dsp."));
        assert!(!lua.contains("dispatch exec"));
    }
}

fn try_focus_fullscreen(pid: Option<u32>, class_or_title: &str) -> bool {
    let window = window_target(pid, class_or_title);
    hypr_focus(&window) && hypr_fullscreen(&window, true)
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
    let output = Command::new("pgrep").args(["-n", name]).output().ok()?;
    if !output.status.success() {
        return None;
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    stdout.trim().parse().ok()
}
