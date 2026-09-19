//! Evdev watcher for the ATONGX / XING WEI air mouse.
//!
//! Nested gamescope+Chrome eats compositor shortcuts, and
//! `tauri-plugin-global-shortcut` fails to register BrowserBack / BrowserHome
//! / Mic / some pointer keys ("Unknown scancode"). This listener reads
//! `/dev/input/event*` directly so Back, Home, Power, Mic, and Pointer still
//! reach Zappe.
//!
//! Keycodes come from [`crate::atongx_map`] (`src/lib/atongx-map.json`).
//! Consumer-control nodes are grabbed so the nest cannot consume those keys.
//! The keyboard node is watched without grab so D-pad / OK stay with Chrome.

use std::sync::Mutex;
use std::time::{Duration, Instant};

use tauri::{AppHandle, Emitter, Manager};

use crate::remote::{apply_mute, apply_volume};

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
pub const KEY_SEARCH: u16 = 217;
pub const KEY_CONTEXT_MENU: u16 = 0x1b6;
pub const KEY_RED: u16 = 0x18e;
pub const KEY_TOUCHPAD_TOGGLE: u16 = 0x212;
pub const KEY_VOICECOMMAND: u16 = 0x246;
/// Living-room alias: Linux has no `KEY_MIC`. ATONGX dumps often label the red
/// button Voice / `KEY_VOICECOMMAND`. Rematch with `ZAPPE_HID_VOICE_CODE`.
pub const KEY_MIC: u16 = KEY_VOICECOMMAND;
pub const KEY_ASSISTANT: u16 = 0x247;
pub const KEY_DICTATE: u16 = 0x24a;

const DEBOUNCE: Duration = Duration::from_millis(280);
const RESCAN: Duration = Duration::from_secs(2);
/// Linux `TASK_COMM_LEN` is 16 including NUL. Longer names fail `thread::spawn`.
const EVDEV_THREAD_NAME: &str = "zappe-hid-evdev";

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

/// Parse `ZAPPE_HID_VOICE_CODE` (comma / space / semicolon separated).
pub fn parse_voice_codes(raw: &str) -> Vec<u16> {
    raw.split([',', ' ', ';'])
        .filter_map(|p| p.trim().parse::<u16>().ok())
        .collect()
}

/// Extra Voice evdev codes from `ZAPPE_HID_VOICE_CODE`.
/// Use this when a live `evtest` dump disagrees with the built-in map.
pub fn extra_voice_codes() -> Vec<u16> {
    std::env::var("ZAPPE_HID_VOICE_CODE")
        .ok()
        .map(|s| parse_voice_codes(&s))
        .unwrap_or_default()
}

/// Global / nest-control actions from the shared JSON map.
/// D-pad / OK / PAGE stay `None` so they are never swallowed by evdev.
pub fn action_for_keycode(code: u16) -> Option<HidAction> {
    if extra_voice_codes().contains(&code) {
        return Some(HidAction::Voice);
    }
    use crate::atongx_map::AtongxAction;
    Some(match crate::atongx_map::action_for_evkey(code)? {
        AtongxAction::Back | AtongxAction::Delete => HidAction::Back,
        AtongxAction::Home => HidAction::Home,
        AtongxAction::Power => HidAction::Power,
        AtongxAction::Menu => HidAction::Menu,
        AtongxAction::PlayPause => HidAction::PlayPause,
        AtongxAction::Voice => HidAction::Voice,
        AtongxAction::Pointer => HidAction::Pointer,
        AtongxAction::VolumeUp => HidAction::VolumeUp,
        AtongxAction::VolumeDown => HidAction::VolumeDown,
        AtongxAction::Mute => HidAction::Mute,
        AtongxAction::Up
        | AtongxAction::Down
        | AtongxAction::Left
        | AtongxAction::Right
        | AtongxAction::Ok
        | AtongxAction::PageUp
        | AtongxAction::PageDown => return None,
    })
}

