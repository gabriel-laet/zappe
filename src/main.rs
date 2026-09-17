//! Zappe — living-room launcher (Linux + Qt6 HUD).

mod accounts;
mod app;
mod catalog;
mod chrome;
mod command;
mod guide;
mod ota;
mod qt;
mod setup;
mod skills;
mod theme;
mod voice;

use std::process::ExitCode;
use anyhow::Result;
use clap::Parser;
use cxx_qt_lib::{QGuiApplication, QQmlApplicationEngine, QUrl};
use tokio::runtime::Runtime;

use app::{build_ota, pending_commands, AppCore};
use catalog::Catalog;
use chrome::{ChromeOpts, ensure_chrome_binary};
use guide::{Guide, HudScreen};
use qt::zappe_backend::APP_CORE;
use setup::initial_screen;
use skills::Service;
use voice::WhisperHook;

#[derive(Parser, Debug)]
#[command(
    name = "zappe",
    about = "Living-room launcher: Qt HUD + zappe-owned Chrome + optional OTA TV."
)]
struct Args {
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
    ota_channels: Option<std::path::PathBuf>,
}

fn main() -> ExitCode {
    if !cfg!(target_os = "linux") {
        eprintln!("zappe: HUD Qt é apenas Linux (Omarchy/Wayland). Use Linux para o guia.");
        return ExitCode::FAILURE;
    }
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
            log::info!("whisper hook armed");
        } else {
            log::warn!("whisper requested but ZAPPE_WHISPER_BIN not set");
        }
    }

    let cdp_attach = args.cdp.is_some();
    let chrome_opts = ChromeOpts {
        cdp_attach: args.cdp.clone(),
        initial_url: args.url.clone(),
        service: args.service,
    };

    if cdp_attach {
        ensure_chrome_binary(&chrome_opts)?;
    }

    let rt = Runtime::new()?;
    let (ota, ota_channels) = build_ota(args.ota_channels.clone());
    let screen = initial_screen(cdp_attach);
    let mut guide = Guide::new(Catalog::with_ota(&ota_channels), screen);
    guide.ota_line = if ota.enabled {
        format!("OTA · {} canais", ota_channels.len())
    } else {
        "OTA · off".into()
    };

    let pending = pending_commands(args.say, args.cmd_stdin);
    let mut core = AppCore::new(guide, ota, pending, cdp_attach, chrome_opts, rt);

    if screen != HudScreen::SetupChrome {
        core.start_chrome()?;
    } else {
        core.guide.chrome_line = "Chrome · aguardando setup".into();
    }

    APP_CORE
        .lock()
        .map_err(|_| anyhow::anyhow!("APP_CORE poisoned"))?
        .replace(core);

    let mut app = QGuiApplication::new();
    let mut engine = QQmlApplicationEngine::new();
    if let Some(engine) = engine.as_mut() {
        engine.load(&QUrl::from("qrc:/qt/qml/com/zappe/app/qml/main.qml"));
    }

    if let Some(app) = app.as_mut() {
        app.exec();
    }

    Ok(())
}
