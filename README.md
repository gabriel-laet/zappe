# Zappe

Personal living-room / desktop launcher. A native **HUD** sits on top of the room; **Netflix, Prime Video, Disney+, and YouTube play in a real Google Chrome window** you already logged into. On Linux, **terrestrial ISDB-Tb / DVB channels** (MyGica-class USB tuners) can appear as an **OTA TV** row and play through **`dvbv5-zap` + `mpv`**, locally over RF — not through Chrome.

This repo is the first runnable sketch: CRT-looking wgpu HUD, placeholder guide rows, and optional Chrome attach over CDP.

## What it is

- A always-on-top native HUD (`winit` + `wgpu`) with a scanline / phosphor shader and a tiny immediate-mode tile grid. No Electron, no Tauri, no in-process webview.
- A remote control for a **separate Chrome process** with a dedicated `--user-data-dir` (`…/zappe/chrome-profile`). You log in by hand, once. Zappe then opens first-party URLs and sends a few CDP / DOM pokes.
- **OTA TV (Linux):** when `channels.conf` is configured, tiles list scanned terrestrial channels. Enter runs `dvbv5-zap` piped into `mpv`; Back / Escape tears down both processes cleanly.
- Living-room input: a dummy TV remote that looks like a keyboard, plus a constrained command language (on-screen bar / stdin / whisper.cpp hook). Not a chat bot.
- Window control via CDP `Browser.setWindowBounds` (fullscreen / raise). macOS `osascript` and Linux `wmctrl` are documented best-effort stubs for when the OS still needs a nudge.

## What it is not

- Not an unofficial Netflix / Prime / Disney / YouTube player.
- Not a scraper. Catalog rows are placeholders. Public metadata is a stub trait (`StubMetadata`) for a later JustWatch-style source.
- Not an embed of those sites, and not a DRM unpacker. Zappe does not extract, record, or re-encode protected video.
- Not a cookie harvester. Session state stays inside Chrome's profile directory. This repo ships no secrets.

## How to run

Needs Rust 1.88+ (the `rust-toolchain.toml` pins `stable`). Builds on macOS and Linux.

On Linux you need a working GPU stack at **runtime** (Vulkan ICD or GL/EGL) plus `libxkbcommon-x11`. Debian/Ubuntu:

```bash
sudo apt install libxkbcommon-x11-0 mesa-vulkan-drivers libegl1
```

```bash
cargo run
```

That is HUD-only. A cheap HDMI-CEC / USB / 2.4 GHz dummy remote that enumerates as a keyboard works on first run — see the keymap below.

Drive a real Chrome profile:

```bash
# first time: Chrome opens the Zappe profile. Log into Netflix / Prime / Disney / YouTube yourself.
cargo run -- --chrome --service youtube

# later sessions reuse ~/.local/share/zappe/chrome-profile (macOS: ~/Library/Application Support/zappe/chrome-profile)
ZAPPE_CHROME=1 cargo run -- --service prime
```

If Chrome is missing, the HUD still starts and the status line says so. Override the binary with `CHROME_PATH`.

Attach to an already-running DevTools endpoint instead of launching:

```bash
# you started Chrome yourself with the zappe user-data-dir and --remote-debugging-port=9222
cargo run -- --cdp http://127.0.0.1:9222 --url https://www.netflix.com
```

`--chrome` / `ZAPPE_CHROME` and `--cdp` / `ZAPPE_CDP` are the feature flags. `cargo run` never requires Chrome.

### Terrestrial OTA (ISDB-Tb / DVB on Linux)

