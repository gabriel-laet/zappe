//! Living-room brand layer: name, accent, optional logo.
//!
//! Load order (later wins per field):
//! 1. Built-in Zappe defaults
//! 2. `~/.config/zappe/branding.json` if present
//! 3. `$ZAPPE_DATA_DIR/branding.json` (default `~/.local/share/zappe/branding.json`)
//! 4. `ZAPPE_BRANDING` file path, if set
//! 5. Field env vars (`ZAPPE_BRAND_NAME`, `_WORDMARK`, `_TAGLINE`, `_ACCENT`, `_LOGO`)

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::paths::{branding_path, data_dir};

const DEFAULT_NAME: &str = "Zappe";
const DEFAULT_TAGLINE: &str = "Home";
const DEFAULT_ACCENT: &str = "oklch(0.72 0.14 45)";
const MAX_LOGO_BYTES: usize = 2 * 1024 * 1024;

#[derive(Clone, Debug, Default, Deserialize)]
pub struct BrandingFile {
    #[serde(default)]
    pub name: Option<String>,
    #[serde(default)]
    pub wordmark: Option<String>,
    #[serde(default)]
    pub tagline: Option<String>,
    #[serde(default)]
    pub accent: Option<String>,
    #[serde(default)]
    pub logo: Option<String>,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct BrandingView {
    pub name: String,
    pub wordmark: String,
    pub tagline: String,
    pub accent: String,
    pub logo_src: Option<String>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
struct BrandingDraft {
    name: String,
    wordmark: String,
    tagline: String,
    accent: String,
    logo: Option<String>,
}

impl Default for BrandingDraft {
    fn default() -> Self {
        Self {
            name: DEFAULT_NAME.to_string(),
            wordmark: DEFAULT_NAME.to_string(),
            tagline: DEFAULT_TAGLINE.to_string(),
            accent: DEFAULT_ACCENT.to_string(),
            logo: None,
        }
    }
}

pub fn load_branding() -> BrandingView {
    finalize(load_draft())
}

fn load_draft() -> BrandingDraft {
    let mut draft = BrandingDraft::default();
    if let Some(file) = read_branding_file(&config_branding_path()) {
        apply_file(&mut draft, &file);
    }
    if let Some(file) = read_branding_file(&branding_path()) {
        apply_file(&mut draft, &file);
    }
    if let Ok(explicit) = std::env::var("ZAPPE_BRANDING") {
        let path = expand_tilde(explicit.trim());
        if let Some(file) = read_branding_file(&path) {
            apply_file(&mut draft, &file);
        }
    }
    apply_env(&mut draft);
    draft
}

fn config_branding_path() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
        .join("branding.json")
}

fn read_branding_file(path: &Path) -> Option<BrandingFile> {
    if !path.exists() {
        return None;
    }
    let bytes = fs::read(path).ok()?;
    serde_json::from_slice(&bytes).ok()
}

fn apply_file(draft: &mut BrandingDraft, file: &BrandingFile) {
    if let Some(v) = nonempty(&file.name) {
        draft.name = v;
        if draft.wordmark == DEFAULT_NAME {
            draft.wordmark = draft.name.clone();
        }
    }
    if let Some(v) = nonempty(&file.wordmark) {
        draft.wordmark = v;
    }
    if let Some(v) = nonempty(&file.tagline) {
        draft.tagline = v;
    }
    if let Some(v) = nonempty(&file.accent) {
        draft.accent = v;
    }
    if let Some(v) = nonempty(&file.logo) {
        draft.logo = Some(v);
    }
}

fn apply_env(draft: &mut BrandingDraft) {
    if let Some(v) = env_nonempty("ZAPPE_BRAND_NAME") {
        draft.name = v;
    }
    if let Some(v) = env_nonempty("ZAPPE_BRAND_WORDMARK") {
        draft.wordmark = v;
    }
    if let Some(v) = env_nonempty("ZAPPE_BRAND_TAGLINE") {
        draft.tagline = v;
    }
    if let Some(v) = env_nonempty("ZAPPE_BRAND_ACCENT") {
        draft.accent = v;
    }
    if let Some(v) = env_nonempty("ZAPPE_BRAND_LOGO") {
        draft.logo = Some(v);
    }
}

fn finalize(draft: BrandingDraft) -> BrandingView {
    BrandingView {
        name: draft.name,
        wordmark: draft.wordmark,
        tagline: draft.tagline,
        accent: draft.accent,
        logo_src: draft.logo.as_deref().and_then(resolve_logo),
    }
}

fn resolve_logo(raw: &str) -> Option<String> {
    let trimmed = raw.trim();
    if trimmed.is_empty() {
        return None;
    }
    if trimmed.starts_with("data:")
        || trimmed.starts_with("https://")
        || trimmed.starts_with("http://")
    {
        return Some(trimmed.to_string());
    }
    let path = resolve_logo_path(trimmed);
    let bytes = fs::read(&path).ok()?;
    if bytes.is_empty() || bytes.len() > MAX_LOGO_BYTES {
        return None;
    }
    let mime = logo_mime(&path);
    Some(format!("data:{mime};base64,{}", base64_encode(&bytes)))
}

fn resolve_logo_path(raw: &str) -> PathBuf {
    let path = expand_tilde(raw);
    if path.is_absolute() {
        path
    } else {
        data_dir().join(path)
    }
}

fn expand_tilde(raw: &str) -> PathBuf {
    if let Some(rest) = raw.strip_prefix("~/") {
        if let Some(home) = dirs::home_dir() {
            return home.join(rest);
        }
    }
    if raw == "~" {
        if let Some(home) = dirs::home_dir() {
            return home;
        }
    }
    PathBuf::from(raw)
}

fn logo_mime(path: &Path) -> &'static str {
    match path
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_ascii_lowercase())
        .as_deref()
    {
        Some("png") => "image/png",
        Some("jpg") | Some("jpeg") => "image/jpeg",
        Some("webp") => "image/webp",
        Some("svg") => "image/svg+xml",
        Some("gif") => "image/gif",
        _ => "application/octet-stream",
    }
}

