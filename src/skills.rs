//! Tiny per-site skills.
//!
//! Open a first-party URL, then poke a handful of controls. Selectors live
//! only here and are treated as fragile — they will break when the site ships
//! a redesign. Do not scrape these pages into a catalog.

use clap::ValueEnum;

#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum Service {
    Netflix,
    Prime,
    Disney,
    Jellyfin,
}

impl Service {
    pub fn home_url(self) -> &'static str {
        match self {
            Self::Netflix => "https://www.netflix.com",
            Self::Prime => "https://www.primevideo.com",
            Self::Disney => "https://www.disneyplus.com",
            Self::Jellyfin => "http://localhost:8096",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Self::Netflix => "NETFLIX",
            Self::Prime => "PRIME",
            Self::Disney => "DISNEY+",
            Self::Jellyfin => "JELLYFIN",
        }
    }

    pub fn skill(self) -> &'static dyn SiteSkill {
        match self {
            Self::Netflix => &NETFLIX,
            Self::Prime => &PRIME,
            Self::Disney => &DISNEY,
            Self::Jellyfin => &JELLYFIN,
        }
    }
}

/// Isolated, fragile DOM hooks for a first-party player page.
pub trait SiteSkill: Send + Sync {
    fn service(&self) -> Service;
    fn open_title_js(&self, hint: &str) -> String;
    fn pause_js(&self) -> &'static str;
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
struct Jellyfin;

static NETFLIX: Netflix = Netflix;
static PRIME: Prime = Prime;
static DISNEY: Disney = Disney;
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn homes_are_first_party() {
        assert!(Service::Netflix.home_url().contains("netflix.com"));
        assert!(Service::Prime.home_url().contains("primevideo.com"));
        assert!(Service::Disney.home_url().contains("disneyplus.com"));
    }

    #[test]
    fn selectors_stay_isolated_in_skills() {
        let js = Service::Netflix.skill().pause_js();
        assert!(js.contains("querySelector"));
        assert!(js.contains("data-uia") || js.contains("aria-label"));
    }
}
