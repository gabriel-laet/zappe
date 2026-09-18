//! ATONGX / XING WEI input: evdev first, then a small global-shortcut fallback.
//!
//! Consumer Control keys (Back / Home / Play-Pause / Menu) do not register as
//! Tauri `KeyboardEvent.code` shortcuts under gamescope. We read those from
//! `/dev/input/event*` instead. D-pad / OK stay on the keyboard node so the
//! focused nest or mpv still sees them — this module only emits `atongx-action`
//! for those. Volume / mute / power / voice / Home / Back are dispatched here.
//!
//! This is Zappe HID mapping — not a Samsung TV API.

use std::collections::HashMap;
use std::sync::{Mutex, OnceLock};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};
use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut, ShortcutState};

use crate::atongx_map::{action_for_evkey, lookup_evkey, AtongxAction, AtongxDispatch};
use crate::remote_stubs::{remote_mute, remote_volume};
use crate::{return_to_guide, AppState};

const DEBOUNCE: Duration = Duration::from_millis(160);

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AtongxInputEvent {
    pub action: AtongxAction,
    pub key: String,
    pub code: u16,
    pub value: i32,
    pub iface: &'static str,
    pub device: String,
    pub dispatch: &'static str,
}

fn last_fire() -> &'static Mutex<HashMap<AtongxAction, Instant>> {
    static LAST: OnceLock<Mutex<HashMap<AtongxAction, Instant>>> = OnceLock::new();
    LAST.get_or_init(|| Mutex::new(HashMap::new()))
}

fn accept(action: AtongxAction) -> bool {
    let mut map = last_fire().lock().expect("atongx debounce");
    if let Some(prev) = map.get(&action) {
        if prev.elapsed() < DEBOUNCE {
            return false;
        }
    }
    map.insert(action, Instant::now());
    true
}

fn linux_code_for_shortcut(code: Code) -> Option<u16> {
    // Only codes that actually register as global shortcuts on this stack.
    // BrowserBack / BrowserHome / MediaPlayPause / ContextMenu are evdev-only.
    Some(match code {
        Code::Escape => 1,            // KEY_ESC
        Code::Home => 102,            // KEY_HOME
        Code::Delete => 111,          // KEY_DELETE
        Code::F1 => 59,               // KEY_F1
        Code::F2 => 60,               // KEY_F2
        Code::F6 => 64,               // KEY_F6
        Code::F7 => 65,               // KEY_F7
        Code::F8 => 66,               // KEY_F8
        Code::F9 => 67,               // KEY_F9
        Code::F10 => 68,              // KEY_F10
        Code::AudioVolumeUp => 115,   // KEY_VOLUMEUP
        Code::AudioVolumeDown => 114, // KEY_VOLUMEDOWN
        Code::AudioVolumeMute => 113, // KEY_MUTE
        _ => return None,
    })
}

pub fn plugin() -> tauri::plugin::TauriPlugin<tauri::Wry> {
    tauri_plugin_global_shortcut::Builder::new()
        .with_handler(|app, shortcut, event| {
            if event.state != ShortcutState::Pressed {
                return;
            }
            let Some(code) = linux_code_for_shortcut(shortcut.key) else {
                return;
            };
            let Some(action) = action_for_evkey(code) else {
                return;
            };
            if !action.is_global() {
                return;
            }
            let handle = app.clone();
            tauri::async_runtime::spawn(async move {
                dispatch(handle, action, "shortcut").await;
            });
        })
        .build()
}

pub fn register(app: &AppHandle) {
    #[cfg(target_os = "linux")]
    evdev_linux::spawn(app.clone());

    for code in [
        Code::Escape,
        Code::Home,
        Code::Delete,
        Code::F1,
        Code::F2,
        Code::F6,
        Code::F7,
        Code::F8,
        Code::F9,
        Code::F10,
        Code::AudioVolumeUp,
        Code::AudioVolumeDown,
        Code::AudioVolumeMute,
    ] {
        if let Err(err) = app.global_shortcut().register(Shortcut::new(None, code)) {
            log::warn!("ATONGX fallback shortcut {code:?} not registered: {err}");
        }
    }
}

fn emit_action(app: &AppHandle, ev: AtongxInputEvent) {
    let _ = app.emit("atongx-action", ev);
}

async fn dispatch(app: AppHandle, action: AtongxAction, source: &str) {
    if !accept(action) {
        return;
    }
    log::info!("ATONGX {source} {}", action.as_str());
    match action {
        AtongxAction::Back | AtongxAction::Delete => {
            let _ = crate::remote_back_inner(&app).await;
        }
        AtongxAction::Home => {
            return_to_guide(&app).await;
        }
        AtongxAction::PlayPause => {
            let state = app.state::<AppState>();
            crate::playback::toggle_play_pause(
                &state.nest,
                &state.ota,
                &state.playback.lock().unwrap(),
            );
        }
        AtongxAction::Menu => {
            return_to_guide(&app).await;
            let _ = app.emit("guide-menu", ());
        }
        AtongxAction::Voice => {
            let _ = app.emit("voice-arm", ());
        }
        AtongxAction::Pointer => {
            let _ = app.emit("guide-pointer", ());
        }
        AtongxAction::VolumeUp => {
            let _ = remote_volume(1);
        }
        AtongxAction::VolumeDown => {
            let _ = remote_volume(-1);
        }
        AtongxAction::Mute => {
            let _ = remote_mute();
        }
        AtongxAction::Power => {
            let _ = crate::remote_power_inner(&app).await;
        }
        AtongxAction::Up
        | AtongxAction::Down
        | AtongxAction::Left
        | AtongxAction::Right
        | AtongxAction::Ok
        | AtongxAction::PageUp
        | AtongxAction::PageDown => {}
    }
}