fn nonempty(value: &Option<String>) -> Option<String> {
    value
        .as_ref()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn env_nonempty(key: &str) -> Option<String> {
    std::env::var(key)
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn base64_encode(data: &[u8]) -> String {
    const TABLE: &[u8] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(data.len().div_ceil(3) * 4);
    for chunk in data.chunks(3) {
        let a = u32::from(chunk[0]);
        let b = u32::from(chunk.get(1).copied().unwrap_or(0));
        let c = u32::from(chunk.get(2).copied().unwrap_or(0));
        let n = (a << 16) | (b << 8) | c;
        out.push(TABLE[((n >> 18) & 63) as usize] as char);
        out.push(TABLE[((n >> 12) & 63) as usize] as char);
        if chunk.len() > 1 {
            out.push(TABLE[((n >> 6) & 63) as usize] as char);
        } else {
            out.push('=');
        }
        if chunk.len() > 2 {
            out.push(TABLE[(n & 63) as usize] as char);
        } else {
            out.push('=');
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    fn clear_brand_env() {
        for key in [
            "ZAPPE_BRANDING",
            "ZAPPE_BRAND_NAME",
            "ZAPPE_BRAND_WORDMARK",
            "ZAPPE_BRAND_TAGLINE",
            "ZAPPE_BRAND_ACCENT",
            "ZAPPE_BRAND_LOGO",
        ] {
            std::env::remove_var(key);
        }
    }

    #[test]
    fn defaults_are_zappe() {
        let d = BrandingDraft::default();
        let view = finalize(d);
        assert_eq!(view.name, "Zappe");
        assert_eq!(view.wordmark, "Zappe");
        assert_eq!(view.tagline, "Home");
        assert_eq!(view.accent, DEFAULT_ACCENT);
        assert!(view.logo_src.is_none());
    }

    #[test]
    fn file_then_env_wins() {
        let _guard = ENV_LOCK.lock().unwrap();
        let prev_name = std::env::var_os("ZAPPE_BRAND_NAME");
        let prev_accent = std::env::var_os("ZAPPE_BRAND_ACCENT");
        std::env::set_var("ZAPPE_BRAND_NAME", "Sala");
        std::env::set_var("ZAPPE_BRAND_ACCENT", "#ff8800");
        let mut draft = BrandingDraft::default();
        apply_file(
            &mut draft,
            &BrandingFile {
                name: Some("Casa".into()),
                wordmark: Some("CASA".into()),
                tagline: Some("Living room".into()),
                accent: Some("#112233".into()),
                logo: None,
            },
        );
        apply_env(&mut draft);
        assert_eq!(draft.name, "Sala");
        assert_eq!(draft.wordmark, "CASA");
        assert_eq!(draft.tagline, "Living room");
        assert_eq!(draft.accent, "#ff8800");
        match prev_name {
            Some(v) => std::env::set_var("ZAPPE_BRAND_NAME", v),
            None => std::env::remove_var("ZAPPE_BRAND_NAME"),
        }
        match prev_accent {
            Some(v) => std::env::set_var("ZAPPE_BRAND_ACCENT", v),
            None => std::env::remove_var("ZAPPE_BRAND_ACCENT"),
        }
    }

    #[test]
    fn setting_name_updates_default_wordmark() {
        let mut draft = BrandingDraft::default();
        apply_file(
            &mut draft,
            &BrandingFile {
                name: Some("Nido".into()),
                ..BrandingFile::default()
            },
        );
        assert_eq!(draft.name, "Nido");
        assert_eq!(draft.wordmark, "Nido");
    }

    #[test]
    fn data_url_logo_passthrough() {
        let src = "data:image/png;base64,aaaa";
        assert_eq!(resolve_logo(src).as_deref(), Some(src));
    }

    #[test]
    fn logo_file_becomes_data_url() {
        let _guard = ENV_LOCK.lock().unwrap();
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        let dir = std::env::temp_dir().join(format!("zappe-brand-{}", std::process::id()));
        let _ = fs::create_dir_all(&dir);
        let logo = dir.join("mark.png");
        fs::write(&logo, [0x89, 0x50, 0x4e, 0x47]).unwrap();
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        let src = resolve_logo("mark.png").expect("logo");
        assert!(src.starts_with("data:image/png;base64,"));
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }

    #[test]
    fn base64_known_vector() {
        assert_eq!(base64_encode(b"Man"), "TWFu");
        assert_eq!(base64_encode(b"Ma"), "TWE=");
        assert_eq!(base64_encode(b"M"), "TQ==");
    }

    #[test]
    fn empty_env_is_ignored() {
        let _guard = ENV_LOCK.lock().unwrap();
        clear_brand_env();
        std::env::set_var("ZAPPE_BRAND_NAME", "   ");
        let mut draft = BrandingDraft::default();
        apply_env(&mut draft);
        assert_eq!(draft.name, DEFAULT_NAME);
        std::env::remove_var("ZAPPE_BRAND_NAME");
    }
}
