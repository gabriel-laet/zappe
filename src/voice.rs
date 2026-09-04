//! Local speech hook. whisper.cpp only — never a chat LLM.
//!
//! This sketch does not vendor a model or download weights. After a transcript
//! exists, it goes through [`crate::command::parse`] and nowhere else.

use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{anyhow, Context, Result};

use crate::command::{self, ParseError};

/// Feature-flagged whisper.cpp attachment point.
#[derive(Clone, Debug)]
pub struct WhisperHook {
    pub bin: Option<PathBuf>,
}

impl WhisperHook {
    pub fn from_env() -> Self {
        let explicit = std::env::var("ZAPPE_WHISPER_BIN")
            .ok()
            .map(PathBuf::from)
            .filter(|p| p.exists());
        Self {
            bin: explicit
                .or_else(|| which("whisper-cli"))
                .or_else(|| which("whisper")),
        }
    }

    pub fn armed(&self) -> bool {
        self.bin.is_some()
    }

    /// Run whisper.cpp on a wav file, then parse the transcript with the
    /// constrained grammar. No tool-calling, no general model.
    #[allow(dead_code)]
    pub fn transcribe_and_parse(&self, wav: &Path) -> Result<command::Command> {
        let text = self.transcribe(wav)?;
        command::parse(&text).map_err(|err: ParseError| anyhow!("{err}"))
    }

    #[allow(dead_code)]
    pub fn transcribe(&self, wav: &Path) -> Result<String> {
        let bin = self
            .bin
            .as_ref()
            .ok_or_else(|| anyhow!("whisper.cpp not configured — set ZAPPE_WHISPER_BIN"))?;
        let output = Command::new(bin)
            .args(["-nt", "-f"])
            .arg(wav)
            .output()
            .with_context(|| format!("spawn {}", bin.display()))?;
        if !output.status.success() {
            return Err(anyhow!(
                "whisper.cpp exited {}: {}",
                output.status,
                String::from_utf8_lossy(&output.stderr)
            ));
        }
        Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
    }
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

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;

    #[test]
    fn transcript_uses_the_same_grammar() {
        let cmd = command::parse("play lofi on youtube").unwrap();
        assert!(matches!(cmd, command::Command::Play { .. }));
        let hook = WhisperHook { bin: None };
        assert!(!hook.armed());
        assert!(hook
            .transcribe(Path::new("/tmp/zappe-missing.wav"))
            .is_err());
        assert!(hook
            .transcribe_and_parse(Path::new("/tmp/zappe-missing.wav"))
            .is_err());
    }
}
