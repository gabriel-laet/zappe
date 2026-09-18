//! ATONGX volume / mute via PulseAudio (`pactl`).
//! Power and menu stay in `lib.rs` so they can talk to playback / the guide.
//! Never power the box off from here.

use std::process::Command;

use log::info;

fn pactl(args: &[&str]) -> Result<String, String> {
    let out = Command::new("pactl")
        .args(args)
        .output()
        .map_err(|err| format!("pactl: {err}"))?;
    if !out.status.success() {
        return Err(format!(
            "pactl failed: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        ));
    }
    Ok(String::from_utf8_lossy(&out.stdout).trim().to_string())
}

#[tauri::command]
pub fn remote_volume(delta: i32) -> Result<String, String> {
    let step = if delta >= 0 { "+5%" } else { "-5%" };
    match pactl(&["set-sink-volume", "@DEFAULT_SINK@", step]) {
        Ok(_) => {
            let now = pactl(&["get-sink-volume", "@DEFAULT_SINK@"]).unwrap_or_default();
            info!("ATONGX volume {step} {now}");
            Ok(if delta >= 0 {
                "volume:+".into()
            } else {
                "volume:-".into()
            })
        }
        Err(err) => {
            info!("ATONGX volume {step} (no pactl: {err})");
            Ok(if delta >= 0 {
                "volume:+".into()
            } else {
                "volume:-".into()
            })
        }
    }
}

#[tauri::command]
pub fn remote_mute() -> Result<String, String> {
    match pactl(&["set-sink-mute", "@DEFAULT_SINK@", "toggle"]) {
        Ok(_) => {
            info!("ATONGX mute toggled");
            Ok("mute".into())
        }
        Err(err) => {
            info!("ATONGX mute (no pactl: {err})");
            Ok("mute".into())
        }
    }
}
