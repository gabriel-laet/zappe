//! ATONGX buttons that are classified in the guide but not fully wired yet.
//! Next PR: pactl / Hyprland / HID air-mouse. Do not power the box off from here.

use log::info;

#[tauri::command]
pub fn remote_volume(delta: i32) -> Result<String, String> {
    info!("ATONGX stub remote_volume delta={delta}");
    Ok(format!("volume:{delta}"))
}

#[tauri::command]
pub fn remote_mute() -> Result<String, String> {
    info!("ATONGX stub remote_mute");
    Ok("mute".into())
}

#[tauri::command]
pub fn remote_power() -> Result<String, String> {
    info!("ATONGX stub remote_power (not shutting down)");
    Ok("power".into())
}

#[tauri::command]
pub fn remote_menu() -> Result<String, String> {
    info!("ATONGX stub remote_menu");
    Ok("menu".into())
}
