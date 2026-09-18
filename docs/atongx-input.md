# ATONGX air mouse — input contract

Living-room box: **no physical keyboard**. Couch input is this remote plus a phone at `zappe-tv.local` (device-code login is the next PR).

Source of truth: [`src/lib/remoteMap.ts`](../src/lib/remoteMap.ts) (`classifyAtongx`). This is Zappe HID mapping — not a Samsung TV API.

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
| Mic (red) | `voice` | Whisper pt-BR (`voice_listen`) |
| VOL + / − | `volumeUp` `volumeDown` | `pactl set-sink-volume` ±5% |
| DEL | `delete` | Same as Back (no on-TV typing) |
| Mute | `mute` | `pactl set-sink-mute toggle` |

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

Confirm on the box (do this once if a new dongle maps differently):

```bash
# pick the XING WEI consumer + kbd nodes
cat /proc/bus/input/devices | less
sudo evtest /dev/input/event5   # press Back / Home / Power; note KEY_* and (code)
sudo evtest /dev/input/event3
```

Zappe logs `ATONGX evdev watching … grab=true` and `ATONGX evdev Back code=158` on press. The user running the kiosk must be in the `input` group (`sudo usermod -aG input glaet` and re-login) so `/dev/input/event*` is readable.

## Nest teardown

`return_to_guide` **kills the nest child gamescope** (the one wrapping Chrome — cmdline contains `google-chrome` / `user-data-dir`). It never signals the outer kiosk (`gamescope -e -f -- zappe`) or any ancestor PID.

## Voice (red mic)

Grammar: [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts).

Examples (pt-BR, short): `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo`.

Runtime: `ZAPPE_WHISPER_BIN` + `arecord`. `ZAPPE_VOICE_FAKE=abrir netflix` for tests.

## Branding (not this map)

Official feras TV lockup (tan dog **Beto**, tuxedo cat **Lek**, beige circle, lowercase “feras”) lives only under `~/.local/share/zappe/` — never git. See `branding.json` + `logo.png`. Do not invent a substitute mark.

## Manual verify (appliance)

Build with `npm run tauri -- build --no-bundle` (never plain `cargo build --release`). Install / restart `zappe.service`.

1. `journalctl --user -u zappe.service -f` — look for `ATONGX evdev watching XING WEI … grab=true` on the consumer node.
2. Open Netflix (or any nest). Press **Back** — nest gamescope+Chrome die, guide is fullscreen. Repeat with **Home** and **Power**. Box stays on.
3. `pgrep -a gamescope` while the nest is up shows two processes (kiosk `-- zappe` and nest Chrome). After Back, only the kiosk remains.
4. VOL / Mute still change Pulse (`pactl get-sink-volume @DEFAULT_SINK@`).
5. D-pad moves Netflix focus; OK activates. See [`nest-input.md`](nest-input.md).
6. Optional: `sudo evtest` as above if Back / Home is ignored — add the printed `KEY_*` to `action_for_keycode` in `src-tauri/src/hid.rs`.
