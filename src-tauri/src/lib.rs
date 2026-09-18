mod a11y;
mod branding;
mod catalog;
mod harvest;
mod nest;
mod ota;
mod paths;
mod playback;
mod setup;
mod skill;
mod skills;
mod teach;
mod voice;
mod wm;

use std::sync::Mutex;

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager, RunEvent, State};

use branding::BrandingView;
use catalog::{CatalogStore, CatalogView};
use nest::{NestManager, NestStatus};
use voice::VoiceOutcome;

pub use catalog::ShelfStatus;
pub use harvest::{HarvestOutcome, HarvestRequest};
use ota::{ota_available, OtaChannel};
use playback::{GuideFocus, PlaybackController, PlaybackStatus, PlaybackSurface};
use setup::{load_setup, save_setup, SetupState};
use skill::NETFLIX_CONTINUE_WATCHING_V1;
use skills::Service;
use teach::{TeachMode, TeachState};

struct AppState {
    nest: NestManager,
    ota: ota::OtaSession,
    playback: Mutex<PlaybackController>,
    setup: Mutex<SetupState>,
    catalog: CatalogStore,
    teach: TeachMode,
}

#[derive(Serialize)]
struct ChromeStatus {
    available: bool,
    path: Option<String>,
    profile: String,
    ready: bool,
    launched_by_zappe: bool,
    pids: Vec<u32>,
    gamescope: Option<String>,
    using_gamescope: bool,
}

impl From<NestStatus> for ChromeStatus {
    fn from(s: NestStatus) -> Self {
        Self {
            available: s.available,
            path: s.chrome_path,
            profile: s.profile,
            ready: s.ready,
            launched_by_zappe: s.launched_by_zappe,
            pids: s.pids,
            gamescope: s.gamescope_path,
            using_gamescope: s.using_gamescope,
        }
    }
}

#[tauri::command]
fn get_setup_state(state: State<AppState>) -> SetupState {
    state.setup.lock().unwrap().clone()
}

