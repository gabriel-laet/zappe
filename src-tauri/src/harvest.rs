//! Harvest orchestration: start nest → run skill → rows in local store.
//!
//! Failures log clearly and never panic the process. Missing anchors mark the
//! skill stale and surface teach-mode instead of guessing a new path.
//!
//! Auto-harvest is a backend scheduler with a kill switch, single-flight lock,
//! and exponential backoff. A broken AT-SPI bus must never respawn Chrome
//! every second and melt the living-room box.

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use anyhow::Result;
use serde::Serialize;

use crate::a11y::{self, A11yNode};
use crate::catalog::{now_secs, CatalogShelf, CatalogStore, ShelfStatus};
use crate::nest::{NestManager, NestMode};
use crate::playback::PlaybackSurface;
use crate::skill::{self, ExtractError, Skill};
use crate::teach::TeachMode;

/// After this many consecutive storm failures, auto-harvest stays off for
/// the rest of the process. Manual Sync on the guide still works.
pub const AUTO_HARVEST_MAX_FAILURES: u32 = 3;
/// Wait after a successful (or non-storm) auto tick before the next one.
pub const AUTO_HARVEST_SUCCESS_SECS: u64 = 3600;
/// 30s → 2m → 10m → 1h. Never a one-second retry.
pub const AUTO_HARVEST_BACKOFF_SECS: &[u64] = &[30, 120, 600, 3600];
/// How often to re-read `ZAPPE_AUTO_HARVEST` when the scheduler is idle.
pub const AUTO_HARVEST_DISABLED_POLL_SECS: u64 = 60;

#[derive(Clone, Debug, Serialize)]
pub struct HarvestOutcome {
    pub skill_id: String,
    pub status: ShelfStatus,
    pub message: String,
    pub rows: usize,
    pub teach: bool,
}

#[derive(Clone, Debug, Default)]
pub struct HarvestRequest {
    pub skill_id: String,
    pub fixture: Option<PathBuf>,
    pub dump: Option<PathBuf>,
    pub skip_nest: bool,
}

/// Auto-harvest is **off** unless `ZAPPE_AUTO_HARVEST` is an explicit truthy
/// value (`1` / `true` / `yes` / `on`). Empty, `0`, `false`, `off`, and unset
/// all skip the scheduler — including when systemd sets
/// `Environment=ZAPPE_AUTO_HARVEST=0`.
pub fn auto_harvest_enabled() -> bool {
    parse_auto_harvest(std::env::var("ZAPPE_AUTO_HARVEST").ok().as_deref())
}

fn parse_auto_harvest(raw: Option<&str>) -> bool {
    match raw {
        Some(v) => matches!(
            v.trim().to_ascii_lowercase().as_str(),
            "1" | "true" | "yes" | "on"
        ),
        None => false,
    }
}

/// Single-flight lock: `harvest_now`, Connect, voice, and the auto scheduler
/// share this so two callers cannot stack Chrome nests.
#[derive(Clone, Default)]
pub struct HarvestGate {
    in_flight: Arc<AtomicBool>,
}

pub struct HarvestLease {
    in_flight: Arc<AtomicBool>,
}

impl Drop for HarvestLease {
    fn drop(&mut self) {
        self.in_flight.store(false, Ordering::SeqCst);
    }
}

impl HarvestGate {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn try_begin(&self) -> Option<HarvestLease> {
        self.in_flight
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
            .ok()
            .map(|_| HarvestLease {
                in_flight: self.in_flight.clone(),
            })
    }

    pub fn in_flight(&self) -> bool {
        self.in_flight.load(Ordering::SeqCst)
    }
}

pub fn already_running_outcome(skill_id: impl Into<String>) -> HarvestOutcome {
    HarvestOutcome {
        skill_id: skill_id.into(),
        status: ShelfStatus::Harvesting,
        message: "harvest already running; not starting another Chrome nest".into(),
        rows: 0,
        teach: false,
    }
}

/// Pure auto-harvest backoff / kill-switch. One instance per process loop.
#[derive(Clone, Debug, Default)]
pub struct AutoHarvestMachine {
    consecutive_storm_failures: u32,
    disabled: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum AutoHarvestNext {
    Wait(Duration),
    StopLifetime,
}

impl AutoHarvestMachine {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_disabled(&self) -> bool {
        self.disabled
    }

    pub fn consecutive_storm_failures(&self) -> u32 {
        self.consecutive_storm_failures
    }

    pub fn after_outcome(&mut self, outcome: &HarvestOutcome) -> AutoHarvestNext {
        self.after_status(&outcome.status, &outcome.message)
    }

