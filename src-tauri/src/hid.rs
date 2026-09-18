//! Evdev watcher for the ATONGX / XING WEI air mouse.
//!
//! Nested gamescope+Chrome eats compositor shortcuts, and
//! `tauri-plugin-global-shortcut` fails to register BrowserBack / BrowserHome
//! ("Unknown scancode"). This listener reads `/dev/input/event*` directly so
//! Back, Home, and Power still return to the guide.
//!
//! Consumer-control nodes are grabbed so the nest cannot consume those keys.
//! The keyboard node is watched without grab so D-pad / OK stay with Chrome.

use std::sync::Mutex;
use std::time::{Duration, Instant};

use tauri::{AppHandle, Emitter, Manager};

use crate::remote_stubs::{remote_mute, remote_volume};

/// Linux `input-event-codes.h` values we actually dispatch.
pub const KEY_ESC: u16 = 1;
pub const KEY_BACKSPACE: u16 = 14;
pub const KEY_F1: u16 = 59;
pub const KEY_F2: u16 = 60;
pub const KEY_F8: u16 = 66;
pub const KEY_F9: u16 = 67;
pub const KEY_HOME: u16 = 102;
pub const KEY_DELETE: u16 = 111;
pub const KEY_MUTE: u16 = 113;
pub const KEY_VOLUMEDOWN: u16 = 114;
pub const KEY_VOLUMEUP: u16 = 115;
pub const KEY_POWER: u16 = 116;
pub const KEY_COMPOSE: u16 = 127;
pub const KEY_MENU: u16 = 139;
pub const KEY_SLEEP: u16 = 142;
pub const KEY_BACK: u16 = 158;
pub const KEY_PLAYPAUSE: u16 = 164;
pub const KEY_RECORD: u16 = 167;
pub const KEY_HOMEPAGE: u16 = 172;
pub const KEY_EXIT: u16 = 174;
pub const KEY_POWER2: u16 = 226;
pub const KEY_CONTEXT_MENU: u16 = 0x1b6;
pub const KEY_VOICECOMMAND: u16 = 0x246;

const DEBOUNCE: Duration = Duration::from_millis(280);
const RESCAN: Duration = Duration::from_secs(2);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum HidAction {
    Back,
    Home,
    Power,
    Menu,
    PlayPause,
    Voice,
    Pointer,
    VolumeUp,
    VolumeDown,
    Mute,
}

/// Full consumer-control map (grabbed node — nest never sees these).
pub fn action_for_keycode(code: u16) -> Option<HidAction> {
    Some(match code {
        KEY_ESC | KEY_BACKSPACE | KEY_DELETE | KEY_BACK | KEY_EXIT => HidAction::Back,
        KEY_HOME | KEY_HOMEPAGE => HidAction::Home,
        KEY_POWER | KEY_POWER2 | KEY_SLEEP => HidAction::Power,
        KEY_F1 | KEY_COMPOSE | KEY_MENU | KEY_CONTEXT_MENU => HidAction::Menu,
        KEY_PLAYPAUSE => HidAction::PlayPause,
        KEY_F8 | KEY_F9 | KEY_RECORD | KEY_VOICECOMMAND => HidAction::Voice,
        KEY_F2 => HidAction::Pointer,
        KEY_VOLUMEUP => HidAction::VolumeUp,
        KEY_VOLUMEDOWN => HidAction::VolumeDown,
        KEY_MUTE => HidAction::Mute,
        _ => return None,
    })
}

/// Keyboard node is not grabbed. Only nest-escape keys are handled here so
/// D-pad / OK / mic / pointer keep flowing to the focused surface + JS.
pub fn action_for_keyboard_passthrough(code: u16) -> Option<HidAction> {
    match action_for_keycode(code) {
        Some(action @ (HidAction::Back | HidAction::Home | HidAction::Power)) => Some(action),
        _ => None,
    }
}

