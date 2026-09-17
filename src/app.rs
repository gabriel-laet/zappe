//! Application core: Chrome, OTA, guide — UI-agnostic.

use std::path::PathBuf;
use std::sync::mpsc::{self, Receiver};
use std::time::{Duration, Instant};

use crate::setup::browser::{detect, try_noninteractive_install, InstallAttempt};
use crate::setup::choices::{choice_at, choice_count};

use anyhow::Result;
use tokio::runtime::Runtime;

use crate::accounts;
use crate::accounts::is_accounts_url;
use crate::catalog::Catalog;
use crate::chrome::{ensure_chrome_binary, ChromeCmd, ChromeEvent, ChromeHandle, ChromeOpts};
use crate::command::{self, Command};
use crate::guide::{Guide, HudScreen};
use crate::ota::{parse_channels_conf, OtaConfig, OtaEvent, OtaHandle};
use crate::setup::onepassword::{mark_skipped, WEB_STORE_URL};
use crate::setup::{advance_after_chrome_setup, advance_after_onepassword};
use crate::skills::Service;

pub struct AppCore {
    pub guide: Guide,
    pub chrome: Option<ChromeHandle>,
    pub ota: OtaHandle,
    pub ota_playing: bool,
    pub pending: Receiver<String>,
    pub cdp_attach: bool,
    pub chrome_opts: ChromeOpts,
    pub rt: Runtime,
    pub hud_visible: bool,
    pub quit_requested: bool,
    toast_since: Option<Instant>,
}

impl AppCore {
    pub fn new(
        guide: Guide,
        ota: OtaHandle,
        pending: Receiver<String>,
        cdp_attach: bool,
        chrome_opts: ChromeOpts,
        rt: Runtime,
    ) -> Self {
        Self {
            guide,
            chrome: None,
            ota,
            ota_playing: false,
            pending,
            cdp_attach,
            chrome_opts,
            rt,
            hud_visible: true,
            quit_requested: false,
            toast_since: None,
        }
    }

    pub fn show_toast(&mut self, msg: impl Into<String>) {
        self.guide.toast = Some(msg.into());
        self.toast_since = Some(Instant::now());
    }

    fn clear_toast_if_stale(&mut self) {
        if let Some(since) = self.toast_since {
            if since.elapsed() > Duration::from_secs(5) {
                self.guide.toast = None;
                self.toast_since = None;
            }
        }
    }

    pub fn start_chrome(&mut self) -> Result<()> {
        if self.chrome.is_some() {
            return Ok(());
        }
        ensure_chrome_binary(&self.chrome_opts)?;
        let mut chrome = ChromeHandle::start(self.rt.handle().clone(), self.chrome_opts.clone());
        chrome.wait_for_ready(Duration::from_secs(45))?;
        self.chrome = Some(chrome);
        Ok(())
    }

    pub fn tick(&mut self) {
        self.clear_toast_if_stale();
        self.drain_text_commands();
        self.apply_chrome_events();
        self.apply_ota_events();
    }

    fn apply_ota_events(&mut self) {
        for ev in self.ota.poll_events() {
            match ev {
                OtaEvent::Playing { channel } => {
                    self.ota_playing = true;
                    self.hud_visible = false;
                    log::info!("OTA playing {channel}");
                }
                OtaEvent::Stopped => {
                    self.ota_playing = false;
                    self.hud_visible = true;
                }
                OtaEvent::Failed(msg) => {
                    self.ota_playing = false;
                    self.hud_visible = true;
                    self.show_toast(msg.clone());
                    log::warn!("{msg}");
                }
            }
        }
    }

    fn apply_chrome_events(&mut self) {
        if let Some(chrome) = &mut self.chrome {
            for ev in chrome.poll_events() {
                match ev {
                    ChromeEvent::Ready(msg) => {
                        log::info!("{msg}");
                    }
                    ChromeEvent::Opened(_) => {}
                    ChromeEvent::LoginRaised(_) => {}
                    ChromeEvent::HiddenHud => self.hud_visible = false,
                    ChromeEvent::Failed(msg) => {
                        self.show_toast(msg);
                        self.hud_visible = true;
                    }
                }
            }
        }
    }

    fn drain_text_commands(&mut self) {
        while let Ok(line) = self.pending.try_recv() {
            self.run_text(&line);
        }
    }

    pub fn run_text(&mut self, line: &str) {
        match command::parse(line) {
            Ok(cmd) => {
                log::info!("command: {line}");
                self.dispatch(cmd);
            }
            Err(err) => {
                log::warn!("{err}");
                self.guide.status = format!("cmd? {err}");
                self.hud_visible = true;
            }
        }
    }

