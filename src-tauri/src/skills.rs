//! Streaming services. Playback is a URL into the Chrome nest — not CDP/JS.

use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Service {
    Netflix,
    Prime,
    Disney,
    Youtube,
    Jellyfin,
}

impl Service {
    pub fn parse_id(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "netflix" => Some(Self::Netflix),
            "prime" | "primevideo" => Some(Self::Prime),
            "disney" | "disneyplus" | "disney+" => Some(Self::Disney),
            "youtube" => Some(Self::Youtube),
            "jellyfin" => Some(Self::Jellyfin),
            _ => None,
        }
    }

    pub fn home_url(self) -> &'static str {
        match self {
            Self::Netflix => "https://www.netflix.com/browse",
            Self::Prime => "https://www.primevideo.com",
            Self::Disney => "https://www.disneyplus.com",
            Self::Youtube => "https://www.youtube.com",
            Self::Jellyfin => "http://localhost:8096",
        }
    }

    #[allow(dead_code)]
    pub fn label(self) -> &'static str {
        match self {
            Self::Netflix => "NETFLIX",
            Self::Prime => "PRIME",
            Self::Disney => "DISNEY+",
            Self::Youtube => "YOUTUBE",
            Self::Jellyfin => "JELLYFIN",
        }
    }

    #[allow(dead_code)]
    pub fn search_url(self, query: &str) -> String {
        match self {
            Self::Youtube => format!(
                "https://www.youtube.com/results?search_query={}",
                query_encode(query)
            ),
            other => other.home_url().to_string(),
        }
    }

    #[allow(dead_code)]
    pub fn watch_url(self, video_id: &str) -> Option<String> {
        match self {
            Self::Youtube if is_youtube_video_id(video_id) => {
                Some(format!("https://www.youtube.com/watch?v={video_id}"))
            }
            _ => None,
        }
    }
}

fn is_youtube_video_id(s: &str) -> bool {
    let s = s.trim();
    s.len() == 11
        && s.chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
}

fn query_encode(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    for &b in s.as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => {
                out.push(b as char);
            }
            b' ' => out.push('+'),
            _ => out.push_str(&format!("%{b:02X}")),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn homes_are_first_party() {
        assert!(Service::Netflix.home_url().contains("netflix.com"));
        assert!(Service::Prime.home_url().contains("primevideo.com"));
        assert!(Service::Disney.home_url().contains("disneyplus.com"));
        assert!(Service::Youtube.home_url().contains("youtube.com"));
    }

    #[test]
    fn youtube_official_deep_links() {
        assert_eq!(
            Service::Youtube.search_url("lofi hip hop"),
            "https://www.youtube.com/results?search_query=lofi+hip+hop"
        );
        assert_eq!(
            Service::Youtube.watch_url("jNQXAC9IVRw").as_deref(),
            Some("https://www.youtube.com/watch?v=jNQXAC9IVRw")
        );
        assert!(Service::Youtube.watch_url("not a video").is_none());
    }
}
