//! Tiny per-site skills.
//!
//! Open a first-party URL, then poke a handful of controls (search / open /
//! pause / play / fullscreen / back). Selectors live only here and are treated
//! as fragile — they will break when the site ships a redesign. Do not scrape
//! these pages into a catalog. YouTube uses official `/watch?v=`,
//! `/feed/subscriptions`, and `/results?search_query=` links.

use clap::ValueEnum;

#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum Service {
    Netflix,
    Prime,
    Disney,
    Youtube,
    Jellyfin,
}

impl Service {
    pub fn home_url(self) -> &'static str {
        match self {
            Self::Netflix => "https://www.netflix.com",
            Self::Prime => "https://www.primevideo.com",
            Self::Disney => "https://www.disneyplus.com",
            Self::Youtube => "https://www.youtube.com",
            Self::Jellyfin => "http://localhost:8096",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Netflix => "NETFLIX",
            Self::Prime => "PRIME",
            Self::Disney => "DISNEY+",
            Self::Youtube => "YOUTUBE",
            Self::Jellyfin => "JELLYFIN",
        }
    }

    pub fn skill(self) -> &'static dyn SiteSkill {
        match self {
            Self::Netflix => &NETFLIX,
            Self::Prime => &PRIME,
            Self::Disney => &DISNEY,
            Self::Youtube => &YOUTUBE,
            Self::Jellyfin => &JELLYFIN,
        }
    }

    /// Official first-party search URL when the site has one.
    pub fn search_url(self, query: &str) -> String {
        match self {
            Self::Youtube => format!(
                "https://www.youtube.com/results?search_query={}",
                query_encode(query)
            ),
            other => other.home_url().to_string(),
        }
    }

    /// Official watch deep link. YouTube only (`/watch?v=`).
    pub fn watch_url(self, video_id: &str) -> Option<String> {
        match self {
            Self::Youtube if is_youtube_video_id(video_id) => {
                Some(format!("https://www.youtube.com/watch?v={video_id}"))
            }
            _ => None,
        }
    }

    pub fn subscriptions_url(self) -> Option<&'static str> {
        match self {
            Self::Youtube => Some("https://www.youtube.com/feed/subscriptions"),
            _ => None,
        }
    }
}

/// Isolated, fragile DOM hooks for a first-party player page.
pub trait SiteSkill: Send + Sync {
    fn service(&self) -> Service;
    fn open_title_js(&self, hint: &str) -> String;
    fn search_js(&self, query: &str) -> String {
        self.open_title_js(query)
    }
    fn pause_js(&self) -> &'static str;
    fn play_js(&self) -> &'static str {
        self.pause_js()
    }
    fn play_pause_js(&self) -> &'static str {
        self.pause_js()
    }
    fn fullscreen_js(&self) -> &'static str;
    fn back_js(&self) -> &'static str;
}

macro_rules! click {
    ($sel:expr) => {
        concat!(
            "(function(){var e=document.querySelector(",
            stringify!($sel),
            "); if(e) e.click();})()"
        )
    };
}

struct Netflix;
struct Prime;
struct Disney;
struct Youtube;
struct Jellyfin;

static NETFLIX: Netflix = Netflix;
static PRIME: Prime = Prime;
static DISNEY: Disney = Disney;
static YOUTUBE: Youtube = Youtube;
static JELLYFIN: Jellyfin = Jellyfin;

impl SiteSkill for Netflix {
    fn service(&self) -> Service {
        Service::Netflix
    }

    fn open_title_js(&self, hint: &str) -> String {
        // FRAGILE: search box is a last resort. Prefer the deep-link URL.
        format!(
            "(function(){{ var e=document.querySelector('input[data-uia=\"search-input\"]'); if(e){{ e.focus(); e.value={hint:?}; e.dispatchEvent(new Event('input',{{bubbles:true}})); }} }})()"
        )
    }

    fn pause_js(&self) -> &'static str {
        // FRAGILE: Netflix player chrome.
        click!("button[data-uia='control-play-pause-pause'], button[aria-label='Pause']")
    }

    fn fullscreen_js(&self) -> &'static str {
        click!("button[data-uia='control-fullscreen-enter'], button[aria-label='Full screen']")
    }

    fn back_js(&self) -> &'static str {
        "window.history.back()"
    }
}

impl SiteSkill for Prime {
    fn service(&self) -> Service {
        Service::Prime
    }

