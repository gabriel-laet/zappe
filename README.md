# Zappe

Personal living-room / desktop launcher. A native **HUD** sits on top of the room; **Netflix, Prime Video, and Disney+ play in a real Google Chrome window** you already logged into.

This repo is the first runnable sketch: CRT-looking wgpu HUD, placeholder guide rows, and optional Chrome attach over CDP.

## What it is

- A always-on-top native HUD (`winit` + `wgpu`) with a scanline / phosphor shader and a tiny immediate-mode tile grid. No Electron, no Tauri, no in-process webview.
- A remote control for a **separate Chrome process** with a dedicated `--user-data-dir` (`…/zappe/chrome-profile`). You log in by hand, once. Zappe then opens first-party URLs and sends a few CDP / DOM pokes.
- Window control via CDP `Browser.setWindowBounds` (fullscreen / raise). macOS `osascript` and Linux `wmctrl` are documented best-effort stubs for when the OS still needs a nudge.

## What it is not

- Not an unofficial Netflix / Prime / Disney player.
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

That is HUD-only. Arrow keys move the focus, Enter "zaps" (logs the URL if Chrome is off), `q` quits, `esc` / `h` show the HUD again.

Drive a real Chrome profile:

```bash
# first time: Chrome opens the Zappe profile. Log into Netflix / Prime / Disney yourself.
cargo run -- --chrome --service netflix

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

### Keys

| Key | Action |
| --- | --- |
| arrows | move the guide focus |
| enter | open the focused tile's first-party URL |
| space | pause skill (fragile per-site selector) |
| f | fullscreen via CDP `setWindowBounds` + site skill |
| backspace | history back |
| esc | show HUD (Chrome stays running) |
| h | hide / show HUD so you can use the Chrome window |
| q | quit HUD |

## How login works

1. Zappe launches **Google Chrome or Chromium**, not a webview, with a dedicated user-data-dir. It does not touch your default Chrome profile.
2. You sign in to Netflix / Prime / Disney in that window, the same way you would in any Chrome. MFA, passwords, cookies — all Chrome's problem.
3. Next `cargo run -- --chrome` reuses the same profile. Zappe never reads or copies those cookies.

## How CDP attaches

1. **Launch path:** `chromiumoxide` starts Chrome headed (`with_head()`), points `--user-data-dir` at the Zappe profile, and talks to the DevTools WebSocket it advertised.
2. **Attach path:** `--cdp http://127.0.0.1:9222` connects to a Chrome you started yourself (same profile, `--remote-debugging-port=9222`).
3. After attach, Zappe `Page.navigate`s to a first-party URL (`https://www.netflix.com`, `https://www.primevideo.com`, `https://www.disneyplus.com`, or a tile deep-link). It does not pull their HTML into the catalog.
4. A tiny **site skill** (`src/skills.rs`) may then click pause / fullscreen / back. Selectors are isolated in that file and treated as fragile — they will break; that is expected.
5. Raise / fullscreen uses **`Browser.getWindowForTarget` + `Browser.setWindowBounds`**. If the window manager ignores that:

   - macOS stub: `osascript` to front the Chrome process
   - Linux stub: `wmctrl -a 'Google Chrome'`

   Both are best-effort and must not be required to build or to show the HUD.

## Layout

```
src/main.rs       CLI + winit event loop
src/hud.rs        wgpu CRT pass + immediate tiles / bitmap font
src/shaders/      crt.wgsl, quad.wgsl
src/chrome.rs     spawn / attach, CDP window bounds, OS stubs
src/catalog.rs    placeholder rows + MetadataSource stub
src/skills.rs     per-site open / pause / fullscreen / back
```

egui was skipped: a fullscreen WGSL pass plus a few instanced quads is enough for a TV-guide HUD and keeps the shaders in-tree.

## Status

First sketch. The HUD is real. Chrome attach is real, and feature-flagged. Catalog, metadata, and site skills are placeholders by design.
