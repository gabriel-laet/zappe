//! Red-mic hook: local whisper.cpp in Portuguese (pt-BR). Not a chat LLM.
//!
//! Press / hold the ATONGX red mic → `arecord` → whisper.cpp (or an opt-in
//! cloud URL) → constrained grammar in the guide. Default is offline.

use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use serde::Serialize;

use crate::paths;

const DEFAULT_SECONDS: u32 = 5;
const MAX_SECONDS: u32 = 8;
const MIN_WAV_BYTES: u64 = 1600;
const PT_BR_PROMPT: &str =
    "Abrir Netflix. Voltar. Início. Volume mais. Volume menos. Mudo. Ir para Globo. Canal Record. Canal SBT. Sincronizar.";

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
pub struct VoiceOutcome {
    pub armed: bool,
    pub transcript: Option<String>,
    pub message: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WhisperKind {
    Cpp,
    Raw,
    Cloud,
}

struct Capture {
    child: Child,
    wav: PathBuf,
    started: Instant,
}

static CAPTURE: Mutex<Option<Capture>> = Mutex::new(None);

pub fn whisper_dir() -> PathBuf {
    paths::data_dir().join("whisper")
}

pub fn listen_seconds() -> u32 {
    let raw = std::env::var("ZAPPE_VOICE_SECONDS").unwrap_or_default();
    raw.trim()
        .parse::<u32>()
        .ok()
        .map(|n| n.clamp(2, MAX_SECONDS))
        .unwrap_or(DEFAULT_SECONDS)
}

pub fn whisper_kind() -> WhisperKind {
    let kind = std::env::var("ZAPPE_WHISPER_KIND")
        .unwrap_or_default()
        .to_ascii_lowercase();
    if !cloud_url().is_empty() || kind == "cloud" {
        return WhisperKind::Cloud;
    }
    if kind == "raw" || kind == "faster" || kind == "faster-whisper" {
        return WhisperKind::Raw;
    }
    WhisperKind::Cpp
}

pub fn cloud_url() -> String {
    std::env::var("ZAPPE_WHISPER_URL")
        .unwrap_or_default()
        .trim()
        .to_string()
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

fn existing(path: PathBuf) -> Option<PathBuf> {
    path.exists().then_some(path)
}

pub fn whisper_bin() -> Option<PathBuf> {
    if let Ok(explicit) = std::env::var("ZAPPE_WHISPER_BIN") {
        let p = PathBuf::from(explicit.trim());
        if p.exists() {
            return Some(p);
        }
    }
    if let Some(p) = which("whisper-cli") {
        return Some(p);
    }
    if let Some(p) = which("whisper-cpp") {
        return Some(p);
    }
    if let Some(home) = dirs::home_dir() {
        if let Some(p) = existing(home.join("bin/whisper-cli")) {
            return Some(p);
        }
        if let Some(p) = existing(home.join("bin/whisper")) {
            return Some(p);
        }
    }
    if let Some(p) = existing(whisper_dir().join("whisper-cli")) {
        return Some(p);
    }
    which("whisper")
}

pub fn model_candidates() -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(explicit) = std::env::var("ZAPPE_WHISPER_MODEL") {
        let p = PathBuf::from(explicit.trim());
        if !p.as_os_str().is_empty() {
            out.push(p);
        }
    }
    let dir = whisper_dir();
    for name in [
        "ggml-small.bin",
        "ggml-base.bin",
        "ggml-small-q5_1.bin",
        "ggml-base-q5_1.bin",
    ] {
        out.push(dir.join(name));
    }
    if let Some(home) = dirs::home_dir() {
        out.push(home.join("whisper.cpp/models/ggml-small.bin"));
        out.push(home.join("whisper.cpp/models/ggml-base.bin"));
    }
    out.push(PathBuf::from("/usr/local/share/whisper/ggml-small.bin"));
    out.push(PathBuf::from("/usr/share/whisper/ggml-small.bin"));
    out
}

pub fn whisper_model() -> Option<PathBuf> {
    model_candidates().into_iter().find(|p| p.is_file())
}

pub fn whisper_cpp_args(model: &Path, wav: &Path) -> Vec<String> {
    vec![
        "-m".into(),
        model.display().to_string(),
        "-f".into(),
        wav.display().to_string(),
        "-l".into(),
        "pt".into(),
        "-nt".into(),
        "--prompt".into(),
        PT_BR_PROMPT.into(),
    ]
}

pub fn raw_whisper_args(wav: &Path) -> Vec<String> {
    vec![wav.display().to_string()]
}

pub fn clean_transcript(raw: &str) -> String {
    raw.lines()
        .map(|line| {
            let mut line = line.trim().trim_matches('"').trim();
            if let Some(rest) = line.strip_prefix('[') {
                if let Some((_, after)) = rest.split_once(']') {
                    line = after.trim();
                }
            }
            line.to_string()
        })
        .filter(|line| !line.is_empty() && !line.to_ascii_lowercase().contains("whisper.cpp"))
        .collect::<Vec<_>>()
        .join(" ")
        .trim()
        .to_string()
}

pub fn parse_cloud_transcript(body: &str) -> Option<String> {
    let trimmed = body.trim();
    if trimmed.is_empty() {
        return None;
    }
    if let Ok(value) = serde_json::from_str::<serde_json::Value>(trimmed) {
        for key in ["text", "transcript", "result"] {
            if let Some(text) = value.get(key).and_then(|v| v.as_str()) {
                let clean = clean_transcript(text);
                if !clean.is_empty() {
                    return Some(clean);
                }
            }
        }
    }
    let clean = clean_transcript(trimmed);
    if clean.is_empty() {
        None
    } else {
        Some(clean)
    }
}

fn fake_transcript() -> Option<String> {
    std::env::var("ZAPPE_VOICE_FAKE")
        .ok()
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn outcome(armed: bool, transcript: Option<String>, message: impl Into<String>) -> VoiceOutcome {
    VoiceOutcome {
        armed,
        transcript,
        message: message.into(),
    }
}

fn not_installed() -> VoiceOutcome {
    outcome(
        false,
        None,
        "Whisper pt-BR não está neste aparelho. Rode packaging/appliance/install-whisper.sh \
         ou defina ZAPPE_WHISPER_BIN e ZAPPE_WHISPER_MODEL. Cloud é opt-in via ZAPPE_WHISPER_URL.",
    )
}

fn wav_path() -> PathBuf {
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    std::env::temp_dir().join(format!("zappe-voice-{stamp}.wav"))
}

pub fn arecord_args(wav: &Path, seconds: u32) -> Vec<String> {
    let mut args = vec![
        "-f".into(),
        "S16_LE".into(),
        "-r".into(),
        "16000".into(),
        "-c".into(),
        "1".into(),
        "-t".into(),
        "wav".into(),
        "-q".into(),
        "-d".into(),
        seconds.to_string(),
    ];
    if let Ok(dev) = std::env::var("ZAPPE_ALSA_DEVICE") {
        let dev = dev.trim();
        if !dev.is_empty() {
            args.push("-D".into());
            args.push(dev.to_string());
        }
    }
    args.push(wav.display().to_string());
    args
}

fn kill_child(child: &mut Child) {
    #[cfg(unix)]
    {
        let _ = Command::new("kill")
            .args(["-INT", &child.id().to_string()])
            .status();
        let start = Instant::now();
        while start.elapsed() < Duration::from_millis(400) {
            if let Ok(Some(_)) = child.try_wait() {
                return;
            }
            std::thread::sleep(Duration::from_millis(40));
        }
    }
    let _ = child.kill();
    let _ = child.wait();
}

fn take_capture() -> Option<Capture> {
    CAPTURE.lock().unwrap_or_else(|e| e.into_inner()).take()
}

fn store_capture(capture: Capture) {
    let mut guard = CAPTURE.lock().unwrap_or_else(|e| e.into_inner());
    if let Some(mut prev) = guard.take() {
        kill_child(&mut prev.child);
        let _ = std::fs::remove_file(&prev.wav);
    }
    *guard = Some(capture);
}

/// Start `arecord`. Hold the mic; call [`end`] on release (or wait for the cap).
pub fn begin() -> VoiceOutcome {
    if fake_transcript().is_some() {
        return outcome(true, None, "Ouvindo…");
    }
    if whisper_kind() == WhisperKind::Cloud {
        if cloud_url().is_empty() {
            return not_installed();
        }
    } else if whisper_bin().is_none() {
        return not_installed();
    } else if whisper_kind() == WhisperKind::Cpp && whisper_model().is_none() {
        return outcome(
            false,
            None,
            format!(
                "Modelo Whisper ausente. Coloque ggml-small.bin em {} ou defina ZAPPE_WHISPER_MODEL.",
                whisper_dir().display()
            ),
        );
    }

    let wav = wav_path();
    let seconds = listen_seconds().max(MAX_SECONDS);
    let mut cmd = Command::new("arecord");
    cmd.args(arecord_args(&wav, seconds))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null());
    match cmd.spawn() {
        Ok(child) => {
            store_capture(Capture {
                child,
                wav,
                started: Instant::now(),
            });
            log::info!("ATONGX voice arecord started");
            outcome(true, None, "Ouvindo…")
        }
        Err(_) => outcome(false, None, "Microfone indisponível (arecord)."),
    }
}

fn transcribe_wav(wav: &Path) -> VoiceOutcome {
    if !wav.is_file() {
        return outcome(true, None, "Não entendi. Tente: início, voltar, Netflix.");
    }
    if let Ok(meta) = std::fs::metadata(wav) {
        if meta.len() < MIN_WAV_BYTES {
            return outcome(true, None, "Não entendi. Segure o microfone e fale.");
        }
    }

    match whisper_kind() {
        WhisperKind::Cloud => transcribe_cloud(wav),
        WhisperKind::Raw => transcribe_raw(wav),
        WhisperKind::Cpp => transcribe_cpp(wav),
    }
}

fn transcribe_cpp(wav: &Path) -> VoiceOutcome {
    let Some(bin) = whisper_bin() else {
        return not_installed();
    };
    let Some(model) = whisper_model() else {
        return outcome(
            false,
            None,
            format!(
                "Modelo Whisper ausente. Coloque ggml-small.bin em {}.",
                whisper_dir().display()
            ),
        );
    };
    let output = Command::new(&bin)
        .args(whisper_cpp_args(&model, wav))
        .output();
    match output {
        Ok(out) if out.status.success() => {
            let text = clean_transcript(&String::from_utf8_lossy(&out.stdout));
            if text.is_empty() {
                outcome(true, None, "Não entendi. Tente: início, voltar, Netflix.")
            } else {
                log::info!("ATONGX voice transcript={text}");
                outcome(true, Some(text.clone()), text)
            }
        }
        Ok(out) => outcome(
            true,
            None,
            format!(
                "whisper.cpp falhou: {}",
                String::from_utf8_lossy(&out.stderr).trim()
            ),
        ),
        Err(err) => outcome(true, None, format!("whisper.cpp: {err}")),
    }
}

fn transcribe_raw(wav: &Path) -> VoiceOutcome {
    let Some(bin) = whisper_bin() else {
        return not_installed();
    };
    let output = Command::new(&bin).args(raw_whisper_args(wav)).output();
    match output {
        Ok(out) if out.status.success() => {
            let text = clean_transcript(&String::from_utf8_lossy(&out.stdout));
            if text.is_empty() {
                outcome(true, None, "Não entendi. Tente: início, voltar, Netflix.")
            } else {
                outcome(true, Some(text.clone()), text)
            }
        }
        Ok(out) => outcome(
            true,
            None,
            format!(
                "whisper falhou: {}",
                String::from_utf8_lossy(&out.stderr).trim()
            ),
        ),
        Err(err) => outcome(true, None, format!("whisper: {err}")),
    }
}

fn transcribe_cloud(wav: &Path) -> VoiceOutcome {
    let url = cloud_url();
    if url.is_empty() {
        return not_installed();
    }
    let mut cmd = Command::new("curl");
    cmd.args(["-sS", "-X", "POST", "--max-time", "30"]);
    if let Ok(token) = std::env::var("ZAPPE_WHISPER_TOKEN") {
        let token = token.trim();
        if !token.is_empty() {
            cmd.args(["-H", &format!("Authorization: Bearer {token}")]);
        }
    }
    let model = std::env::var("ZAPPE_WHISPER_CLOUD_MODEL").unwrap_or_else(|_| "whisper-1".into());
    cmd.arg("-F")
        .arg(format!("file=@{};type=audio/wav", wav.display()))
        .args(["-F", "language=pt", "-F", &format!("model={model}"), &url]);
    match cmd.output() {
        Ok(out) if out.status.success() => {
            let body = String::from_utf8_lossy(&out.stdout);
            match parse_cloud_transcript(&body) {
                Some(text) => outcome(true, Some(text.clone()), text),
                None => outcome(true, None, "Não entendi. Tente: início, voltar, Netflix."),
            }
        }
        Ok(out) => outcome(
            true,
            None,
            format!(
                "Whisper cloud falhou: {}",
                String::from_utf8_lossy(&out.stderr).trim()
            ),
        ),
        Err(err) => outcome(true, None, format!("curl (ZAPPE_WHISPER_URL): {err}")),
    }
}

/// Stop `arecord` (if still running) and transcribe.
pub fn end() -> VoiceOutcome {
    if let Some(text) = fake_transcript() {
        let _ = take_capture();
        return outcome(true, Some(text.clone()), text);
    }
    let Some(mut capture) = take_capture() else {
        return outcome(true, None, "Não estava ouvindo.");
    };
    let elapsed = capture.started.elapsed();
    if elapsed < Duration::from_millis(700) {
        std::thread::sleep(Duration::from_millis(700).saturating_sub(elapsed));
    }
    kill_child(&mut capture.child);
    let result = transcribe_wav(&capture.wav);
    let _ = std::fs::remove_file(&capture.wav);
    result
}

/// Tap / test helper: record a fixed window, then transcribe.
pub fn listen() -> VoiceOutcome {
    if let Some(text) = fake_transcript() {
        return outcome(true, Some(text.clone()), text);
    }
    let started = begin();
    if !started.armed || started.transcript.is_some() {
        return started;
    }
    if started.message.contains("arecord") || started.message.contains("ausente") {
        return started;
    }
    std::thread::sleep(Duration::from_secs(listen_seconds() as u64));
    end()
}

/// Used by docs / tests: dump the local prompt we pass to whisper.cpp.
pub fn decoder_prompt() -> &'static str {
    PT_BR_PROMPT
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cpp_argv_includes_model_language_and_prompt() {
        let args = whisper_cpp_args(Path::new("/tmp/ggml-small.bin"), Path::new("/tmp/a.wav"));
        assert!(args.contains(&"-m".into()));
        assert!(args.contains(&"/tmp/ggml-small.bin".into()));
        assert!(args.contains(&"-l".into()));
        assert!(args.contains(&"pt".into()));
        assert!(args.contains(&"-nt".into()));
        assert!(args.contains(&PT_BR_PROMPT.into()));
        assert!(args.contains(&"/tmp/a.wav".into()));
    }

