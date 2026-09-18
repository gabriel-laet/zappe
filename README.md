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

Production build: `npm run tauri -- build --no-bundle` on the appliance (see updater). Full installer: `npm run tauri build`. Frontend-only preview (no nest / harvest): `npm run dev:web` then open `http://localhost:1420/`. Vite helpers: `?guide=1` Home, `?brand=feras` name/theme + data-dir logo, `?splash=1` hold splash, `?idle=3` screensaver in 3s. Official artwork is served from `~/.local/share/zappe/` (never git).

### Harvest Continue Watching (Linux)

From the guide: focus **Sync Netflix** (Home does **not** harvest on launch — that stole focus into the nest). `ZAPPE_AUTO_HARVEST=1` restores the old auto-sync for debugging.

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

The living-room box has **no physical keyboard**. Couch input is the TV remote plus a phone.

1. **Browser on this TV** — Chrome already on `PATH` (or `CHROME_PATH`). That is an appliance install, not a couch typing step.
2. **Connect account (phone)** — stub: open `http://zappe-tv.local` on your phone. Netflix-style **device code + QR** via a phone companion is the next PR. Setup never asks for a password on the TV and no longer opens the nest to type.
3. **Home** — harvested shelves + OTA when configured. An empty Continue Watching row shows a **Connect account (phone)** shelf.

Setup state: `~/.config/zappe/setup.json`.

### Phone companion (next)

Not in this PR. Planned: a small companion at `http://zappe-tv.local` that shows a device code and QR so the TV only displays “open this on your phone.” Do not add on-TV password fields.

## Custom branding (appliance, not git)

The guide ships only in-code defaults (`Zappe` + Apple TV–black chrome). The official **feras TV** lockup (hugging tan dog **Beto** + tuxedo cat **Lek**, beige circle, lowercase “feras”, “— TV —” — names are not shown in the UI) lives in the **user data dir** so it is never committed. Do not invent substitute marks.

Preferred path (Linux XDG):

```
~/.local/share/zappe/{branding.json,logo.png,splash.png}
```

`$ZAPPE_DATA_DIR` overrides that directory. Put the official PNG next to `branding.json` (**not in git**). ~1.4MB is fine — the Rust loader emits a data URL (8 MiB cap). Vite preview serves the same files over `/__zappe_branding__/` so the browser path does not base64 the PNG.

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/branding.json ~/.local/share/zappe/branding.json
cp /path/to/official-feras-tv.png ~/.local/share/zappe/logo.png
systemctl --user restart zappe.service
```

| Field | Meaning |
| --- | --- |
| `name` | `feras TV` on the appliance. Default in code is `Zappe`. No pet names / taglines. |
| `accent` | Warm beige from the art (`#C4A574`). |
| `logo` / `splash` | Official lockup (`png` / `jpg` / `webp` / `gif`), shown with `object-fit: contain`. |
| `idle` | `{ mode, asset, timeoutSeconds, animation }` — screensaver after ~2 min |
| `theme` | `{ style: "apple-tv", background: "#000000", focusRing: "subtle-scale" }` |

Missing file → defaults. Invalid JSON → log and fall back. See [`packaging/appliance/examples/README.md`](packaging/appliance/examples/README.md).

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

Hyprland 0.56 nudges use `hyprctl eval` + `hl.dsp.*` only. Never `hyprctl dispatch` / `dispatch exec` (rejected on Lua sessions). Gamescope, Chrome, zap, and mpv are spawned from Rust. Failures are logged and ignored.

## Remote (ATONGX air mouse — no keyboard)

Full button contract: [`docs/atongx-input.md`](docs/atongx-input.md) and [`src/lib/atongx-map.json`](src/lib/atongx-map.json) (`classifyAtongx` / `classifyAtongxEvkey` in [`src/lib/remoteMap.ts`](src/lib/remoteMap.ts)). Consumer Control keys are read from evdev — not Tauri global shortcuts — because `BrowserBack` / `BrowserHome` / `MediaPlayPause` / `ContextMenu` fail to register under gamescope. Capture a new remote with [`packaging/appliance/capture-atongx.sh`](packaging/appliance/capture-atongx.sh).

| Button | This PR |
| --- | --- |
| D-pad + OK (orange ring) | Guide focus / activate (stays with nest / mpv when playing) |
| Home, Back, DEL | Return to guide / end playback (also global while nest is up) |
| Play / Pause | Space into Chrome nest or mpv |
| PAGE up / down | Jump a shelf |
| Mic (red) | Whisper pt-BR — `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo` |
| Air-mouse cursor toggle | Shows / hides CSS pointer (`tv-pointer-on`) |
| VOL +/−, Mute | `pactl` ±5% / mute (guide + global) |
| Menu | Open Connect (returns to guide first if playing) |
| Power | Return to guide — never shuts the box down |

This is Zappe HID mapping, not a Samsung TV API. Whisper: `ZAPPE_WHISPER_BIN` (`-l pt`). `ZAPPE_VOICE_FAKE=abrir netflix` for tests. Phone companion + device codes remain the login path — never on-TV typing.

