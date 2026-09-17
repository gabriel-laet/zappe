//! AT-SPI accessibility tree dump (Linux).
//!
//! Used to harvest logged-in shelves (Continue Watching) from the Chrome nest.
//! Failures return errors — callers mark the skill stale instead of improvising.

use std::collections::HashMap;
use std::path::Path;
use std::time::Duration;

use anyhow::{anyhow, Context, Result};
use serde::{Deserialize, Serialize};
use zbus::zvariant::OwnedObjectPath;
use zbus::Connection;

const ACCESSIBLE: &str = "org.a11y.atspi.Accessible";
const ROOT_PATH: &str = "/org/a11y/atspi/accessible/root";
const REGISTRY: &str = "org.a11y.atspi.Registry";
const DEFAULT_MAX_DEPTH: usize = 18;
const DEFAULT_MAX_NODES: usize = 2500;
const DEFAULT_MAX_CHILDREN: i32 = 64;

#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct A11yNode {
    #[serde(default)]
    pub role: String,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub description: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub url: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub children: Vec<A11yNode>,
}

impl A11yNode {
    pub fn walk<'a>(&'a self) -> A11yWalk<'a> {
        A11yWalk {
            stack: vec![self],
        }
    }
}

pub struct A11yWalk<'a> {
    stack: Vec<&'a A11yNode>,
}

impl<'a> Iterator for A11yWalk<'a> {
    type Item = &'a A11yNode;

    fn next(&mut self) -> Option<Self::Item> {
        let node = self.stack.pop()?;
        for child in node.children.iter().rev() {
            self.stack.push(child);
        }
        Some(node)
    }
}

pub fn load_dump(path: &Path) -> Result<A11yNode> {
    let bytes = std::fs::read(path).with_context(|| format!("read {}", path.display()))?;
    serde_json::from_slice(&bytes).context("parse a11y dump JSON")
}

pub fn write_dump(path: &Path, tree: &A11yNode) -> Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let json = serde_json::to_string_pretty(tree)?;
    std::fs::write(path, json)?;
    Ok(())
}

/// Dump Chrome's AT-SPI tree. Times out instead of hanging the app.
pub async fn dump_chrome_tree() -> Result<A11yNode> {
    let timeout = Duration::from_secs(
        std::env::var("ZAPPE_A11Y_TIMEOUT_SECS")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(12),
    );
    match tokio::time::timeout(timeout, dump_chrome_tree_inner()).await {
        Ok(result) => result,
        Err(_) => Err(anyhow!(
            "AT-SPI dump timed out after {}s — is Chrome accessibility enabled?",
            timeout.as_secs()
        )),
    }
}

async fn dump_chrome_tree_inner() -> Result<A11yNode> {
    let conn = connect_a11y_bus().await?;
    dump_from_connection(&conn).await
}

async fn dump_from_connection(conn: &Connection) -> Result<A11yNode> {
    let mut children = match get_children(conn, REGISTRY, ROOT_PATH).await {
        Ok(c) if !c.is_empty() => c,
        Ok(_) | Err(_) => {
            // Some desktops expose the registry under a unique name; try GetChildren on root
            // using the connection's unique name as a last resort via Peer listing.
            get_children(conn, REGISTRY, ROOT_PATH)
                .await
                .unwrap_or_default()
        }
    };

    if children.is_empty() {
        return Err(anyhow!(
            "AT-SPI registry has no applications. Start Chrome with --force-renderer-accessibility \
             and install at-spi2-core. See README (Chrome a11y)."
        ));
    }

    let mut chrome: Option<A11yNode> = None;
    let mut others = Vec::new();
    for (dest, path) in children.drain(..) {
        let node = dump_node(conn, &dest, path.as_str(), 0, &mut 0).await;
        match node {
            Ok(n) => {
                if looks_like_chrome(&n) {
                    chrome = Some(n);
                    break;
                }
                others.push(n);
            }
            Err(err) => log::warn!("a11y skip app {dest}: {err}"),
        }
    }

    chrome.or_else(|| others.into_iter().find(looks_like_chrome)).ok_or_else(|| {
        anyhow!("no Chrome/Chromium application on the AT-SPI bus (is the nest running?)")
    })
}

fn looks_like_chrome(node: &A11yNode) -> bool {
    let blob = format!("{} {}", node.name, node.role).to_ascii_lowercase();
    blob.contains("chrome") || blob.contains("chromium")
}

async fn connect_a11y_bus() -> Result<Connection> {
    let session = Connection::session()
        .await
        .context("connect to D-Bus session bus")?;
    match a11y_bus_address(&session).await {
        Ok(addr) if !addr.is_empty() => {
            zbus::connection::Builder::address(addr.as_str())
                .map_err(|e| anyhow!("invalid AT-SPI bus address {addr}: {e}"))?
                .build()
                .await
                .context("connect to AT-SPI bus")
        }
        Ok(_) => Ok(session),
        Err(err) => {
            log::warn!("org.a11y.Bus.GetAddress failed ({err}); using session bus");
            Ok(session)
        }
    }
}

