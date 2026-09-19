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

Playback is a URL into the nest (deep link when harvest found one). Play/pause, D-pad, and OK are injected into the **nest** compositor (`xdotool` on Chrome’s gamescope `DISPLAY`, then `wtype -k`, then `ydotool`). Hyprland `hl.dsp.*` nudges only raise the gamescope *window* on a desktop session — they are not how keys reach Netflix. Design: [`docs/nest-input.md`](docs/nest-input.md).

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

- `xdotool` (preferred) plus `wtype` or `ydotool` — nest D-pad / OK / play/pause / Escape. `wtype` must use `-k` (keysym), not text.
- `dvbv5-tools` + `mpv` — OTA / TV aberta
- Channel list: `~/tv/channels.conf`, `~/.config/zappe/channels.conf`, or `~/.local/share/zappe/channels.conf` (`ZAPPE_OTA_CHANNELS` overrides)
- `xorg-server-xvfb` — optional invisible login backend (`ZAPPE_LOGIN_BACKEND=xvfb`). Default login uses the same hidden Chrome path as harvest (no second gamescope).

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

### First Sync on the appliance

Home never harvests on mount (that stole the HDMI nest). Use the **Sync Netflix** tile only.

1. Netflix must already be signed in on the shared profile: `~/.local/share/zappe/chrome-profile`.
2. AT-SPI: `busctl --user get-property org.a11y.Bus /org/a11y/bus org.a11y.Status IsEnabled`. Sync sets this `true` when it is `false`; if it stays off you get a guide toast naming `IsEnabled`.
3. Skill file: `ls ~/.local/share/zappe/skills/netflix.continue_watching.v1.yaml` (updater + first launch plant it; the binary also embeds the YAML).
4. On Home, focus **Sync Netflix** (or **Retry sync**) and press OK. Stay on the guide — harvest launches Chrome minimized / without gamescope `-f`, then hides the nest.
5. **Success:** toast `harvested N titles`; `~/.local/share/zappe/catalog.json` has a `continue` shelf with `status: ok` and `rows`; Home tiles refresh via `catalog-changed`.
6. **Failure:** error toast (a11y still disabled, skill missing, or empty Continue Watching). `catalog.json` still updates with `status` + `message` so you can read it on the box.

```bash
# After Sync
test -s ~/.local/share/zappe/catalog.json && python - <<'PY'
import json, pathlib
p = pathlib.Path.home() / ".local/share/zappe/catalog.json"
data = json.loads(p.read_text())
shelf = next((s for s in data.get("shelves", []) if s.get("id") == "continue"), None)
print("status", shelf and shelf.get("status"), "rows", shelf and len(shelf.get("rows") or []))
print((shelf or {}).get("message") or "")
for row in (shelf or {}).get("rows") or []:
    print("-", row.get("title"))
PY
```

## First-run setup

The living-room box has **no physical keyboard**. Couch input is the TV remote plus a phone.

1. **Browser on this TV** — Chrome already on `PATH` (or `CHROME_PATH`). That is an appliance install, not a couch typing step.
2. **Connect account (phone)** — TV shows a live **device code + QR** and a source picker (Netflix, Prime, Disney+, YouTube). Open `http://zappe-tv.local` (or scan). Sign in on the phone. Setup never asks for a password on the TV and never fullscreens Chrome for typing.
3. **Home** — harvested shelves + OTA when configured. An empty Continue Watching row shows a **Connect account (phone)** shelf. ATONGX **Menu** opens Connect; **Back** dismisses it.

Setup state: `~/.config/zappe/setup.json`.

### Phone companion

LAN HTTP server on the appliance (`zappe-companion`, also started by the guide if the port is free). Couch steps and how Chrome stays off HDMI: [`docs/companion.md`](docs/companion.md).

```bash
# appliance (port 80 via systemd capabilities)
cp packaging/appliance/zappe-companion.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now zappe-companion.service
```

The TV only displays the code / QR / “Connected”. Email and password stay on the phone. Each Apps source writes its session into `~/.local/share/zappe/chrome-profile` via the **same hidden Chrome path as harvest** (no second gamescope, `--start-minimized`). Netflix is the wired adapter (then Sync Continue Watching). Prime / Disney+ / YouTube use the same UX with stub cookie checks. Do not add on-TV password fields.

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
| `logo` / `splash` | Official lockup (`png` / `jpg` / `webp` / `gif`). Header clips it to a circle so an opaque white plate still reads as a badge on black — prefer a transparent PNG. |
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

Harvest **and** phone login share one invisible nest: the same Chrome profile, **no gamescope** (a second DRM nest would steal HDMI), **no `-f`**, `--start-minimized` + off-screen window, then hide and raise the guide. Play still wraps gamescope fullscreen. Netflix chrome may flash briefly on some sessions. See [`docs/companion.md`](docs/companion.md).

`ZAPPE_NEST=chrome` skips gamescope (dev fallback). `ZAPPE_HARVEST_FIXTURE` / `zappe-harvest --fixture` skip the live dump.

## Skills

Bundled: [`skills/netflix.continue_watching.v1.yaml`](skills/netflix.continue_watching.v1.yaml).

User overrides: `~/.local/share/zappe/skills/*.yaml` (same `id` wins).

A skill is: **open URL → wait → a11y find anchors → extract rows**. If anchors miss, we do **not** invent a path. Teach-mode is the extension point (`TeachRecorder` in `src-tauri/src/teach.rs`).

## Window handoff

