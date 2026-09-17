# Zappe

Living-room launcher for Linux (Omarchy / Hyprland / Wayland first): a **Tauri guide** that hands off to **real Chrome** for streaming and **mpv** for OTA live TV.

Netflix, Prime, Disney+, and YouTube always run in a Zappe-owned Chrome profile over CDP — not in a webview, not scraped into a catalog.

## Stack

- **UI:** Tauri 2, React, Vite, TypeScript, Tailwind, shadcn-style components
- **Playback:** Google Chrome / Chromium (CDP) + optional `dvbv5-zap` → `mpv` for OTA
- **Legacy:** the old wgpu HUD is archived under `legacy-hud/` (not the default run path)

## Dependencies (Omarchy / Arch)

**Required**

- [Node.js](https://nodejs.org/) 20+ and npm
- Rust 1.88+ (`rust-toolchain.toml` pins stable)
- Tauri Linux system deps — on Arch:

  ```bash
  sudo pacman -S webkit2gtk-4.1 base-devel curl wget openssl pkg-config libappindicator-gtk3 librsvg
  ```

- **Google Chrome or Chromium** on `PATH` (or set `CHROME_PATH`)

**Optional (OTA / MyGica)**

- `dvbv5-tools` (`dvbv5-zap`), `mpv`
- Channel list: auto-detected from default paths, or override with `ZAPPE_OTA_CHANNELS`

Qt is **not** required for the current app.

## Run (development)

```bash
npm install
npm run tauri dev
```

Production build:

```bash
npm run tauri build
```

## First-run setup

1. **Browser required** — Zappe fails clearly if Chrome is missing; install hint for Arch/Omarchy.
2. **1Password optional** — opens the Chrome Web Store in Zappe Chrome (never reads vault/cookies).
3. **Accounts** — sign in inside orchestrated Chrome.
4. **Home** — TV-style shelves, remote-friendly focus.

Setup state is stored in `~/.config/zappe/setup.json`.

## Chrome profile

Dedicated user-data-dir:

- Linux: `~/.local/share/zappe/chrome-profile`
- macOS: `~/Library/Application Support/zappe/chrome-profile`

Zappe launches Chrome headed, navigates via CDP, fullscreen via `Browser.getWindowForTarget` + `Browser.setWindowBounds`. On **Hyprland 0.56+**, window nudges use `hyprctl eval` with `hl.dsp.*` (not legacy `dispatch focuswindow …`). Failures are logged and ignored — they never crash the app. The guide window is always fullscreen (no decorations).

## Window handoff (guide ↔ playback)

| Phase | Behavior |
| --- | --- |
| Guide | Tauri window visible, D-pad navigation on shelves |
| Stream tile / OTA | Tauri hides → Chrome or mpv fullscreen + raised |
| Back / Escape / BrowserBack | Exit playback surface → show + focus Tauri, restore shelf focus |
| Quit | Clean kill of OTA pipeline; close Zappe-launched Chrome |

Global shortcuts (Escape, BrowserBack) call back to the guide even when Tauri is hidden.

## OTA

Without any environment variable, Zappe looks for an existing file at:

1. `$HOME/tv/channels.conf` (common on Omarchy)
2. `~/.config/zappe/channels.conf`

If both exist, both are read. To use a different path (or several files), set:

```bash
export ZAPPE_OTA_CHANNELS="$HOME/.config/dvbv5/channels.conf:/path/to/extra.conf"
```

When set, **`ZAPPE_OTA_CHANNELS` replaces the defaults** (colon-separated list).

The guide shows **TV aberta** in Apps and a **Canais** shelf (HD preferred; obvious 1Seg duplicates filtered).

Playback pipeline (no `mpegts://` URLs):

```bash
dvbv5-zap -a 0 -c … -p "Channel Name" -r -o - | mpv --hwdec=no --demuxer-lavf-format=mpegts --fs --no-terminal -
```

Back stops `mpv` and `dvbv5-zap`.

## Remote (ALTONEX-style keyboard)

| Key | Action |
| --- | --- |
| Arrows | move focus across tiles / shelves |
| Enter | activate tile |
| Escape / Backspace / BrowserBack | back to guide |
| Home | end playback and return to guide |
| Space / MediaPlayPause | play/pause in Chrome |

## Environment

| Variable | Purpose |
| --- | --- |
| `CHROME_PATH` | Override Chrome/Chromium binary |
| `ZAPPE_OTA_CHANNELS` | Override: colon-separated `channels.conf` paths (replaces default lookup) |

## Legacy HUD

```bash
cd legacy-hud && cargo run -- --chrome
```

For reference only; use `npm run tauri dev` for the product UI.
