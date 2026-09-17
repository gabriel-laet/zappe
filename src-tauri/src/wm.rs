//! Best-effort window-manager nudges when CDP bounds are not enough (Hyprland / Wayland).

use std::process::Command;

/// Focus and fullscreen a Chromium window (best-effort on Hyprland).
pub fn nudge_chrome_fullscreen() {
    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("hyprctl")
            .args(["dispatch", "focuswindow", "class:^(chrome|chromium|google-chrome)$"])
            .status();
        let _ = Command::new("hyprctl")
            .args(["dispatch", "fullscreen", "1"])
            .status();
        // Fallback when hyprctl is missing (X11 / other WMs).
        let _ = Command::new("wmctrl")
            .args(["-a", "Google Chrome"])
            .status();
    }
}

/// Exit fullscreen and focus guide window title (best-effort).
pub fn nudge_chrome_unfullscreen() {
    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("hyprctl")
            .args(["dispatch", "focuswindow", "class:^(chrome|chromium|google-chrome)$"])
            .status();
        let _ = Command::new("hyprctl")
            .args(["dispatch", "fullscreen", "0"])
            .status();
    }
}

/// Focus mpv for OTA playback.
pub fn nudge_mpv_fullscreen() {
    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("hyprctl")
            .args(["dispatch", "focuswindow", "class:^mpv$"])
            .status();
        let _ = Command::new("hyprctl")
            .args(["dispatch", "fullscreen", "1"])
            .status();
    }
}

pub fn nudge_mpv_stop() {
    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("hyprctl")
            .args(["dispatch", "focuswindow", "class:^mpv$"])
            .status();
        let _ = Command::new("hyprctl")
            .args(["dispatch", "fullscreen", "0"])
            .status();
    }
}
