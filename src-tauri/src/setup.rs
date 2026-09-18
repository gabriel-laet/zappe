use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct SetupState {
    pub completed: bool,
    pub browser_ack: bool,
    pub onepassword_skipped: bool,
    pub accounts_done: bool,
}

fn config_path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
        .join("setup.json")
}

pub fn load_setup() -> SetupState {
    let path = config_path();
    if let Ok(bytes) = fs::read(&path) {
        if let Ok(state) = serde_json::from_slice(&bytes) {
            return state;
        }
    }
    SetupState::default()
}

pub fn save_setup(state: &SetupState) -> anyhow::Result<()> {
    let path = config_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let json = serde_json::to_string_pretty(state)?;
    fs::write(path, json)?;
    Ok(())
}
