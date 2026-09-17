//! Guide state: focus, catalog rows, accounts screen (no rendering).

use crate::accounts;
use crate::catalog::Catalog;
use crate::skills::Service;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum HudScreen {
    SetupChrome,
    SetupOnePassword,
    Guide,
    Accounts,
}

pub struct Guide {
    pub screen: HudScreen,
    pub account_idx: usize,
    /// Horizontal focus index on setup screens (browser / 1Password).
    pub setup_choice_idx: usize,
    pub catalog: Catalog,
    pub row: usize,
    pub col: usize,
    pub status: String,
    pub chrome_line: String,
    pub ota_line: String,
    pub hidden: bool,
    pub command: Option<String>,
    /// Short user-facing message (errors); not shown as a status strip in the HUD.
    pub toast: Option<String>,
}

impl Guide {
    pub fn new(catalog: Catalog, initial_screen: HudScreen) -> Self {
        Self {
            screen: initial_screen,
            account_idx: 0,
            setup_choice_idx: 0,
            catalog,
            row: 0,
            col: 0,
            status: String::new(),
            chrome_line: String::new(),
            ota_line: String::new(),
            hidden: false,
            command: None,
            toast: None,
        }
    }

    pub fn open_accounts(&mut self) {
        self.screen = HudScreen::Accounts;
        self.account_idx = 0;
        self.status = accounts::ONBOARDING_HEADLINE.into();
    }

    pub fn close_accounts(&mut self) {
        self.screen = HudScreen::Guide;
        self.go_home();
    }

    pub fn move_accounts(&mut self, dcol: isize) {
        let n = accounts::accounts_tile_count() as isize;
        self.account_idx = ((self.account_idx as isize + dcol).rem_euclid(n)) as usize;
    }

    pub fn focused_account(&self) -> Option<(usize, Service)> {
        if self.screen != HudScreen::Accounts {
            return None;
        }
        if self.account_idx >= accounts::ACCOUNTS_DONE_INDEX {
            return None;
        }
        accounts::login_service(self.account_idx).map(|s| (self.account_idx, s))
    }

    pub fn is_accounts_done(&self) -> bool {
        self.screen == HudScreen::Accounts && self.account_idx == accounts::ACCOUNTS_DONE_INDEX
    }

    pub fn move_by(&mut self, drow: isize, dcol: isize) {
        if self.catalog.rows.is_empty() {
            return;
        }
        let rows = self.catalog.rows.len() as isize;
        self.row = ((self.row as isize + drow).rem_euclid(rows)) as usize;
        let cols = self.catalog.rows[self.row].tiles.len() as isize;
        if cols == 0 {
            self.col = 0;
            return;
        }
        self.col = ((self.col as isize + dcol).rem_euclid(cols)) as usize;
    }

    pub fn go_home(&mut self) {
        self.screen = HudScreen::Guide;
        self.row = 0;
        self.col = 0;
        self.command = None;
        self.status.clear();
    }

    pub fn set_screen(&mut self, screen: HudScreen) {
        self.screen = screen;
        self.setup_choice_idx = 0;
    }

    pub fn focused(&self) -> Option<&crate::catalog::Tile> {
        self.catalog.tile(self.row, self.col)
    }

    pub fn open_command(&mut self) {
        self.command = Some(String::new());
        self.status =
            "play <query> [on youtube|netflix|prime|disney]  /  pause fullscreen back home".into();
    }

    pub fn close_command(&mut self) {
        self.command = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::catalog::Catalog;

    #[test]
    fn guide_wraps_focus() {
        let mut guide = Guide::new(Catalog::placeholder(), HudScreen::Guide);
        guide.move_by(-1, 0);
        assert_eq!(guide.row, guide.catalog.rows.len() - 1);
        guide.move_by(1, 0);
        assert_eq!(guide.row, 0);
        assert!(guide.focused().is_some());
        guide.move_by(1, 0);
        guide.go_home();
        assert_eq!((guide.row, guide.col), (0, 0));
    }
}
