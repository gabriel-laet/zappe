# ATONGX air mouse — input contract

Living-room box: **no physical keyboard**. Couch input is this remote plus a phone at `zappe-tv.local` (device code + QR — see [`docs/companion.md`](companion.md)).

Source of truth: [`src/lib/atongx-map.json`](../src/lib/atongx-map.json) (physical button → linux `KEY_*` → action). TypeScript reads it via `classifyAtongx` in [`src/lib/remoteMap.ts`](../src/lib/remoteMap.ts). Rust evdev (`src-tauri/src/hid.rs`) loads the same JSON. Nest nav is Zappe HID. Volume / mute are Samsung Device Connect keys — see [`samsung-tv.md`](samsung-tv.md).

When the guide is hidden (nest / mpv), compositor **global shortcuts** (`src-tauri/src/atongx.rs`) are not enough for Back / Home: nested gamescope+Chrome eats Escape/Home, and BrowserBack / BrowserHome / MediaPlayPause / ContextMenu fail to register (`Unknown scancode`).

The appliance path for leaving the nest is an **evdev watcher** (`src-tauri/src/hid.rs`) on the XING WEI nodes. It calls the same `return_to_guide` / `remote_back_inner` / `remote_power_inner` logic. Back, Home, and Power **always** exit the nest / OTA and show the guide. Power never shuts the box down.

While the Chrome nest is playing, D-pad + OK + Space are grabbed as compositor shortcuts and **injected into the nest** — see [`nest-input.md`](nest-input.md). They are released when the guide returns so shelves keep focus. The evdev keyboard node is **not** grabbed, so those keys can still reach Chrome after injection.

| Button on device | Action | This PR |
| --- | --- | --- |
| Power | `power` | Stop playback and return to the guide. Never shuts the box down. |
| Play / Pause | `playpause` | Space into the Chrome nest (`wtype -k` / xdotool on nest `DISPLAY`), or Space into mpv for OTA |
| Mouse-cursor toggle | `pointer` | Guide: `tv-pointer-on` CSS. Nest: focus gamescope so ATONGX event4 is a real cursor; OK clicks |
| D-pad ↑ ↓ ← → | `up` `down` `left` `right` | Guide focus; arrows into Chrome while the nest is up |
| OK (orange ring) | `ok` | Activate focused tile; Return (or click in pointer mode) in the nest |
| Home | `home` | Return to guide / end playback |
| Back | `back` | Hide nest / stop OTA |
| Menu | `menu` | Open Connect (phone) |
| PAGE up / down | `pageUp` `pageDown` | Jump a shelf row |
| Mic (red) | `voice` | Whisper pt-BR — hold/tap → `voice_begin` / `voice_end` (see [`voice.md`](voice.md)) |
| VOL + / − | `volumeUp` `volumeDown` | Samsung `KEY_VOLUP` / `KEY_VOLDOWN`; `pactl` ±5% if the TV is unreachable |
| DEL | `delete` | Same as Back (no on-TV typing) |
| Mute | `mute` | Samsung `KEY_MUTE`; `pactl` toggle if the TV is unreachable |

## Evdev keycodes (XING WEI 2.4G USB)

Dongle: USB **XING WEI 2.4G USB USB Composite Device** (ATONGX air mouse). Typical nodes:

| Node | by-id | Role |
| --- | --- | --- |
| `/dev/input/event3` | `…-if02-event-kbd` | keyboard (D-pad / OK / Escape / Home). **Not grabbed.** |
| `/dev/input/event4` | mouse | air-mouse pointer. Ignored. |
| `/dev/input/event5` | consumer control | Back / Home / Power / VOL / Mute / PlayPause. **Grabbed** so nest gamescope cannot eat them. |

Match is by name / by-id (`XING WEI`, `XING_WEI`, `ATONGX`, `2.4G USB`), overridable with `ZAPPE_HID_NAME`. Disable with `ZAPPE_HID_DISABLE=1`.

| Button | evdev `KEY_*` | code | How we detect it |
| --- | --- | --- | --- |
| Back | `KEY_BACK`, also `KEY_ESC` / `KEY_EXIT` / `KEY_DELETE` / `KEY_BACKSPACE` | **158** (1 / 174 / 111 / 14) | Consumer `KEY_BACK` is the usual ATONGX “BrowserBack”. Keyboard Escape is a fallback (not grabbed). |
| Home | `KEY_HOMEPAGE`, also `KEY_HOME` | **172** (102) | Consumer `KEY_HOMEPAGE` is “BrowserHome”. |
| Power | `KEY_POWER`, also `KEY_POWER2` / `KEY_SLEEP` | **116** (226 / 142) | Return to guide only — never ACPI shutdown. |
| Mic (red) | `KEY_SEARCH`, `KEY_VOICECOMMAND` (**locked**, alias `KEY_MIC`); aliases F8 / F9 / RECORD / RED / ASSISTANT / MICMUTE / DICTATE | **217** / **582** | Consumer Search / Voice Command (`0x0CF`). Linux has no `KEY_MIC`. Rematch with `ZAPPE_HID_VOICE_CODE` if `evtest` prints another code. |
| Pointer | `KEY_F2`, `KEY_TOUCHPAD_TOGGLE` (**locked**); aliases F6 / F7 / F10 | **60** / **530** | Keyboard F2 is the living-room contract. Some boards toggle gyro in firmware and send no EV_KEY. |

