//! Teach-mode stub.
//!
//! When a harvest skill misses its anchors, Zappe waits for the user to finish
//! the path with the air mouse. A later iteration records HID and writes a new
//! skill version. This module is the extension point — it must not crash, and
//! it must not invent a replacement path.

use std::sync::Mutex;

use serde::{Deserialize, Serialize};

use crate::catalog::TeachView;

/// HID recorder hook for a follow-up. v1 is a no-op stub.
pub trait TeachRecorder: Send + Sync {
    fn start(&mut self, skill_id: &str) -> anyhow::Result<()>;
    #[allow(dead_code)]
    fn stop_and_save(&mut self) -> anyhow::Result<std::path::PathBuf>;
}

pub struct StubTeachRecorder;

impl TeachRecorder for StubTeachRecorder {
    fn start(&mut self, skill_id: &str) -> anyhow::Result<()> {
        log::info!(
            "teach-mode stub: waiting for HID recording of '{skill_id}' \
             (user finishes the path with the air mouse; we re-record the skill later)"
        );
        Ok(())
    }

    fn stop_and_save(&mut self) -> anyhow::Result<std::path::PathBuf> {
        anyhow::bail!("HID recording is not implemented — teach-mode stub only")
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
pub struct TeachState {
    pub active: bool,
    pub skill_id: Option<String>,
    pub message: Option<String>,
}

impl Default for TeachState {
    fn default() -> Self {
        Self {
            active: false,
            skill_id: None,
            message: None,
        }
    }
}

impl TeachState {
    pub fn view(&self) -> TeachView {
        TeachView {
            active: self.active,
            skill_id: self.skill_id.clone(),
            message: self.message.clone(),
        }
    }
}

pub struct TeachMode {
    state: Mutex<TeachState>,
    recorder: Mutex<Box<dyn TeachRecorder>>,
}

impl TeachMode {
    pub fn new() -> Self {
        Self {
            state: Mutex::new(TeachState::default()),
            recorder: Mutex::new(Box::new(StubTeachRecorder)),
        }
    }

    pub fn view(&self) -> TeachView {
        self.state.lock().unwrap().view()
    }

    pub fn begin(&self, skill_id: &str, reason: &str) -> TeachState {
        let message = format!(
            "Teach me: finish the Netflix path with the air mouse. ({reason}) HID recording lands in a follow-up."
        );
        log::warn!("teach-mode waiting on {skill_id}: {reason}");
        if let Err(err) = self.recorder.lock().unwrap().start(skill_id) {
            log::warn!("teach recorder start failed: {err:#}");
        }
        let next = TeachState {
            active: true,
            skill_id: Some(skill_id.to_string()),
            message: Some(message),
        };
        *self.state.lock().unwrap() = next.clone();
        next
    }

    pub fn cancel(&self) -> TeachState {
        *self.state.lock().unwrap() = TeachState::default();
        self.state.lock().unwrap().clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn begin_sets_waiting_state() {
        let teach = TeachMode::new();
        let state = teach.begin("netflix.continue_watching.v1", "anchor missing");
        assert!(state.active);
        assert_eq!(state.skill_id.as_deref(), Some("netflix.continue_watching.v1"));
        assert!(state.message.unwrap().contains("Teach me"));
    }

    #[test]
    fn stub_recorder_cannot_save() {
        let mut rec = StubTeachRecorder;
        rec.start("netflix.continue_watching.v1").unwrap();
        assert!(rec.stop_and_save().is_err());
    }
}
