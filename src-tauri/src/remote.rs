//! ATONGX volume / mute: Samsung TV first, PulseAudio (`pactl`) fallback.
//!
//! Power and menu stay in `lib.rs` so they can talk to playback / the guide.
//! Never power the box off from here. Never send Samsung `KEY_POWER` /
//! `KEY_SOURCE` — those would blank HDMI instead of returning to the guide.

use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use log::{info, warn};
use serde::Serialize;
use tauri::{AppHandle, Emitter};

use crate::samsung::{
    load_config, persist_token, send_key, SamsungConfig, TvError, TvKey, EVENT_TV_CONTROL,
};

static FALLBACK_ANNOUNCED: AtomicBool = AtomicBool::new(false);
static LAST_FAIL: Mutex<Option<Instant>> = Mutex::new(None);

fn cooldown() -> Duration {
    std::env::var("ZAPPE_SAMSUNG_COOLDOWN_MS")
        .ok()
        .and_then(|s| s.parse().ok())
        .map(Duration::from_millis)
        .unwrap_or(Duration::from_secs(45))
}

pub fn should_try_samsung(cfg: &SamsungConfig, now: Instant) -> bool {
    if !cfg.enabled() {
        return false;
    }
    let guard = LAST_FAIL.lock().unwrap_or_else(|e| e.into_inner());
    match *guard {
        Some(at) if now.duration_since(at) < cooldown() => false,
        _ => true,
    }
}

fn mark_fail() {
    *LAST_FAIL.lock().unwrap_or_else(|e| e.into_inner()) = Some(Instant::now());
}

fn mark_ok() {
    *LAST_FAIL.lock().unwrap_or_else(|e| e.into_inner()) = None;
    FALLBACK_ANNOUNCED.store(false, Ordering::Relaxed);
}

fn take_first_fallback() -> bool {
    !FALLBACK_ANNOUNCED.swap(true, Ordering::Relaxed)
}

