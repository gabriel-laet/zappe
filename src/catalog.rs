//! Placeholder living-room catalog.
//!
//! Real streaming libraries stay inside first-party Chrome. Public metadata
//! (JustWatch-style) is a stub trait so a later crate can fill posters/titles
//! without scraping Netflix / Prime / Disney / YouTube HTML.
//!
//! Terrestrial channels come from a local dvbv5 `channels.conf` when configured.

use crate::ota::OtaChannel;
use crate::skills::Service;

/// URL prefix for OTA tiles (`ota://` + channel name matches `channels.conf` `[Name]`).
pub const OTA_URL_PREFIX: &str = "ota://";

/// Opens the in-HUD accounts / login screen (not a web URL).
pub const ACCOUNTS_URL: &str = "zappe://accounts";

#[derive(Clone, Debug)]
pub struct Tile {
    pub title: String,
    pub service: Service,
    /// First-party deep link, service home, or `ota://…` for terrestrial tuners.
    pub url: String,
}

#[derive(Clone, Debug)]
pub struct Row {
    pub label: String,
    pub tiles: Vec<Tile>,
}

pub struct Catalog {
    pub rows: Vec<Row>,
    metadata: Box<dyn MetadataSource>,
}

impl Catalog {
    pub fn placeholder() -> Self {
        Self {
            metadata: Box::new(StubMetadata),
            rows: vec![
                Row {
                    label: "CONTINUE".into(),
                    tiles: vec![
                        Tile {
                            title: "Accounts".into(),
                            service: Service::Netflix,
                            url: ACCOUNTS_URL.into(),
                        },
                        tile("The Night Agent", Service::Netflix),
                        tile("Reacher", Service::Prime),
                        tile("Andor", Service::Disney),
                        Tile {
                            title: "Subscriptions".into(),
                            service: Service::Youtube,
                            url: Service::Youtube.subscriptions_url().unwrap().into(),
                        },
                    ],
                },
                Row {
                    label: "NETFLIX".into(),
                    tiles: vec![
                        tile("Home", Service::Netflix),
                        tile("Stranger Things", Service::Netflix),
                        tile("The Diplomat", Service::Netflix),
                    ],
                },
                Row {
                    label: "PRIME VIDEO".into(),
                    tiles: vec![
                        tile("Home", Service::Prime),
                        tile("The Boys", Service::Prime),
                        tile("Fallout", Service::Prime),
                    ],
                },
                Row {
                    label: "DISNEY+".into(),
                    tiles: vec![
                        tile("Home", Service::Disney),
                        tile("Loki", Service::Disney),
                        tile("Shogun", Service::Disney),
                    ],
                },
                Row {
                    label: "YOUTUBE".into(),
                    tiles: vec![
                        tile("Home", Service::Youtube),
                        Tile {
                            title: "Subscriptions".into(),
                            service: Service::Youtube,
                            url: Service::Youtube.subscriptions_url().unwrap().into(),
                        },
                        Tile {
                            title: "Search lofi".into(),
                            service: Service::Youtube,
                            url: Service::Youtube.search_url("lofi"),
                        },
                        Tile {
                            title: "Me at the zoo".into(),
                            service: Service::Youtube,
                            url: Service::Youtube
                                .watch_url("jNQXAC9IVRw")
                                .expect("placeholder watch id"),
                        },
                    ],
                },
                Row {
                    label: "JELLYFIN".into(),
                    tiles: vec![Tile {
                        title: "later".into(),
                        service: Service::Jellyfin,
                        url: "http://localhost:8096".into(),
                    }],
                },
            ],
        }
    }

    /// Placeholder rows plus an **OTA TV** row when `channels.conf` was parsed.
    pub fn with_ota(channels: &[OtaChannel]) -> Self {
        let mut cat = Self::placeholder();
        if channels.is_empty() {
            return cat;
        }
        let tiles: Vec<Tile> = channels
            .iter()
            .map(|ch| Tile {
                title: ch.name.clone(),
                service: Service::Ota,
                url: format!("{OTA_URL_PREFIX}{}", ch.name),
            })
            .collect();
        let insert_at = cat
            .rows
            .iter()
            .position(|r| r.label == "JELLYFIN")
            .unwrap_or(cat.rows.len());
        cat.rows.insert(
            insert_at,
            Row {
                label: "OTA TV".into(),
                tiles,
            },
        );
        cat
    }

    pub fn ota_channel_name(tile: &Tile) -> Option<&str> {
        if tile.service != Service::Ota {
            return None;
        }
        tile.url
            .strip_prefix(OTA_URL_PREFIX)
            .or(Some(tile.title.as_str()))
    }

    pub fn tile(&self, row: usize, col: usize) -> Option<&Tile> {
        self.rows.get(row).and_then(|r| r.tiles.get(col))
    }

    /// Public metadata only. Never scrapes a logged-in streaming site.
    pub fn public_meta(&self, query: &str) -> Option<TitleMeta> {
        self.metadata.lookup(query)
    }
}

fn tile(title: &str, service: Service) -> Tile {
    Tile {
        title: title.into(),
        service,
        url: service.home_url().into(),
    }
}

/// Poster / year from a public metadata source. Unused until a real source is wired.
#[derive(Clone, Debug)]
#[allow(dead_code)]
pub struct TitleMeta {
    pub title: String,
    pub year: Option<u16>,
    pub poster_url: Option<String>,
}

/// Public catalog metadata. Implementations must not scrape logged-in service HTML.
pub trait MetadataSource: Send {
    fn lookup(&self, query: &str) -> Option<TitleMeta>;
}

/// Placeholder until a JustWatch-style (or similar public) source is wired.
pub struct StubMetadata;

impl MetadataSource for StubMetadata {
    fn lookup(&self, _query: &str) -> Option<TitleMeta> {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
use crate::accounts::is_accounts_url;

    #[test]
    fn placeholder_has_guide_rows() {
        let catalog = Catalog::placeholder();
        let labels: Vec<_> = catalog.rows.iter().map(|r| r.label.as_str()).collect();
        assert_eq!(
            labels,
            [
                "CONTINUE",
                "NETFLIX",
                "PRIME VIDEO",
                "DISNEY+",
                "YOUTUBE",
                "JELLYFIN",
            ]
        );
        assert!(catalog.tile(0, 0).is_some());
        assert!(is_accounts_url(catalog.tile(0, 0).unwrap().url.as_str()));
        assert!(catalog.tile(0, 1).unwrap().url.starts_with("https://"));
        let yt = catalog
            .rows
            .iter()
            .find(|r| r.label == "YOUTUBE")
            .expect("youtube row");
        assert!(yt
            .tiles
            .iter()
            .any(|t| t.url.contains("/feed/subscriptions")));
        assert!(yt
            .tiles
            .iter()
            .any(|t| t.url.contains("/results?search_query=")));
        assert!(yt.tiles.iter().any(|t| t.url.contains("/watch?v=")));
        assert!(catalog.public_meta("andor").is_none());
    }

    #[test]
    fn ota_row_uses_channel_names() {
        let cat = Catalog::with_ota(&[
            OtaChannel {
                name: "Globo HD".into(),
            },
            OtaChannel {
                name: "SBT HD".into(),
            },
        ]);
        let row = cat.rows.iter().find(|r| r.label == "OTA TV").expect("ota");
        assert_eq!(row.tiles.len(), 2);
        assert_eq!(row.tiles[0].service, Service::Ota);
        assert_eq!(
            Catalog::ota_channel_name(&row.tiles[0]),
            Some("Globo HD")
        );
    }
}