    pub fn dispatch(&mut self, cmd: Command) {
        match cmd {
            Command::Home => {
                self.guide.close_accounts();
                self.guide.go_home();
                self.hud_visible = true;
            }
            Command::Back => self.back(),
            Command::Pause => {
                self.chrome_send(ChromeCmd::Pause);
                self.guide.status = "pause".into();
            }
            Command::PlayPause => {
                self.chrome_send(ChromeCmd::PlayPause);
                self.guide.status = "play-pause".into();
            }
            Command::Fullscreen => {
                self.chrome_send(ChromeCmd::Fullscreen);
                self.guide.status = "fullscreen".into();
            }
            Command::Play { query, service } => self.play_query(&query, service),
            Command::Search { query, service } => self.search_query(&query, service),
        }
    }

    fn chrome_send(&self, cmd: ChromeCmd) {
        if let Some(chrome) = &self.chrome {
            chrome.send(cmd);
        }
    }

    fn resolve_service(&self, service: Option<Service>) -> Service {
        service
            .or_else(|| self.guide.focused().map(|t| t.service))
            .unwrap_or(Service::Youtube)
    }

    fn play_query(&mut self, query: &str, service: Option<Service>) {
        let service = self.resolve_service(service);
        let url = service
            .watch_url(query)
            .unwrap_or_else(|| service.search_url(query));
        self.open_url(service, query, url);
    }

    fn search_query(&mut self, query: &str, service: Option<Service>) {
        let service = self.resolve_service(service);
        let url = service.search_url(query);
        self.guide.status = format!("search {query} …");
        self.chrome_send(ChromeCmd::Search {
            query: query.to_string(),
            service,
        });
        log::debug!("search -> {url}");
    }

    fn open_url(&mut self, service: Service, title: &str, url: String) {
        self.chrome_send(ChromeCmd::Open {
            url,
            service,
            title: title.to_string(),
        });
    }

    pub fn back(&mut self) {
        if self.guide.command.is_some() {
            self.guide.close_command();
            self.guide.status = "voltar".into();
            return;
        }
        if self.guide.screen == HudScreen::Accounts {
            accounts::mark_onboarding_done();
            self.guide.close_accounts();
            self.guide.status = "guia".into();
            return;
        }
        if self.ota_playing {
            self.ota.stop();
            self.ota_playing = false;
            self.hud_visible = true;
            self.guide.status = "voltar".into();
            return;
        }
        self.hud_visible = true;
        self.chrome_send(ChromeCmd::Back);
        self.chrome_send(ChromeCmd::ShowHud);
        self.guide.status = "voltar".into();
    }

    pub fn activate_focus(&mut self) {
        if matches!(
            self.guide.screen,
            HudScreen::SetupChrome | HudScreen::SetupOnePassword
        ) {
            let browser = detect();
            let idx = self.guide.setup_choice_idx;
            if let Some(choice) = choice_at(self.guide.screen, &browser, idx) {
                match choice.id {
                    "continue" if self.guide.screen == HudScreen::SetupChrome => {
                        if let Err(err) = self.finish_chrome_setup() {
                            self.show_toast(format!("{err:#}"));
                        }
                    }
                    "continue" => self.continue_onepassword(),
                    "retry" => self.retry_browser_setup(),
                    "install" => self.try_install_browser_setup(),
                    "extension" => self.open_onepassword_extension_page(),
                    "skip" => self.skip_onepassword(),
                    _ => {}
                }
            }
            return;
        }
        if self.guide.screen == HudScreen::Accounts {
            if self.guide.is_accounts_done() {
                accounts::mark_onboarding_done();
                self.guide.close_accounts();
                self.guide.status = "guia".into();
                return;
            }
            if let Some((_, service)) = self.guide.focused_account() {
                self.chrome_send(ChromeCmd::Login { service });
            }
            return;
        }
        self.zap();
    }

    fn zap(&mut self) {
        let Some(tile) = self.guide.focused().cloned() else {
            return;
        };
        if is_accounts_url(&tile.url) {
            self.guide.open_accounts();
            return;
        }
        if tile.service == Service::Ota {
            let Some(channel) = crate::catalog::Catalog::ota_channel_name(&tile) else {
                return;
            };
            if !self.ota.enabled {
                self.show_toast("TV ao vivo não configurada.");
                return;
            }
            self.ota.play(channel);
            return;
        }
        let _ = self.guide.catalog.public_meta(&tile.title);
        self.open_url(tile.service, &tile.title, tile.url);
    }