    #[test]
    fn arecord_is_16khz_mono() {
        let args = arecord_args(Path::new("/tmp/zappe.wav"), 5);
        assert!(args.contains(&"S16_LE".into()));
        assert!(args.contains(&"16000".into()));
        assert!(args.contains(&"1".into()));
        assert!(args.contains(&"5".into()));
        assert_eq!(args.last().map(String::as_str), Some("/tmp/zappe.wav"));
    }

    #[test]
    fn fake_listen_needs_no_mic() {
        let prev = std::env::var_os("ZAPPE_VOICE_FAKE");
        std::env::set_var("ZAPPE_VOICE_FAKE", "abrir netflix");
        let out = listen();
        assert!(out.armed);
        assert_eq!(out.transcript.as_deref(), Some("abrir netflix"));
        match prev {
            Some(v) => std::env::set_var("ZAPPE_VOICE_FAKE", v),
            None => std::env::remove_var("ZAPPE_VOICE_FAKE"),
        }
    }

    #[test]
    fn model_dir_is_under_data_dir() {
        let prev = std::env::var_os("ZAPPE_DATA_DIR");
        std::env::set_var("ZAPPE_DATA_DIR", "/tmp/zappe-voice-test-dir");
        assert_eq!(
            whisper_dir(),
            PathBuf::from("/tmp/zappe-voice-test-dir/whisper")
        );
        let candidates = model_candidates();
        assert!(candidates
            .iter()
            .any(|p| p.ends_with("whisper/ggml-small.bin")));
        match prev {
            Some(v) => std::env::set_var("ZAPPE_DATA_DIR", v),
            None => std::env::remove_var("ZAPPE_DATA_DIR"),
        }
    }

