//! Detect Chromium/Chrome on Linux and offer Arch/Omarchy-friendly install paths.

use std::path::PathBuf;
use std::process::Command;

use crate::chrome::find_chrome;

#[derive(Clone, Debug)]
pub struct BrowserDetect {
    pub found: bool,
    pub path: Option<PathBuf>,
    /// Short PT-BR hint for the wizard.
    pub message: String,
    /// Command the user can paste (or we try with passwordless sudo).
    pub install_command: String,
    pub distro_hint: String,
}

pub fn detect() -> BrowserDetect {
    if let Some(path) = find_chrome() {
        return BrowserDetect {
            found: true,
            path: Some(path),
            message: "Navegador encontrado.".into(),
            install_command: String::new(),
            distro_hint: String::new(),
        };
    }

    let (install_command, distro_hint) = recommended_install();
    BrowserDetect {
        found: false,
        path: None,
        message: "Chrome/Chromium não encontrado. Instale para continuar.".into(),
        install_command,
        distro_hint,
    }
}

fn recommended_install() -> (String, String) {
    if has_pacman() {
        (
            "sudo pacman -S --needed chromium".into(),
            "Arch / Omarchy (pacman)".into(),
        )
    } else if has_apt() {
        (
            "sudo apt install chromium-browser".into(),
            "Debian/Ubuntu (apt)".into(),
        )
    } else {
        (
            "Instale Google Chrome ou Chromium e defina CHROME_PATH se necessário.".into(),
            "Linux".into(),
        )
    }
}

fn has_pacman() -> bool {
    which("pacman")
}

fn has_apt() -> bool {
    which("apt-get") || which("apt")
}

fn which(name: &str) -> bool {
    Command::new("which")
        .arg(name)
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

#[derive(Clone, Debug)]
pub enum InstallAttempt {
    /// Package manager ran successfully.
    Installed,
    /// Show this to the user (needs password or manual step).
    Manual { command: String, detail: String },
    /// Non-interactive sudo not available.
    NeedsPassword { command: String },
}

/// Try a safe non-interactive install when passwordless sudo works.
pub fn try_noninteractive_install() -> InstallAttempt {
    let cmd = recommended_install().0;
    if !sudo_noninteractive_ok() {
        return InstallAttempt::NeedsPassword { command: cmd };
    }

    if has_pacman() {
        let status = Command::new("sudo")
            .args(["pacman", "-S", "--needed", "--noconfirm", "chromium"])
            .status();
        return match status {
            Ok(s) if s.success() => InstallAttempt::Installed,
            Ok(s) => InstallAttempt::Manual {
                command: cmd,
                detail: format!("pacman saiu com código {}", s),
            },
            Err(err) => InstallAttempt::Manual {
                command: cmd,
                detail: format!("{err}"),
            },
        };
    }

    if has_apt() {
        let status = Command::new("sudo")
            .args([
                "env",
                "DEBIAN_FRONTEND=noninteractive",
                "apt-get",
                "install",
                "-y",
                "chromium-browser",
            ])
            .status();
        return match status {
            Ok(s) if s.success() => InstallAttempt::Installed,
            Ok(s) => InstallAttempt::Manual {
                command: cmd,
                detail: format!("apt saiu com código {}", s),
            },
            Err(err) => InstallAttempt::Manual {
                command: cmd,
                detail: format!("{err}"),
            },
        };
    }

    InstallAttempt::Manual {
        command: cmd,
        detail: "Nenhum gerenciador conhecido (pacman/apt).".into(),
    }
}

fn sudo_noninteractive_ok() -> bool {
    Command::new("sudo")
        .args(["-n", "true"])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_does_not_panic() {
        let d = detect();
        if !d.found {
            assert!(!d.install_command.is_empty());
        }
    }
}
