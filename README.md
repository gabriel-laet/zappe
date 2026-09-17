# Zappe

Living-room launcher for Linux (Omarchy / Hyprland 0.56 / Wayland first). The **Tauri guide is the only catalog** the user sees. Google Chrome is a silent player and a background harvester of **logged-in** shelves (Continue Watching, later My List) — not a CDP-controlled browser and not a scrape of Netflix’s public catalog.

No Trakt. No runtime LLM. No TypeSafe/Jev in v1. When a harvest skill misses its anchors we **mark it stale** and offer **teach mode** (user finishes the path with the air mouse; a later iteration re-records the skill).

## Architecture

```
Tauri guide (Home shelves)
        │  reads ~/.local/share/zappe/catalog.json
        ▼
harvest skill  (skills/netflix.continue_watching.v1.yaml)
        │  open URL → wait → AT-SPI dump → extract rows
        ▼
gamescope  ──wraps──►  google-chrome-stable
                         ~/.local/share/zappe/chrome-profile
                         no CDP / no --enable-automation / no remote debugging
```

| Piece | Role |
| --- | --- |
| **Guide** | Tauri 2 + React. Apps, harvested Continue Watching, TV aberta / Canais. |
| **Nest** | `gamescope` wrapping `google-chrome-stable` with a dedicated profile. Real session. |
| **Harvest** | Dump the Chrome AT-SPI tree, walk versioned skill anchors, write the local store. |
| **Skills** | JSON/YAML, e.g. `netflix.continue_watching.v1`. More services are sibling files. |
| **Teach-mode** | Stub: waiting state + `TeachRecorder` trait. HID recording is a follow-up. |
| **OTA** | Unchanged: `dvbv5-zap \| mpv` when `channels.conf` is present. |

Playback is a URL into the nest (deep link when harvest found one). Play/pause is a best-effort HID key (`wtype` / `ydotool` / `xdotool`). Hyprland `hl.dsp.*` nudges apply to the **gamescope** window (or the guide / mpv). They are not used to drive Chrome inside the nest.

## Dependencies (Omarchy / Arch)

**Required**

- Node.js 20+ and npm
- Rust 1.88+ (`rust-toolchain.toml` pins stable)
- Tauri Linux system deps:

  ```bash
  sudo pacman -S webkit2gtk-4.1 base-devel curl wget openssl pkg-config libappindicator-gtk3 librsvg
  ```

- **Google Chrome** (`google-chrome-stable` preferred) and **gamescope**:

  ```bash
  sudo pacman -S google-chrome gamescope at-spi2-core
  ```

  AUR/`google-chrome` if the distro package is not in extra. Override the binary with `CHROME_PATH`.

**Optional**

- `wtype` or `ydotool` — nest play/pause / Escape
- `dvbv5-tools` + `mpv` — OTA / TV aberta
- Channel list: `~/tv/channels.conf` or `~/.config/zappe/channels.conf` (`ZAPPE_OTA_CHANNELS` overrides)

### Chrome accessibility (harvest)

Zappe launches Chrome with `--force-renderer-accessibility` and `ACCESSIBILITY_ENABLED=1` / `GTK_A11Y=atspi`. That is **not** automation.

If Continue Watching harvest is empty on a logged-in profile:

1. Confirm `at-spi2-core` is installed and the session has an a11y bus (`busctl --user introspect org.a11y.Bus /org/a11y/bus`).
2. In the Zappe Chrome profile open `chrome://accessibility` and enable **Native**.
3. On GNOME-ish sessions: `gsettings set org.gnome.desktop.interface toolkit-accessibility true` (Hyprland may not use this; the Chrome flag is the primary path).

## Run

```bash
npm install
npm run tauri dev
```

Production build: `npm run tauri build`.

### Harvest Continue Watching (Linux)

From the guide: Home auto-runs the Netflix skill on load, or focus **Sync Netflix**.

From a terminal (same store the UI reads):

```bash
cd src-tauri
# Live nest + AT-SPI (needs gamescope, Chrome, a logged-in profile)
cargo run --bin zappe-harvest -- --skill netflix.continue_watching.v1

# Offline / CI: walk a saved tree (no Chrome)
cargo run --bin zappe-harvest -- --fixture ../fixtures/a11y/netflix.continue_watching.sample.json
```

Rows land in `~/.local/share/zappe/catalog.json`. Failures log and set the shelf to **stale / teach** — they do not crash the app.

