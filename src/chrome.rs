//! Zappe-owned Google Chrome: launch, CDP control, clean shutdown.
//!
//! Zappe always launches (or debug-attaches to) a headed Chrome with the dedicated
//! zappe `--user-data-dir`. Netflix / Prime / Disney / YouTube play there; cookies
//! never leave Chrome's profile. No embedding, scraping, or DRM work.

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{self, Receiver, Sender};
use std::time::{Duration, Instant};

use anyhow::{anyhow, Context, Result};
use chromiumoxide::browser::{Browser, BrowserConfig};
use chromiumoxide::cdp::browser_protocol::browser::{
    Bounds, GetWindowForTargetParams, SetWindowBoundsParams, WindowState,
};
use chromiumoxide::page::Page;
use futures::StreamExt;
use tokio::sync::mpsc::UnboundedReceiver;

use crate::accounts;
use crate::skills::{Service, SiteSkill};

#[derive(Clone, Debug)]
pub struct ChromeOpts {
    /// When set, connect to existing DevTools (debug only — zappe does not launch).
    pub cdp_attach: Option<String>,
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
    /// Raise Chrome and open a service home so the user can sign in.
    Login { service: Service },
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
    LoginRaised(Service),
    HiddenHud,
    Failed(String),
}

pub struct ChromeHandle {
    tx: tokio::sync::mpsc::UnboundedSender<ChromeCmd>,
    events: Receiver<ChromeEvent>,
}

impl ChromeHandle {
    pub fn start(rt: tokio::runtime::Handle, opts: ChromeOpts) -> Self {
        let (cmd_tx, cmd_rx) = tokio::sync::mpsc::unbounded_channel();
        let (ev_tx, ev_rx) = mpsc::channel();
        rt.spawn(async move {
            if let Err(err) = chrome_worker(opts, cmd_rx, ev_tx.clone()).await {
                log::error!("chrome worker: {err:#}");
                let _ = ev_tx.send(ChromeEvent::Failed(format!("{err:#}")));
            }
        });

        Self {
            tx: cmd_tx,
            events: ev_rx,
        }
    }

    pub fn send(&self, cmd: ChromeCmd) {
        let _ = self.tx.send(cmd);
    }

    pub fn poll_events(&mut self) -> Vec<ChromeEvent> {
        let mut out = Vec::new();
        while let Ok(ev) = self.events.try_recv() {
            out.push(ev);
        }
        out
    }

    /// Block until Chrome CDP is ready or fail fast on launch errors.
    pub fn wait_for_ready(&mut self, timeout: Duration) -> Result<()> {
        let start = Instant::now();
        loop {
            for ev in self.poll_events() {
                match ev {
                    ChromeEvent::Ready(_) => return Ok(()),
                    ChromeEvent::Failed(msg) => {
                        anyhow::bail!("Chrome failed to start: {msg}");
                    }
                    _ => {}
                }
            }
            if start.elapsed() >= timeout {
                anyhow::bail!(
                    "Timed out after {}s waiting for Chrome/CDP. Install Google Chrome or set CHROME_PATH.",
                    timeout.as_secs()
                );
            }
            std::thread::sleep(Duration::from_millis(40));
        }
    }
}

