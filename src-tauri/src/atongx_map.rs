//! Shared ATONGX / XING WEI EV_KEY contract.
//!
//! Loaded from [`src/lib/atongx-map.json`](../../src/lib/atongx-map.json) so
//! TypeScript, Rust, and the appliance capture script stay on one table.

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

    pub fn is_global(self) -> bool {
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
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Keyboard => "keyboard",
            Self::Mouse => "mouse",
            Self::Consumer => "consumer",
            Self::System => "system",
        }
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
    by_code: HashMap<u16, (AtongxAction, AtongxDispatch, AtongxIface, String)>,
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
                ));
            }
        }
        CompiledMap { spec, by_code }
    })
}

pub fn map() -> &'static AtongxMap {
    &compiled().spec
}

pub fn usb_vendor() -> u16 {
    u16::from_str_radix(&compiled().spec.device.usb.vendor, 16).unwrap_or(0x2320)
}

pub fn usb_product() -> u16 {
    u16::from_str_radix(&compiled().spec.device.usb.product, 16).unwrap_or(0x0912)
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

pub fn should_grab(iface: AtongxIface) -> bool {
    compiled()
        .spec
        .device
        .nodes
        .iter()
        .find(|n| n.iface == iface)
        .map(|n| n.grab)
        .unwrap_or(matches!(iface, AtongxIface::Consumer | AtongxIface::System))
}

pub fn lookup_evkey(
    code: u16,
) -> Option<(AtongxAction, AtongxDispatch, AtongxIface, &'static str)> {
    compiled()
        .by_code
        .get(&code)
        .map(|(action, dispatch, iface, name)| (*action, *dispatch, *iface, name.as_str()))
}

pub fn action_for_evkey(code: u16) -> Option<AtongxAction> {
    lookup_evkey(code).map(|(action, _, _, _)| action)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn map_parses_and_covers_living_room_board() {
        let spec = map();
        assert_eq!(spec.version, 1);
        assert!(spec.bindings.iter().any(|b| b.action == AtongxAction::Back));
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
        assert_eq!(action_for_evkey(111), Some(AtongxAction::Delete));
        assert_eq!(action_for_evkey(217), Some(AtongxAction::Voice));
        assert_eq!(usb_vendor(), 0x2320);
        assert_eq!(usb_product(), 0x0912);
        assert!(name_matches("XING WEI 2.4G USB USB Composite Device"));
        assert!(name_matches("ATONGX Voice Remote"));
        assert!(!name_matches("Logitech USB Keyboard"));
    }

    #[test]
    fn first_key_wins_and_aliases_stay_secondary() {
        // KEY_ESC is an alias for Back; KEY_BACK is the Consumer Control primary.
        let (action, dispatch, iface, name) = lookup_evkey(158).unwrap();
        assert_eq!(action, AtongxAction::Back);
        assert_eq!(dispatch, AtongxDispatch::Global);
        assert_eq!(iface, AtongxIface::Consumer);
        assert_eq!(name, "KEY_BACK");
        assert_eq!(action_for_evkey(1), Some(AtongxAction::Back));
    }

    #[test]
    fn every_binding_has_a_linux_key() {
        for binding in &map().bindings {
            assert!(
                !binding.keys.is_empty(),
                "{} has no KEY_* entries",
                binding.button
            );
        }
        assert!(map()
            .bindings
            .iter()
            .flat_map(|b| &b.keys)
            .any(|k| k.confidence == "hid" && !k.web_codes.is_empty()));
        assert!(map()
            .bindings
            .iter()
            .flat_map(|b| &b.keys)
            .any(|k| k.confidence == "needs_device"));
        assert_eq!(map().device.product.contains("XING WEI"), true);
        assert!(map().device.aliases.iter().any(|a| a == "ATONGX"));
    }
}