async fn a11y_bus_address(session: &Connection) -> Result<String> {
    let reply = session
        .call_method(
            Some("org.a11y.Bus"),
            "/org/a11y/bus",
            Some("org.a11y.Bus"),
            "GetAddress",
            &(),
        )
        .await
        .context("org.a11y.Bus.GetAddress")?;
    let addr: String = reply.body().deserialize().context("decode a11y bus address")?;
    Ok(addr)
}

async fn get_children(
    conn: &Connection,
    dest: &str,
    path: &str,
) -> Result<Vec<(String, OwnedObjectPath)>> {
    let reply = conn
        .call_method(Some(dest), path, Some(ACCESSIBLE), "GetChildren", &())
        .await
        .with_context(|| format!("GetChildren {dest} {path}"))?;
    let children: Vec<(String, OwnedObjectPath)> = reply
        .body()
        .deserialize()
        .context("decode GetChildren a(so)")?;
    Ok(children)
}

async fn dump_node(
    conn: &Connection,
    dest: &str,
    path: &str,
    depth: usize,
    nodes: &mut usize,
) -> Result<A11yNode> {
    if depth > DEFAULT_MAX_DEPTH || *nodes >= DEFAULT_MAX_NODES {
        return Ok(A11yNode::default());
    }
    *nodes += 1;

    let proxy = zbus::Proxy::new(conn, dest, path, ACCESSIBLE)
        .await
        .with_context(|| format!("proxy {dest} {path}"))?;

    let name: String = proxy.get_property("Name").await.unwrap_or_default();
    let description: String = proxy.get_property("Description").await.unwrap_or_default();
    let role: String = proxy.call("GetRoleName", &()).await.unwrap_or_default();
    let url = node_url(conn, dest, path, &proxy).await;

    let mut child_refs = match get_children(conn, dest, path).await {
        Ok(c) => c,
        Err(_) => Vec::new(),
    };
    if child_refs.len() > DEFAULT_MAX_CHILDREN as usize {
        child_refs.truncate(DEFAULT_MAX_CHILDREN as usize);
    }

    let mut children = Vec::with_capacity(child_refs.len());
    for (child_dest, child_path) in child_refs {
        match Box::pin(dump_node(
            conn,
            &child_dest,
            child_path.as_str(),
            depth + 1,
            nodes,
        ))
        .await
        {
            Ok(child) => children.push(child),
            Err(err) => log::warn!("a11y child skipped: {err}"),
        }
    }

    Ok(A11yNode {
        role,
        name,
        description,
        url,
        children,
    })
}

async fn node_url(
    conn: &Connection,
    dest: &str,
    path: &str,
    proxy: &zbus::Proxy<'_>,
) -> Option<String> {
    if let Ok(attrs) = proxy
        .call::<&str, (), HashMap<String, String>>("GetAttributes", &())
        .await
    {
        for key in ["url", "URI", "uri", "href", "RecycleURL", "src"] {
            if let Some(v) = attrs.get(key) {
                if looks_like_url(v) {
                    return Some(v.clone());
                }
            }
        }
    }

    if let Ok(reply) = conn
        .call_method(Some(dest), path, Some("org.a11y.atspi.Hyperlink"), "GetURI", &(0i32))
        .await
    {
        if let Ok(uri) = reply.body().deserialize::<String>() {
            if looks_like_url(&uri) {
                return Some(uri);
            }
        }
    }
    None
}

fn looks_like_url(s: &str) -> bool {
    let s = s.trim();
    s.starts_with("http://") || s.starts_with("https://") || s.starts_with("file://")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn walk_visits_descendants() {
        let tree = A11yNode {
            role: "application".into(),
            name: "Google Chrome".into(),
            children: vec![A11yNode {
                role: "heading".into(),
                name: "Continue Watching".into(),
                ..A11yNode::default()
            }],
            ..A11yNode::default()
        };
        let names: Vec<_> = tree.walk().map(|n| n.name.as_str()).collect();
        assert_eq!(names, ["Google Chrome", "Continue Watching"]);
    }

    #[test]
    fn dump_roundtrip() {
        let dir = std::env::temp_dir().join("zappe-a11y-test");
        let _ = std::fs::create_dir_all(&dir);
        let path = dir.join("dump.json");
        let tree = A11yNode {
            role: "application".into(),
            name: "Google Chrome".into(),
            ..A11yNode::default()
        };
        write_dump(&path, &tree).unwrap();
        let loaded = load_dump(&path).unwrap();
        assert_eq!(loaded.name, "Google Chrome");
    }
}