/// Match the living-room dongle. Name is typically
/// `XING WEI 2.4G USB USB Composite Device`; by-id paths contain `XING_WEI`.
pub fn is_atongx_device(name: &str, path: &str) -> bool {
    if let Ok(extra) = std::env::var("ZAPPE_HID_NAME") {
        let extra = extra.trim();
        if !extra.is_empty() {
            let blob = format!("{name} {path}").to_ascii_lowercase();
            if blob.contains(&extra.to_ascii_lowercase()) {
                return true;
            }
        }
    }
    let blob = format!("{name} {path}").to_ascii_lowercase();
    blob.contains("xing wei")
        || blob.contains("xing_wei")
        || blob.contains("atongx")
        || blob.contains("2.4g usb")
}

pub fn is_mouse_only(has_rel_xy: bool, has_nest_escape_key: bool) -> bool {
    has_rel_xy && !has_nest_escape_key
}

pub fn has_nest_escape_key(keys: &[u16]) -> bool {
    keys.iter().any(|c| {
        matches!(
            *c,
            KEY_ESC | KEY_BACK | KEY_HOME | KEY_HOMEPAGE | KEY_EXIT | KEY_POWER | KEY_DELETE
        )
    })
}

/// Grab consumer-control (Back/Home/Power/media) so nest gamescope cannot
/// steal them. Never grab a full QWERTY board — D-pad lives there.
pub fn should_grab_device(keys: &[u16], has_letter_keys: bool) -> bool {
    if has_letter_keys {
        return false;
    }
    keys.iter().any(|c| {
        matches!(
            *c,
            KEY_BACK | KEY_HOMEPAGE | KEY_PLAYPAUSE | KEY_POWER | KEY_EXIT | KEY_MUTE
        )
    })
}

pub fn hid_disabled() -> bool {
    matches!(
        std::env::var("ZAPPE_HID_DISABLE")
            .map(|s| s.to_ascii_lowercase())
            .as_deref(),
        Ok("1") | Ok("true") | Ok("yes")
    )
}

static LAST: Mutex<Option<(HidAction, Instant)>> = Mutex::new(None);

pub fn should_dispatch(action: HidAction, now: Instant) -> bool {
    let mut guard = LAST.lock().unwrap_or_else(|e| e.into_inner());
    if let Some((prev, at)) = *guard {
        if prev == action && now.duration_since(at) < DEBOUNCE {
            return false;
        }
    }
    *guard = Some((action, now));
    true
}

pub fn start(app: AppHandle) {
    #[cfg(target_os = "linux")]
    linux::start(app);
    #[cfg(not(target_os = "linux"))]
    {
        let _ = app;
    }
}

