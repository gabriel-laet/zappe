//! Load optional living-room branding from the user data dir.
//!
//! Preferred file: `~/.local/share/zappe/branding.json` (or `$ZAPPE_DATA_DIR/branding.json`).
//! Logo / splash paths are relative to that directory, or absolute.
//! Missing or invalid config falls back to in-code defaults ("Zappe").

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::paths;

pub const DEFAULT_NAME: &str = "Zappe";
pub const DEFAULT_ACCENT: &str = "#E85A1B";
pub const DEFAULT_BACKGROUND: &str = "#000000";
pub const DEFAULT_IDLE_TIMEOUT: u32 = 120;
/// Official feras lockup is ~1.4 MiB PNG; 8 MiB leaves headroom for data-URL IPC.
const MAX_ASSET_BYTES: u64 = 8 * 1024 * 1024;
const MAX_NAME_CHARS: usize = 40;
const MAX_TAGLINE_CHARS: usize = 80;

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct BrandingView {
    pub name: String,
    pub accent: String,
    pub tagline: Option<String>,
    pub logo_data_url: Option<String>,
    pub splash_data_url: Option<String>,
    pub idle_data_url: Option<String>,
    pub idle_mode: String,
    pub idle_timeout_seconds: u32,
    pub idle_animation: String,
    pub theme_style: String,
    pub theme_background: String,
    pub theme_focus: String,
    pub source: String,
}

impl Default for BrandingView {
    fn default() -> Self {
        Self {
            name: DEFAULT_NAME.to_string(),
            accent: DEFAULT_ACCENT.to_string(),
            tagline: None,
            logo_data_url: None,
            splash_data_url: None,
            idle_data_url: None,
            idle_mode: "screensaver".to_string(),
            idle_timeout_seconds: DEFAULT_IDLE_TIMEOUT,
            idle_animation: "soft-breathe".to_string(),
            theme_style: "apple-tv".to_string(),
            theme_background: DEFAULT_BACKGROUND.to_string(),
            theme_focus: "subtle-scale".to_string(),
            source: "default".to_string(),
        }
    }
}

#[derive(Debug, Deserialize)]
struct BrandingFile {
    #[serde(default)]
    name: Option<String>,
    #[serde(default)]
    accent: Option<String>,
    #[serde(default, alias = "accentColor", alias = "accent_color")]
    accent_color: Option<String>,
    #[serde(default)]
    logo: Option<String>,
    #[serde(default)]
    splash: Option<String>,
    #[serde(default)]
    tagline: Option<String>,
    #[serde(default)]
    idle: Option<IdleFile>,
    #[serde(default)]
    theme: Option<ThemeFile>,
}

#[derive(Debug, Deserialize)]
struct IdleFile {
    #[serde(default)]
    mode: Option<String>,
    #[serde(default)]
    asset: Option<String>,
    #[serde(default, alias = "timeoutSeconds", alias = "timeout_seconds")]
    timeout_seconds: Option<u32>,
    #[serde(default)]
    animation: Option<String>,
}

#[derive(Debug, Deserialize)]
struct ThemeFile {
    #[serde(default)]
    style: Option<String>,
    #[serde(default)]
    background: Option<String>,
    #[serde(default, alias = "focusRing", alias = "focus_ring")]
    focus_ring: Option<String>,
    #[serde(default)]
    accent: Option<String>,
}

pub fn load_branding() -> BrandingView {
    load_branding_from(&paths::data_dir())
}

pub fn load_branding_from(dir: &Path) -> BrandingView {
    let path = dir.join("branding.json");
    if !path.exists() {
        log::info!(
            "branding.json missing at {}; using defaults",
            path.display()
        );
        return BrandingView::default();
    }
    let text = match fs::read_to_string(&path) {
        Ok(text) => text,
        Err(err) => {
            log::warn!(
                "could not read branding.json at {}: {err}; using defaults",
                path.display()
            );
            return BrandingView::default();
        }
    };
    let file: BrandingFile = match serde_json::from_str(&text) {
        Ok(file) => file,
        Err(err) => {
            log::warn!(
                "invalid branding.json at {}: {err}; using defaults",
                path.display()
            );
            return BrandingView::default();
        }
    };
    resolve_branding(dir, file)
}

