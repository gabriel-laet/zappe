//! Global ATONGX shortcuts so the remote still works when the guide is hidden
//! behind the Chrome nest or mpv.
//!
//! Always-on: Home / Back / Play-Pause / Menu / Mic / VOL / Mute / pointer.
//! While Chrome is playing: D-pad + OK + Space are grabbed and injected into
//! the nest (see `nest_input`). They are unregistered on return-to-guide so
//! the Home shelves keep spatial focus. This is Zappe HID mapping — not a
//! Samsung TV API.
//!
//! Back / Home themselves are owned by the return-to-guide path. Do not send
//! those into Netflix from here.

use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut, ShortcutState};

use crate::nest_input;
use crate::playback::PlaybackSurface;
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
    NestUp,
    NestDown,
    NestLeft,
    NestRight,
    NestOk,
}

fn action_for(code: Code) -> Option<GlobalAction> {
    Some(match code {
        Code::Escape | Code::BrowserBack | Code::Delete => GlobalAction::Back,
        Code::Home | Code::BrowserHome => GlobalAction::Home,
        Code::MediaPlayPause | Code::Space => GlobalAction::PlayPause,
        Code::F1 | Code::ContextMenu => GlobalAction::Menu,
        Code::F8 | Code::F9 => GlobalAction::Voice,
        Code::F2 => GlobalAction::Pointer,
        Code::AudioVolumeUp => GlobalAction::VolumeUp,
        Code::AudioVolumeDown => GlobalAction::VolumeDown,
        Code::AudioVolumeMute => GlobalAction::Mute,
        Code::ArrowUp => GlobalAction::NestUp,
        Code::ArrowDown => GlobalAction::NestDown,
        Code::ArrowLeft => GlobalAction::NestLeft,
        Code::ArrowRight => GlobalAction::NestRight,
        Code::Enter | Code::NumpadEnter => GlobalAction::NestOk,
        _ => return None,
    })
}

const NEST_NAV_CODES: &[Code] = &[
    Code::ArrowUp,
    Code::ArrowDown,
    Code::ArrowLeft,
    Code::ArrowRight,
    Code::Enter,
    Code::NumpadEnter,
];

const PLAY_SPACE: &[Code] = &[Code::Space];

/// Grab D-pad/OK only while the Chrome nest is up. Grab Space while Chrome or
/// mpv is playing. Unregister on Idle so the guide keeps those keys.
pub fn set_playback_grabs(app: &AppHandle, surface: &PlaybackSurface) {
    let nest_nav = matches!(surface, PlaybackSurface::Chrome);
    let play_space = matches!(surface, PlaybackSurface::Chrome | PlaybackSurface::Ota);
    set_codes(app, NEST_NAV_CODES, nest_nav);
    set_codes(app, PLAY_SPACE, play_space);
}

fn set_codes(app: &AppHandle, codes: &[Code], want: bool) {
    for code in codes {
        let shortcut = Shortcut::new(None, *code);
        let result = if want {
            app.global_shortcut().register(shortcut)
        } else {
            app.global_shortcut().unregister(shortcut)
        };
        if let Err(err) = result {
            log::debug!("ATONGX playback grab {code:?} want={want}: {err}");
        }
    }
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
        GlobalAction::NestUp => inject_nest(&app, "up"),
        GlobalAction::NestDown => inject_nest(&app, "down"),
        GlobalAction::NestLeft => inject_nest(&app, "left"),
        GlobalAction::NestRight => inject_nest(&app, "right"),
        GlobalAction::NestOk => inject_nest(&app, "ok"),
        GlobalAction::Menu => {
            return_to_guide(&app).await;
            let _ = app.emit("guide-menu", ());
        }
        GlobalAction::Voice => {
            let _ = app.emit("voice-arm", ());
        }
        GlobalAction::Pointer => {
            crate::remote_pointer_inner(&app);
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

fn inject_nest(app: &AppHandle, action: &str) {
    let state = app.state::<AppState>();
    let surface = state.playback.lock().unwrap().surface.clone();
    if surface != PlaybackSurface::Chrome {
        return;
    }
    nest_input::inject_action(action);
}
