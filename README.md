# Zappe

Personal living-room launcher: a native **HUD** (remote-friendly guide) plus **zappe-owned Google Chrome** for Netflix, Prime Video, Disney+, and YouTube. On Linux, **OTA TV** (ISDB-Tb / DVB via MyGica-class tuners) plays through **`dvbv5-zap` + `mpv`** — local RF, no Chrome login.

Zappe **orchestrates Chrome end-to-end**: finds the binary, launches a headed window with a dedicated profile, speaks CDP, navigates first-party URLs, raises/fullscreens the player, sends play/pause/back skills, and **closes the browser process on quit**. The HUD is the remote; Chrome is the streaming surface under zappe control.

## What it is

- A modern, flat **wgpu HUD** (`winit` + instanced quads) — large tiles, high contrast, calm focus motion. No Electron, no in-process webview, no retro CRT shader.
- **Chrome is required** for streaming. `cargo run` launches Chrome automatically (profile under `~/.local/share/zappe/chrome-profile` on Linux). Missing Chrome → clear error and **non-zero exit**.
- **Accounts screen** on first run (and via the **Accounts** tile): raises Chrome to each service home so you sign in once. Install the **1Password browser extension** in that profile if you use 1Password. Cookies stay in Chrome; zappe never harvests them.
- **OTA TV** when `channels.conf` is configured: HUD row of terrestrial channels; Enter tunes via `dvbv5-zap | mpv`; Back stops both processes.
- **ALTONEX-style remotes** (HID keyboard): D-pad + OK, Home, Back, Play/Pause. Volume stays with the OS. Mic button → future local whisper hook.

## What it is not

- Not an unofficial stream ripper, scraper, or DRM workaround.
- Not “optional Chrome attach” as the normal path. **`ZAPPE_CDP` attach is debug-only** (you must already have DevTools up); day-to-day use lets zappe launch Chrome.

## Dependencies

Rust **1.88+** (`rust-toolchain.toml` pins stable).

**Linux (HUD + Chrome + optional OTA):**

```bash
sudo apt install google-chrome-stable libxkbcommon-x11-0 mesa-vulkan-drivers libegl1
# optional OTA:
sudo apt install v4l-utils mpv
```

Set `CHROME_PATH` if Chrome is not on `PATH`.

## Run

```bash
cargo run
```

That **starts Chrome and the HUD**. First launch may show the **Accounts** screen — pick a service, sign in in the Chrome window, then **Continue to guide** or **Back**.

Open the guide tile **Accounts** anytime to add another service login.

```bash
# optional: open a specific URL in the zappe profile after launch
cargo run -- --url https://www.youtube.com

# debug only — attach instead of launch (you must start Chrome yourself with remote debugging)
cargo run -- --cdp http://127.0.0.1:9222
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

Channel names must match `[Name]` sections in `channels.conf`. Zappe uses `dvbv5-zap … -r -o -` piped to `mpv` (not `-P`, not a second opener on `dvr0`).

## Remote / keyboard

| Key | Action |
| --- | --- |
| D-pad | move focus (guide rows or accounts tiles) |
| OK / Enter | open tile, tune OTA, or open login |
| Back / Esc | stop OTA, Chrome history back, or leave accounts |
| Home | guide root |
| Play/Pause | Chrome player skill |
| `/` | command bar (constrained grammar) |
| `h` | hide/show HUD |
| `q` | quit (stops OTA + closes zappe-owned Chrome) |
| Volume | OS |

## Login & 1Password

1. Zappe launches **only** the zappe Chrome profile (`…/zappe/chrome-profile`).
2. Use **Accounts** in the HUD → pick Netflix / Prime / Disney / YouTube → sign in in Chrome (password manager extension recommended).
3. Next `cargo run` reuses the same profile — no cookie sync from your phone, no secrets in this repo.

## Layout

```
src/main.rs       HUD loop, Chrome boot, OTA, accounts navigation
src/hud.rs        modern wgpu guide + accounts UI
src/theme.rs      colors / branding
src/accounts.rs   first-run onboarding marker + login services
src/chrome.rs     launch, CDP, skills, Browser.close on quit
src/ota.rs        dvbv5-zap → mpv
src/catalog.rs    guide rows + OTA from channels.conf
src/skills.rs     fragile per-site CDP hooks
src/shaders/quad.wgsl
```

## Status

Runnable on Linux/macOS with real Chrome. OTA on Linux when `channels.conf` exists. Site skills and catalog metadata remain placeholders by design.
