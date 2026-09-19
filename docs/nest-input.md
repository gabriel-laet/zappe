# Nest input routing

Living-room remote (ATONGX / XING WEI 2.4G) must drive **Netflix inside the Chrome nest**, not only the Tauri guide.

This is Zappe HID → nest injection. No CDP, no `--enable-automation`. Volume / mute are a separate Samsung Device Connect path — see [`samsung-tv.md`](samsung-tv.md).

## Stack

```text
ATONGX USB composite
  event3  keyboard (D-pad, OK, Back, …)
  event4  air-mouse
  event5  consumer (Play/Pause, VOL, …)
        │
        ▼
outer gamescope --backend drm -e -f -- zappe     (kiosk)
        │
        ├── Tauri guide (hidden while playing)
        └── nest: gamescope -W 1920 -H 1080 -f -- google-chrome-stable
                    └── Chrome Xwayland (Netflix spatial nav)
```

Gamescope is a micro-compositor. The nest has its **own** Wayland + Xwayland. Keys injected on the *outer* `WAYLAND_DISPLAY` (or typed as text by `wtype`) never reach Chrome.

Hyprland `hl.dsp.*` nudges help on a developer desktop. They do nothing on the appliance kiosk — the outer compositor *is* gamescope.

## What was broken

| Symptom | Cause |
| --- | --- |
| Play/Pause does nothing in Netflix | `wtype space` is **text mode** (types `s-p-a-c-e`) and exits 0, so `xdotool` never ran |
| D-pad / OK do nothing in Netflix | Never injected. Design said “leave them on the focused surface”, but the hidden guide or the **kiosk** gamescope PID kept focus |
| `nest_window_pids()` focused the kiosk | `pgrep -f gamescope` matches `gamescope -- zappe` first |
| Air-mouse “pointer” | Only toggled guide CSS (`tv-pointer-on`). No host cursor in the nest |

## Routing (this PR)

```text
                    ┌─ Idle / guide ── D-pad+OK stay in React (shelves)
ATONGX key ─────────┤
                    └─ Chrome nest ── grab D-pad+OK+Space (atongx.rs)
                                      │
                                      ▼
                               nest_input::inject_action
                                      │
                         1. focus nest window on the host
                         2. read Chrome /proc/pid/environ
                            (DISPLAY=:N, GAMESCOPE_WAYLAND_DISPLAY)
                         3. xdotool on that DISPLAY   ← usual path
                         4. wtype -k on nest Wayland
                         5. host xdotool / wtype -k
                         6. ydotool evdev (uinput)
```

| Input | Guide visible | Nest playing | OTA / mpv |
| --- | --- | --- | --- |
| D-pad / OK | Shelf focus (React) | Arrows / Return into Chrome (Enter = click if pointer mode) | Left with mpv (not grabbed) |
| Play/Pause | no-op | Space into nest | Space into mpv |
| Air-mouse toggle | CSS cursor on guide | Focus nest + center host pointer; OK becomes `xdotool click 1` | CSS only |
| Air-mouse motion (event4) | Guide pointer (if CSS on) | Real cursor once nest window is focused | mpv |
| Back / Home | — | **Return to guide** via evdev (`hid.rs`). Not injected as Netflix Back | Stop mpv |

Back / Home are owned by the evdev watcher in `hid.rs` (consumer-control grab). This module only injects navigation, play/pause, and pointer clicks. Shared helpers: `NestManager::send_key`, `nest_window_pids` (inner gamescope only), `nest_input::inject_key`.

## Injectors

Preferred order for a key named `space` / `up` / `ok`:

1. `DISPLAY=<chrome> xdotool key --clearmodifiers space` (Chrome is Xwayland in gamescope)
2. `WAYLAND_DISPLAY=<gamescope-N> wtype -k space` (only if the nest exposes virtual-keyboard)
3. Same tools on the host display after `windowactivate` gamescope
4. `ydotool key 57:1 57:0` (kernel uinput — needs membership in `input` / a running daemon)

`wtype` **must** use `-k <keysym>`. Never `wtype space`.

## Pointer mode

`remote_pointer` (F2 / global shortcut) toggles a Rust flag + `guide-pointer` for CSS.

When the nest is up and the flag is on:

1. Focus the nest gamescope window so ATONGX **event4** moves Chrome’s cursor
2. `xdotool mousemove` to the nest center (makes the cursor visible)
3. OK sends `click 1` instead of Return

D-pad still sends arrows in pointer mode (Netflix spatial nav + mouse coexist).

## Manual verify (appliance)

Tools: `wtype` and `xdotool` already on the box. `ydotool` optional.

```bash
# After opening Netflix from the guide
pgrep -af gamescope
# expect TWO lines: kiosk `-- zappe` and nest `-- google-chrome-stable`

# Chrome's nest display (Xwayland inside the inner gamescope)
tr '\0' '\n' < /proc/$(pgrep -n -f 'user-data-dir=.*chrome-profile')/environ \
  | grep -E '^(DISPLAY|WAYLAND_DISPLAY|GAMESCOPE_WAYLAND_DISPLAY)='

# Should move Netflix focus without touching the guide
DISPLAY=:N xdotool key --clearmodifiers Right
DISPLAY=:N xdotool key --clearmodifiers Return
```

On the couch:

1. Open Netflix (or any stream tile). Guide hides, nest fullscreen.
2. D-pad moves the Netflix focus ring; OK activates the title.
3. Play/Pause toggles playback (Space).
4. Press the air-mouse cursor button: move the remote — a real pointer moves in Netflix; OK (or the mouse button) clicks.
5. Back / Home return to the guide via evdev (`hid.rs`). Do not expect Netflix in-app Back from those keys.

## Appliance notes

`npm run tauri -- build --no-bundle` on the box. Restart `zappe.service`.

`xdotool` is enough when Chrome is Xwayland (default). `ydotool` needs `/dev/uinput` (`input` group or `ydotoold`). No Chrome flags are added.
