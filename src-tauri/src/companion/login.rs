//! Hidden Chrome login driver. HDMI stays on the Zappe Connect screen.
//!
//! Prefer Xvfb (virtual display) so Netflix never appears on the living-room
//! output. Fall back to gamescope **without** `-f` and the harvest hide nudge.
//! Never CDP / `--enable-automation` / Chrome `--headless`.

use std::path::Path;
use std::process::Stdio;
use std::time::Duration;

use anyhow::{anyhow, Result};
use tokio::process::{Child, Command};

use super::sources::{self, AuthSource};
use super::{Hub, SessionStatus};
use crate::nest::{
    chrome_args, chrome_args_for_mode, env_flag, find_chrome, has_no_cdp_flags, which, NestManager,
    NestMode,
};
use crate::nest_input::{inject_key_quiet, inject_text_quiet};
use crate::paths::profile_dir;
use crate::wm;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoginProvider {
    Password,
    EmailCode,
    Google,
}

impl LoginProvider {
    pub fn parse(raw: Option<&str>) -> Self {
        match raw
            .unwrap_or("password")
            .trim()
            .to_ascii_lowercase()
            .as_str()
        {
            "email_code" | "email-code" | "code" => Self::EmailCode,
            "google" => Self::Google,
            _ => Self::Password,
        }
    }

    fn start_url(self, source: &AuthSource) -> &'static str {
        match self {
            Self::Google => source.google_continue.unwrap_or(source.login_url),
            Self::Password | Self::EmailCode => source.login_url,
        }
    }
}

pub struct LoginJob {
    pub email: String,
    pub password: String,
    pub provider: LoginProvider,
    pub source: String,
}

pub async fn drive_login(hub: Hub, nest: NestManager, job: LoginJob) {
    match run_login(&hub, &nest, &job).await {
        Ok(()) => {
            let src = sources::require(&job.source);
            hub.set_connected(format!(
                "Connected. {} is on this TV — HDMI never left Zappe.",
                src.label
            ))
            .await;
        }
        Err(err) => {
            log::warn!("hidden login failed: {err:#}");
            hub.set_error(format!("Sign-in did not finish: {err}"))
                .await;
        }
    }
    nest.hide().await;
    wm::nudge_guide_fullscreen(None);
}

async fn run_login(hub: &Hub, nest: &NestManager, job: &LoginJob) -> Result<()> {
    if env_flag("ZAPPE_LOGIN_FAKE") {
        tokio::time::sleep(Duration::from_millis(40)).await;
        return Ok(());
    }

    let source = sources::require(&job.source);
    let profile = profile_dir();
    if sources::profile_has_session(&profile, source) {
        sources::mark_connected(source.id);
        return Ok(());
    }

    hub.set_status(
        SessionStatus::SigningIn,
        format!(
            "Starting a hidden browser for {} with this TV's Chrome profile…",
            source.label
        ),
    )
    .await;

    let start_url = job.provider.start_url(source);
    let mut xvfb = if prefer_xvfb() {
        match spawn_xvfb_chrome(&profile, start_url).await {
            Ok(child) => Some(child),
            Err(err) => {
                log::info!(
                    "Xvfb login unavailable ({err:#}); using minimized Chrome (same as harvest)"
                );
                None
            }
        }
    } else {
        None
    };

    let _bg = if xvfb.is_none() {
        let lease = nest.try_begin_background().ok_or_else(|| {
            anyhow!("harvest/login nest already running; not spawning another Chrome")
        })?;
        nest.open_url(start_url, NestMode::Login).await?;
        Some(lease)
    } else {
        None
    };

    keep_hidden(nest).await;
    let hide = spawn_keep_hidden(nest.clone());

    tokio::time::sleep(Duration::from_millis(3500)).await;
    keep_hidden(nest).await;

    if !job.email.is_empty() {
        let _ = inject_text_quiet(&job.email);
        tokio::time::sleep(Duration::from_millis(400)).await;
        let _ = inject_key_quiet("ok");
        tokio::time::sleep(Duration::from_millis(1600)).await;
    }

    match job.provider {
        LoginProvider::Password | LoginProvider::Google => {
            if !job.password.is_empty() {
                let _ = inject_text_quiet(&job.password);
                tokio::time::sleep(Duration::from_millis(300)).await;
                let _ = inject_key_quiet("ok");
            }
        }
        LoginProvider::EmailCode => {
            hub.set_status(
                SessionStatus::NeedsFactor,
                "Enter the email/SMS code on your phone. The TV stays on Connect.",
            )
            .await;
        }
    }

    let deadline = tokio::time::Instant::now() + Duration::from_secs(90);
    while tokio::time::Instant::now() < deadline {
        keep_hidden(nest).await;
        if sources::profile_has_session(&profile, source) {
            hide.abort();
            if let Some(mut child) = xvfb.take() {
                let _ = child.start_kill();
            }
            sources::mark_connected(source.id);
            return Ok(());
        }
        if let Some(factor) = hub.take_factor().await {
            let _ = inject_text_quiet(&factor);
            tokio::time::sleep(Duration::from_millis(200)).await;
            let _ = inject_key_quiet("ok");
            hub.set_status(
                SessionStatus::SigningIn,
                "Submitting the extra code in the hidden browser…",
            )
            .await;
        }
        tokio::time::sleep(Duration::from_millis(1000)).await;
    }

    hide.abort();
    if let Some(mut child) = xvfb.take() {
        let _ = child.start_kill();
    }
    if sources::profile_has_session(&profile, source) {
        sources::mark_connected(source.id);
        return Ok(());
    }
    Err(anyhow!(
        "no {} session in the Chrome profile yet (try email+password or the email code again)",
        source.label
    ))
}

