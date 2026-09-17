//! Qt/QML bridge for the Zappe HUD.

use core::pin::Pin;
use std::sync::Mutex;

use cxx_qt::CxxQtType;
use cxx_qt_lib::QString;

use crate::accounts;
use crate::app::AppCore;
use crate::guide::HudScreen;
use crate::setup::browser::{detect, try_noninteractive_install, InstallAttempt};
use crate::setup::onepassword::{app_install_hint, extension_in_profile, readiness_summary};

/// Set before the QML engine loads.
pub static APP_CORE: Mutex<Option<AppCore>> = Mutex::new(None);

/// Empty catalog JSON for QML `JSON.parse` before the first `tick()`.
pub const EMPTY_CATALOG_JSON: &str = r#"{"rows":[],"accounts":[]}"#;

#[cxx_qt::bridge]
pub mod qobject {
    unsafe extern "C++" {
        include!("cxx-qt-lib/qstring.h");
        type QString = cxx_qt_lib::QString;
    }

    extern "RustQt" {
        #[qobject]
        #[qml_element]
        #[qproperty(i32, screen)]
        #[qproperty(QString, status_message)]
        #[qproperty(QString, chrome_line)]
        #[qproperty(QString, ota_line)]
        #[qproperty(bool, hud_visible)]
        #[qproperty(QString, install_command)]
        #[qproperty(QString, browser_message)]
        #[qproperty(QString, distro_hint)]
        #[qproperty(QString, onepassword_summary)]
        #[qproperty(QString, onepassword_app_hint)]
        #[qproperty(QString, catalog_json)]
        #[qproperty(QString, command_text)]
        #[qproperty(i32, account_index)]
        type ZappeBackend = super::ZappeBackendRust;

        #[qinvokable]
        fn tick(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "retryBrowserDetect"]
        fn retry_browser_detect(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "tryInstallBrowser"]
        fn try_install_browser(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "continueChromeSetup"]
        fn continue_chrome_setup(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "skipOnePassword"]
        fn skip_one_password(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "continueOnePassword"]
        fn continue_one_password(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "openOnePasswordExtension"]
        fn open_one_password_extension(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        fn move_focus(self: Pin<&mut ZappeBackend>, drow: i32, dcol: i32);

        #[qinvokable]
        fn activate(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        fn back(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "playPause"]
        fn play_pause(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        fn quit(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "toggleHud"]
        fn toggle_hud(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "openCommand"]
        fn open_command(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "commandCommit"]
        fn command_commit(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "commandCancel"]
        fn command_cancel(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "commandBackspace"]
        fn command_backspace(self: Pin<&mut ZappeBackend>);

        #[qinvokable]
        #[cxx_name = "commandAppend"]
        fn command_append(self: Pin<&mut ZappeBackend>, text: &QString);

        #[qinvokable]
        #[cxx_name = "shouldQuit"]
        fn should_quit(self: &ZappeBackend) -> bool;
    }
}

pub struct ZappeBackendRust {
    screen: i32,
    status_message: QString,
    chrome_line: QString,
    ota_line: QString,
    hud_visible: bool,
    install_command: QString,
    browser_message: QString,
    distro_hint: QString,
    onepassword_summary: QString,
    onepassword_app_hint: QString,
    catalog_json: QString,
    command_text: QString,
    account_index: i32,
}

impl Default for ZappeBackendRust {
    fn default() -> Self {
        let mut rust = Self {
            screen: 0,
            status_message: QString::from(""),
            chrome_line: QString::from("Chrome · …"),
            ota_line: QString::from("OTA · off"),
            hud_visible: true,
            install_command: QString::from(""),
            browser_message: QString::from(""),
            distro_hint: QString::from(""),
            onepassword_summary: QString::from(&readiness_summary()),
            onepassword_app_hint: QString::from(&app_install_hint()),
            catalog_json: QString::from(EMPTY_CATALOG_JSON),
            command_text: QString::from(""),
            account_index: 0,
        };
        refresh_browser_props(&mut rust);
        if let Ok(guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_ref() {
                sync_props(&mut rust, core);
            }
        }
        rust
    }
}

fn sync_props(rust: &mut ZappeBackendRust, core: &AppCore) {
    rust.screen = screen_to_i32(core.guide.screen);
    rust.status_message = QString::from(&core.guide.status);
    rust.chrome_line = QString::from(&core.guide.chrome_line);
    rust.ota_line = QString::from(&core.guide.ota_line);
    rust.hud_visible = core.hud_visible;
    rust.account_index = core.guide.account_idx as i32;
    rust.command_text = QString::from(core.guide.command.as_deref().unwrap_or(""));
    rust.catalog_json = QString::from(&catalog_to_json(core));
    rust.onepassword_summary = QString::from(&readiness_summary());
    rust.onepassword_app_hint = QString::from(&app_install_hint());
}

fn refresh_browser_props(rust: &mut ZappeBackendRust) {
    let d = detect();
    rust.browser_message = QString::from(&d.message);
    rust.install_command = QString::from(&d.install_command);
    rust.distro_hint = QString::from(&d.distro_hint);
}

fn push_props(mut pin: Pin<&mut qobject::ZappeBackend>, rust: &ZappeBackendRust) {
    pin.as_mut().set_screen(rust.screen);
    pin.as_mut().set_status_message(QString::from(&rust.status_message.to_string()));
    pin.as_mut().set_chrome_line(QString::from(&rust.chrome_line.to_string()));
    pin.as_mut().set_ota_line(QString::from(&rust.ota_line.to_string()));
    pin.as_mut().set_hud_visible(rust.hud_visible);
    pin.as_mut().set_install_command(QString::from(&rust.install_command.to_string()));
    pin.as_mut().set_browser_message(QString::from(&rust.browser_message.to_string()));
    pin.as_mut().set_distro_hint(QString::from(&rust.distro_hint.to_string()));
    pin.as_mut().set_onepassword_summary(QString::from(&rust.onepassword_summary.to_string()));
    pin.as_mut().set_onepassword_app_hint(QString::from(&rust.onepassword_app_hint.to_string()));
    pin.as_mut().set_catalog_json(QString::from(&rust.catalog_json.to_string()));
    pin.as_mut().set_command_text(QString::from(&rust.command_text.to_string()));
    pin.as_mut().set_account_index(rust.account_index);
}

impl qobject::ZappeBackend {
    fn sync_from_core(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                let rust = Pin::get_mut(self.as_mut().rust_mut());
                sync_props(rust, core);
            }
        }
        let snapshot = self.rust().clone_snapshot();
        push_props(self, &snapshot);
    }

    pub fn tick(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.tick();
            }
        }
        self.sync_from_core();
    }

    pub fn retry_browser_detect(mut self: Pin<&mut Self>) {
        {
            let rust = Pin::get_mut(self.as_mut().rust_mut());
            refresh_browser_props(rust);
            if detect().found {
                rust.status_message = QString::from("Navegador encontrado — toque Continuar.");
            }
        }
        let snapshot = self.rust().clone_snapshot();
        push_props(self, &snapshot);
    }

    pub fn try_install_browser(mut self: Pin<&mut Self>) {
        let attempt = try_noninteractive_install();
        let msg = {
            let rust = Pin::get_mut(self.as_mut().rust_mut());
            match attempt {
                InstallAttempt::Installed => {
                    refresh_browser_props(rust);
                    "Chromium instalado. Toque Continuar.".to_string()
                }
                InstallAttempt::NeedsPassword { command } => {
                    rust.install_command = QString::from(&command);
                    "Precisa de senha sudo — copie o comando e instale no terminal.".into()
                }
                InstallAttempt::Manual { command, detail } => {
                    rust.install_command = QString::from(&command);
                    format!("Instalação manual: {detail}")
                }
            }
        };
        Pin::get_mut(self.as_mut().rust_mut()).status_message = QString::from(&msg);
        let snapshot = self.rust().clone_snapshot();
        push_props(self, &snapshot);
    }

    pub fn continue_chrome_setup(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                if let Err(err) = core.finish_chrome_setup() {
                    core.guide.status = format!("Chrome: {err:#}");
                }
            }
        }
        self.sync_from_core();
    }

    pub fn skip_one_password(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.skip_onepassword();
            }
        }
        self.sync_from_core();
    }

    pub fn continue_one_password(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                if !extension_in_profile() {
                    core.guide.status =
                        "Extensão ainda não detectada — você pode instalar depois.".into();
                }
                core.continue_onepassword();
            }
        }
        self.sync_from_core();
    }

    pub fn open_one_password_extension(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.open_onepassword_extension_page();
            }
        }
        self.sync_from_core();
    }

    pub fn move_focus(mut self: Pin<&mut Self>, drow: i32, dcol: i32) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.move_focus(drow as isize, dcol as isize);
            }
        }
        self.sync_from_core();
    }

    pub fn activate(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.activate_focus();
            }
        }
        self.sync_from_core();
    }

    pub fn back(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.back();
            }
        }
        self.sync_from_core();
    }

    pub fn play_pause(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.play_pause();
            }
        }
        self.sync_from_core();
    }

    pub fn quit(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.quit();
            }
        }
        self.sync_from_core();
    }

    pub fn toggle_hud(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.toggle_hud();
            }
        }
        self.sync_from_core();
    }

    pub fn open_command(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.open_command_bar();
            }
        }
        self.sync_from_core();
    }

    pub fn command_commit(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.command_commit();
            }
        }
        self.sync_from_core();
    }

    pub fn command_cancel(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.command_cancel();
            }
        }
        self.sync_from_core();
    }

    pub fn command_backspace(mut self: Pin<&mut Self>) {
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                core.command_backspace();
            }
        }
        self.sync_from_core();
    }

    pub fn command_append(mut self: Pin<&mut Self>, text: &QString) {
        let s = text.to_string();
        if let Ok(mut guard) = APP_CORE.lock() {
            if let Some(core) = guard.as_mut() {
                for ch in s.chars() {
                    core.command_push_char(ch);
                }
            }
        }
        self.sync_from_core();
    }

    pub fn should_quit(self: &Self) -> bool {
        APP_CORE
            .lock()
            .ok()
            .and_then(|g| g.as_ref().map(|c| c.quit_requested))
            .unwrap_or(false)
    }
}