#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use std::collections::HashSet;
    use std::path::PathBuf;
    use std::sync::{Arc, Mutex as StdMutex};

    use evdev::{Device, EventSummary, KeyCode, RelativeAxisCode};

    type Watched = Arc<StdMutex<HashSet<PathBuf>>>;

    pub fn start(app: AppHandle) {
        if hid_disabled() {
            log::info!("ATONGX evdev disabled (ZAPPE_HID_DISABLE)");
            return;
        }
        if let Err(err) = std::thread::Builder::new()
            .name("zappe-atongx-evdev".into())
            .spawn(move || watch_loop(app))
        {
            log::warn!("ATONGX evdev thread failed to start: {err}");
        }
    }

    fn watch_loop(app: AppHandle) {
        let watched: Watched = Arc::new(StdMutex::new(HashSet::new()));
        loop {
            for path in candidate_paths() {
                {
                    let mut guard = watched.lock().unwrap_or_else(|e| e.into_inner());
                    if !guard.insert(path.clone()) {
                        continue;
                    }
                }
                match Device::open(&path) {
                    Ok(device) => attach(app.clone(), path, device, watched.clone()),
                    Err(err) => {
                        watched
                            .lock()
                            .unwrap_or_else(|e| e.into_inner())
                            .remove(&path);
                        log::debug!("ATONGX evdev skip {}: {err}", path.display());
                    }
                }
            }
            std::thread::sleep(RESCAN);
        }
    }

    fn candidate_paths() -> Vec<PathBuf> {
        let mut paths = Vec::new();
        if let Ok(dir) = std::fs::read_dir("/dev/input/by-id") {
            for entry in dir.flatten() {
                let name = entry.file_name();
                let name = name.to_string_lossy();
                if name.to_ascii_lowercase().contains("xing")
                    || name.to_ascii_lowercase().contains("atongx")
                    || name.to_ascii_lowercase().contains("2.4g")
                {
                    if let Ok(real) = std::fs::canonicalize(entry.path()) {
                        paths.push(real);
                    }
                }
            }
        }
        for (path, device) in evdev::enumerate() {
            let name = device.name().unwrap_or("");
            let path_str = path.to_string_lossy();
            if is_atongx_device(name, &path_str) {
                paths.push(path);
            }
        }
        paths.sort();
        paths.dedup();
        paths
    }

    fn attach(app: AppHandle, path: PathBuf, mut device: Device, watched: Watched) {
        let name = device.name().unwrap_or("unknown").to_string();
        let path_str = path.to_string_lossy();
        if !is_atongx_device(&name, &path_str) {
            return;
        }

        let keys = supported_key_codes(&device);
        let has_letters = keys.iter().any(|c| (KEY_A..=KEY_Z).contains(c));
        let has_rel = device
            .supported_relative_axes()
            .is_some_and(|axes| axes.contains(RelativeAxisCode::REL_X));
        if is_mouse_only(has_rel, has_nest_escape_key(&keys)) {
            log::info!("ATONGX evdev ignore mouse-only {}", path.display());
            return;
        }
        if !has_nest_escape_key(&keys) && !keys.iter().any(|c| action_for_keycode(*c).is_some()) {
            return;
        }

        let grab = should_grab_device(&keys, has_letters);
        let grabbed = if grab {
            match device.grab() {
                Ok(()) => true,
                Err(err) => {
                    log::warn!(
                        "ATONGX evdev grab failed on {} ({name}): {err}",
                        path.display()
                    );
                    false
                }
            }
        } else {
            false
        };

        log::info!(
            "ATONGX evdev watching {name} path={} grab={grabbed} (want_grab={grab})",
            path.display()
        );

        let unwatch = path.clone();
        let watched_reader = watched.clone();
        if let Err(err) = std::thread::Builder::new()
            .name(format!("zappe-hid-{}", path.display()))
            .spawn({
                let unwatch = unwatch.clone();
                move || {
                    read_loop(app, path, device, grabbed);
                    watched_reader
                        .lock()
                        .unwrap_or_else(|e| e.into_inner())
                        .remove(&unwatch);
                }
            })
        {
            log::warn!("ATONGX evdev reader failed: {err}");
            watched
                .lock()
                .unwrap_or_else(|e| e.into_inner())
                .remove(&unwatch);
        }
    }

    const KEY_A: u16 = 30;
    const KEY_Z: u16 = 44;

    fn supported_key_codes(device: &Device) -> Vec<u16> {
        let Some(keys) = device.supported_keys() else {
            return Vec::new();
        };
        (0u16..=0x2ff)
            .filter(|code| keys.contains(KeyCode(*code)))
            .collect()
    }

    fn read_loop(app: AppHandle, path: PathBuf, mut device: Device, grabbed: bool) {
        loop {
            let events = match device.fetch_events() {
                Ok(iter) => iter,
                Err(err) => {
                    log::warn!("ATONGX evdev lost {}: {err}", path.display());
                    return;
                }
            };
            for event in events {
                let EventSummary::Key(_, key, 1) = event.destructure() else {
                    continue;
                };
                let code = key.0;
                let action = if grabbed {
                    action_for_keycode(code)
                } else {
                    action_for_keyboard_passthrough(code)
                };
                let Some(action) = action else {
                    continue;
                };
                if !should_dispatch(action, Instant::now()) {
                    continue;
                }
                log::info!(
                    "ATONGX evdev {action:?} code={code} from {}",
                    path.display()
                );
                let handle = app.clone();
                tauri::async_runtime::spawn(async move {
                    dispatch(handle, action).await;
                });
            }
        }
    }
}

