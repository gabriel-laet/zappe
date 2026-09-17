//! 1Password readiness checks — never reads vault data.

use std::fs;
use std::path::PathBuf;
use std::process::Command;

use crate::accounts::zappe_data_dir;
use crate::chrome::profile_dir;

/// Chrome Web Store id for 1Password extension.
pub const EXTENSION_ID: &str = "aeblfdkhhhdcdjpifhhbdiojplfjncoa";

pub const WEB_STORE_URL: &str =
    "https://chrome.google.com/webstore/detail/1password-%E2%80%93-password-manager/aeblfdkhhhdcdjpifhhbdiojplfjncoa";

pub fn skip_marker() -> PathBuf {
    zappe_data_dir().join("onepassword.skipped")
}

pub fn skipped_by_user() -> bool {
    skip_marker().exists()
}

pub fn mark_skipped() {
    let dir = zappe_data_dir();
    let _ = fs::create_dir_all(&dir);
    let _ = fs::write(skip_marker(), b"skip");
}

pub fn desktop_app_paths() -> Vec<PathBuf> {
    vec![
        PathBuf::from("/opt/1Password/1password"),
        PathBuf::from("/usr/bin/1password"),
        PathBuf::from("/usr/local/bin/1password"),
    ]
}

pub fn desktop_app_installed() -> bool {
    if Command::new("which")
        .arg("1password")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
    {
        return true;
    }
    desktop_app_paths().iter().any(|p| p.exists())
}

pub fn extension_in_profile() -> bool {
    let ext_root = profile_dir().join("Default").join("Extensions").join(EXTENSION_ID);
    if !ext_root.exists() {
        return false;
    }
    fs::read_dir(&ext_root)
        .map(|entries| entries.count() > 0)
        .unwrap_or(false)
}

pub fn readiness_summary() -> String {
    let app = desktop_app_installed();
    let ext = extension_in_profile();
    match (app, ext) {
        (true, true) => "1Password app + extensão no perfil Zappe.".into(),
        (true, false) => "App instalado — falta extensão no Chrome do Zappe.".into(),
        (false, true) => "Extensão no perfil — app desktop opcional.".into(),
        (false, false) => "1Password não configurado (opcional).".into(),
    }
}

pub fn app_install_hint() -> String {
    if has_pacman() {
        "Instale 1Password pelo site oficial ou AUR (1password). Depois ative integração com o navegador.".into()
    } else {
        "Baixe 1Password em https://1password.com/downloads/linux/ e ative integração com o navegador.".into()
    }
}

fn has_pacman() -> bool {
    Command::new("which")
        .arg("pacman")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}