    pub fn after_status(&mut self, status: &ShelfStatus, message: &str) -> AutoHarvestNext {
        if self.disabled {
            return AutoHarvestNext::StopLifetime;
        }
        if matches!(status, ShelfStatus::Ok) {
            self.consecutive_storm_failures = 0;
            return AutoHarvestNext::Wait(Duration::from_secs(AUTO_HARVEST_SUCCESS_SECS));
        }
        if is_storm_failure_message(message) {
            self.consecutive_storm_failures = self.consecutive_storm_failures.saturating_add(1);
            if self.consecutive_storm_failures >= AUTO_HARVEST_MAX_FAILURES {
                self.disabled = true;
                return AutoHarvestNext::StopLifetime;
            }
            return AutoHarvestNext::Wait(backoff_after_failures(self.consecutive_storm_failures));
        }
        // Empty shelf / teach / extract miss: do not treat as a nest storm.
        self.consecutive_storm_failures = 0;
        AutoHarvestNext::Wait(Duration::from_secs(AUTO_HARVEST_SUCCESS_SECS))
    }
}

/// Delay before the next auto tick after `n` consecutive storm failures.
/// `n == 0` and `n == 1` both yield 30s so a bug cannot schedule 0s/1s.
pub fn backoff_after_failures(n: u32) -> Duration {
    let idx = n.saturating_sub(1) as usize;
    let secs = AUTO_HARVEST_BACKOFF_SECS[idx.min(AUTO_HARVEST_BACKOFF_SECS.len() - 1)];
    Duration::from_secs(secs.max(30))
}

/// AT-SPI timeout, missing Chrome on the bus, nest spawn/kill — these are
/// the failures that used to tight-loop and pin the appliance at load ~13.
pub fn is_storm_failure_message(message: &str) -> bool {
    let m = message.to_ascii_lowercase();
    m.contains("at-spi")
        || m.contains("timed out")
        || m.contains("no chrome")
        || m.contains("nest start")
        || m.contains("nest open")
        || m.contains("accessibility")
}

impl HarvestRequest {
    pub fn for_skill(id: impl Into<String>) -> Self {
        Self {
            skill_id: id.into(),
            fixture: std::env::var_os("ZAPPE_HARVEST_FIXTURE").map(PathBuf::from),
            dump: std::env::var_os("ZAPPE_A11Y_DUMP").map(PathBuf::from),
            skip_nest: std::env::var("ZAPPE_HARVEST_NO_NEST").ok().as_deref() == Some("1"),
        }
    }
}

pub async fn run_harvest(
    nest: &NestManager,
    catalog: &CatalogStore,
    teach: &TeachMode,
    playback: PlaybackSurface,
    req: HarvestRequest,
) -> HarvestOutcome {
    if let Err(err) = skill::install_bundled_skills() {
        log::warn!("could not plant bundled skills in data dir: {err:#}");
    }

    let skill = match skill::load_skill(&req.skill_id) {
        Ok(s) => s,
        Err(err) => {
            let message = format!(
                "Netflix skill not found ({err}). Expected bundled netflix.continue_watching.v1 or a YAML under ~/.local/share/zappe/skills."
            );
            log::error!("{message}");
            write_error_shelf(catalog, &req.skill_id, "Continue watching", &message);
            return fail_outcome(&req.skill_id, ShelfStatus::Error, message, false);
        }
    };

    if let Err(err) = catalog.mark_harvesting(&skill.shelf_id, &skill.id, &skill.shelf_title) {
        log::warn!("catalog harvesting mark failed: {err:#}");
    }

    let outcome = match harvest_skill(nest, playback, &skill, &req).await {
        Ok(rows) if rows.is_empty() => {
            let message =
                String::from("harvest returned no titles (signed-in Continue Watching empty?)");
            log::warn!("{}: {message}", skill.id);
            write_shelf(
                catalog,
                &skill,
                ShelfStatus::Empty,
                Some(message.clone()),
                Vec::new(),
            );
            HarvestOutcome {
                skill_id: skill.id.clone(),
                status: ShelfStatus::Empty,
                message,
                rows: 0,
                teach: false,
            }
        }
        Ok(rows) => {
            let n = rows.len();
            write_shelf(catalog, &skill, ShelfStatus::Ok, None, rows);
            if teach.view().skill_id.as_deref() == Some(skill.id.as_str()) {
                teach.cancel();
            }
            HarvestOutcome {
                skill_id: skill.id.clone(),
                status: ShelfStatus::Ok,
                message: format!("harvested {n} titles"),
                rows: n,
                teach: false,
            }
        }
        Err(err) => {
            let (status, teach_now) = classify_fail(&skill, &err);
            let message = err.to_string();
            log::warn!("{} failed: {message}", skill.id);
            write_shelf(
                catalog,
                &skill,
                status.clone(),
                Some(message.clone()),
                Vec::new(),
            );
            if teach_now {
                teach.begin(&skill.id, &message);
            }
            HarvestOutcome {
                skill_id: skill.id.clone(),
                status,
                message,
                rows: 0,
                teach: teach_now,
            }
        }
    };
    outcome
}

async fn harvest_skill(
    nest: &NestManager,
    playback: PlaybackSurface,
    skill: &Skill,
    req: &HarvestRequest,
) -> Result<Vec<crate::catalog::CatalogRow>> {
    if let Some(fixture) = &req.fixture {
        let tree = a11y::load_dump(fixture)?;
        maybe_write_dump(req, &tree);
        return map_extract(skill, &tree);
    }

    // Enable AT-SPI before launching Chrome so the nest registers on the bus.
    a11y::ensure_a11y_enabled().await?;

    // Hold the background nest slot for the whole dump so login cannot spawn
    // a second Chrome, and so a failed hide/open cannot immediately respawn.
    let _bg = if !req.skip_nest && playback != PlaybackSurface::Chrome {
        let lease = nest.try_begin_background().ok_or_else(|| {
            anyhow::anyhow!("harvest/login nest already running; not spawning another Chrome")
        })?;
        nest.open_url(&skill.open.url, NestMode::Harvest)
            .await
            .map_err(|err| anyhow::anyhow!("nest start for harvest failed: {err:#}"))?;
        // Hide immediately so the HDMI kiosk stays on the guide during the wait.
        keep_guide_after_harvest(nest).await;
        let wait = std::time::Duration::from_millis(skill.open.wait_ms);
        tokio::time::sleep(wait).await;
        Some(lease)
    } else if playback == PlaybackSurface::Chrome {
        log::info!("harvest: nest already playing; dumping a11y without navigation");
        None
    } else {
        None
    };

    let mut last_err = None;
    let attempts = skill.open.retries.saturating_add(1).max(1);
    for attempt in 0..attempts {
        match a11y::dump_chrome_tree().await {
            Ok(tree) => {
                maybe_write_dump(req, &tree);
                match map_extract(skill, &tree) {
                    Ok(rows) => {
                        if playback != PlaybackSurface::Chrome {
                            keep_guide_after_harvest(nest).await;
                        }
                        return Ok(rows);
                    }
                    Err(err) => last_err = Some(err),
                }
            }
            Err(err) => last_err = Some(err),
        }
        if attempt + 1 < attempts {
            tokio::time::sleep(std::time::Duration::from_millis(1500)).await;
        }
    }

    if playback != PlaybackSurface::Chrome {
        keep_guide_after_harvest(nest).await;
    }
    Err(last_err.unwrap_or_else(|| anyhow_none()))
}

async fn keep_guide_after_harvest(nest: &NestManager) {
    nest.hide().await;
    crate::wm::nudge_guide_fullscreen(None);
}

fn anyhow_none() -> anyhow::Error {
    anyhow::anyhow!("harvest failed without a detailed error")
}

fn map_extract(skill: &Skill, tree: &A11yNode) -> Result<Vec<crate::catalog::CatalogRow>> {
    match skill::extract_rows(tree, skill) {
        Ok(rows) => Ok(rows),
        Err(err) => Err(anyhow::Error::new(err)),
    }
}

fn maybe_write_dump(req: &HarvestRequest, tree: &A11yNode) {
    if let Some(path) = &req.dump {
        if let Err(err) = a11y::write_dump(path, tree) {
            log::warn!("write a11y dump {}: {err:#}", path.display());
        }
    }
}

fn classify_fail(skill: &Skill, err: &anyhow::Error) -> (ShelfStatus, bool) {
    if err.downcast_ref::<ExtractError>().is_some() {
        let status = if skill.on_fail.mark_stale {
            if skill.on_fail.teach {
                ShelfStatus::Teach
            } else {
                ShelfStatus::Stale
            }
        } else {
            ShelfStatus::Error
        };
        return (status, skill.on_fail.teach);
    }
    (ShelfStatus::Error, false)
}

fn write_error_shelf(catalog: &CatalogStore, skill_id: &str, title: &str, message: &str) {
    let shelf = CatalogShelf {
        id: "continue".into(),
        skill_id: skill_id.to_string(),
        title: title.to_string(),
        status: ShelfStatus::Error,
        message: Some(message.to_string()),
        rows: Vec::new(),
        updated_at: Some(now_secs()),
    };
    if let Err(err) = catalog.upsert_shelf(shelf) {
        log::warn!("catalog write failed: {err:#}");
    }
}

fn write_shelf(
    catalog: &CatalogStore,
    skill: &Skill,
    status: ShelfStatus,
    message: Option<String>,
    rows: Vec<crate::catalog::CatalogRow>,
) {
    let shelf = CatalogShelf {
        id: skill.shelf_id.clone(),
        skill_id: skill.id.clone(),
        title: skill.shelf_title.clone(),
        status,
        message,
        rows,
        updated_at: Some(now_secs()),
    };
    if let Err(err) = catalog.upsert_shelf(shelf) {
        log::warn!("catalog write failed: {err:#}");
    }
}

fn fail_outcome(
    skill_id: &str,
    status: ShelfStatus,
    message: String,
    teach: bool,
) -> HarvestOutcome {
    HarvestOutcome {
        skill_id: skill_id.to_string(),
        status,
        message,
        rows: 0,
        teach,
    }
}

pub async fn run_cli(req: HarvestRequest) -> Result<HarvestOutcome> {
    let nest = NestManager::start();
    let catalog = CatalogStore::load();
    let teach = TeachMode::new();
    Ok(run_harvest(&nest, &catalog, &teach, PlaybackSurface::Idle, req).await)
}

#[cfg(test)]
mod tests {
    use super::{
        backoff_after_failures, is_storm_failure_message, parse_auto_harvest, AutoHarvestMachine,
        AutoHarvestNext, HarvestGate, AUTO_HARVEST_BACKOFF_SECS, AUTO_HARVEST_MAX_FAILURES,
        AUTO_HARVEST_SUCCESS_SECS,
    };
    use crate::a11y::load_dump;
    use crate::catalog::ShelfStatus;
    use crate::skill::{extract_rows, load_skill, NETFLIX_CONTINUE_WATCHING_V1};
    use std::time::Duration;

