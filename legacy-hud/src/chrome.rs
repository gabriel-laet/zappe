//! Dedicated Google Chrome process, driven over CDP.
//!
//! Zappe never embeds Netflix / Prime / Disney / YouTube, never scrapes their HTML into
//! a catalog, and never extracts or re-encodes DRM video. The user logs in by
//! hand once inside this profile. Cookies stay in Chrome's user-data-dir.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use chromiumoxide::browser::{Browser, BrowserConfig};
use chromiumoxide::cdp::browser_protocol::browser::{
    Bounds, GetWindowForTargetParams, SetWindowBoundsParams, WindowState,
};
use chromiumoxide::page::Page;
use futures::StreamExt;
use tokio::sync::mpsc::UnboundedReceiver;

use crate::skills::{Service, SiteSkill};

const DEFAULT_CDP: &str = "http://127.0.0.1:9222";

#[derive(Clone, Debug)]
pub struct ChromeOpts {
    pub enabled: bool,
    pub cdp: Option<String>,
    pub initial_url: Option<String>,
    pub service: Service,
}

#[derive(Debug)]
pub enum ChromeCmd {
    Open {
        url: String,
        service: Service,
        title: String,
    },
    Pause,
    Play,
    PlayPause,
    Search {
        query: String,
        service: Service,
    },
    Fullscreen,
    Back,
    ShowHud,
    Shutdown,
}

#[derive(Clone, Debug)]
pub enum ChromeEvent {
    Ready(String),
    Opened(String),
    HiddenHud,
    Failed(String),
    MissingChrome,
}

pub struct ChromeHandle {
    pub enabled: bool,
    tx: Option<tokio::sync::mpsc::UnboundedSender<ChromeCmd>>,
    events: Receiver<ChromeEvent>,
}

impl ChromeHandle {
    pub fn start(rt: tokio::runtime::Handle, opts: ChromeOpts) -> Self {
        if !opts.enabled {
            log::info!("Chrome/CDP disabled — HUD only. Pass --chrome to attach.");
            let (_tx, rx) = mpsc::channel();
            return Self {
                enabled: false,
                tx: None,
                events: rx,
            };
        }

        let (cmd_tx, cmd_rx) = tokio::sync::mpsc::unbounded_channel();
        let (ev_tx, ev_rx) = mpsc::channel();
        rt.spawn(async move {
            if let Err(err) = chrome_worker(opts, cmd_rx, ev_tx.clone()).await {
                log::warn!("chrome worker: {err:#}");
                let _ = ev_tx.send(ChromeEvent::Failed(format!("{err:#}")));
            }
        });

        Self {
            enabled: true,
            tx: Some(cmd_tx),
            events: ev_rx,
        }
    }

    pub fn send(&self, cmd: ChromeCmd) {
        if let Some(tx) = &self.tx {
            let _ = tx.send(cmd);
        }
    }

    pub fn poll_events(&mut self) -> Vec<ChromeEvent> {
        let mut out = Vec::new();
        while let Ok(ev) = self.events.try_recv() {
            out.push(ev);
        }
        out
    }
}

pub fn profile_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
        .join("chrome-profile")
}

pub fn find_chrome() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("CHROME_PATH") {
        let path = PathBuf::from(explicit);
        if path.exists() {
            return Some(path);
        }
    }

    const NAMES: &[&str] = &[
        "google-chrome",
        "google-chrome-stable",
        "google-chrome-beta",
        "chromium",
        "chromium-browser",
        "chrome",
    ];
    for name in NAMES {
        if let Some(path) = which(name) {
            return Some(path);
        }
    }

    #[cfg(target_os = "macos")]
    {
        const MAC: &[&str] = &[
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
        ];
        for path in MAC {
            let path = PathBuf::from(path);
            if path.exists() {
                return Some(path);
            }
        }
    }

    None
}

fn which(name: &str) -> Option<PathBuf> {
    let output = Command::new("which").arg(name).output().ok()?;
    if !output.status.success() {
        return None;
    }
    let line = String::from_utf8_lossy(&output.stdout);
    let line = line.trim();
    if line.is_empty() {
        None
    } else {
        Some(PathBuf::from(line))
    }
}