fn resolve_branding(dir: &Path, file: BrandingFile) -> BrandingView {
    let mut view = BrandingView::default();
    view.source = "user".to_string();

    if let Some(name) = sanitize_text(file.name.as_deref(), MAX_NAME_CHARS) {
        view.name = name;
    } else if file.name.is_some() {
        log::warn!("branding name empty or too long; using {DEFAULT_NAME}");
    }

    let accent_raw = file
        .accent
        .as_deref()
        .or(file.accent_color.as_deref())
        .map(str::trim)
        .filter(|s| !s.is_empty());
    match accent_raw {
        Some(raw) if is_safe_css_color(raw) => view.accent = raw.to_string(),
        Some(raw) => {
            log::warn!("branding accent {raw:?} rejected; using default");
        }
        None => {}
    }

    view.tagline = sanitize_text(file.tagline.as_deref(), MAX_TAGLINE_CHARS);
    if file.tagline.is_some() && view.tagline.is_none() {
        log::warn!("branding tagline empty or too long; omitting");
    }

    view.logo_data_url = file
        .logo
        .as_deref()
        .and_then(|spec| load_asset_data_url(dir, spec, "logo"));
    view.splash_data_url = file
        .splash
        .as_deref()
        .and_then(|spec| load_asset_data_url(dir, spec, "splash"));

    if let Some(theme) = &file.theme {
        if let Some(style) = sanitize_token(theme.style.as_deref(), 24) {
            view.theme_style = style;
        }
        if let Some(bg) = theme.background.as_deref().map(str::trim) {
            if is_safe_css_color(bg) {
                view.theme_background = bg.to_string();
            } else if !bg.is_empty() {
                log::warn!("branding theme.background {bg:?} rejected; using default");
            }
        }
        if let Some(focus) = sanitize_token(theme.focus_ring.as_deref(), 32) {
            view.theme_focus = focus;
        }
        if let Some(accent) = theme.accent.as_deref().map(str::trim) {
            if is_safe_css_color(accent) {
                view.accent = accent.to_string();
            }
        }
    }

    if let Some(idle) = &file.idle {
        if let Some(mode) = sanitize_token(idle.mode.as_deref(), 24) {
            view.idle_mode = if mode == "off" {
                "off".to_string()
            } else {
                "screensaver".to_string()
            };
        }
        if let Some(anim) = sanitize_token(idle.animation.as_deref(), 32) {
            view.idle_animation = anim;
        }
        if let Some(secs) = idle.timeout_seconds {
            view.idle_timeout_seconds = secs.clamp(15, 3600);
        }
        view.idle_data_url = idle
            .asset
            .as_deref()
            .and_then(|spec| load_asset_data_url(dir, spec, "idle"));
    }
    if view.idle_data_url.is_none() {
        view.idle_data_url = view.logo_data_url.clone();
    }

    view
}

fn sanitize_token(raw: Option<&str>, max_chars: usize) -> Option<String> {
    let value = raw?.trim().to_ascii_lowercase();
    if value.is_empty() || value.len() > max_chars {
        return None;
    }
    if !value
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
    {
        return None;
    }
    Some(value)
}

fn sanitize_text(raw: Option<&str>, max_chars: usize) -> Option<String> {
    let value = raw?.trim();
    if value.is_empty() || value.chars().count() > max_chars {
        return None;
    }
    Some(value.to_string())
}

/// Conservative CSS color check so we can set `--brand-accent` safely.
pub fn is_safe_css_color(raw: &str) -> bool {
    let s = raw.trim();
    if s.is_empty() || s.len() > 80 {
        return false;
    }
    if s.bytes().any(|b| {
        matches!(
            b,
            b'<' | b'>' | b';' | b'{' | b'}' | b'"' | b'\'' | b'\\' | b'`'
        )
    }) {
        return false;
    }
    let lower = s.to_ascii_lowercase();
    if lower.contains("url(") || lower.contains("expression") || lower.contains("javascript") {
        return false;
    }
    if let Some(hex) = s.strip_prefix('#') {
        return matches!(hex.len(), 3 | 4 | 6 | 8) && hex.bytes().all(|b| b.is_ascii_hexdigit());
    }
    if let Some(paren) = lower.find('(') {
        if !lower.ends_with(')') {
            return false;
        }
        return matches!(
            &lower[..paren],
            "oklch" | "oklab" | "rgb" | "rgba" | "hsl" | "hsla" | "hwb" | "lab" | "lch" | "color"
        );
    }
    s.chars().all(|c| c.is_ascii_alphabetic()) && (3..=20).contains(&s.len())
}

