//! Local-only data paths (`~/.local/share/zappe` on Linux).

use std::path::PathBuf;

pub fn data_dir() -> PathBuf {
    if let Ok(explicit) = std::env::var("ZAPPE_DATA_DIR") {
        return PathBuf::from(explicit);
    }
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
}

pub fn profile_dir() -> PathBuf {
    data_dir().join("chrome-profile")
}

pub fn catalog_path() -> PathBuf {
    data_dir().join("catalog.json")
}

/// User branding config. Not shipped in the repo — drop this on the appliance.
pub fn branding_path() -> PathBuf {
    data_dir().join("branding.json")
}

pub fn skills_override_dir() -> PathBuf {
    data_dir().join("skills")
}

/// Live device-code session (no passwords). Written by the companion.
pub fn companion_session_path() -> PathBuf {
    data_dir().join("companion-session.json")
}

/// Per-source auth flags (Netflix / Prime / Disney+ / YouTube).
pub fn auth_sources_path() -> PathBuf {
    data_dir().join("auth-sources.json")
}

pub fn ensure_data_dir() -> std::io::Result<PathBuf> {
    let dir = data_dir();
    std::fs::create_dir_all(&dir)?;
    std::fs::create_dir_all(profile_dir())?;
    Ok(dir)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn override_data_dir() {
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        std::env::set_var("ZAPPE_DATA_DIR", "/tmp/zappe-test-paths");
        assert_eq!(data_dir(), PathBuf::from("/tmp/zappe-test-paths"));
        assert_eq!(
            profile_dir(),
            PathBuf::from("/tmp/zappe-test-paths/chrome-profile")
        );
        assert_eq!(
            branding_path(),
            PathBuf::from("/tmp/zappe-test-paths/branding.json")
        );
        assert_eq!(
            companion_session_path(),
            PathBuf::from("/tmp/zappe-test-paths/companion-session.json")
        );
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }
}