    #[test]
    fn fixture_extracts_continue_watching() {
        let skill = load_skill(NETFLIX_CONTINUE_WATCHING_V1).unwrap();
        let tree = load_dump(
            &std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../fixtures/a11y/netflix.continue_watching.sample.json"),
        )
        .expect("sample fixture");
        let rows = extract_rows(&tree, &skill).unwrap();
        let titles: Vec<_> = rows.iter().map(|r| r.title.as_str()).collect();
        assert!(titles.contains(&"The Bear"));
        assert!(titles.contains(&"Squid Game"));
        assert!(!titles.contains(&"Play"));
        assert!(!titles.contains(&"More Info"));
        assert!(!titles.contains(&"Trending Now"));
    }

    #[test]
    fn auto_harvest_is_off_unless_explicitly_enabled() {
        assert!(!parse_auto_harvest(None));
        assert!(!parse_auto_harvest(Some("")));
        assert!(!parse_auto_harvest(Some("   ")));
        assert!(!parse_auto_harvest(Some("0")));
        assert!(!parse_auto_harvest(Some("false")));
        assert!(!parse_auto_harvest(Some("off")));
        assert!(!parse_auto_harvest(Some("OFF")));
        assert!(!parse_auto_harvest(Some("no")));
        assert!(parse_auto_harvest(Some("1")));
        assert!(parse_auto_harvest(Some("true")));
        assert!(parse_auto_harvest(Some("YES")));
        assert!(parse_auto_harvest(Some(" on ")));
    }

