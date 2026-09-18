//! Local catalog store the Tauri guide reads. No network, no Trakt.

use std::fs;
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

use crate::paths::{catalog_path, ensure_data_dir};

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ShelfStatus {
    #[default]
    Empty,
    Ok,
    Stale,
    Error,
    Teach,
    Harvesting,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogRow {
    pub title: String,
    pub service: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub href: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub artwork: Option<String>,
    pub skill_id: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogShelf {
    pub id: String,
    pub skill_id: String,
    pub title: String,
    pub status: ShelfStatus,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(default)]
    pub rows: Vec<CatalogRow>,
    #[serde(default)]
    pub updated_at: Option<u64>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogFile {
    #[serde(default)]
    pub updated_at: Option<u64>,
    #[serde(default)]
    pub shelves: Vec<CatalogShelf>,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct TeachView {
    pub active: bool,
    pub skill_id: Option<String>,
    pub message: Option<String>,
}

impl Default for TeachView {
    fn default() -> Self {
        Self {
            active: false,
            skill_id: None,
            message: None,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CatalogView {
    pub shelves: Vec<CatalogShelf>,
    pub teach: TeachView,
}

pub struct CatalogStore {
    inner: Mutex<CatalogFile>,
}

impl CatalogStore {
    pub fn load() -> Self {
        let file = read_catalog().unwrap_or_default();
        Self {
            inner: Mutex::new(file),
        }
    }

    pub fn view(&self, teach: TeachView) -> CatalogView {
        let file = self.inner.lock().unwrap().clone();
        CatalogView {
            shelves: file.shelves,
            teach,
        }
    }

    pub fn upsert_shelf(&self, shelf: CatalogShelf) -> Result<()> {
        let mut file = self.inner.lock().unwrap();
        if let Some(existing) = file.shelves.iter_mut().find(|s| s.id == shelf.id) {
            *existing = shelf;
        } else {
            file.shelves.push(shelf);
        }
        file.updated_at = Some(now_secs());
        persist(&file)
    }

    pub fn mark_harvesting(&self, id: &str, skill_id: &str, title: &str) -> Result<()> {
        let rows = {
            let file = self.inner.lock().unwrap();
            file.shelves
                .iter()
                .find(|s| s.id == id)
                .map(|s| s.rows.clone())
                .unwrap_or_default()
        };
        self.upsert_shelf(CatalogShelf {
            id: id.to_string(),
            skill_id: skill_id.to_string(),
            title: title.to_string(),
            status: ShelfStatus::Harvesting,
            message: Some("Syncing Netflix…".into()),
            rows,
            updated_at: Some(now_secs()),
        })
    }
}

fn persist(file: &CatalogFile) -> Result<()> {
    ensure_data_dir()?;
    let path = catalog_path();
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let json = serde_json::to_string_pretty(file)?;
    fs::write(&path, json).with_context(|| format!("write {}", path.display()))?;
    Ok(())
}

fn read_catalog() -> Result<CatalogFile> {
    let path = catalog_path();
    if !path.exists() {
        return Ok(CatalogFile::default());
    }
    let bytes = fs::read(&path).with_context(|| format!("read {}", path.display()))?;
    Ok(serde_json::from_slice(&bytes).unwrap_or_default())
}

pub fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn upsert_roundtrip() {
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        let dir = std::env::temp_dir().join(format!("zappe-catalog-{}", std::process::id()));
        let _ = std::fs::create_dir_all(&dir);
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        let store = CatalogStore::load();
        store
            .upsert_shelf(CatalogShelf {
                id: "continue".into(),
                skill_id: "netflix.continue_watching.v1".into(),
                title: "Continue watching".into(),
                status: ShelfStatus::Ok,
                message: None,
                rows: vec![CatalogRow {
                    title: "The Bear".into(),
                    service: "netflix".into(),
                    href: None,
                    artwork: None,
                    skill_id: "netflix.continue_watching.v1".into(),
                }],
                updated_at: Some(1),
            })
            .unwrap();
        let view = store.view(TeachView::default());
        assert_eq!(view.shelves[0].rows[0].title, "The Bear");
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }
}