| Phase | Behavior |
| --- | --- |
| Guide | Tauri visible, D-pad on shelves |
| Harvest / login | Chrome (no gamescope) minimized / off-screen, dump or sign-in, hide nest; guide stays up |
| Stream tile | Guide hides → gamescope fullscreen |
| OTA | Guide hides → mpv fullscreen |
| Back / Home / Power | Kill nest child gamescope (never the kiosk) or stop mpv → guide fullscreen + focus. Evdev on the XING WEI dongle; Power never shuts the box down. |
| Quit | Stop OTA; kill a Zappe-launched nest |

Hyprland 0.56 nudges use `hyprctl eval` + `hl.dsp.*` only. Never `hyprctl dispatch` / `dispatch exec` (rejected on Lua sessions). Gamescope, Chrome, zap, and mpv are spawned from Rust. Failures are logged and ignored.

## Remote (ATONGX air mouse — no keyboard)

Full button contract: [`src/lib/atongx-map.json`](src/lib/atongx-map.json) (single table), [`docs/atongx-input.md`](docs/atongx-input.md), and `classifyAtongx` in [`src/lib/remoteMap.ts`](src/lib/remoteMap.ts). Capture on the box: `./packaging/appliance/zappe-atongx-capture --mic-pointer`.

| Button | This PR |
| --- | --- |
| D-pad + OK (orange ring) | Guide focus / activate. While Netflix is up: arrows + Return into the nest |
| Home, Back, DEL | Return to guide / end playback. Evdev on XING WEI (`KEY_BACK` 158 / `KEY_HOMEPAGE` 172) so nest Chrome cannot eat them. |
| Play / Pause | Space into Chrome nest or mpv |
| PAGE up / down | Jump a shelf |
| Mic (red) | Whisper pt-BR — `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo`. Evdev `KEY_SEARCH` **217** / `KEY_VOICECOMMAND` **582** (locked). |
| Air-mouse cursor toggle | Guide CSS (`tv-pointer-on`). Nest: real pointer + click. Evdev `KEY_F2` **60** / `KEY_TOUCHPAD_TOGGLE` **530** (locked). |
| VOL +/−, Mute | Samsung TV Device Connect (`KEY_VOLUP` / `KEY_VOLDOWN` / `KEY_MUTE`). Pulse `pactl` if the TV is unreachable (one toast). |
| Menu | Open Connect (returns to guide first if playing) |
| Power | Return to guide — never shuts the box down, never sends `KEY_POWER` to the TV |

Nest nav is Zappe HID. Volume / mute are the Samsung websocket — [`docs/samsung-tv.md`](docs/samsung-tv.md). Whisper: `ZAPPE_WHISPER_BIN` (`-l pt`). `ZAPPE_VOICE_FAKE=abrir netflix` for tests. Phone companion + device codes are the login path — never on-TV typing.

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
| `ZAPPE_HID_DISABLE` | `1` skips the ATONGX evdev watcher |
| `ZAPPE_HID_NAME` | extra substring to match `/dev/input` names |
| `ZAPPE_SAMSUNG_HOST` | Samsung TV IP (default `192.168.3.6`) |
| `ZAPPE_SAMSUNG_PORT` | Device Connect `ws://` port (default `8001`) |
| `ZAPPE_SAMSUNG_SECURE_PORT` | `wss://` fallback (default `8002`) |
| `ZAPPE_SAMSUNG_TOKEN` | Device Connect token (prefer `~/.local/share/zappe/samsung.json`) |
| `ZAPPE_SAMSUNG_NAME` | Client name shown on the TV pair popup (default `Zappe`) |
| `ZAPPE_SAMSUNG_DISABLE` | `1` skips Samsung and uses `pactl` only |
| `ZAPPE_COMPANION_HOST` | public name on the QR (`zappe-tv.local`) |
| `ZAPPE_COMPANION_PORT` | bind port (default try `80`, then `8780`) |
| `ZAPPE_COMPANION_BIND` | bind address (default `0.0.0.0`) |
| `ZAPPE_COMPANION` | `0` / `remote` — only poll an existing companion |
| `ZAPPE_LOGIN_BACKEND` | `xvfb` to force a virtual display; default is harvest-style minimized Chrome |
| `ZAPPE_LOGIN_FAKE` | `1` — mark the selected source connected (tests) |

## OTA

Defaults (when `ZAPPE_OTA_CHANNELS` is unset): `$HOME/tv/channels.conf`, then `~/.config/zappe/channels.conf`, then `~/.local/share/zappe/channels.conf`. HD preferred; 1Seg filtered. `Globo` / `Record` / `SBT` resolve onto the HD section name in the conf.

Selecting another Canais tile **always stops the previous process group** (`dvbv5-zap` + `mpv`) before starting the next. The guide stays up until the new pipeline is alive, then hides. Missing conf or `/dev/dvb/adapter0` is a toast / Canais error tile — not a silent fail.

```bash
dvbv5-zap -a 0 -c … -p "Channel Name" -r -o - | mpv --hwdec=no --vo=gpu \
  --demuxer-lavf-format=mpegts --demuxer-lavf-analyzeduration=5 --cache=yes --fs --no-terminal -
```

Play fails with a restore if the adapter or conf is missing, or if zap/mpv exits immediately. stderr lands in `~/.local/share/zappe/ota-pipeline.log`.

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
   packaging/appliance/zappe-companion.service \
   ~/.config/systemd/user/
# optional — only if you do not already have a kiosk unit
cp packaging/appliance/zappe.service ~/.config/systemd/user/
chmod +x packaging/appliance/zappe-update.sh packaging/appliance/zappe-kiosk.sh

loginctl enable-linger "$USER"
systemctl --user daemon-reload
systemctl --user enable --now zappe-update.timer
systemctl --user enable --now zappe-companion.service
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
- Perfect harvest “never flash Netflix chrome” (login uses Xvfb / hidden nest)
- Full HID teach-mode recording

## Legacy HUD

The old wgpu HUD lives under `legacy-hud/` and is not the product path.
