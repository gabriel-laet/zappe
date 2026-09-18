//! Versioned harvest skills (JSON / YAML).
//!
//! Skills describe a path: open URL → wait → a11y anchors → extract rows.
//! On missing anchors we mark the skill stale and offer teach-mode — we do
//! not improvise a new path.

use std::path::{Path, PathBuf};

use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};

use crate::a11y::A11yNode;
use crate::catalog::CatalogRow;
use crate::paths::{skill_search_dirs, skills_override_dir};

pub const NETFLIX_CONTINUE_WATCHING_V1: &str = "netflix.continue_watching.v1";

const BUNDLED_NETFLIX_CW: &str = include_str!("../../skills/netflix.continue_watching.v1.yaml");

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum SkillKind {
    Harvest,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SkillOpen {
    pub url: String,
    #[serde(default = "default_wait_ms")]
    pub wait_ms: u64,
    #[serde(default)]
    pub retries: u32,
}

fn default_wait_ms() -> u64 {
    8000
}

#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum MatchMode {
    #[default]
    Contains,
    Equals,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SkillAnchor {
    #[serde(default)]
    pub id: Option<String>,
    #[serde(default)]
    pub names: Vec<String>,
    #[serde(default)]
    pub roles: Vec<String>,
    #[serde(default)]
    pub match_mode: MatchMode,
}

#[derive(Clone, Copy, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum ExtractFrom {
    #[default]
    Section,
    FollowingSiblings,
    Descendants,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SkillExtract {
    #[serde(default)]
    pub from: ExtractFrom,
    #[serde(default)]
    pub item_roles: Vec<String>,
    #[serde(default = "default_max_items")]
    pub max_items: usize,
    #[serde(default)]
    pub skip_names: Vec<String>,
}

fn default_max_items() -> usize {
    24
}

impl Default for SkillExtract {
    fn default() -> Self {
        Self {
            from: ExtractFrom::Section,
            item_roles: Vec::new(),
            max_items: default_max_items(),
            skip_names: Vec::new(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct SkillOnFail {
    #[serde(default = "default_true")]
    pub mark_stale: bool,
    #[serde(default = "default_true")]
    pub teach: bool,
}

fn default_true() -> bool {
    true
}

impl Default for SkillOnFail {
    fn default() -> Self {
        Self {
            mark_stale: true,
            teach: true,
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct Skill {
    pub id: String,
    pub service: String,
    pub kind: SkillKind,
    pub version: u32,
    #[serde(default)]
    pub description: Option<String>,
    #[serde(default = "default_shelf_id")]
    pub shelf_id: String,
    #[serde(default = "default_shelf_title")]
    pub shelf_title: String,
    pub open: SkillOpen,
    #[serde(default)]
    pub anchors: Vec<SkillAnchor>,
    #[serde(default)]
    pub extract: SkillExtract,
    #[serde(default)]
    pub on_fail: SkillOnFail,
}

fn default_shelf_id() -> String {
    "continue".into()
}

fn default_shelf_title() -> String {
    "Continue watching".into()
}

#[derive(Clone, Debug)]
pub enum ExtractError {
    AnchorMissing { tried: Vec<String> },
    NoRows { anchor: String },
}

impl std::fmt::Display for ExtractError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::AnchorMissing { tried } => {
                write!(f, "skill anchors not found (stale): {}", tried.join(" | "))
            }
            Self::NoRows { anchor } => {
                write!(f, "anchor '{anchor}' found but no title rows extracted")
            }
        }
    }
}

impl std::error::Error for ExtractError {}

pub fn parse_skill(text: &str) -> Result<Skill> {
    let trimmed = text.trim_start();
    if trimmed.starts_with('{') {
        serde_json::from_str(trimmed).context("parse skill JSON")
    } else {
        serde_yaml::from_str(text).context("parse skill YAML")
    }
}

pub fn load_skill_file(path: &Path) -> Result<Skill> {
    let text =
        std::fs::read_to_string(path).with_context(|| format!("read skill {}", path.display()))?;
    parse_skill(&text)
}

pub fn bundled_skills() -> Result<Vec<Skill>> {
    Ok(vec![parse_skill(BUNDLED_NETFLIX_CW)?])
}

/// Write the embedded Netflix skill into `~/.local/share/zappe/skills`
/// so the appliance data dir always has a file the CLI / updater can see.
pub fn install_bundled_skills() -> Result<()> {
    crate::paths::ensure_data_dir()?;
    let dir = skills_override_dir();
    std::fs::create_dir_all(&dir).with_context(|| format!("create skill dir {}", dir.display()))?;
    let path = dir.join("netflix.continue_watching.v1.yaml");
    std::fs::write(&path, BUNDLED_NETFLIX_CW)
        .with_context(|| format!("write {}", path.display()))?;
    Ok(())
}

fn merge_skill(skills: &mut Vec<Skill>, skill: Skill) {
    if let Some(existing) = skills.iter_mut().find(|s| s.id == skill.id) {
        *existing = skill;
    } else {
        skills.push(skill);
    }
}

fn load_skill_dir(skills: &mut Vec<Skill>, dir: &Path) {
    if !dir.is_dir() {
        return;
    }
    let mut entries: Vec<PathBuf> = match std::fs::read_dir(dir) {
        Ok(rd) => rd
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| {
                matches!(
                    p.extension().and_then(|s| s.to_str()),
                    Some("yaml" | "yml" | "json")
                )
            })
            .collect(),
        Err(err) => {
            log::warn!("read skill dir {}: {err}", dir.display());
            return;
        }
    };
    entries.sort();
    for path in entries {
        match load_skill_file(&path) {
            Ok(skill) => merge_skill(skills, skill),
            Err(err) => log::warn!("skip skill {}: {err:#}", path.display()),
        }
    }
}

/// Bundled ids first; later dirs win. User overrides in
/// `~/.local/share/zappe/skills` are last so they beat the checkout copy.
pub fn load_skills() -> Result<Vec<Skill>> {
    let mut skills = match bundled_skills() {
        Ok(s) => s,
        Err(err) => {
            log::error!("bundled Netflix skill failed to parse: {err:#}");
            Vec::new()
        }
    };
    for dir in skill_search_dirs() {
        load_skill_dir(&mut skills, &dir);
    }
    if skills.is_empty() {
        return Err(anyhow!(
            "no harvest skills found (bundled parse failed and no YAML under {})",
            skills_override_dir().display()
        ));
    }
    Ok(skills)
}

pub fn load_skill(id: &str) -> Result<Skill> {
    load_skills()?
        .into_iter()
        .find(|s| s.id == id)
        .ok_or_else(|| {
            anyhow!(
                "unknown skill '{id}' (looked in bundled embed, {}, $ZAPPE_SKILLS_DIR, $ZAPPE_SRC/skills, next to the binary)",
                skills_override_dir().display()
            )
        })
}

pub fn extract_rows(tree: &A11yNode, skill: &Skill) -> Result<Vec<CatalogRow>, ExtractError> {
    let tried: Vec<String> = skill.anchors.iter().flat_map(|a| a.names.clone()).collect();
    let located = find_anchor(tree, &skill.anchors).ok_or(ExtractError::AnchorMissing { tried })?;
    let items = collect_items(tree, located, skill);
    if items.is_empty() {
        return Err(ExtractError::NoRows {
            anchor: located.name.clone(),
        });
    }
    Ok(items)
}

fn find_anchor<'a>(tree: &'a A11yNode, anchors: &[SkillAnchor]) -> Option<&'a A11yNode> {
    if anchors.is_empty() {
        return None;
    }
    tree.walk()
        .find(|node| anchors.iter().any(|a| anchor_matches(node, a)))
}

fn anchor_matches(node: &A11yNode, anchor: &SkillAnchor) -> bool {
    if !anchor.roles.is_empty()
        && !anchor.roles.iter().any(|r| {
            eq_ci(&node.role, r)
                || node
                    .role
                    .to_ascii_lowercase()
                    .contains(&r.to_ascii_lowercase())
        })
    {
        return false;
    }
    if anchor.names.is_empty() {
        return false;
    }
    anchor.names.iter().any(|want| match anchor.match_mode {
        MatchMode::Equals => eq_ci(&node.name, want),
        MatchMode::Contains => contains_ci(&node.name, want),
    })
}

fn collect_items(tree: &A11yNode, anchor: &A11yNode, skill: &Skill) -> Vec<CatalogRow> {
    let region = match skill.extract.from {
        ExtractFrom::Descendants => collect_descendants(anchor),
        ExtractFrom::FollowingSiblings => following_siblings(tree, anchor),
        ExtractFrom::Section => section_nodes(tree, anchor),
    };

    let mut rows = Vec::new();
    let mut seen = std::collections::HashSet::new();
    for node in region {
        if !is_item(node, skill) {
            continue;
        }
        let title = node.name.trim();
        if title.is_empty() || !seen.insert(normalize_title(title)) {
            continue;
        }
        rows.push(CatalogRow {
            title: title.to_string(),
            service: skill.service.clone(),
            href: node.url.clone(),
            artwork: artwork_hint(node),
            skill_id: skill.id.clone(),
        });
        if rows.len() >= skill.extract.max_items {
            break;
        }
    }
    rows
}

fn is_item(node: &A11yNode, skill: &Skill) -> bool {
    let title = node.name.trim();
    if title.len() < 2 || title.len() > 80 {
        return false;
    }
    if skill.anchors.iter().any(|a| {
        a.names
            .iter()
            .any(|n| eq_ci(title, n) || contains_ci(title, n) && n.len() > 8)
    }) {
        return false;
    }
    if skill
        .extract
        .skip_names
        .iter()
        .any(|n| eq_ci(title, n) || contains_ci(title, n) && n.len() > 12)
    {
        return false;
    }
    if !skill.extract.item_roles.is_empty() {
        let role_ok = skill.extract.item_roles.iter().any(|r| {
            eq_ci(&node.role, r)
                || node
                    .role
                    .to_ascii_lowercase()
                    .contains(&r.to_ascii_lowercase())
        });
        if !role_ok {
            return false;
        }
    }
    true
}

fn artwork_hint(node: &A11yNode) -> Option<String> {
    let d = node.description.trim();
    if looks_like_art(d) {
        return Some(d.to_string());
    }
    node.url.clone().filter(|u| looks_like_art(u))
}

fn looks_like_art(s: &str) -> bool {
    let l = s.to_ascii_lowercase();
    l.starts_with("http")
        && (l.contains(".jpg") || l.contains(".png") || l.contains(".webp") || l.contains("art"))
}

fn collect_descendants(node: &A11yNode) -> Vec<&A11yNode> {
    node.walk().skip(1).collect()
}

fn section_nodes<'a>(tree: &'a A11yNode, anchor: &'a A11yNode) -> Vec<&'a A11yNode> {
    if let Some(parent) = parent_of(tree, anchor) {
        let mut out = Vec::new();
        let mut take = false;
        for child in &parent.children {
            if std::ptr::eq(child, anchor) {
                take = true;
                out.extend(child.walk().skip(1));
                continue;
            }
            if take {
                if eq_ci(&child.role, "heading") {
                    break;
                }
                out.extend(child.walk());
            }
        }
        if out.is_empty() {
            out.extend(parent.walk().skip(1));
        }
        out
    } else {
        following_siblings(tree, anchor)
    }
}

fn following_siblings<'a>(tree: &'a A11yNode, anchor: &'a A11yNode) -> Vec<&'a A11yNode> {
    if let Some(parent) = parent_of(tree, anchor) {
        let mut out = Vec::new();
        let mut take = false;
        for child in &parent.children {
            if std::ptr::eq(child, anchor) {
                take = true;
                continue;
            }
            if take {
                if eq_ci(&child.role, "heading") {
                    break;
                }
                out.extend(child.walk());
            }
        }
        return out;
    }
    collect_descendants(anchor)
}

fn parent_of<'a>(tree: &'a A11yNode, target: &'a A11yNode) -> Option<&'a A11yNode> {
    fn rec<'a>(node: &'a A11yNode, target: &'a A11yNode) -> Option<&'a A11yNode> {
        if node.children.iter().any(|c| std::ptr::eq(c, target)) {
            return Some(node);
        }
        for child in &node.children {
            if let Some(found) = rec(child, target) {
                return Some(found);
            }
        }
        None
    }
    rec(tree, target)
}

fn eq_ci(a: &str, b: &str) -> bool {
    a.trim().eq_ignore_ascii_case(b.trim())
}

fn contains_ci(hay: &str, needle: &str) -> bool {
    hay.to_ascii_lowercase()
        .contains(&needle.to_ascii_lowercase())
}

fn normalize_title(s: &str) -> String {
    s.trim().to_ascii_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bundled_netflix_skill_parses() {
        let skill = parse_skill(BUNDLED_NETFLIX_CW).unwrap();
        assert_eq!(skill.id, NETFLIX_CONTINUE_WATCHING_V1);
        assert_eq!(skill.service, "netflix");
        assert!(skill.open.url.contains("netflix.com"));
        assert!(!skill.anchors.is_empty());
        assert!(skill.on_fail.mark_stale);
        assert!(skill.on_fail.teach);
    }

    #[test]
    fn missing_anchor_is_stale_not_improvised() {
        let skill = parse_skill(BUNDLED_NETFLIX_CW).unwrap();
        let tree = A11yNode {
            role: "application".into(),
            name: "Google Chrome".into(),
            children: vec![A11yNode {
                role: "document web".into(),
                name: "Netflix".into(),
                children: vec![A11yNode {
                    role: "heading".into(),
                    name: "Trending Now".into(),
                    ..A11yNode::default()
                }],
                ..A11yNode::default()
            }],
            ..A11yNode::default()
        };
        let err = extract_rows(&tree, &skill).unwrap_err();
        match err {
            ExtractError::AnchorMissing { tried } => {
                assert!(tried.iter().any(|t| t.contains("Continue")));
            }
            other => panic!("expected AnchorMissing, got {other}"),
        }
    }

    #[test]
    fn load_skill_finds_bundled_netflix_without_data_dir() {
        let skill = load_skill(NETFLIX_CONTINUE_WATCHING_V1).unwrap();
        assert_eq!(skill.id, NETFLIX_CONTINUE_WATCHING_V1);
        assert!(skill.open.url.contains("netflix.com"));
    }

    #[test]
    fn install_bundled_writes_override_yaml() {
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        let dir = std::env::temp_dir().join(format!("zappe-skills-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let _ = std::fs::create_dir_all(&dir);
        std::env::set_var("ZAPPE_DATA_DIR", &dir);
        install_bundled_skills().unwrap();
        let path = dir.join("skills/netflix.continue_watching.v1.yaml");
        assert!(path.is_file(), "{}", path.display());
        let on_disk = load_skill_file(&path).unwrap();
        assert_eq!(on_disk.id, NETFLIX_CONTINUE_WATCHING_V1);
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }
}