/// Keyboard node is not grabbed. Nest-escape plus Mic / Pointer so those
/// still fire while the guide is hidden. D-pad / OK stay with Chrome.
pub fn action_for_keyboard_passthrough(code: u16) -> Option<HidAction> {
    match action_for_keycode(code) {
        Some(
            action @ (HidAction::Back
            | HidAction::Home
            | HidAction::Power
            | HidAction::Voice
            | HidAction::Pointer),
        ) => Some(action),
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
    crate::atongx_map::name_matches(&format!("{name} {path}"))
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

/// Grab consumer-control (Back/Home/Power/media/Mic) so nest gamescope cannot
/// steal them. Never grab a full QWERTY board — D-pad lives there. Keyboard
/// F-keys (pointer KEY_F2, mic F8/F9) are not grab keys.
pub fn should_grab_device(keys: &[u16], has_letter_keys: bool) -> bool {
    if has_letter_keys {
        return false;
    }
    keys.iter().any(|c| crate::atongx_map::is_grab_keycode(*c))
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
            .name(EVDEV_THREAD_NAME.into())
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

    fn unwatch(watched: &Watched, path: &PathBuf) {
        watched
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .remove(path);
    }

    fn attach(app: AppHandle, path: PathBuf, mut device: Device, watched: Watched) {
        let name = device.name().unwrap_or("unknown").to_string();
        let path_str = path.to_string_lossy();
        if !is_atongx_device(&name, &path_str) {
            unwatch(&watched, &path);
            return;
        }

        let keys = supported_key_codes(&device);
        let has_letters = keys.iter().any(|c| (KEY_A..=KEY_Z).contains(c));
        let has_rel = device
            .supported_relative_axes()
            .is_some_and(|axes| axes.contains(RelativeAxisCode::REL_X));
        if is_mouse_only(has_rel, has_nest_escape_key(&keys)) {
            log::info!("ATONGX evdev ignore mouse-only {}", path.display());
            unwatch(&watched, &path);
            return;
        }
        if !has_nest_escape_key(&keys) && !keys.iter().any(|c| action_for_keycode(*c).is_some()) {
            unwatch(&watched, &path);
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

        let unwatch_path = path.clone();
        let watched_reader = watched.clone();
        if let Err(err) = std::thread::Builder::new()
            .name(EVDEV_THREAD_NAME.into())
            .spawn({
                let unwatch_path = unwatch_path.clone();
                move || {
                    read_loop(app, path, device, grabbed);
                    unwatch(&watched_reader, &unwatch_path);
                }
            })
        {
            log::warn!("ATONGX evdev reader failed: {err}");
            unwatch(&watched, &unwatch_path);
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
                let EventSummary::Key(_, key, value) = event.destructure() else {
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
                if value == 0 {
                    if action == HidAction::Voice {
                        log::info!(
                            "ATONGX evdev Voice release code={code} from {}",
                            path.display()
                        );
                        let _ = app.emit("voice-release", ());
                    }
                    continue;
                }
                if value != 1 {
                    continue;
                }
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
            crate::remote_pointer_inner(&app);
        }
        HidAction::VolumeUp => {
            let _ = apply_volume(Some(&app), 1).await;
        }
        HidAction::VolumeDown => {
            let _ = apply_volume(Some(&app), -1).await;
        }
        HidAction::Mute => {
            let _ = apply_mute(Some(&app)).await;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn evdev_thread_name_fits_linux_task_comm() {
        assert!(
            EVDEV_THREAD_NAME.len() <= 15,
            "pthread name {:?} is {} chars; Linux max is 15",
            EVDEV_THREAD_NAME,
            EVDEV_THREAD_NAME.len()
        );
        assert_eq!(EVDEV_THREAD_NAME, "zappe-hid-evdev");
    }

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
        assert_eq!(action_for_keycode(KEY_BACKSPACE), Some(HidAction::Back));
        assert_eq!(action_for_keycode(KEY_F1), Some(HidAction::Menu));
        assert_eq!(action_for_keycode(KEY_COMPOSE), Some(HidAction::Menu));
        assert_eq!(action_for_keycode(KEY_MENU), Some(HidAction::Menu));
        assert_eq!(action_for_keycode(KEY_CONTEXT_MENU), Some(HidAction::Menu));
        assert_eq!(action_for_keycode(KEY_MUTE), Some(HidAction::Mute));
        assert_eq!(
            action_for_keycode(KEY_VOLUMEDOWN),
            Some(HidAction::VolumeDown)
        );
        assert_eq!(KEY_BACK, 158);
        assert_eq!(KEY_HOMEPAGE, 172);
        assert_eq!(KEY_POWER, 116);
        assert_eq!(KEY_ESC, 1);
        assert_eq!(KEY_HOME, 102);
        assert_eq!(action_for_keycode(KEY_F8), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_F9), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_RECORD), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_VOICECOMMAND), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_MIC), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_ASSISTANT), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_DICTATE), Some(HidAction::Voice));
        assert_eq!(KEY_MIC, KEY_VOICECOMMAND);
        assert_eq!(KEY_VOICECOMMAND, 0x246);
    }

    #[test]
    fn mic_and_pointer_are_recognized_not_unknown() {
        assert_eq!(action_for_keycode(KEY_SEARCH), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_VOICECOMMAND), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_ASSISTANT), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_RECORD), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_F8), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_F9), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_RED), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_DICTATE), Some(HidAction::Voice));
        assert_eq!(action_for_keycode(KEY_F2), Some(HidAction::Pointer));
        assert_eq!(
            action_for_keycode(KEY_TOUCHPAD_TOGGLE),
            Some(HidAction::Pointer)
        );
        assert_eq!(KEY_SEARCH, 217);
        assert_eq!(KEY_VOICECOMMAND, 0x246);
        assert_eq!(KEY_F2, 60);
        assert_eq!(KEY_TOUCHPAD_TOGGLE, 0x212);
        assert_eq!(action_for_keycode(103), None); // KEY_UP — D-pad not swallowed
        assert_eq!(action_for_keycode(28), None); // KEY_ENTER
    }

    #[test]
    fn keyboard_passthrough_is_nest_escape_plus_mic_pointer() {
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
        assert_eq!(
            action_for_keyboard_passthrough(KEY_F9),
            Some(HidAction::Voice)
        );
        assert_eq!(
            action_for_keyboard_passthrough(KEY_SEARCH),
            Some(HidAction::Voice)
        );
        assert_eq!(
            action_for_keyboard_passthrough(KEY_MIC),
            Some(HidAction::Voice)
        );
        assert_eq!(
            action_for_keyboard_passthrough(KEY_F2),
            Some(HidAction::Pointer)
        );
        assert_eq!(action_for_keyboard_passthrough(KEY_VOLUMEUP), None);
        assert_eq!(action_for_keyboard_passthrough(KEY_PLAYPAUSE), None);
        assert_eq!(action_for_keyboard_passthrough(30), None); // KEY_A
        assert_eq!(action_for_keyboard_passthrough(103), None); // KEY_UP
    }

    #[test]
    fn voice_code_env_rematch() {
        assert_eq!(parse_voice_codes("404, 511"), vec![404, 511]);
        assert_eq!(parse_voice_codes("582"), vec![582]);
        assert!(parse_voice_codes("").is_empty());
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
        assert!(should_grab_device(&[KEY_SEARCH, KEY_VOICECOMMAND], false));
        assert!(should_grab_device(&[KEY_TOUCHPAD_TOGGLE], false));
        let keyboard = [KEY_ESC, KEY_HOME, KEY_A];
        const KEY_A: u16 = 30;
        assert!(!should_grab_device(&keyboard, true));
        // Pointer F2 / mic F8 live on the keyboard node — grabbing would eat D-pad.
        assert!(!should_grab_device(&[KEY_F2, KEY_F8, KEY_F9], false));
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
