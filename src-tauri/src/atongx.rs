//! Global ATONGX shortcuts so the remote still works when the guide is hidden
//! behind the Chrome nest or mpv. D-pad / OK stay with the focused surface.
//! This is Zappe HID mapping — not a Samsung TV API.

use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut, ShortcutState};

use crate::remote_stubs::{remote_mute, remote_volume};
use crate::{return_to_guide, AppState};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum GlobalAction {
    Back,
    Home,
    PlayPause,
    Menu,
    Voice,
    Pointer,
    VolumeUp,
    VolumeDown,
    Mute,
}

fn action_for(code: Code) -> Option<GlobalAction> {
    Some(match code {
        Code::Escape | Code::BrowserBack | Code::Delete => GlobalAction::Back,
        Code::Home | Code::BrowserHome => GlobalAction::Home,
        Code::MediaPlayPause => GlobalAction::PlayPause,
        Code::F1 | Code::ContextMenu => GlobalAction::Menu,
        Code::F8 | Code::F9 => GlobalAction::Voice,
        Code::F2 => GlobalAction::Pointer,
        Code::AudioVolumeUp => GlobalAction::VolumeUp,
        Code::AudioVolumeDown => GlobalAction::VolumeDown,
        Code::AudioVolumeMute => GlobalAction::Mute,
        _ => return None,
    })
}

pub fn plugin() -> tauri::plugin::TauriPlugin<tauri::Wry> {
    tauri_plugin_global_shortcut::Builder::new()
        .with_handler(|app, shortcut, event| {
            if event.state != ShortcutState::Pressed {
                return;
            }
            let Some(action) = action_for(shortcut.key) else {
                return;
            };
            let handle = app.clone();
            tauri::async_runtime::spawn(async move {
                dispatch(handle, action).await;
            });
        })
        .build()
}

pub fn register(app: &AppHandle) {
    for code in [
        Code::Escape,
        Code::BrowserBack,
        Code::Delete,
        Code::Home,
        Code::BrowserHome,
        Code::MediaPlayPause,
        Code::F1,
        Code::ContextMenu,
        Code::F8,
        Code::F9,
        Code::F2,
        Code::AudioVolumeUp,
        Code::AudioVolumeDown,
        Code::AudioVolumeMute,
    ] {
        if let Err(err) = app.global_shortcut().register(Shortcut::new(None, code)) {
            log::warn!("ATONGX global {code:?} not registered: {err}");
        }
    }
}

async fn dispatch(app: AppHandle, action: GlobalAction) {
    match action {
        GlobalAction::Back => {
            let _ = crate::remote_back_inner(&app).await;
        }
        GlobalAction::Home => {
            return_to_guide(&app).await;
        }
        GlobalAction::PlayPause => {
            let state = app.state::<AppState>();
            crate::playback::toggle_play_pause(
                &state.nest,
                &state.ota,
                &state.playback.lock().unwrap(),
            );
        }
        GlobalAction::Menu => {
            return_to_guide(&app).await;
            let _ = app.emit("guide-menu", ());
        }
        GlobalAction::Voice => {
            let _ = app.emit("voice-arm", ());
        }
        GlobalAction::Pointer => {
            let _ = app.emit("guide-pointer", ());
        }
        GlobalAction::VolumeUp => {
            let _ = remote_volume(1);
        }
        GlobalAction::VolumeDown => {
            let _ = remote_volume(-1);
        }
        GlobalAction::Mute => {
            let _ = remote_mute();
        }
    }
}