fn load_asset_data_url(dir: &Path, spec: &str, kind: &str) -> Option<String> {
    let path = resolve_asset_path(dir, spec)?;
    if !path.is_file() {
        log::warn!("branding {kind} missing at {}", path.display());
        return None;
    }
    let meta = match fs::metadata(&path) {
        Ok(meta) => meta,
        Err(err) => {
            log::warn!("branding {kind} stat failed at {}: {err}", path.display());
            return None;
        }
    };
    if meta.len() > MAX_ASSET_BYTES {
        log::warn!(
            "branding {kind} too large ({} bytes) at {}; skipping",
            meta.len(),
            path.display()
        );
        return None;
    }
    let mime = match mime_from_path(&path) {
        Some(mime) => mime,
        None => {
            log::warn!(
                "branding {kind} has unsupported type at {}; use png/jpg/webp/gif",
                path.display()
            );
            return None;
        }
    };
    let bytes = match fs::read(&path) {
        Ok(bytes) => bytes,
        Err(err) => {
            log::warn!("branding {kind} read failed at {}: {err}", path.display());
            return None;
        }
    };
    Some(format!("data:{mime};base64,{}", encode_base64(&bytes)))
}

fn resolve_asset_path(dir: &Path, spec: &str) -> Option<PathBuf> {
    let trimmed = spec.trim();
    if trimmed.is_empty() {
        return None;
    }
    let path = Path::new(trimmed);
    let resolved = if path.is_absolute() {
        path.to_path_buf()
    } else {
        dir.join(path)
    };
    Some(resolved)
}

fn mime_from_path(path: &Path) -> Option<&'static str> {
    match path
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_ascii_lowercase())
        .as_deref()
    {
        Some("png") => Some("image/png"),
        Some("jpg") | Some("jpeg") => Some("image/jpeg"),
        Some("webp") => Some("image/webp"),
        Some("gif") => Some("image/gif"),
        _ => None,
    }
}

