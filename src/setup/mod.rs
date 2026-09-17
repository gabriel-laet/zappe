//! First-run setup orchestration (browser + optional 1Password).

pub mod browser;
pub mod choices;
pub mod onepassword;

use crate::accounts;
use crate::chrome::find_chrome;
use crate::guide::HudScreen;
use crate::setup::browser::detect;
use crate::setup::onepassword::{extension_in_profile, skipped_by_user};

/// Decide the first HUD screen after Qt starts (before Chrome CDP is up).
pub fn initial_screen(cdp_attach: bool) -> HudScreen {
    if cdp_attach {
        return if accounts::needs_onboarding() {
            HudScreen::Accounts
        } else {
            HudScreen::Guide
        };
    }
    if find_chrome().is_none() {
        return HudScreen::SetupChrome;
    }
    if !skipped_by_user() && !extension_in_profile() {
        return HudScreen::SetupOnePassword;
    }
    if accounts::needs_onboarding() {
        HudScreen::Accounts
    } else {
        HudScreen::Guide
    }
}

pub fn chrome_setup_complete() -> bool {
    detect().found
}

pub fn advance_after_chrome_setup() -> HudScreen {
    if !skipped_by_user() && !extension_in_profile() {
        HudScreen::SetupOnePassword
    } else if accounts::needs_onboarding() {
        HudScreen::Accounts
    } else {
        HudScreen::Guide
    }
}

pub fn advance_after_onepassword() -> HudScreen {
    if accounts::needs_onboarding() {
        HudScreen::Accounts
    } else {
        HudScreen::Guide
    }
}
