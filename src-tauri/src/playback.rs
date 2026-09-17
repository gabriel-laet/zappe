use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager};

use crate::chrome::{ChromeCmd, ChromeManager, find_chrome};
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

fn show_guide(app: &AppHandle) {
    if let Some(win) = app.get_webview_window("main") {
        let _ = win.unminimize();
        let _ = win.show();
        let _ = win.set_focus();
    }
}

pub fn begin_chrome(
    app: &AppHandle,
    chrome: &ChromeManager,
    playback: &mut PlaybackController,
    service: crate::skills::Service,
    url: String,
    title: String,
    focus: GuideFocus,
) -> Result<(), String> {
    if find_chrome().is_none() {
        return Err(
            "Google Chrome or Chromium is required. On Arch/Omarchy: sudo pacman -S google-chrome or chromium."
                .into(),
        );
    }
    playback.focus_snapshot = Some(focus);
    playback.surface = PlaybackSurface::Chrome;
    hide_guide(app);
    chrome.send(ChromeCmd::Open {
        url,
        service,
        title,
    });
    let _ = app.emit("playback-changed", playback.status());
    Ok(())
}

pub async fn begin_ota(
    app: &AppHandle,
    ota: &OtaSession,
    playback: &mut PlaybackController,
    channel: String,
    conf: std::path::PathBuf,
    focus: GuideFocus,
) -> Result<(), String> {
    playback.focus_snapshot = Some(focus);
    hide_guide(app);
    if let Err(err) = ota.play(&channel, &conf).await {
        playback.surface = PlaybackSurface::Idle;
        show_guide(app);
        return Err(err.to_string());
    }
    wm::nudge_mpv_fullscreen();
    playback.surface = PlaybackSurface::Ota;
    let _ = app.emit("playback-changed", playback.status());
    Ok(())
}

pub async fn stop_playback_surface(
    chrome: &ChromeManager,
    ota: &OtaSession,
    surface: PlaybackSurface,
) {
    match surface {
        PlaybackSurface::Chrome => {
            chrome.send(ChromeCmd::ExitFullscreen);
            chrome.send(ChromeCmd::Back);
        }
        PlaybackSurface::Ota => {
            wm::nudge_mpv_stop();
            ota.stop().await;
        }
        PlaybackSurface::Idle => {}
    }
}

pub fn finish_return_to_guide(
    app: &AppHandle,
    playback: &mut PlaybackController,
) {
    playback.surface = PlaybackSurface::Idle;
    show_guide(app);
    let status = playback.status();
    let _ = app.emit("playback-changed", &status);
    if let Some(focus) = status.focus.clone() {
        let _ = app.emit("focus-restore", focus);
    }
}

pub async fn end_playback(
    app: &AppHandle,
    chrome: &ChromeManager,
    ota: &OtaSession,
    playback: &mut PlaybackController,
) {
    let surface = playback.surface.clone();
    if surface == PlaybackSurface::Idle {
        return;
    }
    stop_playback_surface(chrome, ota, surface).await;
    finish_return_to_guide(app, playback);
}

pub fn toggle_play_pause(chrome: &ChromeManager, playback: &PlaybackController) {
    if playback.surface == PlaybackSurface::Chrome {
        chrome.send(ChromeCmd::PlayPause);
    }
}