    #[test]
    fn cloud_json_and_plain_text() {
        assert_eq!(
            parse_cloud_transcript(r#"{"text":"ir para Globo"}"#).as_deref(),
            Some("ir para Globo")
        );
        assert_eq!(
            parse_cloud_transcript("  voltar  ").as_deref(),
            Some("voltar")
        );
        assert_eq!(parse_cloud_transcript("   "), None);
    }

    #[test]
    fn kind_cloud_is_opt_in() {
        let prev_url = std::env::var_os("ZAPPE_WHISPER_URL");
        let prev_kind = std::env::var_os("ZAPPE_WHISPER_KIND");
        std::env::remove_var("ZAPPE_WHISPER_URL");
        std::env::remove_var("ZAPPE_WHISPER_KIND");
        assert_eq!(whisper_kind(), WhisperKind::Cpp);
        std::env::set_var("ZAPPE_WHISPER_URL", "https://example.invalid/v1/audio");
        assert_eq!(whisper_kind(), WhisperKind::Cloud);
        match prev_url {
            Some(v) => std::env::set_var("ZAPPE_WHISPER_URL", v),
            None => std::env::remove_var("ZAPPE_WHISPER_URL"),
        }
        match prev_kind {
            Some(v) => std::env::set_var("ZAPPE_WHISPER_KIND", v),
            None => std::env::remove_var("ZAPPE_WHISPER_KIND"),
        }
    }

    #[test]
    fn clean_drops_timestamp_noise() {
        assert_eq!(
            clean_transcript("[00:00.000]  abrir Netflix\n"),
            "abrir Netflix"
        );
        assert!(decoder_prompt().contains("Netflix"));
        assert!(decoder_prompt().contains("Globo"));
    }
}