fn prefer_xvfb() -> bool {
    // Default: same invisible Chrome path as harvest (no second gamescope,
    // --start-minimized). Xvfb is opt-in.
    matches!(
        std::env::var("ZAPPE_LOGIN_BACKEND")
            .map(|s| s.to_ascii_lowercase())
            .as_deref(),
        Ok("xvfb")
    ) && which("Xvfb").is_some()
}

async fn spawn_xvfb_chrome(profile: &Path, url: &str) -> Result<Child> {
    let xvfb = which("Xvfb").ok_or_else(|| anyhow!("Xvfb not on PATH"))?;
    let chrome = find_chrome().ok_or_else(|| anyhow!("Chrome not installed"))?;
    std::fs::create_dir_all(profile)?;
    let display = free_x_display().ok_or_else(|| anyhow!("no free X display"))?;
    let display_spec = format!(":{display}");
    let mut server = Command::new(xvfb);
    server
        .args([
            &display_spec,
            "-screen",
            "0",
            "1280x720x24",
            "-nolisten",
            "tcp",
            "-ac",
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    let xvfb_child = server.spawn()?;
    tokio::time::sleep(Duration::from_millis(250)).await;

    let args = chrome_args(profile, Some(url));
    if !has_no_cdp_flags(&args) {
        return Err(anyhow!("refusing Chrome automation flags"));
    }
    let mut chrome_cmd = Command::new(&chrome);
    chrome_cmd
        .args(&args)
        .env("DISPLAY", &display_spec)
        .env("ACCESSIBILITY_ENABLED", "1")
        .env("GTK_A11Y", "atspi")
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    if let Err(err) = chrome_cmd.spawn() {
        let _ = xvfb_child;
        return Err(err.into());
    }
    log::info!("login Chrome on Xvfb {display_spec} (not on HDMI)");
    Ok(xvfb_child)
}

fn free_x_display() -> Option<u32> {
    for n in 70..99 {
        let lock = format!("/tmp/.X{n}-lock");
        if !Path::new(&lock).exists() {
            return Some(n);
        }
    }
    None
}

async fn keep_hidden(nest: &NestManager) {
    nest.hide().await;
    wm::nudge_guide_fullscreen(None);
}

fn spawn_keep_hidden(nest: NestManager) -> tokio::task::JoinHandle<()> {
    tokio::spawn(async move {
        for _ in 0..180 {
            nest.hide().await;
            wm::nudge_guide_fullscreen(None);
            tokio::time::sleep(Duration::from_millis(500)).await;
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn providers_map_from_phone_form() {
        assert_eq!(LoginProvider::parse(Some("google")), LoginProvider::Google);
        assert_eq!(
            LoginProvider::parse(Some("email_code")),
            LoginProvider::EmailCode
        );
        assert_eq!(
            LoginProvider::parse(Some("password")),
            LoginProvider::Password
        );
        assert_eq!(
            LoginProvider::Google.start_url(&sources::NETFLIX),
            sources::NETFLIX.google_continue.unwrap()
        );
        assert_eq!(
            LoginProvider::Password.start_url(&sources::NETFLIX),
            sources::NETFLIX.login_url
        );
        assert_eq!(
            LoginProvider::Password.start_url(&sources::PRIME),
            sources::PRIME.login_url
        );
    }

    #[test]
    fn login_never_uses_chrome_headless_or_cdp() {
        let args = chrome_args_for_mode(
            Path::new("/tmp/zappe/chrome-profile"),
            Some("https://www.netflix.com/login"),
            NestMode::Login,
        );
        assert!(has_no_cdp_flags(&args));
        assert!(args.iter().any(|a| a == "--start-minimized"));
        assert!(!args.iter().any(|a| a.contains("enable-automation")));
        assert!(!args.iter().any(|a| a.contains("remote-debugging")));
        assert!(!args.iter().any(|a| a == "--headless"));
        let src = include_str!("login.rs");
        assert!(src.contains("NestMode::Login"));
        assert!(src.contains("inject_text_quiet"));
    }
}