impl ZappeBackendRust {
    fn clone_snapshot(&self) -> ZappeBackendRust {
        ZappeBackendRust {
            screen: self.screen,
            status_message: QString::from(&self.status_message.to_string()),
            chrome_line: QString::from(&self.chrome_line.to_string()),
            ota_line: QString::from(&self.ota_line.to_string()),
            hud_visible: self.hud_visible,
            install_command: QString::from(&self.install_command.to_string()),
            browser_message: QString::from(&self.browser_message.to_string()),
            distro_hint: QString::from(&self.distro_hint.to_string()),
            onepassword_summary: QString::from(&self.onepassword_summary.to_string()),
            onepassword_app_hint: QString::from(&self.onepassword_app_hint.to_string()),
            catalog_json: QString::from(&self.catalog_json.to_string()),
            command_text: QString::from(&self.command_text.to_string()),
            account_index: self.account_index,
        }
    }
}

fn screen_to_i32(s: HudScreen) -> i32 {
    match s {
        HudScreen::SetupChrome => 0,
        HudScreen::SetupOnePassword => 1,
        HudScreen::Accounts => 2,
        HudScreen::Guide => 3,
    }
}

fn catalog_to_json(core: &AppCore) -> String {
    let guide = &core.guide;
    let mut out = String::from("{\"rows\":[");
    for (ri, row) in guide.catalog.rows.iter().enumerate() {
        if ri > 0 {
            out.push(',');
        }
        out.push_str("{\"label\":\"");
        escape_json(&row.label, &mut out);
        out.push_str("\",\"tiles\":[");
        for (ci, tile) in row.tiles.iter().enumerate() {
            if ci > 0 {
                out.push(',');
            }
            let focused = guide.screen == HudScreen::Guide && ri == guide.row && ci == guide.col;
            out.push_str("{\"title\":\"");
            escape_json(&tile.title, &mut out);
            out.push_str("\",\"service\":\"");
            escape_json(tile.service.label(), &mut out);
            out.push_str("\",\"focused\":");
            out.push_str(if focused { "true" } else { "false" });
            out.push('}');
        }
        out.push_str("]}");
    }
    out.push_str("],\"accounts\":[");
    for idx in 0..accounts::accounts_tile_count() {
        if idx > 0 {
            out.push(',');
        }
        let focused = guide.screen == HudScreen::Accounts && guide.account_idx == idx;
        out.push_str("{\"label\":\"");
        escape_json(accounts::accounts_label(idx), &mut out);
        out.push_str("\",\"focused\":");
        out.push_str(if focused { "true" } else { "false" });
        out.push('}');
    }
    out.push_str("]}");
    out
}

fn escape_json(s: &str, out: &mut String) {
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            _ => out.push(ch),
        }
    }
}