async fn chrome_worker(
    opts: ChromeOpts,
    mut cmds: UnboundedReceiver<ChromeCmd>,
    events: Sender<ChromeEvent>,
) -> Result<()> {
    let Some(exe) = find_chrome() else {
        log::warn!("Chrome binary not found; HUD keeps running.");
        let _ = events.send(ChromeEvent::MissingChrome);
        return Ok(());
    };

    let profile = profile_dir();
    std::fs::create_dir_all(&profile)
        .with_context(|| format!("create profile dir {}", profile.display()))?;

    let (browser, page) = if let Some(cdp) = opts.cdp.clone() {
        attach_existing(&cdp).await?
    } else {
        launch_profile(&exe, &profile).await?
    };

    let _ = events.send(ChromeEvent::Ready(format!(
        "cdp {} profile {}",
        browser.websocket_address(),
        profile.display()
    )));

    if let Some(url) = opts.initial_url.clone() {
        open_service(&page, opts.service, &url, opts.service.label()).await?;
        let _ = events.send(ChromeEvent::Opened(url));
    }

    let mut active_service = opts.service;
    while let Some(cmd) = cmds.recv().await {
        match cmd {
            ChromeCmd::Open {
                url,
                service,
                title,
            } => {
                active_service = service;
                match open_service(&page, service, &url, &title).await {
                    Ok(()) => {
                        let _ = raise_fullscreen(&page).await;
                        os_raise_stub("Google Chrome");
                        let _ = events.send(ChromeEvent::Opened(url));
                        let _ = events.send(ChromeEvent::HiddenHud);
                    }
                    Err(err) => {
                        let _ = events.send(ChromeEvent::Failed(format!("{err:#}")));
                    }
                }
            }
            ChromeCmd::Pause => {
                run_skill(&page, active_service.skill(), SkillOp::Pause).await;
            }
            ChromeCmd::Play => {
                run_skill(&page, active_service.skill(), SkillOp::Play).await;
            }
            ChromeCmd::PlayPause => {
                run_skill(&page, active_service.skill(), SkillOp::PlayPause).await;
            }
            ChromeCmd::Search { query, service } => {
                active_service = service;
                let url = service.search_url(&query);
                if crate::skills::is_official_deep_link(&url) {
                    match open_service(&page, service, &url, &query).await {
                        Ok(()) => {
                            let _ = events.send(ChromeEvent::Opened(url));
                        }
                        Err(err) => {
                            let _ = events.send(ChromeEvent::Failed(format!("{err:#}")));
                        }
                    }
                } else {
                    match search_via_skill(&page, service, &query).await {
                        Ok(()) => {
                            let _ = events.send(ChromeEvent::Opened(url));
                        }
                        Err(err) => {
                            log::warn!("search skill missed (expected): {err}");
                            let _ = events.send(ChromeEvent::Failed(format!("{err:#}")));
                        }
                    }
                }
            }
            ChromeCmd::Fullscreen => {
                let _ = raise_fullscreen(&page).await;
                run_skill(&page, active_service.skill(), SkillOp::Fullscreen).await;
            }
            ChromeCmd::Back => {
                run_skill(&page, active_service.skill(), SkillOp::Back).await;
            }
            ChromeCmd::ShowHud => {}
            ChromeCmd::Shutdown => break,
        }
    }

    Ok(())
}

async fn launch_profile(exe: &Path, profile: &Path) -> Result<(Browser, Page)> {
    log::info!(
        "launching Chrome {} with zappe profile {}",
        exe.display(),
        profile.display()
    );

    let config = BrowserConfig::builder()
        .chrome_executable(exe)
        .user_data_dir(profile)
        .with_head()
        .viewport(None)
        .arg("--no-first-run")
        .arg("--no-default-browser-check")
        .build()
        .map_err(|e| anyhow!(e))?;

    let (browser, mut handler) = Browser::launch(config).await?;
    tokio::spawn(async move {
        while let Some(item) = handler.next().await {
            if item.is_err() {
                break;
            }
        }
    });

    // Dedicated profile, first-party sites only. User signs in by hand.
    let page = browser.new_page("about:blank").await?;
    Ok((browser, page))
}