#[cfg(test)]
pub fn reset_runtime_state() {
    FALLBACK_ANNOUNCED.store(false, Ordering::Relaxed);
    *LAST_FAIL.lock().unwrap_or_else(|e| e.into_inner()) = None;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum ControlVia {
    Samsung,
    Pactl,
}

#[derive(Clone, Debug, Serialize)]
pub struct TvControlEvent {
    pub action: String,
    pub via: ControlVia,
    pub message: String,
    pub first_fallback: bool,
}

fn pactl(args: &[&str]) -> Result<String, String> {
    let out = Command::new("pactl")
        .args(args)
        .output()
        .map_err(|err| format!("pactl: {err}"))?;
    if !out.status.success() {
        return Err(format!(
            "pactl failed: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}

pub fn pactl_volume(delta: i32) -> Result<String, String> {
    let step = if delta >= 0 { "+5%" } else { "-5%" };
    match pactl(&["set-sink-volume", "@DEFAULT_SINK@", step]) {
        Ok(_) => {
            let now = pactl(&["get-sink-volume", "@DEFAULT_SINK@"]).unwrap_or_default();
            info!("ATONGX volume {step} via pactl {now}");
            Ok(if delta >= 0 {
                "volume:+".into()
            } else {
                "volume:-".into()
            })
        }
        Err(err) => {
            info!("ATONGX volume {step} (no pactl: {err})");
            Ok(if delta >= 0 {
                "volume:+".into()
            } else {
                "volume:-".into()
            })
        }
    }
}

pub fn pactl_mute() -> Result<String, String> {
    match pactl(&["set-sink-mute", "@DEFAULT_SINK@", "toggle"]) {
        Ok(_) => {
            info!("ATONGX mute toggled via pactl");
            Ok("mute".into())
        }
        Err(err) => {
            info!("ATONGX mute (no pactl: {err})");
            Ok("mute".into())
        }
    }
}

fn short_label(action: &str) -> &'static str {
    match action {
        "volume+" => "Volume +",
        "volume-" => "Volume −",
        "mute" => "Mudo",
        _ => "TV",
    }
}

fn emit_control(app: Option<&AppHandle>, event: &TvControlEvent) {
    if let Some(app) = app {
        let _ = app.emit(EVENT_TV_CONTROL, event);
    }
}

pub async fn dispatch_with<F, Fut, P>(
    app: Option<&AppHandle>,
    action: &str,
    try_samsung: F,
    pactl_fn: P,
    cfg: &SamsungConfig,
) -> Result<String, String>
where
    F: FnOnce() -> Fut,
    Fut: std::future::Future<Output = Result<Option<String>, TvError>>,
    P: FnOnce() -> Result<String, String>,
{
    let mut fallback_reason: Option<String> = None;
    if should_try_samsung(cfg, Instant::now()) {
        match try_samsung().await {
            Ok(token) => {
                mark_ok();
                if let Some(token) = token.as_deref().filter(|t| !t.is_empty()) {
                    if let Err(err) = persist_token(token) {
                        warn!("Samsung token persist failed: {err:#}");
                    }
                }
                info!("ATONGX {action} via samsung");
                let event = TvControlEvent {
                    action: action.to_string(),
                    via: ControlVia::Samsung,
                    message: short_label(action).into(),
                    first_fallback: false,
                };
                emit_control(app, &event);
                return Ok(format!("{action}:samsung"));
            }
            Err(err) => {
                mark_fail();
                fallback_reason = Some(err.user_message(cfg));
                warn!("ATONGX {action} Samsung failed ({err}); falling back to pactl");
            }
        }
    } else if cfg.enabled() {
        log::debug!("ATONGX {action} skipping Samsung (cooldown after last failure)");
    }

    let first_fallback = fallback_reason
        .as_ref()
        .map(|_| take_first_fallback())
        .unwrap_or(false);
    let _ = pactl_fn();
    let message = if first_fallback {
        fallback_reason.unwrap_or_else(|| short_label(action).into())
    } else {
        short_label(action).into()
    };
    if first_fallback {
        warn!("ATONGX {action} using pactl fallback: {message}");
    } else {
        info!("ATONGX {action} via pactl");
    }
    let event = TvControlEvent {
        action: action.to_string(),
        via: ControlVia::Pactl,
        message,
        first_fallback,
    };
    emit_control(app, &event);
    Ok(format!("{action}:pactl"))
}

pub async fn apply_volume(app: Option<&AppHandle>, delta: i32) -> Result<String, String> {
    let action = if delta >= 0 { "volume+" } else { "volume-" };
    let key = if delta >= 0 {
        TvKey::VolumeUp
    } else {
        TvKey::VolumeDown
    };
    let cfg = load_config();
    dispatch_with(
        app,
        action,
        || send_key(&cfg, key),
        || pactl_volume(delta),
        &cfg,
    )
    .await
}

pub async fn apply_mute(app: Option<&AppHandle>) -> Result<String, String> {
    let cfg = load_config();
    dispatch_with(
        app,
        "mute",
        || send_key(&cfg, TvKey::Mute),
        pactl_mute,
        &cfg,
    )
    .await
}

#[tauri::command]
pub async fn remote_volume(app: AppHandle, delta: i32) -> Result<String, String> {
    apply_volume(Some(&app), delta).await
}

#[tauri::command]
pub async fn remote_mute(app: AppHandle) -> Result<String, String> {
    apply_mute(Some(&app)).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::samsung::TvError;
    use std::sync::atomic::AtomicU32;

    fn test_cfg() -> SamsungConfig {
        SamsungConfig {
            host: "127.0.0.1".into(),
            port: 8001,
            secure_port: 8002,
            token: None,
            name: "Zappe".into(),
            disabled: false,
        }
    }

    #[test]
    fn disabled_or_cooldown_skips_samsung() {
        reset_runtime_state();
        let mut cfg = test_cfg();
        cfg.disabled = true;
        assert!(!should_try_samsung(&cfg, Instant::now()));
        cfg.disabled = false;
        assert!(should_try_samsung(&cfg, Instant::now()));
        mark_fail();
        assert!(!should_try_samsung(&cfg, Instant::now()));
        reset_runtime_state();
        assert!(should_try_samsung(&cfg, Instant::now()));
    }

    #[tokio::test]
    async fn samsung_success_skips_pactl() {
        reset_runtime_state();
        let hits = AtomicU32::new(0);
        let out = dispatch_with(
            None,
            "volume+",
            || async { Ok(None) },
            || {
                hits.fetch_add(1, Ordering::Relaxed);
                Ok("pactl".into())
            },
            &test_cfg(),
        )
        .await
        .unwrap();
        assert_eq!(out, "volume+:samsung");
        assert_eq!(hits.load(Ordering::Relaxed), 0);
    }

    #[tokio::test]
    async fn samsung_error_falls_back_and_cools_down() {
        reset_runtime_state();
        let pactl_hits = AtomicU32::new(0);
        let out = dispatch_with(
            None,
            "mute",
            || async { Err(TvError::Connect("refused".into())) },
            || {
                pactl_hits.fetch_add(1, Ordering::Relaxed);
                Ok("mute".into())
            },
            &test_cfg(),
        )
        .await
        .unwrap();
        assert_eq!(out, "mute:pactl");
        assert_eq!(pactl_hits.load(Ordering::Relaxed), 1);

        let samsung_hits = AtomicU32::new(0);
        let out = dispatch_with(
            None,
            "mute",
            || {
                samsung_hits.fetch_add(1, Ordering::Relaxed);
                async { Err(TvError::Connect("refused".into())) }
            },
            || {
                pactl_hits.fetch_add(1, Ordering::Relaxed);
                Ok("mute".into())
            },
            &test_cfg(),
        )
        .await
        .unwrap();
        assert_eq!(out, "mute:pactl");
        assert_eq!(
            samsung_hits.load(Ordering::Relaxed),
            0,
            "cooldown must skip a second Samsung connect"
        );
        assert_eq!(pactl_hits.load(Ordering::Relaxed), 2);
        reset_runtime_state();
    }

    #[tokio::test]
    async fn pairing_error_uses_device_connect_copy() {
        reset_runtime_state();
        let out = dispatch_with(
            None,
            "volume-",
            || async { Err(TvError::Unauthorized) },
            || Ok("ok".into()),
            &SamsungConfig::appliance_default(),
        )
        .await
        .unwrap();
        assert_eq!(out, "volume-:pactl");
        reset_runtime_state();
    }
}
