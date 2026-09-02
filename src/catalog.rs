//! Placeholder living-room catalog.
//!
//! Real streaming libraries stay inside first-party Chrome. Public metadata
//! (JustWatch-style) is a stub trait so a later crate can fill posters/titles
//! without scraping Netflix / Prime / Disney HTML.

use crate::skills::Service;

#[derive(Clone, Debug)]
pub struct Tile {
    pub title: String,
    pub service: Service,
    /// First-party deep link or service home. Never a scraped catalog URL.
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
                        tile("The Night Agent", Service::Netflix),
                        tile("Reacher", Service::Prime),
                        tile("Andor", Service::Disney),
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
pub trait MetadataSource {
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

    #[test]
    fn placeholder_has_guide_rows() {
        let catalog = Catalog::placeholder();
        let labels: Vec<_> = catalog.rows.iter().map(|r| r.label.as_str()).collect();
        assert_eq!(
            labels,
            ["CONTINUE", "NETFLIX", "PRIME VIDEO", "DISNEY+", "JELLYFIN"]
        );
        assert!(catalog.tile(0, 0).is_some());
        assert!(catalog.tile(0, 0).unwrap().url.starts_with("https://"));
        assert!(catalog.public_meta("andor").is_none());
    }

    #[test]
    fn stub_metadata_does_not_invent_titles() {
        assert!(StubMetadata.lookup("andor").is_none());
    }
}