pub(crate) fn handle_evkey(app: &AppHandle, code: u16, value: i32, device: &str) {
    if value != 1 {
        return;
    }
    let Some((action, dispatch_kind, iface, name)) = lookup_evkey(code) else {
        return;
    };
    emit_action(
        app,
        AtongxInputEvent {
            action,
            key: name.to_string(),
            code,
            value,
            iface: iface.as_str(),
            device: device.to_string(),
            dispatch: match dispatch_kind {
                AtongxDispatch::Global => "global",
                AtongxDispatch::Focus => "focus",
            },
        },
    );
    if dispatch_kind != AtongxDispatch::Global {
        return;
    }
    let handle = app.clone();
    tauri::async_runtime::spawn(async move {
        dispatch(handle, action, "evdev").await;
    });
}

#[cfg(target_os = "linux")]
mod evdev_linux {
    use std::collections::HashSet;
    use std::path::PathBuf;
    use std::sync::{Arc, Mutex};
    use std::time::Duration;

    use evdev::{Device, EventType, KeyCode};
    use tauri::AppHandle;

    use crate::atongx_map::{name_matches, should_grab, usb_product, usb_vendor, AtongxIface};

    pub fn spawn(app: AppHandle) {
        if std::env::var_os("ZAPPE_ATONGX_EVDEV")
            .map(|v| v == "0")
            .unwrap_or(false)
        {
            log::info!("ATONGX evdev disabled (ZAPPE_ATONGX_EVDEV=0)");
            return;
        }
        std::thread::Builder::new()
            .name("atongx-evdev".into())
            .spawn(move || watch(app))
            .expect("atongx evdev thread");
    }

    fn watch(app: AppHandle) {
        let opened: Arc<Mutex<HashSet<PathBuf>>> = Arc::new(Mutex::new(HashSet::new()));
        loop {
            open_new(&app, &opened);
            std::thread::sleep(Duration::from_secs(4));
        }
    }

    fn open_new(app: &AppHandle, opened: &Arc<Mutex<HashSet<PathBuf>>>) {
        for (path, device) in evdev::enumerate() {
            {
                let seen = opened.lock().expect("atongx evdev paths");
                if seen.contains(&path) {
                    continue;
                }
            }
            if !is_atongx(&device) {
                continue;
            }
            let iface = classify_iface(&path, &device);
            if iface == AtongxIface::Mouse {
                continue;
            }
            let name = device
                .name()
                .unwrap_or("XING WEI")
                .to_string();
            let grab = should_grab(iface)
                && !std::env::var_os("ZAPPE_ATONGX_GRAB")
                    .map(|v| v == "0")
                    .unwrap_or(false);
            let mut device = device;
            if grab {
                match device.grab() {
                    Ok(()) => log::info!("ATONGX evdev grab {} ({})", path.display(), name),
                    Err(err) => log::warn!(
                        "ATONGX evdev grab {} failed (need input group?): {err}",
                        path.display()
                    ),
                }
            }
            opened.lock().expect("atongx evdev paths").insert(path.clone());
            log::info!(
                "ATONGX evdev listen {} iface={} name={name}",
                path.display(),
                iface.as_str()
            );
            let app = app.clone();
            let opened = Arc::clone(opened);
            std::thread::Builder::new()
                .name(format!("atongx-{}", iface.as_str()))
                .spawn(move || {
                    read_device(app, path.clone(), device, name);
                    opened.lock().expect("atongx evdev paths").remove(&path);
                })
                .ok();
        }
    }

    fn is_atongx(device: &Device) -> bool {
        if device.name().map(name_matches).unwrap_or(false) {
            return true;
        }
        let id = device.input_id();
        id.vendor() == usb_vendor() && id.product() == usb_product()
    }

    fn has_key(device: &Device, code: u16) -> bool {
        device
            .supported_keys()
            .map(|keys| keys.contains(KeyCode::new(code)))
            .unwrap_or(false)
    }

    fn classify_iface(path: &std::path::Path, device: &Device) -> AtongxIface {
        let name = device.name().unwrap_or("");
        let path_s = path.to_string_lossy();
        if name.contains("Consumer Control") || path_s.contains("event-if03") {
            return AtongxIface::Consumer;
        }
        if name.contains("System Control") {
            return AtongxIface::System;
        }
        if path_s.contains("event-mouse") || name.to_ascii_lowercase().contains("mouse") {
            return AtongxIface::Mouse;
        }
        if path_s.contains("event-kbd") {
            return AtongxIface::Keyboard;
        }
        let has_rel = device.supported_events().contains(EventType::RELATIVE);
        // Consumer Control typically advertises AC Back / AC Home / PlayPause.
        if has_key(device, 158) || has_key(device, 172) || has_key(device, 164) {
            return AtongxIface::Consumer;
        }
        // System Control is KEY_POWER without a D-pad.
        if has_key(device, 116) && !has_key(device, 28) && !has_key(device, 103) {
            return AtongxIface::System;
        }
        if has_rel && !has_key(device, 103) {
            return AtongxIface::Mouse;
        }
        AtongxIface::Keyboard
    }

    fn read_device(app: AppHandle, path: PathBuf, mut device: Device, name: String) {
        loop {
            match device.fetch_events() {
                Ok(events) => {
                    for ev in events {
                        if ev.event_type() != EventType::KEY {
                            continue;
                        }
                        super::handle_evkey(&app, ev.code(), ev.value(), &name);
                    }
                }
                Err(err) => {
                    log::warn!("ATONGX evdev {} closed: {err}", path.display());
                    break;
                }
            }
        }
    }
}