## Environment

| Variable | Purpose |
| --- | --- |
| `CHROME_PATH` | Chrome binary |
| `GAMESCOPE_PATH` | gamescope binary |
| `ZAPPE_NEST` | `gamescope` (default) or `chrome` |
| `ZAPPE_NEST_WIDTH` / `ZAPPE_NEST_HEIGHT` | nest size (default 1920×1080) |
| `ZAPPE_DATA_DIR` | override `~/.local/share/zappe` (catalog, Chrome profile, `branding.json`) |
| `ZAPPE_HARVEST_FIXTURE` | a11y JSON dump (skip live AT-SPI) |
| `ZAPPE_A11Y_DUMP` | write the live tree to this path |
| `ZAPPE_OTA_CHANNELS` | colon-separated `channels.conf` paths |
| `ZAPPE_AUTO_HARVEST` | `1` to harvest on Home mount (debug only; default off) |
| `ZAPPE_SRC` | appliance updater checkout (default `~/src/zappe`) |
| `ZAPPE_WHISPER_BIN` | whisper.cpp binary for the red-mic button (pt-BR) |
| `ZAPPE_VOICE_FAKE` | fake transcript for voice tests (no mic) |

## OTA

Defaults (when `ZAPPE_OTA_CHANNELS` is unset): `$HOME/tv/channels.conf`, then `~/.config/zappe/channels.conf`. HD preferred; 1Seg filtered.

```bash
dvbv5-zap -a 0 -c … -p "Channel Name" -r -o - | mpv --hwdec=no --vo=gpu \
  --demuxer-lavf-format=mpegts --demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal -
```

Play fails fast (guide stays / is restored) if `/dev/dvb/adapter0` is missing or zap/mpv exits immediately. stderr lands in `~/.local/share/zappe/ota-pipeline.log`.

## Appliance auto-update

The living-room box (`zappe-tv`, user `glaet`) polls `origin/main` on a user systemd timer. A merge to `main` is picked up within ~20 minutes (or ~2 minutes after boot). No inbound ports, GitHub webhook, or Tailscale.

Force an update now:

```bash
systemctl --user start zappe-update.service
journalctl --user -u zappe-update.service -n 50
# also: ~/.local/share/zappe/update.log
```

### One-time install

Repo is expected at `~/src/zappe` (`ZAPPE_SRC` overrides). If the checkout is elsewhere, edit `ZAPPE_SRC` and `ExecStart` in the copied units. Copy the units, linger so user systemd runs without a login, then enable the timer:

```bash
mkdir -p ~/.config/systemd/user
cp packaging/appliance/zappe-update.service \
   packaging/appliance/zappe-update.timer \
   ~/.config/systemd/user/
# optional — only if you do not already have a kiosk unit
cp packaging/appliance/zappe.service ~/.config/systemd/user/
chmod +x packaging/appliance/zappe-update.sh packaging/appliance/zappe-kiosk.sh

loginctl enable-linger "$USER"
systemctl --user daemon-reload
systemctl --user enable --now zappe-update.timer
systemctl --user enable --now zappe.service   # kiosk, if using the example unit
```

Expected kiosk unit name is **`zappe.service`**. Passwordless `sudo -n` is optional and only used to also install `/usr/local/bin/zappe`. The kiosk `PATH` prefers `~/bin`.

### What the updater does

1. `git fetch origin main` in `ZAPPE_SRC` (default `~/src/zappe`).
2. Compare `origin/main` to `~/.local/share/zappe/installed-sha` (directory created if needed). Unchanged → exit 0 quietly.
3. If the working tree is dirty, log and exit non-zero. Never `reset --hard` or wipe local changes. Never touches `~/.local/share/zappe/chrome-profile`.
4. `git checkout main && git pull --ff-only`.
5. `npm ci` (or `npm install`), then **build the binary only** (see below).
6. Install to `~/bin/zappe` and, if `sudo -n` works, `/usr/local/bin/zappe`.
7. Write the new SHA; `systemctl --user restart zappe.service` if that unit exists. Safe when gamescope/zappe is not running.

### Build path (AppImage must not block updates)

`npm run tauri build` (and `bundle.targets = "all"` in `src-tauri/tauri.conf.json`) packages AppImage/deb via linuxdeploy. That step can fail after the executable is already linked.

Appliance updates **do not** run the bundler. They **must** go through the Tauri CLI so `cfg(dev)` is off and the binary embeds `frontendDist` instead of `http://localhost:1420`:

```bash
npm run tauri -- build --no-bundle
# binary: src-tauri/target/release/zappe
```

Never `cargo build --release` alone for the appliance — that leaves `cfg(dev)` on and the kiosk on `http://localhost:1420`. `npm run check:updater` guards the script.

## Out of scope (v1)

- Jev / UI-TARS / RL mutation loops
- Prime / Disney harvest (skill files can plug in later)
- Perfect “never flash Netflix chrome”
- Full HID teach-mode recording

## Legacy HUD

The old wgpu HUD lives under `legacy-hud/` and is not the product path.