fn encode_base64(input: &[u8]) -> String {
    const T: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::with_capacity(input.len().div_ceil(3) * 4);
    for chunk in input.chunks(3) {
        let a = chunk[0] as u32;
        let b = chunk.get(1).copied().unwrap_or(0) as u32;
        let c = chunk.get(2).copied().unwrap_or(0) as u32;
        let n = (a << 16) | (b << 8) | c;
        out.push(T[((n >> 18) & 63) as usize] as char);
        out.push(T[((n >> 12) & 63) as usize] as char);
        if chunk.len() > 1 {
            out.push(T[((n >> 6) & 63) as usize] as char);
        } else {
            out.push('=');
        }
        if chunk.len() > 2 {
            out.push(T[(n & 63) as usize] as char);
        } else {
            out.push('=');
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_dir() -> PathBuf {
        let dir = std::env::temp_dir().join(format!(
            "zappe-brand-{}-{}",
            std::process::id(),
            SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    // 1×1 transparent PNG.
    const TINY_PNG: &[u8] = &[
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0x00,
        0x01, 0x00, 0x00, 0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
    ];

    #[test]
    fn missing_file_uses_defaults() {
        let dir = temp_dir();
        let view = load_branding_from(&dir);
        assert_eq!(view, BrandingView::default());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn invalid_json_uses_defaults() {
        let dir = temp_dir();
        fs::write(dir.join("branding.json"), "{not json").unwrap();
        let view = load_branding_from(&dir);
        assert_eq!(view, BrandingView::default());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn valid_file_and_relative_logo() {
        let dir = temp_dir();
        fs::write(dir.join("logo.png"), TINY_PNG).unwrap();
        fs::write(
            dir.join("branding.json"),
            r##"{
              "name": "Living Room",
              "accent": "#E8A84C",
              "logo": "logo.png",
              "tagline": "What are we watching?"
            }"##,
        )
        .unwrap();
        let view = load_branding_from(&dir);
        assert_eq!(view.name, "Living Room");
        assert_eq!(view.accent, "#E8A84C");
        assert_eq!(view.tagline.as_deref(), Some("What are we watching?"));
        assert_eq!(view.source, "user");
        assert!(view
            .logo_data_url
            .as_deref()
            .unwrap()
            .starts_with("data:image/png;base64,"));
        assert!(view.splash_data_url.is_none());
        assert_eq!(view.idle_mode, "screensaver");
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn idle_and_theme_feras_shape() {
        let dir = temp_dir();
        fs::write(dir.join("logo.png"), TINY_PNG).unwrap();
        fs::write(
            dir.join("branding.json"),
            r##"{
              "name": "feras TV",
              "accent": "#C4A574",
              "logo": "logo.png",
              "splash": "logo.png",
              "idle": {
                "mode": "screensaver",
                "asset": "logo.png",
                "timeoutSeconds": 120,
                "animation": "soft-breathe"
              },
              "theme": {
                "style": "apple-tv",
                "background": "#000000",
                "focusRing": "subtle-scale"
              }
            }"##,
        )
        .unwrap();
        let view = load_branding_from(&dir);
        assert_eq!(view.name, "feras TV");
        assert_eq!(view.accent, "#C4A574");
        assert_eq!(view.theme_background, "#000000");
        assert_eq!(view.theme_style, "apple-tv");
        assert_eq!(view.theme_focus, "subtle-scale");
        assert_eq!(view.idle_mode, "screensaver");
        assert_eq!(view.idle_timeout_seconds, 120);
        assert_eq!(view.idle_animation, "soft-breathe");
        assert!(view.logo_data_url.is_some());
        assert!(view.splash_data_url.is_some());
        assert!(view.idle_data_url.is_some());
        assert!(view.tagline.is_none());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn large_png_under_cap_encodes_data_url() {
        let dir = temp_dir();
        let mut bytes = TINY_PNG.to_vec();
        bytes.resize(1_453_923, 0);
        fs::write(dir.join("logo.png"), &bytes).unwrap();
        fs::write(
            dir.join("branding.json"),
            r##"{"name":"feras TV","accent":"#C4A574","logo":"logo.png"}"##,
        )
        .unwrap();
        let view = load_branding_from(&dir);
        let url = view.logo_data_url.expect("1.4MB logo should become a data URL");
        assert!(url.starts_with("data:image/png;base64,"));
        assert!(
            url.len() > 1_900_000,
            "base64 data URL too small: {}",
            url.len()
        );
        assert_eq!(view.accent, "#C4A574");
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn invalid_accent_falls_back() {
        let dir = temp_dir();
        fs::write(
            dir.join("branding.json"),
            r#"{"name":"X","accent":"url(javascript:alert(1))"}"#,
        )
        .unwrap();
        let view = load_branding_from(&dir);
        assert_eq!(view.name, "X");
        assert_eq!(view.accent, DEFAULT_ACCENT);
        assert_eq!(view.source, "user");
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn absolute_logo_path() {
        let dir = temp_dir();
        let logo = dir.join("mark.jpg");
        fs::write(&logo, TINY_PNG).unwrap();
        fs::write(
            dir.join("branding.json"),
            format!(
                r#"{{"name":"Abs","logo":"{}"}}"#,
                logo.display().to_string().replace('\\', "\\\\")
            ),
        )
        .unwrap();
        let view = load_branding_from(&dir);
        assert!(view.logo_data_url.is_some());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn missing_logo_is_omitted() {
        let dir = temp_dir();
        fs::write(
            dir.join("branding.json"),
            r#"{"name":"NoArt","logo":"missing.png"}"#,
        )
        .unwrap();
        let view = load_branding_from(&dir);
        assert_eq!(view.name, "NoArt");
        assert!(view.logo_data_url.is_none());
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn color_allowlist() {
        assert!(is_safe_css_color("#fc0"));
        assert!(is_safe_css_color("#E8A84C"));
        assert!(is_safe_css_color("oklch(0.72 0.14 45)"));
        assert!(is_safe_css_color("rgb(232, 168, 76)"));
        assert!(is_safe_css_color("coral"));
        assert!(!is_safe_css_color(""));
        assert!(!is_safe_css_color("#zz"));
        assert!(!is_safe_css_color("red; background: pink"));
        assert!(!is_safe_css_color("url(https://evil.example)"));
    }

    #[test]
    fn base64_padding() {
        assert_eq!(encode_base64(b"Man"), "TWFu");
        assert_eq!(encode_base64(b"Ma"), "TWE=");
        assert_eq!(encode_base64(b"M"), "TQ==");
    }
}