async fn dispatch(app: AppHandle, action: HidAction) {
    match action {
        HidAction::Back => {
            let _ = crate::remote_back_inner(&app).await;
        }
        HidAction::Home => {
            crate::return_to_guide(&app).await;
        }
        HidAction::Power => {
            let _ = crate::remote_power_inner(&app).await;
        }
        HidAction::Menu => {
            crate::return_to_guide(&app).await;
            let _ = app.emit("guide-menu", ());
        }
        HidAction::PlayPause => {
            let state = app.state::<crate::AppState>();
            crate::playback::toggle_play_pause(
                &state.nest,
                &state.ota,
                &state.playback.lock().unwrap(),
            );
        }
        HidAction::Voice => {
            let _ = app.emit("voice-arm", ());
        }
        HidAction::Pointer => {
            let _ = app.emit("guide-pointer", ());
        }
        HidAction::VolumeUp => {
            let _ = remote_volume(1);
        }
        HidAction::VolumeDown => {
            let _ = remote_volume(-1);
        }
        HidAction::Mute => {
            let _ = remote_mute();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn documents_living_room_evdev_codes() {
        assert_eq!(action_for_keycode(KEY_BACK), Some(HidAction::Back));
        assert_eq!(action_for_keycode(KEY_ESC), Some(HidAction::Back));
        assert_eq!(action_for_keycode(KEY_EXIT), Some(HidAction::Back));
        assert_eq!(action_for_keycode(KEY_HOMEPAGE), Some(HidAction::Home));
        assert_eq!(action_for_keycode(KEY_HOME), Some(HidAction::Home));
        assert_eq!(action_for_keycode(KEY_POWER), Some(HidAction::Power));
        assert_eq!(action_for_keycode(KEY_POWER2), Some(HidAction::Power));
        assert_eq!(action_for_keycode(KEY_SLEEP), Some(HidAction::Power));
        assert_eq!(KEY_BACK, 158);
        assert_eq!(KEY_HOMEPAGE, 172);
        assert_eq!(KEY_POWER, 116);
        assert_eq!(KEY_ESC, 1);
        assert_eq!(KEY_HOME, 102);
    }

    #[test]
    fn keyboard_passthrough_is_only_nest_escape() {
        assert_eq!(
            action_for_keyboard_passthrough(KEY_BACK),
            Some(HidAction::Back)
        );
        assert_eq!(
            action_for_keyboard_passthrough(KEY_HOMEPAGE),
            Some(HidAction::Home)
        );
        assert_eq!(
            action_for_keyboard_passthrough(KEY_POWER),
            Some(HidAction::Power)
        );
        assert_eq!(action_for_keyboard_passthrough(KEY_VOLUMEUP), None);
        assert_eq!(action_for_keyboard_passthrough(KEY_PLAYPAUSE), None);
        assert_eq!(action_for_keyboard_passthrough(KEY_F9), None);
        assert_eq!(action_for_keyboard_passthrough(30), None); // KEY_A
    }

    #[test]
    fn xing_wei_name_and_by_id_match() {
        assert!(is_atongx_device(
            "XING WEI 2.4G USB USB Composite Device",
            "/dev/input/event5"
        ));
        assert!(is_atongx_device(
            "",
            "/dev/input/by-id/usb-XING_WEI_2.4G_USB_USB_Composite_Device-if02-event-kbd"
        ));
        assert!(!is_atongx_device("Power Button", "/dev/input/event0"));
        assert!(!is_atongx_device(
            "AT Translated Set 2 keyboard",
            "/dev/input/event2"
        ));
    }

    #[test]
    fn grab_consumer_not_qwerty() {
        let consumer = [KEY_BACK, KEY_HOMEPAGE, KEY_PLAYPAUSE, KEY_VOLUMEUP];
        assert!(should_grab_device(&consumer, false));
        let keyboard = [KEY_ESC, KEY_HOME, KEY_A];
        const KEY_A: u16 = 30;
        assert!(!should_grab_device(&keyboard, true));
        assert!(is_mouse_only(true, false));
        assert!(!is_mouse_only(true, true));
    }

    #[test]
    fn debounce_drops_duplicate_back_from_two_nodes() {
        *LAST.lock().unwrap_or_else(|e| e.into_inner()) = None;
        let a = HidAction::Back;
        let t0 = Instant::now();
        assert!(should_dispatch(a, t0));
        assert!(!should_dispatch(a, t0 + Duration::from_millis(40)));
        assert!(should_dispatch(
            HidAction::Home,
            t0 + Duration::from_millis(40)
        ));
        assert!(should_dispatch(a, t0 + Duration::from_millis(400)));
    }
}
