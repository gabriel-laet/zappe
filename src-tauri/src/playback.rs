use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager};

use crate::nest::{find_chrome, NestManager, NestMode};
use crate::ota::OtaSession;
use crate::wm;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct GuideFocus {
    pub shelf_id: String,
    pub index: usize,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum PlaybackSurface {
    Idle,
    Chrome,
    Ota,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PlaybackStatus {
    pub surface: PlaybackSurface,
    pub focus: Option<GuideFocus>,
}

pub struct PlaybackController {
    pub surface: PlaybackSurface,
    pub focus_snapshot: Option<GuideFocus>,
}

impl PlaybackController {
    pub fn new() -> Self {
        Self {
            surface: PlaybackSurface::Idle,
            focus_snapshot: None,
        }
    }

    pub fn status(&self) -> PlaybackStatus {
        PlaybackStatus {
            surface: self.surface.clone(),
            focus: self.focus_snapshot.clone(),
        }
    }
}

fn hide_guide(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.hide();
    }
}

/// Show guide, Tauri fullscreen, and Hyprland focus (best-effort).
pub fn restore_guide_fullscreen(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.unminimize();
        let _ = win.show();
        let _ = win.set_decorations(false);
        let _ = win.set_fullscreen(true);
        let _ = win.set_focus();
    }
    wm::nudge_guide_fullscreen(None);
}

pub fn begin_nest(
    app: &AppHandle,
    nest: &NestManager,
    playback: &mut PlaybackController,
    url: String,
    focus: GuideFocus,
) -> Result<(), String> {
    if find_chrome().is_none() {
        return Err(
            "Google Chrome is required. On Arch/Omarchy: sudo pacman -S google-chrome (or set CHROME_PATH)."
                .into(),
        );
    }
    playback.focus_snapshot = Some(focus);
    playback.surface = PlaybackSurface::Chrome;
    crate::atongx::set_playback_grabs(app, &playback.surface);
    hide_guide(app);
    nest.open_url_detached(url, NestMode::Play);
    let _ = app.emit("playback-changed", playback.status());
    Ok(())
}

pub async fn stop_playback_surface(nest: &NestManager, ota: &OtaSession, surface: PlaybackSurface) {
    match surface {
        PlaybackSurface::Chrome => {
            nest.exit_play().await;
        }
        PlaybackSurface::Ota => {
            let pid = ota.latest_mpv_pid();
            wm::nudge_mpv_stop(pid);
            ota.stop().await;
        }
        PlaybackSurface::Idle => {}
    }
}

pub fn finish_return_to_guide(app: &AppHandle, playback: &mut PlaybackController) {
    playback.surface = PlaybackSurface::Idle;
    crate::atongx::set_playback_grabs(app, &playback.surface);
    restore_guide_fullscreen(app);
    let status = playback.status();
    let _ = app.emit("playback-changed", &status);
    if let Some(focus) = status.focus.clone() {
        let _ = app.emit("focus-restore", focus);
    }
}

pub fn toggle_play_pause(nest: &NestManager, ota: &OtaSession, playback: &PlaybackController) {
    match playback.surface {
        PlaybackSurface::Chrome => {
            let nest = nest.clone();
            tauri::async_runtime::spawn(async move {
                nest.send_key("space").await;
            });
        }
        PlaybackSurface::Ota => {
            let pid = ota.latest_mpv_pid();
            wm::nudge_mpv_fullscreen(pid);
            crate::nest::send_media_key("space");
        }
        PlaybackSurface::Idle => {}
    }
}
