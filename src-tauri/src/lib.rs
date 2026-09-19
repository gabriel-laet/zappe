mod a11y;
mod atongx;
mod atongx_map;
mod branding;
mod catalog;
mod companion;
mod harvest;
mod hid;
mod nest;
mod nest_input;
mod ota;
mod paths;
pub(crate) mod playback;
pub(crate) mod remote;
mod samsung;
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
use companion::{Hub as CompanionHub, SessionView as CompanionSession};
use nest::{NestManager, NestStatus};
use remote::{remote_mute, remote_volume};
use voice::VoiceOutcome;

pub use catalog::ShelfStatus;
pub use harvest::{HarvestOutcome, HarvestRequest};
use ota::{ota_available, OtaChannel, OtaStatus};
use playback::{GuideFocus, PlaybackController, PlaybackStatus, PlaybackSurface};
use setup::{load_setup, save_setup, SetupState};
use skill::NETFLIX_CONTINUE_WATCHING_V1;
use skills::Service;
use teach::{TeachMode, TeachState};

pub(crate) struct AppState {
    nest: NestManager,
    ota: ota::OtaSession,
    playback: Mutex<PlaybackController>,
    setup: Mutex<SetupState>,
    catalog: CatalogStore,
    teach: TeachMode,
    companion: CompanionHub,
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
async fn companion_session(state: State<'_, AppState>) -> Result<CompanionSession, String> {
    Ok(state.companion.view().await)
}

#[tauri::command]
async fn companion_begin(state: State<'_, AppState>) -> Result<CompanionSession, String> {
    Ok(state.companion.begin().await)
}

#[tauri::command]
async fn companion_select_source(
    state: State<'_, AppState>,
    source: String,
) -> Result<CompanionSession, String> {
    Ok(state.companion.select_source(&source).await)
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
fn ota_status() -> OtaStatus {
    ota::ota_status()
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
async fn voice_listen() -> VoiceOutcome {
    tauri::async_runtime::spawn_blocking(voice::listen)
        .await
        .unwrap_or_else(|err| VoiceOutcome {
            armed: false,
            transcript: None,
            message: format!("voice_listen: {err}"),
        })
}

#[tauri::command]
async fn voice_begin() -> VoiceOutcome {
    tauri::async_runtime::spawn_blocking(voice::begin)
        .await
        .unwrap_or_else(|err| VoiceOutcome {
            armed: false,
            transcript: None,
            message: format!("voice_begin: {err}"),
        })
}

#[tauri::command]
async fn voice_end() -> VoiceOutcome {
    tauri::async_runtime::spawn_blocking(voice::end)
        .await
        .unwrap_or_else(|err| VoiceOutcome {
            armed: false,
            transcript: None,
            message: format!("voice_end: {err}"),
        })
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
    if let Err(err) = state
        .catalog
        .mark_harvesting("continue", &id, "Continue watching")
    {
        log::warn!("catalog harvesting mark failed: {err:#}");
    }
    let _ = app.emit("catalog-changed", state.catalog.view(state.teach.view()));
    let outcome = harvest::run_harvest(
        &state.nest,
        &state.catalog,
        &state.teach,
        surface,
        HarvestRequest::for_skill(id),
    )
    .await;
    // Always emit after harvest (ok / empty / error / teach) so Home shelves refresh.
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
    playback::begin_ota(
        &app,
        &state.nest,
        &state.ota,
        &state.playback,
        channel,
        std::path::PathBuf::from(conf),
        focus,
    )
    .await
}

#[tauri::command]
fn playback_status(state: State<AppState>) -> PlaybackStatus {
    state.playback.lock().unwrap().status()
}

pub(crate) async fn remote_back_inner(app: &AppHandle) -> Result<(), String> {
    let state = app.state::<AppState>();
    let surface = {
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Idle {
        return Ok(());
    }
    playback::stop_playback_surface(&state.nest, &state.ota, surface).await;
    let mut playback = state.playback.lock().unwrap();
    playback::finish_return_to_guide(app, &mut *playback);
    Ok(())
}

#[tauri::command]
async fn remote_back(app: AppHandle) -> Result<(), String> {
    remote_back_inner(&app).await
}

#[tauri::command]
fn remote_play_pause(state: State<'_, AppState>) -> Result<(), String> {
    let playback = state.playback.lock().unwrap();
    playback::toggle_play_pause(&state.nest, &state.ota, &*playback);
    Ok(())
}

/// Air-mouse cursor mode: CSS on the guide, real pointer + click in the nest.
pub(crate) fn remote_pointer_inner(app: &AppHandle) -> bool {
    let Some(on) = nest_input::toggle_pointer_debounced() else {
        return nest_input::pointer_mode();
    };
    let surface = {
        let state = app.state::<AppState>();
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Chrome {
        nest_input::on_pointer_toggled(on);
    }
    let _ = app.emit("guide-pointer", on);
    on
}

#[tauri::command]
fn remote_pointer(app: AppHandle) -> Result<bool, String> {
    Ok(remote_pointer_inner(&app))
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

pub(crate) async fn remote_power_inner(app: &AppHandle) -> Result<String, String> {
    let state = app.state::<AppState>();
    let surface = {
        let playback = state.playback.lock().unwrap();
        playback.surface.clone()
    };
    if surface == PlaybackSurface::Idle {
        log::info!("ATONGX power on guide (not shutting down)");
        return Ok("power:idle".into());
    }
    log::info!("ATONGX power — stop playback, return to guide");
    playback::stop_playback_surface(&state.nest, &state.ota, surface).await;
    let mut playback = state.playback.lock().unwrap();
    playback::finish_return_to_guide(app, &mut *playback);
    Ok("power:home".into())
}

/// Power never shuts the box down. If something is playing, return to the guide.
#[tauri::command]
async fn remote_power(app: AppHandle) -> Result<String, String> {
    remote_power_inner(&app).await
}

/// Menu opens Connect. If the nest / mpv is up, return to the guide first.
#[tauri::command]
async fn remote_menu(app: AppHandle) -> Result<String, String> {
    log::info!("ATONGX menu");
    return_to_guide(&app).await;
    let _ = app.emit("guide-menu", ());
    Ok("menu".into())
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

pub(crate) async fn return_to_guide(app: &AppHandle) {
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
    let companion = CompanionHub::new(nest.clone());
    let companion_boot = companion.clone();
    tauri::async_runtime::spawn(async move {
        companion::boot(companion_boot).await;
    });

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(atongx::plugin())
        .setup(|app| {
            atongx::register(app.handle());
            hid::start(app.handle().clone());
            tauri::async_runtime::spawn(async {
                crate::samsung::startup_probe().await;
            });
            if let Err(err) = skill::install_bundled_skills() {
                log::warn!("could not plant bundled harvest skills: {err:#}");
            }

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
            companion,
        })
        .invoke_handler(tauri::generate_handler![
            get_setup_state,
            update_setup,
            complete_setup,
            chrome_status,
            companion_session,
            companion_begin,
            companion_select_source,
            ota_enabled,
            list_ota_channels,
            ota_status,
            get_catalog,
            get_branding,
            voice_listen,
            voice_begin,
            voice_end,
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
            remote_pointer,
            remote_home,
            remote_volume,
            remote_mute,
            remote_power,
            remote_menu,
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

/// Standalone phone companion (`zappe-companion`).
pub async fn run_companion() -> anyhow::Result<()> {
    companion::run_standalone().await
}
