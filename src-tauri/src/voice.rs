//! Red-mic hook: whisper.cpp in Portuguese (pt-BR). Not a chat LLM.

use std::path::PathBuf;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
pub struct VoiceOutcome {
    pub armed: bool,
    pub transcript: Option<String>,
    pub message: String,
}

fn whisper_bin() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("ZAPPE_WHISPER_BIN") {
        let p = PathBuf::from(explicit);
        if p.exists() {
            return Some(p);
        }
    }
    which("whisper-cli").or_else(|| which("whisper"))
}

fn which(name: &str) -> Option<PathBuf> {
    let output = Command::new("which").arg(name).output().ok()?;
    if !output.status.success() {
        return None;
    }
    let line = String::from_utf8_lossy(&output.stdout);
    let line = line.trim();
    if line.is_empty() {
        None
    } else {
        Some(PathBuf::from(line))
    }
}

pub fn listen() -> VoiceOutcome {
    if let Ok(fake) = std::env::var("ZAPPE_VOICE_FAKE") {
        let text = fake.trim().to_string();
        if !text.is_empty() {
            return VoiceOutcome {
                armed: true,
                transcript: Some(text.clone()),
                message: text,
            };
        }
    }

    let Some(bin) = whisper_bin() else {
        return VoiceOutcome {
            armed: false,
            transcript: None,
            message: "Whisper pt-BR não está neste aparelho. Defina ZAPPE_WHISPER_BIN.".to_string(),
        };
    };

    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let wav = std::env::temp_dir().join(format!("zappe-voice-{stamp}.wav"));

    let rec = Command::new("arecord")
        .args(["-d", "4", "-f", "cd", "-q"])
        .arg(&wav)
        .status();
    match rec {
        Ok(status) if status.success() && wav.is_file() => {}
        Ok(_) | Err(_) => {
            let _ = std::fs::remove_file(&wav);
            return VoiceOutcome {
                armed: true,
                transcript: None,
                message: "Microfone indisponível (arecord).".to_string(),
            };
        }
    }

    let output = Command::new(&bin)
        .args(["-nt", "-l", "pt", "-f"])
        .arg(&wav)
        .output();
    let _ = std::fs::remove_file(&wav);

    match output {
        Ok(out) if out.status.success() => {
            let text = String::from_utf8_lossy(&out.stdout).trim().to_string();
            if text.is_empty() {
                VoiceOutcome {
                    armed: true,
                    transcript: None,
                    message: "Não entendi. Tente: início, voltar, Netflix.".to_string(),
                }
            } else {
                VoiceOutcome {
                    armed: true,
                    transcript: Some(text.clone()),
                    message: text,
                }
            }
        }
        Ok(out) => VoiceOutcome {
            armed: true,
            transcript: None,
            message: format!(
                "whisper.cpp falhou: {}",
                String::from_utf8_lossy(&out.stderr).trim()
            ),
        },
        Err(err) => VoiceOutcome {
            armed: true,
            transcript: None,
            message: format!("whisper.cpp: {err}"),
        },
    }
}
