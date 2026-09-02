//! Zappe — native living-room launcher.
//!
//! HUD: winit + wgpu. Playback: a real Chrome process over CDP.

mod catalog;
mod chrome;
mod hud;
mod skills;

use std::sync::Arc;
use std::time::Instant;

use anyhow::Result;
use clap::Parser;
use winit::application::ApplicationHandler;
use winit::event::{ElementState, KeyEvent, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{KeyCode, PhysicalKey};
use winit::window::{Window, WindowAttributes, WindowId, WindowLevel};

use catalog::Catalog;
use chrome::{ChromeCmd, ChromeEvent, ChromeHandle, ChromeOpts};
use hud::{Gpu, Guide};
use skills::Service;

#[derive(Parser, Debug)]
#[command(
    name = "zappe",
    about = "Personal living-room launcher. Native wgpu HUD + real Chrome via CDP."
)]
struct Args {
    /// Spawn (or attach) the dedicated Zappe Chrome profile over CDP.
    /// Without this flag the HUD still runs if Chrome is missing.
    #[arg(long, env = "ZAPPE_CHROME")]
    chrome: bool,

    /// Attach to an existing DevTools HTTP endpoint instead of launching.
    /// Example: http://127.0.0.1:9222
    #[arg(long, env = "ZAPPE_CDP")]
    cdp: Option<String>,

    /// Service whose home URL is opened when Chrome starts.
    #[arg(long, value_enum, default_value_t = Service::Netflix)]
    service: Service,

    /// Override the first-party URL Chrome should open (still your profile, still Chrome).
    #[arg(long)]
    url: Option<String>,
}

struct App {
    window: Option<Arc<Window>>,
    gpu: Option<Gpu>,
    guide: Guide,
    chrome: ChromeHandle,
    started: Instant,
}

impl App {
    fn apply_chrome_events(&mut self) {
        for ev in self.chrome.poll_events() {
            match ev {
                ChromeEvent::Ready(msg) => {
                    self.guide.chrome_line = format!("CHROME: on  {msg}");
                    log::info!("{msg}");
                }
                ChromeEvent::Opened(url) => {
                    self.guide.status = format!("opened {url}");
                }
                ChromeEvent::HiddenHud => {
                    self.set_hud_visible(false);
                }
                ChromeEvent::Failed(msg) => {
                    self.guide.chrome_line = format!("CHROME: fail");
                    self.guide.status = msg;
                    self.set_hud_visible(true);
                }
                ChromeEvent::MissingChrome => {
                    self.guide.chrome_line = "CHROME: missing (HUD only)".into();
                    self.guide.status =
                        "chrome binary not found — HUD still up. install chrome or set CHROME_PATH"
                            .into();
                }
            }
        }
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

    fn zap(&mut self) {
        let Some(tile) = self.guide.focused().cloned() else {
            return;
        };
        if !self.chrome.enabled {
            self.guide.status = format!(
                "would open {} on {} (pass --chrome)",
                tile.title,
                tile.service.label()
            );
            log::info!("{}", self.guide.status);
            return;
        }
        self.guide.status = format!("zapping {} …", tile.title);
        self.chrome.send(ChromeCmd::Open {
            url: tile.url,
            service: tile.service,
            title: tile.title,
        });
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        let attrs = WindowAttributes::default()
            .with_title("ZAPPE")
            .with_inner_size(winit::dpi::LogicalSize::new(1280.0, 800.0))
            .with_window_level(WindowLevel::AlwaysOnTop);
        match event_loop.create_window(attrs) {
            Ok(window) => {
                let window = Arc::new(window);
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
                self.chrome.send(ChromeCmd::Shutdown);
                event_loop.exit();
            }
            WindowEvent::Resized(size) => {
                if let Some(gpu) = &mut self.gpu {
                    gpu.resize(size.width, size.height);
                }
            }
            WindowEvent::RedrawRequested => {
                self.apply_chrome_events();
                if let Some(gpu) = &mut self.gpu {
                    if let Err(err) = gpu.render(&self.guide, self.started) {
                        log::error!("render: {err:#}");
                    }
                }
            }
            WindowEvent::KeyboardInput {
                event:
                    KeyEvent {
                        state: ElementState::Pressed,
                        repeat: false,
                        physical_key: PhysicalKey::Code(code),
                        ..
                    },
                ..
            } => match code {
                KeyCode::Escape => {
                    self.set_hud_visible(true);
                    self.chrome.send(ChromeCmd::ShowHud);
                }
                KeyCode::KeyQ => {
                    self.chrome.send(ChromeCmd::Shutdown);
                    event_loop.exit();
                }
                KeyCode::ArrowUp => self.guide.move_by(-1, 0),
                KeyCode::ArrowDown => self.guide.move_by(1, 0),
                KeyCode::ArrowLeft => self.guide.move_by(0, -1),
                KeyCode::ArrowRight => self.guide.move_by(0, 1),
                KeyCode::Enter | KeyCode::NumpadEnter => self.zap(),
                KeyCode::Space => self.chrome.send(ChromeCmd::Pause),
                KeyCode::KeyF => self.chrome.send(ChromeCmd::Fullscreen),
                KeyCode::Backspace => self.chrome.send(ChromeCmd::Back),
                KeyCode::KeyH => self.set_hud_visible(self.guide.hidden),
                _ => {}
            },
            _ => {}
        }
    }

    fn about_to_wait(&mut self, _event_loop: &ActiveEventLoop) {
        self.apply_chrome_events();
        if let Some(window) = &self.window {
            window.request_redraw();
        }
    }
}

fn main() -> Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("zappe=info"))
        .init();
    let args = Args::parse();

    let rt = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()?;
    let _enter = rt.enter();

    let initial_url = args.url.or_else(|| {
        if args.chrome {
            Some(args.service.home_url().to_string())
        } else {
            None
        }
    });

    let chrome = ChromeHandle::start(
        rt.handle().clone(),
        ChromeOpts {
            enabled: args.chrome || args.cdp.is_some(),
            cdp: args.cdp,
            initial_url,
            service: args.service,
        },
    );

    let mut guide = Guide::new(Catalog::placeholder());
    guide.chrome_line = if chrome.enabled {
        "CHROME: attaching…".into()
    } else {
        "CHROME: off".into()
    };

    let event_loop = EventLoop::new()?;
    event_loop.set_control_flow(ControlFlow::Poll);
    let mut app = App {
        window: None,
        gpu: None,
        guide,
        chrome,
        started: Instant::now(),
    };
    event_loop.run_app(&mut app)?;
    Ok(())
}