    fn open_title_js(&self, hint: &str) -> String {
        format!(
            "(function(){{ var e=document.querySelector('input[type=\"search\"]'); if(e){{ e.focus(); e.value={hint:?}; }} }})()"
        )
    }

    fn pause_js(&self) -> &'static str {
        click!("button[aria-label='Pause'], button[aria-label='Pause Video']")
    }

    fn fullscreen_js(&self) -> &'static str {
        click!("button[aria-label='Full screen'], button[aria-label='Fullscreen']")
    }

    fn back_js(&self) -> &'static str {
        "window.history.back()"
    }
}

impl SiteSkill for Disney {
    fn service(&self) -> Service {
        Service::Disney
    }

    fn open_title_js(&self, hint: &str) -> String {
        format!(
            "(function(){{ var e=document.querySelector('input[type=\"search\"]'); if(e){{ e.focus(); e.value={hint:?}; }} }})()"
        )
    }

    fn pause_js(&self) -> &'static str {
        click!("button[aria-label='Pause'], button[data-testid='pause']")
    }

    fn fullscreen_js(&self) -> &'static str {
        click!("button[aria-label='Full screen'], button[data-testid='fullscreen']")
    }

    fn back_js(&self) -> &'static str {
        "window.history.back()"
    }
}

impl SiteSkill for Youtube {
    fn service(&self) -> Service {
        Service::Youtube
    }

    fn open_title_js(&self, hint: &str) -> String {
        // Prefer official /watch?v= or /results?search_query=. Do not scrape results.
        let url = Service::Youtube
            .watch_url(hint)
            .unwrap_or_else(|| Service::Youtube.search_url(hint));
        format!("window.location.assign({url:?})")
    }

    fn search_js(&self, query: &str) -> String {
        let url = Service::Youtube.search_url(query);
        format!("window.location.assign({url:?})")
    }

    fn pause_js(&self) -> &'static str {
        // FRAGILE: in-page <video>, not a catalog scrape.
        "(function(){var v=document.querySelector('video'); if(v) v.pause();})()"
    }

    fn play_js(&self) -> &'static str {
        "(function(){var v=document.querySelector('video'); if(v) v.play();})()"
    }

    fn play_pause_js(&self) -> &'static str {
        "(function(){var v=document.querySelector('video'); if(!v) return; if(v.paused) v.play(); else v.pause();})()"
    }

    fn fullscreen_js(&self) -> &'static str {
        // FRAGILE: YouTube player chrome.
        click!("button.ytp-fullscreen-button, button[aria-label='Full screen']")
    }

    fn back_js(&self) -> &'static str {
        "window.history.back()"
    }
}

impl SiteSkill for Jellyfin {
    fn service(&self) -> Service {
        Service::Jellyfin
    }

    fn open_title_js(&self, hint: &str) -> String {
        format!(
            "(function(){{ var e=document.querySelector('input[type=\"search\"]'); if(e){{ e.focus(); e.value={hint:?}; }} }})()"
        )
    }

    fn pause_js(&self) -> &'static str {
        click!("button.playPauseButton, button[title='Pause']")
    }

    fn fullscreen_js(&self) -> &'static str {
        click!("button.fullscreenButton, button[title='Fullscreen']")
    }

    fn back_js(&self) -> &'static str {
        "window.history.back()"
    }
}

pub fn is_official_deep_link(url: &str) -> bool {
    let lower = url.to_ascii_lowercase();
    lower.contains("/watch?")
        || lower.contains("/results?")
        || lower.contains("/feed/")
        || lower.contains("search_query=")
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
        assert_eq!(
            Service::Youtube.subscriptions_url(),
            Some("https://www.youtube.com/feed/subscriptions")
        );
        assert!(is_official_deep_link(
            "https://www.youtube.com/results?search_query=lofi"
        ));
        let open = Service::Youtube.skill().open_title_js("jNQXAC9IVRw");
        assert!(open.contains("/watch?v=jNQXAC9IVRw"));
        assert!(!open.contains("querySelectorAll"));
    }

    #[test]
    fn selectors_stay_isolated_in_skills() {
        let js = Service::Netflix.skill().pause_js();
        assert!(js.contains("querySelector"));
        assert!(js.contains("data-uia") || js.contains("aria-label"));
        let yt = Service::Youtube.skill();
        assert!(yt.pause_js().contains("video"));
        assert!(yt.play_js().contains("play"));
        assert!(yt.play_pause_js().contains("paused"));
        assert!(
            yt.fullscreen_js().contains("ytp-fullscreen")
                || yt.fullscreen_js().contains("aria-label")
        );
    }
}
