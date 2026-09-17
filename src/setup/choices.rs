//! Setup wizard choices (remote-focusable, one row per screen).

use crate::guide::HudScreen;
use crate::setup::browser::BrowserDetect;

#[derive(Clone, Copy)]
pub struct SetupChoice {
    pub id: &'static str,
    pub label: &'static str,
    pub primary: bool,
}

pub fn chrome_choices(browser: &BrowserDetect) -> Vec<SetupChoice> {
    if browser.found {
        vec![
            SetupChoice {
                id: "continue",
                label: "Continuar",
                primary: true,
            },
            SetupChoice {
                id: "retry",
                label: "Tentar de novo",
                primary: false,
            },
        ]
    } else {
        vec![
            SetupChoice {
                id: "retry",
                label: "Tentar de novo",
                primary: true,
            },
            SetupChoice {
                id: "install",
                label: "Instalar automaticamente",
                primary: false,
            },
        ]
    }
}

pub fn onepassword_choices() -> Vec<SetupChoice> {
    vec![
        SetupChoice {
            id: "continue",
            label: "Continuar",
            primary: true,
        },
        SetupChoice {
            id: "extension",
            label: "Abrir extensão",
            primary: false,
        },
        SetupChoice {
            id: "skip",
            label: "Pular",
            primary: false,
        },
    ]
}

pub fn choice_count(screen: HudScreen, browser: &BrowserDetect) -> usize {
    match screen {
        HudScreen::SetupChrome => chrome_choices(browser).len(),
        HudScreen::SetupOnePassword => onepassword_choices().len(),
        _ => 0,
    }
}

pub fn choice_at(screen: HudScreen, browser: &BrowserDetect, idx: usize) -> Option<SetupChoice> {
    let choices = match screen {
        HudScreen::SetupChrome => chrome_choices(browser),
        HudScreen::SetupOnePassword => onepassword_choices(),
        _ => return None,
    };
    choices.get(idx).copied()
}

pub fn setup_title(screen: HudScreen) -> &'static str {
    match screen {
        HudScreen::SetupChrome => "Navegador",
        HudScreen::SetupOnePassword => "1Password",
        _ => "",
    }
}

pub fn setup_subtitle(screen: HudScreen, browser: &BrowserDetect) -> String {
    match screen {
        HudScreen::SetupChrome if browser.found => {
            "Chromium encontrado. Vamos abrir o player do Zappe.".into()
        }
        HudScreen::SetupChrome => {
            "Instale o Chromium para assistir. Use Tentar de novo após instalar.".into()
        }
        HudScreen::SetupOnePassword => {
            "Opcional: 1Password preenche login no Chrome do Zappe.".into()
        }
        _ => String::new(),
    }
}
