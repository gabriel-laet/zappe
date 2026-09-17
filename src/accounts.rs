//! First-run accounts / login flow inside the zappe-owned Chrome profile.

use std::fs;
use std::path::PathBuf;

use crate::catalog::ACCOUNTS_URL;
use crate::skills::Service;

pub fn zappe_data_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("zappe")
}

pub fn onboarding_marker() -> PathBuf {
    zappe_data_dir().join("onboarding.done")
}

pub fn needs_onboarding() -> bool {
    !onboarding_marker().exists()
}

pub fn mark_onboarding_done() {
    let dir = zappe_data_dir();
    let _ = fs::create_dir_all(&dir);
    let _ = fs::write(onboarding_marker(), b"ok");
}

pub fn is_accounts_url(url: &str) -> bool {
    url == ACCOUNTS_URL
}

/// Streamable services the user signs into once in the zappe Chrome profile.
pub const LOGIN_SERVICES: [Service; 4] = [
    Service::Netflix,
    Service::Prime,
    Service::Disney,
    Service::Youtube,
];

pub fn login_service(index: usize) -> Option<Service> {
    LOGIN_SERVICES.get(index).copied()
}

/// Last tile on the accounts grid — return to the program guide.
pub const ACCOUNTS_DONE_INDEX: usize = LOGIN_SERVICES.len();

pub fn accounts_tile_count() -> usize {
    LOGIN_SERVICES.len() + 1
}

pub fn accounts_label(index: usize) -> &'static str {
    match index {
        0 => "Netflix",
        1 => "Prime Video",
        2 => "Disney+",
        3 => "YouTube",
        4 => "Continue to guide",
        _ => "?",
    }
}

pub const ONBOARDING_HEADLINE: &str = "Sign in once in the Zappe Chrome window";
pub const ONBOARDING_BODY: &str =
    "Pick a service, log in with your account (1Password extension OK). Next launch reuses that profile.";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn login_services_are_streaming_only() {
        for s in LOGIN_SERVICES {
            assert!(s.uses_chrome());
        }
    }
}
