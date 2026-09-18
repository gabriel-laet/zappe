//! Harvest orchestration: start nest → run skill → rows in local store.
//!
//! Failures log clearly and never panic the process. Missing anchors mark the
//! skill stale and surface teach-mode instead of guessing a new path.

use std::path::PathBuf;

use anyhow::Result;
use serde::Serialize;

use crate::a11y::{self, A11yNode};
use crate::catalog::{now_secs, CatalogShelf, CatalogStore, ShelfStatus};
use crate::nest::{NestManager, NestMode};
use crate::playback::PlaybackSurface;
use crate::skill::{self, ExtractError, Skill};
use crate::teach::TeachMode;

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

/// Home must not harvest on mount unless this is set (debug / old behavior).
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

    if !req.skip_nest && playback != PlaybackSurface::Chrome {
        if let Err(err) = nest.open_url(&skill.open.url, NestMode::Harvest).await {
            log::warn!("nest start for harvest failed: {err:#}");
        }
        // Hide immediately so the HDMI kiosk stays on the guide during the wait.
        keep_guide_after_harvest(nest).await;
        let wait = std::time::Duration::from_millis(skill.open.wait_ms);
        tokio::time::sleep(wait).await;
    } else if playback == PlaybackSurface::Chrome {
        log::info!("harvest: nest already playing; dumping a11y without navigation");
    }

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
    use super::parse_auto_harvest;
    use crate::a11y::load_dump;
    use crate::skill::{extract_rows, load_skill, NETFLIX_CONTINUE_WATCHING_V1};

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
        assert!(!parse_auto_harvest(Some("0")));
        assert!(!parse_auto_harvest(Some("false")));
        assert!(parse_auto_harvest(Some("1")));
        assert!(parse_auto_harvest(Some("true")));
        assert!(parse_auto_harvest(Some("YES")));
    }

    #[test]
    fn a11y_disabled_is_error_not_teach() {
        use crate::a11y::A11Y_DISABLED_MSG;
        use crate::catalog::ShelfStatus;
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