pub fn profile_dir() -> PathBuf {
    accounts::zappe_data_dir().join("chrome-profile")
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

pub fn ensure_chrome_binary(opts: &ChromeOpts) -> Result<PathBuf> {
    if opts.cdp_attach.is_some() {
        return Ok(PathBuf::from("(attach-debug)"));
    }
    find_chrome().ok_or_else(|| {
        anyhow!(
            "Google Chrome or Chromium not found. Install Chrome or set CHROME_PATH. \
             Streaming requires a real browser under zappe control."
        )
    })
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
    let owns_process = opts.cdp_attach.is_none();
    let profile = profile_dir();
    std::fs::create_dir_all(&profile)
        .with_context(|| format!("create profile dir {}", profile.display()))?;

    let (mut browser, page) = if let Some(cdp) = opts.cdp_attach.clone() {
        log::warn!("ZAPPE_CDP attach is a debug escape hatch — normal use launches Chrome automatically");
        attach_existing(&cdp).await?
    } else {
        let exe = find_chrome().context("Chrome binary missing after preflight")?;
        launch_profile(&exe, &profile).await?
    };

    let _ = events.send(ChromeEvent::Ready(format!(
        "profile {}",
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
            ChromeCmd::Login { service } => {
                active_service = service;
                let url = service.home_url();
                match open_service(&page, service, url, "Login").await {
                    Ok(()) => {
                        let _ = raise_login(&page).await;
                        os_raise_stub("Google Chrome");
                        let _ = events.send(ChromeEvent::LoginRaised(service));
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
            ChromeCmd::Shutdown => {
                if owns_process {
                    if let Err(err) = browser.close().await {
                        log::warn!("Browser.close: {err}");
                    }
                }
                break;
            }
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

    let page = browser.new_page("about:blank").await?;
    Ok((browser, page))
}

async fn attach_existing(cdp: &str) -> Result<(Browser, Page)> {
    log::info!("debug attach to Chrome at {cdp}");
    let (browser, mut handler) = Browser::connect(cdp).await?;
    tokio::spawn(async move {
        while let Some(item) = handler.next().await {
            if item.is_err() {
                break;
            }
        }
    });
    tokio::time::sleep(Duration::from_millis(200)).await;
    let pages = browser.pages().await?;
    let page = match pages.into_iter().next() {
        Some(page) => page,
        None => browser.new_page("about:blank").await?,
    };
    Ok((browser, page))
}

async fn search_via_skill(page: &Page, service: Service, query: &str) -> Result<()> {
    let Some(skill) = service.skill() else {
        anyhow::bail!("{} is not a Chrome service", service.label());
    };
    page.goto(service.home_url()).await?;
    page.evaluate(skill.search_js(query)).await?;
    Ok(())
}

async fn open_service(page: &Page, service: Service, url: &str, title: &str) -> Result<()> {
    if !service.uses_chrome() {
        anyhow::bail!("{} uses the OTA tuner, not Chrome", service.label());
    }
    log::info!("opening {} ({title}) -> {url}", service.label());
    page.goto(url).await?;
    if !crate::skills::is_official_deep_link(url)
        && title != "Home"
        && title != "Login"
        && title != service.label()
        && title != "Subscriptions"
    {
        if let Some(skill) = service.skill() {
            let js = skill.open_title_js(title);
            if let Err(err) = page.evaluate(js).await {
                log::debug!("open-title skill missed (expected): {err}");
            }
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

async fn run_skill(page: &Page, skill: Option<&dyn SiteSkill>, op: SkillOp) {
    let Some(skill) = skill else {
        return;
    };
    let js = match op {
        SkillOp::Pause => skill.pause_js().to_string(),
        SkillOp::Play => skill.play_js().to_string(),
        SkillOp::PlayPause => skill.play_pause_js().to_string(),
        SkillOp::Fullscreen => skill.fullscreen_js().to_string(),
        SkillOp::Back => skill.back_js().to_string(),
    };
    if let Err(err) = page.evaluate(js).await {
        log::warn!(
            "fragile {} skill failed (expected to break sometimes): {err}",
            skill.service().label()
        );
    }
}

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

async fn raise_login(page: &Page) -> Result<()> {
    let window = page
        .execute(GetWindowForTargetParams::builder().build())
        .await
        .context("Browser.getWindowForTarget")?;
    let bounds = Bounds::builder()
        .window_state(WindowState::Maximized)
        .build();
    page.execute(SetWindowBoundsParams::new(window.window_id, bounds))
        .await
        .context("Browser.setWindowBounds")?;
    Ok(())
}

pub fn os_raise_stub(title_hint: &str) {
    #[cfg(target_os = "macos")]
    {
        let script = "tell application \"System Events\" to set frontmost of first process whose name contains \"Chrome\" to true";
        let _ = Command::new("osascript").arg("-e").arg(script).status();
        let _ = title_hint;
    }
    #[cfg(target_os = "linux")]
    {
        let _ = Command::new("wmctrl").args(["-a", title_hint]).status();
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        let _ = title_hint;
    }
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
