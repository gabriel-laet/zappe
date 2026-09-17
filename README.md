# Zappe

Personal living-room launcher: **Qt6 HUD** (remote-friendly guide) plus **zappe-owned Google Chrome** for Netflix, Prime Video, Disney+, and YouTube. On Linux (Omarchy / Hyprland / Wayland), **OTA TV** (ISDB-Tb / DVB via MyGica-class tuners) plays through **`dvbv5-zap` + `mpv`**.

Zappe **orchestrates Chrome end-to-end**: finds the binary (with a first-run wizard if missing), launches a headed window with a dedicated profile, speaks CDP, navigates first-party URLs, raises/fullscreens the player, sends play/pause/back skills, and **closes the browser process on quit**. The HUD is the remote; Chrome is the streaming surface under zappe control.

**HUD target: Linux only** (Qt6 + Wayland). macOS is not supported for the guide UI.

## What it is

- **Qt6 / QML HUD** — large tiles, high contrast, calm focus; setup wizard in short PT-BR.
- **Orchestrated setup** — Chrome/Chromium detection and Arch/Omarchy install hints (`pacman` / `apt`), optional **1Password** (app + extension in the zappe profile; never reads vault data), then **Accounts** for service logins.
- **Chrome is required** for streaming. Normal `cargo run` walks you through browser setup, then launches Chrome (profile under `~/.local/share/zappe/chrome-profile`). Missing Chrome blocks progress at step 1 (no skip).
- **Accounts** — Netflix / Prime / Disney / YouTube; OK opens login in orchestrated Chrome; `onboarding.done` is written only when you choose **Continue to guide**.
- **OTA TV** when `channels.conf` is configured: HUD row of terrestrial channels; Enter tunes via `dvbv5-zap | mpv`; Back stops both processes.
- **ALTONEX-style remotes** (HID keyboard): D-pad + OK, Home, Back, Play/Pause. Volume stays with the OS.

## What it is not

- Not an unofficial stream ripper, scraper, or DRM workaround.
- Not “optional Chrome attach” as the normal path. **`ZAPPE_CDP` attach is debug-only** (you must already have DevTools up).

## Dependencies

Rust **1.88+** (`rust-toolchain.toml` pins stable).

**Arch / Omarchy (recommended):**

```bash
sudo pacman -S --needed base-devel qt6-base qt6-declarative qt6-quickcontrols2 \
  chromium mpv v4l-utils
# OTA: dvbv5 tools from your distro (e.g. v4l-utils / dvb-tools)
```

**Debian/Ubuntu-like:**

```bash
sudo apt install build-essential qt6-base-dev qt6-declarative-dev \
  libgl1-mesa-dev chromium-browser mpv
```

Set `CHROME_PATH` if Chrome/Chromium is not on `PATH`. For builds, `qmake` from Qt 6 must be on `PATH` (or set `QMAKE=/path/to/qmake`). The project links with **g++** (see `.cargo/config.toml`).

## Run

```bash
export QMAKE="$(command -v qmake6 || command -v qmake)"   # if needed
cargo run
```

First launch:

1. **Navegador** — detect/install Chromium; **Continuar** starts zappe-owned Chrome.
2. **1Password** (optional) — open Web Store in zappe Chrome or **Pular**.
3. **Contas** — sign in per service; **Continuar para o guia** marks onboarding done.

```bash
cargo run -- --url https://www.youtube.com
cargo run -- --cdp http://127.0.0.1:9222   # debug attach only
```

## OTA (ISDB-Tb / DVB)

```bash
export ZAPPE_OTA_CHANNELS="$HOME/tv/channels.conf"
cargo run
```

| Variable / flag | Meaning |
| --- | --- |
| `ZAPPE_OTA_CHANNELS` / `--ota-channels` | dvbv5 `channels.conf` |
| default | `~/tv/channels.conf` if present |
| `ZAPPE_DVB_ADAPTER` | `dvbv5-zap -a` (default `0`) |
| `ZAPPE_DVBV5_ZAP`, `ZAPPE_MPV` | binary overrides |
| `ZAPPE_OTA_ZAP_LOG` | zap stderr (default `/tmp/zappe-zap.log`) |

## Remote / keyboard

| Key | Action |
| --- | --- |
| D-pad | move focus (guide or accounts) |
| OK / Enter | open tile, tune OTA, or open login |
| Back / Esc | stop OTA, Chrome back, or leave accounts |
| Home | guide root |
| Play/Pause | Chrome player skill |
| `/` | command bar |
| `h` | hide/show HUD |
| `q` | quit (stops OTA + closes zappe Chrome) |

## Login & 1Password

1. Zappe launches **only** the zappe Chrome profile (`…/zappe/chrome-profile`).
2. Setup wizard can open the **1Password extension** page in that profile; install the extension and enable desktop integration if you use 1Password.
3. **Accounts** in the HUD → pick a service → sign in in Chrome. Zappe never harvests cookies or vault secrets.

## Layout

```
src/main.rs           Qt app entry (Linux)
src/app.rs            Chrome / OTA / guide logic
src/qt/zappe_backend.rs  cxx-qt ↔ QML
qml/main.qml          HUD + setup wizard
src/setup/            browser + 1Password orchestration
src/chrome.rs         launch, CDP, skills, Browser.close on quit
src/ota.rs            dvbv5-zap → mpv
src/guide.rs          focus + screens (no wgpu)
src/accounts.rs       onboarding marker
```

## Status

Runnable on Linux with Qt6, Chromium/Chrome, and optional OTA. Site skills and catalog metadata remain placeholders by design.
