//! Zappe-owned Chrome over CDP (dedicated profile).

use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use anyhow::{anyhow, Context, Result};
use chromiumoxide::browser::{Browser, BrowserConfig};
use chromiumoxide::cdp::browser_protocol::browser::{
    Bounds, GetWindowForTargetParams, SetWindowBoundsParams, WindowState,
};
use chromiumoxide::page::Page;
use futures::StreamExt;
use tokio::sync::{mpsc, Mutex};

use crate::skills::{Service, SiteSkill};

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
    Fullscreen,
    Back,
    ExitFullscreen,
    Shutdown,
}

pub struct ChromeManager {
    tx: mpsc::UnboundedSender<ChromeCmd>,
    launched_by_zappe: Arc<AtomicBool>,
    profile: PathBuf,
    ready: Arc<AtomicBool>,
}

impl ChromeManager {
    pub fn start() -> Self {
        let (tx, rx) = mpsc::unbounded_channel();
        let launched_by_zappe = Arc::new(AtomicBool::new(false));
        let ready = Arc::new(AtomicBool::new(false));
        let profile = profile_dir();
        let launched_flag = launched_by_zappe.clone();
        let ready_flag = ready.clone();
        tauri::async_runtime::spawn(async move {
            if let Err(err) = chrome_worker(rx, launched_flag, ready_flag).await {
                log::warn!("chrome worker ended: {err:#}");
            }
        });
        Self {
            tx,
            launched_by_zappe,
            profile,
            ready,
        }
    }

    pub fn profile_dir(&self) -> &Path {
        &self.profile
    }

    pub fn is_ready(&self) -> bool {
        self.ready.load(Ordering::SeqCst)
    }

    pub fn launched_by_zappe(&self) -> bool {
        self.launched_by_zappe.load(Ordering::SeqCst)
    }

    pub fn send(&self, cmd: ChromeCmd) {
        let _ = self.tx.send(cmd);
    }

    pub async fn shutdown_if_owned(&self) {
        if self.launched_by_zappe.load(Ordering::SeqCst) {
            self.send(ChromeCmd::Shutdown);
        }
    }
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
        for path in [
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
            "/Applications/Chromium.app/Contents/MacOS/Chromium",
        ] {
            let p = PathBuf::from(path);
            if p.exists() {
                return Some(p);
            }
        }
    }
    None
}

pub fn profile_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
        .join("chrome-profile")
}

pub fn chrome_pids_for_profile(profile: &Path) -> Vec<u32> {
    let needle = profile.to_string_lossy();
    let output = Command::new("pgrep")
        .args(["-f", &format!("user-data-dir={needle}")])
        .output();
    match output {
        Ok(out) if out.status.success() => String::from_utf8_lossy(&out.stdout)
            .lines()
            .filter_map(|l| l.trim().parse().ok())
            .collect(),
        _ => Vec::new(),
    }
}

fn which(name: &str) -> Option<PathBuf> {
    let output = Command::new("which").arg(name).output().ok()?;
    if !output.status.success() {
        return None;
    }
    let stdout = String::from_utf8_lossy(&output.stdout);
    let line = stdout.trim();
    if line.is_empty() {
        None
    } else {
        Some(PathBuf::from(line))
    }
}

struct WorkerState {
    browser: Browser,
    page: Page,
    active_service: Service,
}

async fn chrome_worker(
    mut cmds: mpsc::UnboundedReceiver<ChromeCmd>,
    launched_by_zappe: Arc<AtomicBool>,
    ready: Arc<AtomicBool>,
) -> Result<()> {
    let Some(exe) = find_chrome() else {
        return Ok(());
    };

    let profile = profile_dir();
    std::fs::create_dir_all(&profile)
        .with_context(|| format!("create profile dir {}", profile.display()))?;

    let (browser, page) = launch_profile(&exe, &profile).await?;
    launched_by_zappe.store(true, Ordering::SeqCst);
    ready.store(true, Ordering::SeqCst);

    let state = Mutex::new(WorkerState {
        browser,
        page,
        active_service: Service::Youtube,
    });

    while let Some(cmd) = cmds.recv().await {
        let mut st = state.lock().await;
        match cmd {
            ChromeCmd::Open {
                url,
                service,
                title,
            } => {
                st.active_service = service;
                if let Err(err) = open_service(&st.page, service, &url, &title).await {
                    log::warn!("open: {err:#}");
                } else {
                    let _ = raise_fullscreen(&st.page).await;
                    crate::wm::nudge_chrome_fullscreen();
                }
            }
            ChromeCmd::Pause => {
                run_skill(&st.page, st.active_service.skill(), SkillOp::Pause).await;
            }
            ChromeCmd::Play => {
                run_skill(&st.page, st.active_service.skill(), SkillOp::Play).await;
            }
            ChromeCmd::PlayPause => {
                run_skill(&st.page, st.active_service.skill(), SkillOp::PlayPause).await;
            }
            ChromeCmd::Fullscreen => {
                let _ = raise_fullscreen(&st.page).await;
                crate::wm::nudge_chrome_fullscreen();
                run_skill(&st.page, st.active_service.skill(), SkillOp::Fullscreen).await;
            }
            ChromeCmd::Back => {
                run_skill(&st.page, st.active_service.skill(), SkillOp::Back).await;
            }
            ChromeCmd::ExitFullscreen => {
                let _ = restore_windowed(&st.page).await;
                crate::wm::nudge_chrome_unfullscreen();
            }
            ChromeCmd::Shutdown => {
                break;
            }
        }
    }

    let _ = state.lock().await.browser.close().await;
    launched_by_zappe.store(false, Ordering::SeqCst);
    ready.store(false, Ordering::SeqCst);
    Ok(())
}

async fn launch_profile(exe: &Path, profile: &Path) -> Result<(Browser, Page)> {
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
    tauri::async_runtime::spawn(async move {
        while let Some(item) = handler.next().await {
            if item.is_err() {
                break;
            }
        }
    });

    let page = browser.new_page("about:blank").await?;
    Ok((browser, page))
}

async fn open_service(page: &Page, service: Service, url: &str, title: &str) -> Result<()> {
    page.goto(url).await?;
    if !crate::skills::is_official_deep_link(url)
        && title != "Home"
        && title != service.label()
    {
        let js = service.skill().open_title_js(title);
        let _ = page.evaluate(js).await;
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
        SkillOp::Pause => skill.pause_js(),
        SkillOp::Play => skill.play_js(),
        SkillOp::PlayPause => skill.play_pause_js(),
        SkillOp::Fullscreen => skill.fullscreen_js(),
        SkillOp::Back => skill.back_js(),
    };
    let _ = page.evaluate(js.to_string()).await;
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

async fn restore_windowed(page: &Page) -> Result<()> {
    let window = page
        .execute(GetWindowForTargetParams::builder().build())
        .await?;
    let bounds = Bounds::builder()
        .window_state(WindowState::Normal)
        .build();
    page.execute(SetWindowBoundsParams::new(window.window_id, bounds))
        .await?;
    Ok(())
}
