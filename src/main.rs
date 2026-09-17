//! Zappe — native living-room launcher.
//!
//! HUD: winit + wgpu. Streaming: zappe-owned Chrome over CDP. OTA: dvbv5-zap + mpv.

mod accounts;
mod catalog;
mod chrome;
mod command;
mod hud;
mod ota;
mod skills;
mod theme;
mod voice;

use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::mpsc::{self, Receiver};
use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::Result;
use clap::Parser;
use winit::application::ApplicationHandler;
use winit::event::{ElementState, Ime, KeyEvent, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{Key, KeyCode, NamedKey, PhysicalKey};
use winit::window::{Window, WindowAttributes, WindowId, WindowLevel};

use accounts::is_accounts_url;
use catalog::Catalog;
use chrome::{ensure_chrome_binary, ChromeCmd, ChromeEvent, ChromeHandle, ChromeOpts};
use command::Command;
use hud::{Gpu, Guide, HudScreen};
use ota::{parse_channels_conf, OtaConfig, OtaEvent, OtaHandle};
use skills::Service;
use voice::WhisperHook;

#[derive(Parser, Debug)]
#[command(
    name = "zappe",
    about = "Living-room launcher: wgpu HUD + zappe-owned Chrome + optional OTA TV."
)]
struct Args {
    /// Debug only: attach to an existing DevTools endpoint instead of launching Chrome.
    #[arg(long, env = "ZAPPE_CDP", hide_env_values = true)]
    cdp: Option<String>,

    #[arg(long, value_enum, default_value_t = Service::Netflix)]
    service: Service,

    #[arg(long)]
    url: Option<String>,

    #[arg(long)]
    say: Option<String>,

    #[arg(long, env = "ZAPPE_CMD_STDIN")]
    cmd_stdin: bool,

    #[arg(long, env = "ZAPPE_WHISPER")]
    whisper: bool,

    #[arg(long, env = "ZAPPE_OTA_CHANNELS")]
    ota_channels: Option<PathBuf>,
}

struct App {
    window: Option<Arc<Window>>,
    gpu: Option<Gpu>,
    guide: Guide,
    chrome: ChromeHandle,
    ota: OtaHandle,
    ota_playing: bool,
    started: Instant,
    pending: Receiver<String>,
}

impl App {
    fn apply_ota_events(&mut self) {
        for ev in self.ota.poll_events() {
            match ev {
                OtaEvent::Playing { channel } => {
                    self.ota_playing = true;
                    self.guide.status = format!("OTA · {channel}");
                    self.guide.ota_line = format!("OTA · {channel}");
                    self.set_hud_visible(false);
                }
                OtaEvent::Stopped => {
                    self.ota_playing = false;
                    self.guide.ota_line = "OTA · ready".into();
                    self.set_hud_visible(true);
                    self.guide.status = "OTA stopped".into();
                }
                OtaEvent::Failed(msg) => {
                    self.ota_playing = false;
                    self.guide.status = msg.clone();
                    self.guide.ota_line = "OTA · error".into();
                    self.set_hud_visible(true);
                    log::warn!("{msg}");
                }
            }
        }
    }

    fn apply_chrome_events(&mut self) {
        for ev in self.chrome.poll_events() {
            match ev {
                ChromeEvent::Ready(msg) => {
                    self.guide.chrome_line = format!("Chrome · ready ({msg})");
                    log::info!("{msg}");
                }
                ChromeEvent::Opened(url) => {
                    self.guide.status = format!("Playing · {url}");
                }
                ChromeEvent::LoginRaised(service) => {
                    self.guide.status = format!(
                        "Sign in to {} in the Chrome window · Back when done",
                        service.label()
                    );
                }
                ChromeEvent::HiddenHud => self.set_hud_visible(false),
                ChromeEvent::Failed(msg) => {
                    self.guide.chrome_line = "Chrome · error".into();
                    self.guide.status = msg;
                    self.set_hud_visible(true);
                }
            }
        }
    }

    fn drain_text_commands(&mut self) {
        while let Ok(line) = self.pending.try_recv() {
            self.run_text(&line);
        }
    }