#[tauri::command]
fn update_setup(state: State<AppState>, patch: SetupState) -> Result<(), String> {
    let mut current = state.setup.lock().unwrap();
    *current = patch;
    save_setup(&current).map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
fn complete_setup(state: State<AppState>) -> Result<(), String> {
    let mut current = state.setup.lock().unwrap();
    current.completed = true;
    save_setup(&current).map_err(|e| e.to_string())?;
    Ok(())
}

#[tauri::command]
async fn chrome_status(state: State<'_, AppState>) -> Result<ChromeStatus, String> {
    Ok(state.nest.status().await.into())
}

#[tauri::command]
fn ota_enabled() -> bool {
    ota_available()
}

#[tauri::command]
fn list_ota_channels() -> Result<Vec<OtaChannel>, String> {
    ota::list_channels().map_err(|e| e.to_string())
}

#[tauri::command]
fn get_catalog(state: State<AppState>) -> CatalogView {
    state.catalog.view(state.teach.view())
}

#[tauri::command]
fn get_branding() -> BrandingView {
    branding::load_branding()
}

#[tauri::command]
fn voice_listen() -> VoiceOutcome {
    voice::listen()
}

#[tauri::command]
fn auto_harvest_enabled() -> bool {
    harvest::auto_harvest_enabled()
}

#[tauri::command]
async fn harvest_now(
    app: AppHandle,
    state: State<'_, AppState>,
    skill_id: Option<String>,
) -> Result<HarvestOutcome, String> {
    let id = skill_id.unwrap_or_else(|| NETFLIX_CONTINUE_WATCHING_V1.to_string());
    let surface = state.playback.lock().unwrap().surface.clone();
    let outcome = harvest::run_harvest(
        &state.nest,
        &state.catalog,
        &state.teach,
        surface,
        HarvestRequest::for_skill(id),
    )
    .await;
    let _ = app.emit("catalog-changed", state.catalog.view(state.teach.view()));
    Ok(outcome)
}

#[tauri::command]
fn begin_teach(app: AppHandle, state: State<AppState>, skill_id: String) -> TeachState {
    let reason = "user asked to teach this path";
    let teach = state.teach.begin(&skill_id, reason);
    let _ = app.emit("catalog-changed", state.catalog.view(state.teach.view()));
    teach
}

#[tauri::command]
fn cancel_teach(app: AppHandle, state: State<AppState>) -> TeachState {
    let teach = state.teach.cancel();
    let _ = app.emit("catalog-changed", state.catalog.view(state.teach.view()));
    teach
}

#[tauri::command]
fn open_app(
    app: AppHandle,
    state: State<'_, AppState>,
    service_id: String,
    focus: GuideFocus,
) -> Result<(), String> {
    let service = Service::parse_id(&service_id).ok_or("unknown service")?;
    let url = service.home_url().to_string();
    let mut playback = state.playback.lock().unwrap();
    playback::begin_nest(&app, &state.nest, &mut *playback, url, focus)?;
    Ok(())
}

#[tauri::command]
fn open_chrome_url(
    app: AppHandle,
    state: State<'_, AppState>,
    service_id: String,
    url: String,
    _title: String,
    focus: GuideFocus,
) -> Result<(), String> {
    let _ = Service::parse_id(&service_id).ok_or("unknown service")?;
    let mut playback = state.playback.lock().unwrap();
    playback::begin_nest(&app, &state.nest, &mut *playback, url, focus)?;
    Ok(())
}

#[tauri::command]
async fn play_ota(
    app: AppHandle,
    state: State<'_, AppState>,
    channel: String,
    conf: String,
    focus: GuideFocus,
) -> Result<(), String> {
    {
        let mut playback = state.playback.lock().unwrap();
        playback.focus_snapshot = Some(focus);
        if let Some(win) = app.get_webview_window("main") {
            let _ = win.hide();
        }
    }
    let conf_path = std::path::PathBuf::from(conf);
    if let Err(err) = state.ota.play(&channel, &conf_path).await {
        playback::restore_guide_fullscreen(&app);
        return Err(err.to_string());
    }
    wm::nudge_mpv_fullscreen(state.ota.latest_mpv_pid());
    let mut playback = state.playback.lock().unwrap();
    playback.surface = PlaybackSurface::Ota;
    let _ = app.emit("playback-changed", playback.status());
    Ok(())
}

#[tauri::command]
fn playback_status(state: State<AppState>) -> PlaybackStatus {
    state.playback.lock().unwrap().status()
}

#[tauri::command]
async fn remote_back(app: AppHandle, state: State<'_, AppState>) -> Result<(), String> {
    let surface = {
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Idle {
        return Ok(());
    }
    playback::stop_playback_surface(&state.nest, &state.ota, surface).await;
    let mut playback = state.playback.lock().unwrap();
    playback::finish_return_to_guide(&app, &mut *playback);
    Ok(())
}

#[tauri::command]
fn remote_play_pause(state: State<'_, AppState>) -> Result<(), String> {
    let playback = state.playback.lock().unwrap();
    playback::toggle_play_pause(&state.nest, &*playback);
    Ok(())
}

#[tauri::command]
async fn remote_home(app: AppHandle, state: State<'_, AppState>) -> Result<(), String> {
    let surface = {
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Idle {
        return Ok(());
    }
    playback::stop_playback_surface(&state.nest, &state.ota, surface).await;
    let mut playback = state.playback.lock().unwrap();
    playback::finish_return_to_guide(&app, &mut *playback);
    Ok(())
}

#[tauri::command]
fn open_onepassword_extension(
    app: AppHandle,
    state: State<'_, AppState>,
    focus: GuideFocus,
) -> Result<(), String> {
    let url = "https://chromewebstore.google.com/detail/1password-%E2%80%93-password-mana/aeblfdkhhhdcdjpifhhbdiojplfjncoa";
    let mut playback = state.playback.lock().unwrap();
    playback::begin_nest(&app, &state.nest, &mut *playback, url.to_string(), focus)?;
    Ok(())
}

async fn return_to_guide(app: &AppHandle) {
    let state = app.state::<AppState>();
    let surface = {
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Idle {
        return;
    }
    playback::stop_playback_surface(&state.nest, &state.ota, surface).await;
    let mut playback = state.playback.lock().unwrap();
    playback::finish_return_to_guide(app, &mut *playback);
}

async fn app_shutdown(app: &AppHandle) {
    return_to_guide(app).await;
    let state = app.state::<AppState>();
    state.nest.shutdown_if_owned().await;
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let _ = env_logger::try_init();
    let setup = load_setup();
    let nest = NestManager::start();
    let catalog = CatalogStore::load();
    let teach = TeachMode::new();

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, _shortcut, event| {
                    use tauri_plugin_global_shortcut::ShortcutState;
                    if event.state == ShortcutState::Pressed {
                        let handle = app.clone();
                        tauri::async_runtime::spawn(async move {
                            return_to_guide(&handle).await;
                        });
                    }
                })
                .build(),
        )
        .setup(|app| {
            use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Shortcut};
            let esc = Shortcut::new(None, Code::Escape);
            let back = Shortcut::new(None, Code::BrowserBack);
            let _ = app.global_shortcut().register(esc);
            let _ = app.global_shortcut().register(back);

            if let Some(win) = app.get_webview_window("main") {
                let _ = win.set_decorations(false);
                let _ = win.set_fullscreen(true);
            }
            wm::nudge_guide_fullscreen(None);
            Ok(())
        })
        .manage(AppState {
            nest,
            ota: ota::OtaSession::new(),
            playback: Mutex::new(PlaybackController::new()),
            setup: Mutex::new(setup),
            catalog,
            teach,
        })
        .invoke_handler(tauri::generate_handler![
            get_setup_state,
            update_setup,
            complete_setup,
            chrome_status,
            ota_enabled,
            list_ota_channels,
            get_catalog,
            get_branding,
            voice_listen,
            auto_harvest_enabled,
            harvest_now,
            begin_teach,
            cancel_teach,
            open_app,
            open_chrome_url,
            play_ota,
            playback_status,
            remote_back,
            remote_play_pause,
            remote_home,
            open_onepassword_extension,
        ])
        .build(tauri::generate_context!())
        .expect("build tauri")
        .run(|app_handle, event| {
            if matches!(event, RunEvent::Exit) {
                let handle = app_handle.clone();
                tauri::async_runtime::block_on(async move {
                    app_shutdown(&handle).await;
                });
            }
        });
}

/// CLI / tests: harvest without starting the Tauri shell.
pub async fn harvest_cli(req: HarvestRequest) -> anyhow::Result<HarvestOutcome> {
    harvest::run_cli(req).await
}