Zappe does **not** decode broadcast MPEG itself. It spawns the same tools you would use from a shell: **`dvbv5-zap`** (from [v4l-utils](https://www.linuxtv.org/wiki/index.php/V4l-utils)) writes a transport stream to stdout; **`mpv`** reads that pipe. That keeps the Chrome CDP path untouched — OTA is additive.

Typical setup (Brazil ISDB-Tb, MyGica S270-class stick with `smsusb` / `smsdvb`, adapter `/dev/dvb/adapter0`):

```bash
sudo apt install v4l-utils mpv libxkbcommon-x11-0 mesa-vulkan-drivers libegl1
# scan once with your stick (example — use your local transponder list):
# dvbv5-scan … > ~/tv/channels.conf
export ZAPPE_OTA_CHANNELS="$HOME/tv/channels.conf"
cargo run
```

Channel tiles are built from **`[Channel Name]`** sections in `channels.conf`. The name passed to `dvbv5-zap -p` must match exactly (e.g. `Globo HD`).

| Variable / flag | Meaning |
| --- | --- |
| `--ota-channels` / `ZAPPE_OTA_CHANNELS` | Path to `channels.conf` |
| (default) | `~/tv/channels.conf` when that file exists |
| `ZAPPE_DVB_ADAPTER` | DVB adapter index for `dvbv5-zap -a` (default `0`) |
| `ZAPPE_DVBV5_ZAP` | `dvbv5-zap` binary (default: on `PATH`) |
| `ZAPPE_MPV` | `mpv` binary (default: on `PATH`) |
| `ZAPPE_OTA_ZAP_LOG` | stderr log from zap (default `/tmp/zappe-zap.log`) |

Do **not** open `/dev/dvb/adapter0/dvr0` from a second process while zap holds the tuner via `-o -`. Avoid `dvbv5-zap -P` (full mux) if it confuses the demuxer — Zappe uses `-r -o -` like a manual pipe.

Focus an **OTA TV** tile and press **Enter** to tune; **Escape / Back** stops playback and returns to the HUD. A few seconds of black video until the H.264 IDR is normal on live OTA.

### Remote / keyboard (must-have)

Cheap HDMI-CEC, USB, and 2.4 GHz remotes show up as a keyboard. Zappe reads them in `winit` (physical `KeyCode` plus `NamedKey` aliases). Volume stays with the OS.

| Remote / key | Action |
| --- | --- |
| Arrows | move the **big amber focus ring** across tiles and rows |
| Enter / Return | activate the focused tile (Chrome URL or OTA tune) |
| Escape / Backspace / BrowserBack | back (close the command bar, show the HUD, stop OTA or Chrome history back) |
| Home / BrowserHome | root of the guide (first tile of Continue) |
| Space / MediaPlayPause | play-pause the Chrome player skill |
| `p` | play (keyboard extra) |
| `/` | open the on-screen command bar |
| `h` | hide / show HUD so you can use the Chrome window |
| `q` | quit HUD (keyboard only) |
| 0–9 | reserved for later |
| Volume | OS / AVR — Zappe does not eat these keys |

Point a dummy remote at the HUD and use arrows + OK + Back. The focus ring is a fat stroke, not a 1px outline.

### Voice and the command bar

Voice is **local whisper.cpp only**, not a chat LLM and not an unconstrained agent. After transcription, text is parsed with a tiny grammar — the same parser as the on-screen bar, `--say`, and stdin:

```text
play <query> [on youtube|netflix|prime|disney]
search <query> [on youtube|netflix|prime|disney]
pause | fullscreen | back | home
```

whisper.cpp is a stub/hook in this sketch (`--whisper` / `ZAPPE_WHISPER` / `ZAPPE_WHISPER_BIN`). It does not download a model. Until a binary is on PATH, type into the HUD (`/` then Enter) or:

```bash
cargo run -- --say "play lofi on youtube"
echo "pause" | cargo run -- --cmd-stdin
```

Unknown utterances (`what's the weather`) are rejected. No general-purpose tool calling.

## How login works

1. Zappe launches **Google Chrome or Chromium**, not a webview, with a dedicated user-data-dir. It does not touch your default Chrome profile.
2. You sign in to Netflix / Prime / Disney / YouTube in that window, the same way you would in any Chrome. MFA, passwords, cookies — all Chrome's problem.
3. Next `cargo run -- --chrome` reuses the same profile. Zappe never reads or copies those cookies.

## How CDP attaches

1. **Launch path:** `chromiumoxide` starts Chrome headed (`with_head()`), points `--user-data-dir` at the Zappe profile, and talks to the DevTools WebSocket it advertised.
2. **Attach path:** `--cdp http://127.0.0.1:9222` connects to a Chrome you started yourself (same profile, `--remote-debugging-port=9222`).
3. After attach, Zappe `Page.navigate`s to a first-party URL (`https://www.netflix.com`, `https://www.primevideo.com`, `https://www.disneyplus.com`, `https://www.youtube.com`, or a tile deep-link). YouTube prefers official `/watch?v=`, `/feed/subscriptions`, and `/results?search_query=` links. It does not pull their HTML into the catalog.
4. A tiny **site skill** (`src/skills.rs`) may then search / open / pause / play / fullscreen / back. Selectors are isolated in that file and treated as fragile — they will break; that is expected.
5. Raise / fullscreen uses **`Browser.getWindowForTarget` + `Browser.setWindowBounds`**. If the window manager ignores that:

   - macOS stub: `osascript` to front the Chrome process
   - Linux stub: `wmctrl -a 'Google Chrome'`

   Both are best-effort and must not be required to build or to show the HUD.

## Layout

```
src/main.rs       CLI + winit remote/keyboard loop
src/command.rs    constrained play/pause/search grammar
src/voice.rs      whisper.cpp hook (no model, no LLM)
src/hud.rs        wgpu CRT pass + tiles + fat focus ring
src/shaders/      crt.wgsl, quad.wgsl
src/chrome.rs     spawn / attach, CDP window bounds, OS stubs
src/catalog.rs    placeholder rows + OTA row from channels.conf
src/ota.rs        dvbv5-zap → mpv pipeline, process lifecycle
src/skills.rs     per-site search / open / pause / play / fullscreen / back
```

egui was skipped: a fullscreen WGSL pass plus a few instanced quads is enough for a TV-guide HUD and keeps the shaders in-tree.

## Status

First sketch. The HUD is real. Chrome attach is real, and feature-flagged. **OTA TV** is real on Linux when `channels.conf` is present. Catalog metadata and site skills are placeholders by design.