    fn run_text(&mut self, line: &str) {
        match command::parse(line) {
            Ok(cmd) => {
                log::info!("command: {line}");
                self.dispatch(cmd);
            }
            Err(err) => {
                log::warn!("{err}");
                self.guide.status = format!("cmd? {err}");
                self.set_hud_visible(true);
            }
        }
    }

    fn dispatch(&mut self, cmd: Command) {
        match cmd {
            Command::Home => {
                self.guide.close_accounts();
                self.guide.go_home();
                self.set_hud_visible(true);
            }
            Command::Back => self.back(),
            Command::Pause => {
                self.chrome.send(ChromeCmd::Pause);
                self.guide.status = "pause".into();
            }
            Command::PlayPause => {
                self.chrome.send(ChromeCmd::PlayPause);
                self.guide.status = "play-pause".into();
            }
            Command::Fullscreen => {
                self.chrome.send(ChromeCmd::Fullscreen);
                self.guide.status = "fullscreen".into();
            }
            Command::Play { query, service } => self.play_query(&query, service),
            Command::Search { query, service } => self.search_query(&query, service),
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
        self.chrome.send(ChromeCmd::Search {
            query: query.to_string(),
            service,
        });
        log::debug!("search -> {url}");
    }

    fn open_url(&mut self, service: Service, title: &str, url: String) {
        self.guide.status = format!("Opening {title} …");
        self.chrome.send(ChromeCmd::Open {
            url,
            service,
            title: title.to_string(),
        });
    }

    fn set_hud_visible(&mut self, visible: bool) {
        self.guide.hidden = !visible;
        if let Some(window) = &self.window {
            window.set_visible(visible);
            if visible {
                window.focus_window();
            }
        }
    }

    fn back(&mut self) {
        if self.guide.command.is_some() {
            self.guide.close_command();
            self.guide.status = "back".into();
            return;
        }
        if self.guide.screen == HudScreen::Accounts {
            accounts::mark_onboarding_done();
            self.guide.close_accounts();
            self.guide.status = "guide".into();
            return;
        }
        if self.ota_playing {
            self.ota.stop();
            self.ota_playing = false;
            self.set_hud_visible(true);
            self.guide.status = "back".into();
            return;
        }
        self.set_hud_visible(true);
        self.chrome.send(ChromeCmd::Back);
        self.chrome.send(ChromeCmd::ShowHud);
        self.guide.status = "back".into();
    }

    fn activate_focus(&mut self) {
        if self.guide.screen == HudScreen::Accounts {
            if self.guide.is_accounts_done() {
                accounts::mark_onboarding_done();
                self.guide.close_accounts();
                self.guide.status = "guide".into();
                return;
            }
            if let Some((_, service)) = self.guide.focused_account() {
                self.chrome.send(ChromeCmd::Login { service });
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
            let Some(channel) = catalog::Catalog::ota_channel_name(&tile) else {
                return;
            };
            if !self.ota.enabled {
                self.guide.status =
                    "OTA not configured — set ZAPPE_OTA_CHANNELS or ~/tv/channels.conf".into();
                return;
            }
            self.guide.status = format!("Tuning {channel} …");
            self.ota.play(channel);
            return;
        }
        let _ = self.guide.catalog.public_meta(&tile.title);
        self.open_url(tile.service, &tile.title, tile.url);
    }

    fn handle_remote(&mut self, event: &KeyEvent, event_loop: &ActiveEventLoop) {
        let code = match event.physical_key {
            PhysicalKey::Code(code) => Some(code),
            _ => None,
        };
        let named = match &event.logical_key {
            Key::Named(name) => Some(*name),
            _ => None,
        };

        if self.guide.command.is_some() {
            if matches!(code, Some(KeyCode::Enter | KeyCode::NumpadEnter))
                || matches!(named, Some(NamedKey::Enter))
            {
                if let Some(line) = self.guide.command.take() {
                    if line.is_empty() {
                        self.guide.close_command();
                    } else {
                        self.run_text(&line);
                    }
                }
                return;
            }
            if matches!(code, Some(KeyCode::Escape)) || matches!(named, Some(NamedKey::Escape)) {
                self.guide.close_command();
                self.guide.status = "cmd cancelled".into();
                return;
            }
            if matches!(code, Some(KeyCode::Backspace))
                || matches!(named, Some(NamedKey::Backspace))
            {
                if let Some(buf) = &mut self.guide.command {
                    buf.pop();
                }
                return;
            }
            if let Some(text) = event.text.as_ref() {
                if let Some(buf) = &mut self.guide.command {
                    for ch in text.chars() {
                        if !ch.is_control() {
                            buf.push(ch);
                        }
                    }
                }
            }
            return;
        }

        if self.guide.screen == HudScreen::Accounts {
            if is_arrow(code, named, NamedKey::ArrowLeft, KeyCode::ArrowLeft) {
                self.guide.move_accounts(-1);
            } else if is_arrow(code, named, NamedKey::ArrowRight, KeyCode::ArrowRight) {
                self.guide.move_accounts(1);
            } else if is_arrow(code, named, NamedKey::ArrowUp, KeyCode::ArrowUp) {
                self.guide.move_accounts(-1);
            } else if is_arrow(code, named, NamedKey::ArrowDown, KeyCode::ArrowDown) {
                self.guide.move_accounts(1);
            } else if matches!(code, Some(KeyCode::Enter | KeyCode::NumpadEnter))
                || matches!(named, Some(NamedKey::Enter))
            {
                self.activate_focus();
            } else if matches!(
                code,
                Some(KeyCode::Escape | KeyCode::Backspace | KeyCode::BrowserBack)
            ) || matches!(
                named,
                Some(NamedKey::Escape | NamedKey::Backspace | NamedKey::BrowserBack | NamedKey::GoBack)
            ) {
                self.back();
            } else if matches!(code, Some(KeyCode::Home | KeyCode::BrowserHome))
                || matches!(named, Some(NamedKey::Home | NamedKey::BrowserHome))
            {
                accounts::mark_onboarding_done();
                self.guide.close_accounts();
                self.guide.go_home();
            }
            return;
        }

        if is_arrow(code, named, NamedKey::ArrowUp, KeyCode::ArrowUp) {
            self.set_hud_visible(true);
            self.guide.move_by(-1, 0);
        } else if is_arrow(code, named, NamedKey::ArrowDown, KeyCode::ArrowDown) {
            self.set_hud_visible(true);
            self.guide.move_by(1, 0);
        } else if is_arrow(code, named, NamedKey::ArrowLeft, KeyCode::ArrowLeft) {
            self.set_hud_visible(true);
            self.guide.move_by(0, -1);
        } else if is_arrow(code, named, NamedKey::ArrowRight, KeyCode::ArrowRight) {
            self.set_hud_visible(true);
            self.guide.move_by(0, 1);
        } else if matches!(code, Some(KeyCode::Enter | KeyCode::NumpadEnter))
            || matches!(named, Some(NamedKey::Enter))
        {
            self.activate_focus();
        } else if matches!(
            code,
            Some(KeyCode::Escape | KeyCode::Backspace | KeyCode::BrowserBack)
        ) || matches!(
            named,
            Some(NamedKey::Escape | NamedKey::Backspace | NamedKey::BrowserBack | NamedKey::GoBack)
        ) {
            self.back();
        } else if matches!(code, Some(KeyCode::Home | KeyCode::BrowserHome))
            || matches!(named, Some(NamedKey::Home | NamedKey::BrowserHome))
        {
            self.guide.go_home();
            self.set_hud_visible(true);
        } else if matches!(code, Some(KeyCode::Space | KeyCode::MediaPlayPause))
            || matches!(named, Some(NamedKey::MediaPlayPause | NamedKey::MediaPlay))
        {
            self.chrome.send(ChromeCmd::PlayPause);
            self.guide.status = "play-pause".into();
        } else if matches!(code, Some(KeyCode::KeyP)) {
            self.chrome.send(ChromeCmd::Play);
        } else if matches!(code, Some(KeyCode::KeyF)) {
            self.chrome.send(ChromeCmd::Fullscreen);
        } else if matches!(code, Some(KeyCode::Slash)) {
            self.set_hud_visible(true);
            self.guide.open_command();
        } else if matches!(code, Some(KeyCode::KeyQ)) {
            self.ota.shutdown();
            self.chrome.send(ChromeCmd::Shutdown);
            event_loop.exit();
        } else if matches!(code, Some(KeyCode::KeyH)) {
            self.set_hud_visible(self.guide.hidden);
        }
    }
}

fn is_arrow(
    code: Option<KeyCode>,
    named: Option<NamedKey>,
    named_want: NamedKey,
    code_want: KeyCode,
) -> bool {
    code == Some(code_want) || named == Some(named_want)
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        let attrs = WindowAttributes::default()
            .with_title("Zappe")
            .with_inner_size(winit::dpi::LogicalSize::new(1280.0, 900.0))
            .with_window_level(WindowLevel::AlwaysOnTop);
        match event_loop.create_window(attrs) {
            Ok(window) => {
                let window = Arc::new(window);
                window.set_ime_allowed(true);
                match pollster::block_on(Gpu::new(window.clone())) {
                    Ok(gpu) => {
                        self.gpu = Some(gpu);
                        self.window = Some(window);
                    }
                    Err(err) => {
                        log::error!("failed to start wgpu HUD: {err:#}");
                        event_loop.exit();
                    }
                }
            }
            Err(err) => {
                log::error!("failed to create window: {err}");
                event_loop.exit();
            }
        }
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => {
                self.ota.shutdown();
                self.chrome.send(ChromeCmd::Shutdown);
                event_loop.exit();
            }
            WindowEvent::Resized(size) => {
                if let Some(gpu) = &mut self.gpu {
                    gpu.resize(size.width, size.height);
                }
            }
            WindowEvent::RedrawRequested => {
                self.drain_text_commands();
                self.apply_chrome_events();
                self.apply_ota_events();
                if let Some(gpu) = &mut self.gpu {
                    if let Err(err) = gpu.render(&self.guide, self.started) {
                        log::error!("render: {err:#}");
                    }
                }
            }
            WindowEvent::Ime(Ime::Commit(text)) => {
                if let Some(buf) = &mut self.guide.command {
                    buf.push_str(&text);
                }
            }
            WindowEvent::KeyboardInput { event, .. } => {
                if event.state == ElementState::Pressed {
                    self.handle_remote(&event, event_loop);
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        self.drain_text_commands();
        self.apply_chrome_events();
        self.apply_ota_events();
        if let Some(window) = &self.window {
            window.request_redraw();
        }
    }
}

fn main() -> ExitCode {
    if let Err(err) = run() {
        eprintln!("zappe: {err:#}");
        ExitCode::FAILURE
    } else {
        ExitCode::SUCCESS
    }
}

fn run() -> Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("zappe=info"))
        .init();
    let args = Args::parse();

    let whisper = WhisperHook::from_env();
    if args.whisper {
        if whisper.armed() {
            log::info!("whisper hook armed — grammar-only commands");
        } else {
            log::warn!("whisper requested but ZAPPE_WHISPER_BIN not set");
        }
    }

    let chrome_opts = ChromeOpts {
        cdp_attach: args.cdp.clone(),
        initial_url: args.url.clone(),
        service: args.service,
    };
    ensure_chrome_binary(&chrome_opts)?;

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    let _enter = rt.enter();

    let mut chrome = ChromeHandle::start(rt.handle().clone(), chrome_opts);
    chrome.wait_for_ready(Duration::from_secs(45))?;

    let ota_config = OtaConfig::resolve(args.ota_channels.clone());
    let ota_channels = ota_config
        .as_ref()
        .and_then(|cfg| parse_channels_conf(&cfg.channels_conf).ok())
        .unwrap_or_default();
    let ota = OtaHandle::start(ota_config);

    let mut guide = Guide::new(Catalog::with_ota(&ota_channels));
    guide.chrome_line = "Chrome · ready".into();
    guide.ota_line = if ota.enabled {
        format!("OTA · {} channels", ota_channels.len())
    } else {
        "OTA · off".into()
    };

    let pending = {
        let (tx, rx) = mpsc::channel();
        if let Some(say) = args.say {
            let _ = tx.send(say);
        }
        if args.cmd_stdin {
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
    };

    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);
    let mut app = App {
        window: None,
        gpu: None,
        guide,
        chrome,
        ota,
        ota_playing: false,
        started: Instant::now(),
        pending,
    };
    event_loop.run_app(&mut app)?;
    Ok(())
}