D-pad / OK / PAGE stay `dispatch: focus` on the **ungrabbed** keyboard node so nest Chrome injection is unchanged.

## 60-second capture on zappe-tv

This cloud environment does not have the physical remote. Lock the Mic / Pointer lines by running the helper on the box and pasting anything that prints `(unmapped)`:

```bash
cd ~/src/zappe   # or $ZAPPE_SRC
sudo usermod -aG input "$USER"   # once; re-login
./packaging/appliance/zappe-atongx-capture --mic-pointer
# Press Mic (red), then the pointer-mode button.
# Expected: KEY_SEARCH (217) or KEY_VOICECOMMAND (582) → voice (locked)
#           KEY_F2 (60) or KEY_TOUCHPAD_TOGGLE (530) → pointer (locked)
# If Pointer is silent, wave the remote — REL_* means firmware-local gyro.
```

`evtest` fallback (no Python):

```bash
cat /proc/bus/input/devices | less
sudo evtest /dev/input/event5   # consumer — Mic / Back / Home
sudo evtest /dev/input/event3   # keyboard — Pointer / D-pad
```

Paste a line like `event5 consumer KEY_SEARCH (217) press → voice (locked)  MSC_SCAN 0x…` back into the map if the code is new.

Zappe logs `ATONGX evdev watching … grab=true` and `ATONGX evdev Voice code=217` on press. The user running the kiosk must be in the `input` group so `/dev/input/event*` is readable. Optional udev: `sudo cp packaging/appliance/99-atongx.rules /etc/udev/rules.d/`.

## Nest teardown

`return_to_guide` **kills the nest child gamescope** (the one wrapping Chrome — cmdline contains `google-chrome` / `user-data-dir`). It never signals the outer kiosk (`gamescope -e -f -- zappe`) or any ancestor PID.

## Voice (red mic)

Full path, lexicon, model location, and rematch: [`voice.md`](voice.md).

Press / hold the red mic → overlay **Ouvindo…** (guide stays) → local whisper.cpp (`-l pt` + `~/.local/share/zappe/whisper/ggml-small.bin`) → [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts).

Examples: `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo`, `Record`, `sincronizar`.

- Install: `packaging/appliance/install-whisper.sh`
- Tests without a mic: `ZAPPE_VOICE_FAKE=abrir netflix` or `npm run check:input`
- Wrong scancode: `sudo evtest` the XING WEI node, then `ZAPPE_HID_VOICE_CODE=<n>` — do not invent pointer codes here.

## Branding (not this map)

Optional appliance branding lives only under `~/.local/share/zappe/` — never git. See `branding.json` + `logo.png`.

## Manual verify (appliance)

Build with `npm run tauri -- build --no-bundle` (never plain `cargo build --release`). Install / restart `zappe.service`.

1. `journalctl --user -u zappe.service -f` — look for `ATONGX evdev watching XING WEI … grab=true` on the consumer node.
2. Open Netflix (or any nest). Press **Back** — nest gamescope+Chrome die, guide is fullscreen. Repeat with **Home** and **Power**. Box stays on.
3. `pgrep -a gamescope` while the nest is up shows two processes (kiosk `-- zappe` and nest Chrome). After Back, only the kiosk remains.
4. VOL / Mute move the **Samsung TV** volume (OSD). Log: `ATONGX volume+ via samsung`. Pulse (`pactl get-sink-volume`) stays put unless Device Connect failed — then one toast and pactl. See [`samsung-tv.md`](samsung-tv.md).
5. D-pad moves Netflix focus; OK activates. See [`nest-input.md`](nest-input.md).
6. **Mic (red)** — Whisper toast (`Ouvindo…`) on the guide **and** while Netflix is up. Log: `ATONGX evdev Voice code=217` (or 582). Not Unknown, not swallowed.
7. **Pointer** — Guide: `Ponteiro` toast + CSS cursor. Nest: real cursor; OK clicks. Log: `ATONGX evdev Pointer code=60` (or 530). If the button is silent, motion itself is the mode change.
8. Back / Home still exit the nest. D-pad still moves Netflix focus.
9. Optional: `./packaging/appliance/zappe-atongx-capture --mic-pointer` if Mic / Pointer is ignored — add the printed `KEY_*` to `src/lib/atongx-map.json` (not a third table).
