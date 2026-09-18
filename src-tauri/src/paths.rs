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

/// Directories that may contain harvest skill YAML/JSON on the appliance.
///
/// The binary embeds `netflix.continue_watching.v1`; these paths are the
/// install / override fallbacks when someone drops sibling skills or when
/// the updater copies `skills/` into the data dir.
pub fn skill_search_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();
    if let Ok(explicit) = std::env::var("ZAPPE_SKILLS_DIR") {
        if !explicit.trim().is_empty() {
            dirs.push(PathBuf::from(explicit));
        }
    }
    dirs.push(skills_override_dir());
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            dirs.push(dir.join("skills"));
            if let Some(parent) = dir.parent() {
                dirs.push(parent.join("share/zappe/skills"));
            }
        }
    }
    if let Ok(src) = std::env::var("ZAPPE_SRC") {
        dirs.push(PathBuf::from(src).join("skills"));
    }
    dirs.push(PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../skills"));
    dirs
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
            skills_override_dir(),
            PathBuf::from("/tmp/zappe-test-paths/skills")
        );
        let search = skill_search_dirs();
        assert!(search.iter().any(|p| p == &skills_override_dir()));
        assert!(search.iter().any(|p| p.ends_with("skills")));
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }
}