    #[test]
    fn backoff_is_exponential_and_never_sub_second() {
        assert_eq!(AUTO_HARVEST_BACKOFF_SECS, &[30, 120, 600, 3600]);
        assert_eq!(backoff_after_failures(0), Duration::from_secs(30));
        assert_eq!(backoff_after_failures(1), Duration::from_secs(30));
        assert_eq!(backoff_after_failures(2), Duration::from_secs(120));
        assert_eq!(backoff_after_failures(3), Duration::from_secs(600));
        assert_eq!(backoff_after_failures(4), Duration::from_secs(3600));
        assert_eq!(backoff_after_failures(99), Duration::from_secs(3600));
        for n in 0..8 {
            assert!(
                backoff_after_failures(n).as_secs() >= 30,
                "backoff({n}) must never be a tight retry"
            );
        }
    }

    #[test]
    fn storm_classifier_matches_appliance_incident() {
        assert!(is_storm_failure_message(
            "AT-SPI dump timed out after 12s — is Chrome accessibility enabled?"
        ));
        assert!(is_storm_failure_message(
            "no Chrome/Chromium application on the AT-SPI bus (is the nest running?)"
        ));
        assert!(is_storm_failure_message(
            "nest start for harvest failed: killed"
        ));
        assert!(!is_storm_failure_message(
            "harvest returned no titles (signed-in Continue Watching empty?)"
        ));
        assert!(!is_storm_failure_message("harvested 4 titles"));
    }