async fn attach_existing(cdp: &str) -> Result<(Browser, Page)> {
    log::info!("attaching to existing Chrome at {cdp}");
    let (mut browser, mut handler) = Browser::connect(cdp).await?;
    tokio::spawn(async move {
        while let Some(item) = handler.next().await {
            if item.is_err() {
                break;
            }
        }
    });
    let _ = browser.fetch_targets().await;
    tokio::time::sleep(Duration::from_millis(200)).await;
    let pages = browser.pages().await?;
    let page = match pages.into_iter().next() {
        Some(page) => page,
        None => browser.new_page("about:blank").await?,
    };
    Ok((browser, page))
}

async fn search_via_skill(page: &Page, service: Service, query: &str) -> Result<()> {
    page.goto(service.home_url()).await?;
    page.evaluate(service.skill().search_js(query)).await?;
    Ok(())
}

async fn open_service(page: &Page, service: Service, url: &str, title: &str) -> Result<()> {
    log::info!("opening {} ({title}) -> {url}", service.label());
    page.goto(url).await?;
    // Deep links already express intent. Do not scrape search results into a pick.
    if !crate::skills::is_official_deep_link(url)
        && title != "Home"
        && title != service.label()
        && title != "Subscriptions"
    {
        let js = service.skill().open_title_js(title);
        if let Err(err) = page.evaluate(js).await {
            log::debug!("open-title skill missed (expected): {err}");
        }
    }
    Ok(())
}

enum SkillOp {
    Pause,
    Play,
    PlayPause,
    Fullscreen,
    Back,
}

async fn run_skill(page: &Page, skill: &dyn SiteSkill, op: SkillOp) {
    let js = match op {
        SkillOp::Pause => skill.pause_js().to_string(),
        SkillOp::Play => skill.play_js().to_string(),
        SkillOp::PlayPause => skill.play_pause_js().to_string(),
        SkillOp::Fullscreen => skill.fullscreen_js().to_string(),
        SkillOp::Back => skill.back_js().to_string(),
    };
    if let Err(err) = page.evaluate(js).await {
        log::warn!(
            "fragile {} skill failed (this is expected to break): {err}",
            skill.service().label()
        );
    }
}

/// CDP `Browser.setWindowBounds` — the supported way to raise / fullscreen Chrome.
pub async fn raise_fullscreen(page: &Page) -> Result<()> {
    let window = page
        .execute(GetWindowForTargetParams::builder().build())
        .await
        .context("Browser.getWindowForTarget")?;
    let bounds = Bounds::builder()
        .window_state(WindowState::Fullscreen)
        .build();
    page.execute(SetWindowBoundsParams::new(window.window_id, bounds))
        .await
        .context("Browser.setWindowBounds")?;
    Ok(())
}

/// OS helpers when CDP window bounds are not enough. Best-effort stubs.
pub fn os_raise_stub(title_hint: &str) {
    #[cfg(target_os = "macos")]
    {
        // tell application "Google Chrome" to activate
        let script = format!(
            "tell application \"System Events\" to set frontmost of first process whose name contains \"Chrome\" to true"
        );
        let _ = Command::new("osascript").arg("-e").arg(script).status();
        let _ = title_hint;
    }
    #[cfg(target_os = "linux")]
    {
        // Requires wmctrl on PATH. Missing binary is fine.
        let _ = Command::new("wmctrl").args(["-a", title_hint]).status();
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        let _ = title_hint;
    }
}

#[allow(dead_code)]
pub fn default_cdp_url() -> &'static str {
    DEFAULT_CDP
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn profile_lives_under_zappe() {
        let dir = profile_dir();
        let text = dir.to_string_lossy();
        assert!(text.contains("zappe"));
        assert!(text.contains("chrome-profile"));
    }

    #[test]
    fn find_chrome_does_not_panic() {
        let _ = find_chrome();
    }
}
