//! Per-source auth adapters. Netflix is the reference (wired) impl.
//! Other Apps-shelf sources share the same companion + hidden-nest path;
//! adding the next one is login URL + cookie needles + optional harvest skill.

use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::paths::{auth_sources_path, ensure_data_dir, profile_dir};
use crate::skill::NETFLIX_CONTINUE_WATCHING_V1;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AuthSource {
    pub id: &'static str,
    pub label: &'static str,
    pub login_url: &'static str,
    pub google_continue: Option<&'static str>,
    pub cookie_needles: &'static [&'static [u8]],
    pub harvest_skill: Option<&'static str>,
    /// Full adapter (cookie + harvest) vs stub (same UX, heuristic cookies).
    pub wired: bool,
    pub show_on_connect: bool,
}

pub const NETFLIX: AuthSource = AuthSource {
    id: "netflix",
    label: "Netflix",
    login_url: "https://www.netflix.com/login",
    google_continue: Some(
        "https://accounts.google.com/ServiceLogin?continue=https://www.netflix.com/login",
    ),
    cookie_needles: &[b"NetflixId"],
    harvest_skill: Some(NETFLIX_CONTINUE_WATCHING_V1),
    wired: true,
    show_on_connect: true,
};

pub const PRIME: AuthSource = AuthSource {
    id: "prime",
    label: "Prime Video",
    login_url: "https://www.primevideo.com",
    google_continue: None,
    cookie_needles: &[b"at-main", b"sess-at-main", b"x-main", b"prime-fe-device"],
    harvest_skill: None,
    wired: false,
    show_on_connect: true,
};

pub const DISNEY: AuthSource = AuthSource {
    id: "disney",
    label: "Disney+",
    login_url: "https://www.disneyplus.com/identity/login",
    google_continue: None,
    cookie_needles: &[b"identity-token", b"__Secure-access-token", b"SWID"],
    harvest_skill: None,
    wired: false,
    show_on_connect: true,
};

pub const YOUTUBE: AuthSource = AuthSource {
    id: "youtube",
    label: "YouTube",
    login_url: "https://accounts.google.com/ServiceLogin?service=youtube&continue=https://www.youtube.com",
    google_continue: Some(
        "https://accounts.google.com/ServiceLogin?service=youtube&continue=https://www.youtube.com",
    ),
    cookie_needles: &[b"LOGIN_INFO", b"SAPISID", b"__Secure-1PSID"],
    harvest_skill: None,
    wired: false,
    show_on_connect: true,
};

pub const JELLYFIN: AuthSource = AuthSource {
    id: "jellyfin",
    label: "Jellyfin",
    login_url: "http://localhost:8096",
    google_continue: None,
    cookie_needles: &[b"Jellyfin.Server"],
    harvest_skill: None,
    wired: false,
    show_on_connect: false,
};

const ALL: &[AuthSource] = &[NETFLIX, PRIME, DISNEY, YOUTUBE, JELLYFIN];

#[allow(dead_code)]
pub fn all() -> &'static [AuthSource] {
    ALL
}

pub fn connect_sources() -> impl Iterator<Item = &'static AuthSource> {
    ALL.iter().filter(|s| s.show_on_connect)
}

pub fn parse_id(raw: &str) -> Option<&'static AuthSource> {
    let id = raw.trim().to_ascii_lowercase();
    ALL.iter().find(|s| {
        s.id == id
            || (s.id == "prime" && matches!(id.as_str(), "primevideo" | "amazon"))
            || (s.id == "disney" && matches!(id.as_str(), "disneyplus" | "disney+"))
    })
}

pub fn require(raw: &str) -> &'static AuthSource {
    parse_id(raw).unwrap_or(&NETFLIX)
}

pub fn default_source() -> &'static AuthSource {
    &NETFLIX
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SourceView {
    pub id: String,
    pub label: String,
    pub wired: bool,
    pub connected: bool,
    pub harvest_skill: Option<String>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
struct AuthFile {
    #[serde(default)]
    sources: std::collections::BTreeMap<String, AuthRecord>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
struct AuthRecord {
    connected: bool,
    #[serde(default)]
    updated_at: u64,
}

pub fn source_views() -> Vec<SourceView> {
    let file = load_auth_file();
    let profile = profile_dir();
    connect_sources()
        .map(|src| {
            let cookies = profile_has_session(&profile, src);
            let stored = file
                .sources
                .get(src.id)
                .map(|r| r.connected)
                .unwrap_or(false);
            SourceView {
                id: src.id.to_string(),
                label: src.label.to_string(),
                wired: src.wired,
                connected: cookies || stored,
                harvest_skill: src.harvest_skill.map(|s| s.to_string()),
            }
        })
        .collect()
}

pub fn is_connected(id: &str) -> bool {
    source_views()
        .into_iter()
        .any(|s| s.id == id && s.connected)
}

pub fn mark_connected(id: &str) {
    let mut file = load_auth_file();
    file.sources.insert(
        id.to_string(),
        AuthRecord {
            connected: true,
            updated_at: now_secs(),
        },
    );
    let _ = save_auth_file(&file);
}

pub fn profile_has_session(profile: &Path, source: &AuthSource) -> bool {
    source
        .cookie_needles
        .iter()
        .any(|needle| cookie_files_contain(profile, needle))
}

fn cookie_files_contain(profile: &Path, needle: &[u8]) -> bool {
    for rel in [
        "Default/Network/Cookies",
        "Default/Cookies",
        "Default/Network/Cookies-wal",
        "Default/Cookies-wal",
    ] {
        let path = profile.join(rel);
        if file_contains(&path, needle) {
            return true;
        }
        if let Some(parent) = path.parent() {
            let tmp = parent.join("zappe-cookies-scan");
            if std::fs::copy(&path, &tmp).is_ok() && file_contains(&tmp, needle) {
                let _ = std::fs::remove_file(&tmp);
                return true;
            }
            let _ = std::fs::remove_file(&tmp);
        }
    }
    false
}

fn file_contains(path: &Path, needle: &[u8]) -> bool {
    let Ok(bytes) = std::fs::read(path) else {
        return false;
    };
    bytes.windows(needle.len()).any(|w| w == needle)
}

fn load_auth_file() -> AuthFile {
    let path = auth_sources_path();
    std::fs::read(&path)
        .ok()
        .and_then(|b| serde_json::from_slice(&b).ok())
        .unwrap_or_default()
}

fn save_auth_file(file: &AuthFile) -> anyhow::Result<()> {
    ensure_data_dir()?;
    std::fs::write(auth_sources_path(), serde_json::to_vec_pretty(file)?)?;
    Ok(())
}

fn now_secs() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn apps_shelf_sources_are_listed() {
        let ids: Vec<_> = connect_sources().map(|s| s.id).collect();
        assert_eq!(ids, vec!["netflix", "prime", "disney", "youtube"]);
        assert!(require("netflix").wired);
        assert!(!require("prime").wired);
        assert!(!require("disney").wired);
        assert!(!require("youtube").wired);
        assert_eq!(parse_id("disney+").map(|s| s.id), Some("disney"));
        assert_eq!(parse_id("primevideo").map(|s| s.id), Some("prime"));
    }

    #[test]
    fn netflix_is_the_reference_adapter() {
        assert_eq!(NETFLIX.login_url, "https://www.netflix.com/login");
        assert_eq!(NETFLIX.cookie_needles[0], b"NetflixId");
        assert_eq!(
            NETFLIX.harvest_skill,
            Some(NETFLIX_CONTINUE_WATCHING_V1)
        );
    }
}