    #[test]
    fn auto_harvest_machine_backs_off_then_disables() {
        let mut machine = AutoHarvestMachine::new();
        assert_eq!(AUTO_HARVEST_MAX_FAILURES, 3);
        let timeout = "AT-SPI dump timed out after 12s — is Chrome accessibility enabled?";
        assert_eq!(
            machine.after_status(&ShelfStatus::Error, timeout),
            AutoHarvestNext::Wait(Duration::from_secs(30))
        );
        assert_eq!(machine.consecutive_storm_failures(), 1);
        assert_eq!(
            machine.after_status(
                &ShelfStatus::Error,
                "no Chrome/Chromium application on the AT-SPI bus"
            ),
            AutoHarvestNext::Wait(Duration::from_secs(120))
        );
        assert_eq!(
            machine.after_status(&ShelfStatus::Error, "nest start for harvest failed"),
            AutoHarvestNext::StopLifetime
        );
        assert!(machine.is_disabled());
        assert_eq!(
            machine.after_status(&ShelfStatus::Ok, "harvested 2 titles"),
            AutoHarvestNext::StopLifetime
        );
    }

    #[test]
    fn auto_harvest_success_resets_storm_streak() {
        let mut machine = AutoHarvestMachine::new();
        let _ = machine.after_status(&ShelfStatus::Error, "AT-SPI dump timed out");
        assert_eq!(
            machine.after_status(&ShelfStatus::Ok, "harvested 3 titles"),
            AutoHarvestNext::Wait(Duration::from_secs(AUTO_HARVEST_SUCCESS_SECS))
        );
        assert_eq!(machine.consecutive_storm_failures(), 0);
        assert!(!machine.is_disabled());
        assert_eq!(
            machine.after_status(&ShelfStatus::Error, "AT-SPI dump timed out"),
            AutoHarvestNext::Wait(Duration::from_secs(30))
        );
    }

    #[test]
    fn empty_or_teach_is_not_a_storm() {
        let mut machine = AutoHarvestMachine::new();
        assert_eq!(
            machine.after_status(
                &ShelfStatus::Empty,
                "harvest returned no titles (signed-in Continue Watching empty?)"
            ),
            AutoHarvestNext::Wait(Duration::from_secs(AUTO_HARVEST_SUCCESS_SECS))
        );
        assert_eq!(machine.consecutive_storm_failures(), 0);
        assert!(!machine.is_disabled());
    }

    #[test]
    fn harvest_gate_is_single_flight() {
        let gate = HarvestGate::new();
        let first = gate.try_begin().expect("first harvest");
        assert!(gate.in_flight());
        assert!(
            gate.try_begin().is_none(),
            "second harvest must not start while the first holds the gate"
        );
        drop(first);
        assert!(gate.try_begin().is_some());
    }

    #[test]
    fn a11y_disabled_is_error_not_teach() {
        use crate::a11y::A11Y_DISABLED_MSG;
        let skill = load_skill(NETFLIX_CONTINUE_WATCHING_V1).unwrap();
        let err = anyhow::anyhow!(A11Y_DISABLED_MSG);
        let (status, teach) = super::classify_fail(&skill, &err);
        assert_eq!(status, ShelfStatus::Error);
        assert!(!teach);
    }

    #[tokio::test]
    async fn fixture_harvest_writes_catalog_json() {
        use crate::catalog::ShelfStatus;
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        let dir = std::env::temp_dir().join(format!(
            "zappe-harvest-cli-{}-{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_nanos())
                .unwrap_or(0)
        ));
        let _ = std::fs::create_dir_all(&dir);
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        let mut req = super::HarvestRequest::for_skill(NETFLIX_CONTINUE_WATCHING_V1);
        req.fixture = Some(
            std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
                .join("../fixtures/a11y/netflix.continue_watching.sample.json"),
        );
        req.skip_nest = true;
        let out = super::run_cli(req).await.expect("fixture harvest");
        assert_eq!(out.status, ShelfStatus::Ok);
        assert!(out.rows >= 2, "rows={}", out.rows);
        let json = std::fs::read_to_string(dir.join("catalog.json")).expect("catalog.json");
        assert!(json.contains("The Bear"), "{json}");
        assert!(
            json.contains("\"status\": \"ok\"") || json.contains("\"status\":\"ok\""),
            "{json}"
        );
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }
}