    pub fn move_focus(&mut self, drow: isize, dcol: isize) {
        if matches!(
            self.guide.screen,
            HudScreen::SetupChrome | HudScreen::SetupOnePassword
        ) {
            let browser = detect();
            let n = choice_count(self.guide.screen, &browser) as isize;
            if n > 0 {
                let delta = if dcol != 0 { dcol } else { drow };
                self.guide.setup_choice_idx =
                    ((self.guide.setup_choice_idx as isize + delta).rem_euclid(n)) as usize;
            }
            return;
        }
        if self.guide.screen == HudScreen::Accounts {
            let delta = if drow != 0 { drow } else { dcol };
            self.guide.move_accounts(delta);
            return;
        }
        if self.guide.screen == HudScreen::Guide {
            self.hud_visible = true;
            self.guide.move_by(drow, dcol);
        }
    }

    pub fn retry_browser_setup(&mut self) {
        let browser = detect();
        if browser.found {
            self.guide.toast = None;
            self.toast_since = None;
        } else {
            self.show_toast("Navegador não encontrado.");
        }
        let n = choice_count(HudScreen::SetupChrome, &browser);
        if self.guide.setup_choice_idx >= n {
            self.guide.setup_choice_idx = 0;
        }
    }

    pub fn try_install_browser_setup(&mut self) {
        match try_noninteractive_install() {
            InstallAttempt::Installed => self.show_toast("Chromium instalado."),
            InstallAttempt::NeedsPassword { command } => {
                self.show_toast(format!("No terminal: {command}"));
            }
            InstallAttempt::Manual { command, detail } => {
                self.show_toast(if detail.is_empty() {
                    format!("Instale: {command}")
                } else {
                    detail
                });
            }
        }
        self.retry_browser_setup();
    }

    pub fn open_command_bar(&mut self) {
        self.hud_visible = true;
        self.guide.open_command();
    }

    pub fn play_pause(&mut self) {
        self.chrome_send(ChromeCmd::PlayPause);
        self.guide.status = "play-pause".into();
    }

    pub fn quit(&mut self) {
        self.ota.shutdown();
        self.chrome_send(ChromeCmd::Shutdown);
        self.quit_requested = true;
    }

    pub fn toggle_hud(&mut self) {
        self.hud_visible = !self.hud_visible;
    }

    pub fn finish_chrome_setup(&mut self) -> Result<()> {
        self.start_chrome()?;
        self.guide.set_screen(advance_after_chrome_setup());
        Ok(())
    }

    pub fn skip_onepassword(&mut self) {
        mark_skipped();
        self.guide.set_screen(advance_after_onepassword());
    }

    pub fn continue_onepassword(&mut self) {
        self.guide.set_screen(advance_after_onepassword());
    }

    pub fn open_onepassword_extension_page(&mut self) {
        self.chrome_send(ChromeCmd::Open {
            url: WEB_STORE_URL.into(),
            service: Service::Youtube,
            title: "1Password extension".into(),
        });
        self.show_toast("Instale a extensão e volte aqui.");
    }

    pub fn command_commit(&mut self) {
        if let Some(line) = self.guide.command.take() {
            if line.is_empty() {
                self.guide.close_command();
            } else {
                self.run_text(&line);
            }
        }
    }

    pub fn command_cancel(&mut self) {
        self.guide.close_command();
        self.guide.status = "comando cancelado".into();
    }

    pub fn command_push_char(&mut self, ch: char) {
        if let Some(buf) = &mut self.guide.command {
            if !ch.is_control() {
                buf.push(ch);
            }
        }
    }

    pub fn command_backspace(&mut self) {
        if let Some(buf) = &mut self.guide.command {
            buf.pop();
        }
    }
}

pub fn build_ota(ota_channels: Option<PathBuf>) -> (OtaHandle, Vec<crate::ota::OtaChannel>) {
    let ota_config = OtaConfig::resolve(ota_channels);
    let ota_channels = ota_config
        .as_ref()
        .and_then(|cfg| parse_channels_conf(&cfg.channels_conf).ok())
        .unwrap_or_default();
    let ota = OtaHandle::start(ota_config);
    (ota, ota_channels)
}

pub fn pending_commands(
    say: Option<String>,
    cmd_stdin: bool,
) -> Receiver<String> {
    let (tx, rx) = mpsc::channel();
    if let Some(say) = say {
        let _ = tx.send(say);
    }
    if cmd_stdin {
        std::thread::Builder::new()
            .name("zappe-cmd-stdin".into())
            .spawn(move || {
                for line in std::io::stdin().lines().flatten() {
                    if tx.send(line).is_err() {
                        break;
                    }
                }
            })
            .expect("stdin thread");
    }
    rx
}