Dump the tree while debugging:

```bash
cargo run --bin zappe-harvest -- --skill netflix.continue_watching.v1 --dump /tmp/zappe-a11y.json
```

## First-run setup

1. **Browser required** — Chrome on `PATH` (or `CHROME_PATH`).
2. **1Password optional** — opens the Web Store in the nest (never reads vault/cookies).
3. **Accounts** — sign in inside the nest so harvest can see Continue Watching.
4. **Home** — harvested shelves + OTA when configured.

Setup state: `~/.config/zappe/setup.json`.

## Chrome profile / nest

- Profile: `~/.local/share/zappe/chrome-profile` (Linux)
- Override data root: `ZAPPE_DATA_DIR`
- Nest command (play):

  ```bash
  gamescope -W 1920 -H 1080 -f -- google-chrome-stable \
    --user-data-dir="$HOME/.local/share/zappe/chrome-profile" \
    --no-first-run --no-default-browser-check \
    --force-renderer-accessibility \
    https://www.netflix.com/watch/…
  ```

Harvest uses the same profile **without** `-f`, then hides the nest (best-effort: Hyprland special workspace). Netflix chrome may flash; hiding after harvest is enough for v1.

`ZAPPE_NEST=chrome` skips gamescope (dev fallback). `ZAPPE_HARVEST_FIXTURE` / `zappe-harvest --fixture` skip the live dump.

## Skills

Bundled: [`skills/netflix.continue_watching.v1.yaml`](skills/netflix.continue_watching.v1.yaml).

User overrides: `~/.local/share/zappe/skills/*.yaml` (same `id` wins).

A skill is: **open URL → wait → a11y find anchors → extract rows**. If anchors miss, we do **not** invent a path. Teach-mode is the extension point (`TeachRecorder` in `src-tauri/src/teach.rs`).

## Window handoff

| Phase | Behavior |
| --- | --- |
| Guide | Tauri visible, D-pad on shelves |
| Harvest | Nest starts windowed, dump, hide nest; guide stays up |
| Stream tile | Guide hides → gamescope fullscreen |
| OTA | Guide hides → mpv fullscreen |
| Back / Escape | Hide nest or stop mpv → guide fullscreen + focus |
| Quit | Stop OTA; kill a Zappe-launched nest |

Hyprland 0.56 nudges use `hyprctl eval` + `hl.dsp.*` (not legacy `dispatch focuswindow`). Failures are logged and ignored.

## Remote (ALTONEX-style keyboard)

| Key | Action |
| --- | --- |
| Arrows | move focus |
| Enter | activate tile |
| Escape / Backspace / BrowserBack | back to guide |
| Home | end playback |
| Space / MediaPlayPause | play/pause in the nest (HID) |

## Environment

| Variable | Purpose |
| --- | --- |
| `CHROME_PATH` | Chrome binary |
| `GAMESCOPE_PATH` | gamescope binary |
| `ZAPPE_NEST` | `gamescope` (default) or `chrome` |
| `ZAPPE_NEST_WIDTH` / `ZAPPE_NEST_HEIGHT` | nest size (default 1920×1080) |
| `ZAPPE_DATA_DIR` | override `~/.local/share/zappe` |
| `ZAPPE_HARVEST_FIXTURE` | a11y JSON dump (skip live AT-SPI) |
| `ZAPPE_A11Y_DUMP` | write the live tree to this path |
| `ZAPPE_OTA_CHANNELS` | colon-separated `channels.conf` paths |

## OTA

Defaults (when `ZAPPE_OTA_CHANNELS` is unset): `$HOME/tv/channels.conf`, then `~/.config/zappe/channels.conf`. HD preferred; 1Seg filtered.

```bash
dvbv5-zap -a 0 -c … -p "Channel Name" -r -o - | mpv --hwdec=no --vo=gpu \
  --demuxer-lavf-format=mpegts --demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal -
```

Play fails fast (guide stays / is restored) if `/dev/dvb/adapter0` is missing or zap/mpv exits immediately. stderr lands in `~/.local/share/zappe/ota-pipeline.log`.

## Out of scope (v1)

- Jev / UI-TARS / RL mutation loops
- Prime / Disney harvest (skill files can plug in later)
- Perfect “never flash Netflix chrome”
- Full HID teach-mode recording

## Legacy HUD

The old wgpu HUD lives under `legacy-hud/` and is not the product path.
