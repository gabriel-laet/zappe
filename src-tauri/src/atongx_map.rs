//! Shared ATONGX / XING WEI EV_KEY contract.
//!
//! Loaded from [`src/lib/atongx-map.json`](../../src/lib/atongx-map.json) so
//! TypeScript, Rust evdev (`hid.rs`), and the appliance capture helper stay
//! on one table. D-pad / OK stay `dispatch: focus` and are never grabbed.

use std::collections::HashMap;
use std::sync::OnceLock;

use serde::{Deserialize, Serialize};

const RAW_MAP: &str = include_str!("../../src/lib/atongx-map.json");

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum AtongxAction {
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Home,
    Menu,
    #[serde(rename = "playpause")]
    PlayPause,
    PageUp,
    PageDown,
    Voice,
    VolumeUp,
    VolumeDown,
    Mute,
    Power,
    Pointer,
    Delete,
}

impl AtongxAction {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Up => "up",
            Self::Down => "down",
            Self::Left => "left",
            Self::Right => "right",
            Self::Ok => "ok",
            Self::Back => "back",
            Self::Home => "home",
            Self::Menu => "menu",
            Self::PlayPause => "playpause",
            Self::PageUp => "pageUp",
            Self::PageDown => "pageDown",
            Self::Voice => "voice",
            Self::VolumeUp => "volumeUp",
            Self::VolumeDown => "volumeDown",
            Self::Mute => "mute",
            Self::Power => "power",
            Self::Pointer => "pointer",
            Self::Delete => "delete",
        }
    }

    /// Nest-escape + media + mic/pointer. Not D-pad / OK / PAGE.
    pub fn is_evdev_dispatch(self) -> bool {
        matches!(
            self,
            Self::Back
                | Self::Home
                | Self::Menu
                | Self::PlayPause
                | Self::Voice
                | Self::VolumeUp
                | Self::VolumeDown
                | Self::Mute
                | Self::Power
                | Self::Pointer
                | Self::Delete
        )
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum AtongxDispatch {
    Global,
    Focus,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum AtongxIface {
    Keyboard,
    Mouse,
    Consumer,
    System,
}

impl AtongxIface {
    pub fn is_grab_iface(self) -> bool {
        matches!(self, Self::Consumer | Self::System)
    }
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AtongxKey {
    pub name: String,
    pub code: u16,
    pub iface: AtongxIface,
    pub confidence: String,
    #[serde(default)]
    pub web_codes: Vec<String>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AtongxBinding {
    pub button: String,
    pub action: AtongxAction,
    pub dispatch: AtongxDispatch,
    #[serde(default)]
    #[allow(dead_code)]
    pub note: Option<String>,
    pub keys: Vec<AtongxKey>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
#[allow(dead_code)]
pub struct AtongxNode {
    pub id: String,
    pub by_id: String,
    pub event_hint: String,
    pub iface: AtongxIface,
    pub grab: bool,
    #[serde(default)]
    pub note: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AtongxUsb {
    pub vendor: String,
    pub product: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AtongxDevice {
    pub product: String,
    pub aliases: Vec<String>,
    pub usb: AtongxUsb,
    pub match_names: Vec<String>,
    pub nodes: Vec<AtongxNode>,
}

#[derive(Clone, Debug, Deserialize)]
pub struct AtongxMap {
    pub version: u32,
    pub device: AtongxDevice,
    pub bindings: Vec<AtongxBinding>,
}

struct CompiledMap {
    spec: AtongxMap,
    by_code: HashMap<u16, (AtongxAction, AtongxDispatch, AtongxIface, String, String)>,
}

fn compiled() -> &'static CompiledMap {
    static COMPILED: OnceLock<CompiledMap> = OnceLock::new();
    COMPILED.get_or_init(|| {
        let spec: AtongxMap = serde_json::from_str(RAW_MAP).expect("atongx-map.json");
        let mut by_code = HashMap::new();
        for binding in &spec.bindings {
            for key in &binding.keys {
                by_code.entry(key.code).or_insert((
                    binding.action,
                    binding.dispatch,
                    key.iface,
                    key.name.clone(),
                    key.confidence.clone(),
                ));
            }
        }
        CompiledMap { spec, by_code }
    })
}

pub fn map() -> &'static AtongxMap {
    &compiled().spec
}

pub fn name_matches(name: &str) -> bool {
    let upper = name.to_ascii_uppercase();
    compiled()
        .spec
        .device
        .match_names
        .iter()
        .any(|needle| upper.contains(&needle.to_ascii_uppercase()))
}

pub fn lookup_evkey(
    code: u16,
) -> Option<(AtongxAction, AtongxDispatch, AtongxIface, &'static str)> {
    compiled()
        .by_code
        .get(&code)
        .map(|(action, dispatch, iface, name, _)| (*action, *dispatch, *iface, name.as_str()))
}

pub fn action_for_evkey(code: u16) -> Option<AtongxAction> {
    lookup_evkey(code).map(|(action, _, _, _)| action)
}

/// Consumer / system keys that should be exclusive-grabbed. Keyboard F-keys
/// (mic/pointer aliases) stay ungrabbed so D-pad still reaches Chrome.
pub fn is_grab_keycode(code: u16) -> bool {
    compiled()
        .by_code
        .get(&code)
        .is_some_and(|(action, dispatch, iface, _, _)| {
            action.is_evdev_dispatch()
                && *dispatch == AtongxDispatch::Global
                && iface.is_grab_iface()
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn binding(action: AtongxAction) -> &'static AtongxBinding {
        map()
            .bindings
            .iter()
            .find(|b| b.action == action)
            .unwrap_or_else(|| panic!("missing {action:?}"))
    }

    #[test]
    fn map_parses_and_covers_living_room_board() {
        let spec = map();
        assert_eq!(spec.version, 1);
        assert_eq!(action_for_evkey(103), Some(AtongxAction::Up));
        assert_eq!(action_for_evkey(28), Some(AtongxAction::Ok));
        assert_eq!(action_for_evkey(158), Some(AtongxAction::Back));
        assert_eq!(action_for_evkey(172), Some(AtongxAction::Home));
        assert_eq!(action_for_evkey(164), Some(AtongxAction::PlayPause));
        assert_eq!(action_for_evkey(127), Some(AtongxAction::Menu));
        assert_eq!(action_for_evkey(115), Some(AtongxAction::VolumeUp));
        assert_eq!(action_for_evkey(114), Some(AtongxAction::VolumeDown));
        assert_eq!(action_for_evkey(113), Some(AtongxAction::Mute));
        assert_eq!(action_for_evkey(116), Some(AtongxAction::Power));
        assert_eq!(action_for_evkey(226), Some(AtongxAction::Power));
        assert_eq!(action_for_evkey(111), Some(AtongxAction::Delete));
        assert_eq!(action_for_evkey(174), Some(AtongxAction::Back));
        assert_eq!(spec.device.usb.vendor, "2320");
        assert_eq!(spec.device.usb.product, "0912");
        assert!(name_matches("XING WEI 2.4G USB USB Composite Device"));
        assert!(name_matches("ATONGX Voice Remote"));
        assert!(name_matches("/dev/input/by-id/usb-XING_WEI_2.4G"));
        assert!(!name_matches("Logitech USB Keyboard"));
        assert!(binding(AtongxAction::Voice)
            .keys
            .iter()
            .any(|k| k.web_codes.iter().any(|c| c == "F9")));
        assert!(binding(AtongxAction::Pointer)
            .keys
            .iter()
            .any(|k| k.web_codes.iter().any(|c| c == "F2")));
    }

    #[test]
    fn mic_and_pointer_are_locked_and_documented() {
        let voice = binding(AtongxAction::Voice);
        assert_eq!(voice.button, "Mic (red)");
        assert!(
            voice.note.as_deref().is_some_and(|n| n.contains("Locked")),
            "Mic note must document the locked contract"
        );
        let voice_locked: Vec<&str> = voice
            .keys
            .iter()
            .filter(|k| k.confidence == "locked")
            .map(|k| k.name.as_str())
            .collect();
        assert!(voice_locked.contains(&"KEY_SEARCH"));
        assert!(voice_locked.contains(&"KEY_VOICECOMMAND"));
        assert_eq!(action_for_evkey(217), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(582), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(583), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(167), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(66), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(67), Some(AtongxAction::Voice));
        assert_eq!(action_for_evkey(398), Some(AtongxAction::Voice));

        let pointer = binding(AtongxAction::Pointer);
        assert!(pointer.button.to_ascii_lowercase().contains("mouse"));
        assert!(
            pointer
                .note
                .as_deref()
                .is_some_and(|n| n.contains("Locked")),
            "Pointer note must document the locked contract"
        );
        let pointer_locked: Vec<&str> = pointer
            .keys
            .iter()
            .filter(|k| k.confidence == "locked")
            .map(|k| k.name.as_str())
            .collect();
        assert!(pointer_locked.contains(&"KEY_F2"));
        assert!(pointer_locked.contains(&"KEY_TOUCHPAD_TOGGLE"));
        assert_eq!(action_for_evkey(60), Some(AtongxAction::Pointer));
        assert_eq!(action_for_evkey(530), Some(AtongxAction::Pointer));
        assert_eq!(action_for_evkey(64), Some(AtongxAction::Pointer));
    }

    #[test]
    fn dpad_stays_focus_and_is_not_a_grab_key() {
        for action in [
            AtongxAction::Up,
            AtongxAction::Down,
            AtongxAction::Left,
            AtongxAction::Right,
            AtongxAction::Ok,
            AtongxAction::PageUp,
            AtongxAction::PageDown,
        ] {
            let b = binding(action);
            assert_eq!(b.dispatch, AtongxDispatch::Focus);
            assert!(!action.is_evdev_dispatch());
            for key in &b.keys {
                assert!(!is_grab_keycode(key.code), "{} must not grab", key.name);
            }
        }
        assert!(!is_grab_keycode(60), "KEY_F2 is keyboard — do not grab");
        assert!(!is_grab_keycode(66), "KEY_F8 is keyboard — do not grab");
        assert!(is_grab_keycode(217), "KEY_SEARCH is consumer — grab");
        assert!(is_grab_keycode(582), "KEY_VOICECOMMAND is consumer — grab");
        assert!(
            is_grab_keycode(530),
            "KEY_TOUCHPAD_TOGGLE is consumer — grab"
        );
        assert!(is_grab_keycode(158));
        assert!(is_grab_keycode(172));
    }

    #[test]
    fn every_binding_has_a_linux_key() {
        let actions: Vec<_> = map().bindings.iter().map(|b| b.action.as_str()).collect();
        for needed in [
            "up",
            "down",
            "left",
            "right",
            "ok",
            "back",
            "home",
            "menu",
            "playpause",
            "pageUp",
            "pageDown",
            "voice",
            "volumeUp",
            "volumeDown",
            "mute",
            "power",
            "pointer",
            "delete",
        ] {
            assert!(actions.contains(&needed), "missing {needed}");
        }
        for binding in &map().bindings {
            assert!(
                !binding.keys.is_empty(),
                "{} has no KEY_* entries",
                binding.button
            );
        }
        assert!(map().device.product.contains("XING WEI"));
        assert!(map().device.aliases.iter().any(|a| a == "ATONGX"));
        assert!(map()
            .device
            .nodes
            .iter()
            .any(|n| n.iface == AtongxIface::Keyboard && !n.grab));
        assert!(map()
            .device
            .nodes
            .iter()
            .any(|n| n.iface == AtongxIface::Consumer && n.grab));
    }
}
