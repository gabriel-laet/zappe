# ATONGX / XING WEI air mouse — input contract

Living-room box: **no physical keyboard**. Couch input is this remote plus a phone at `zappe-tv.local`.

This is Zappe HID mapping — not a Samsung TV API.

## Source of truth

[`src/lib/atongx-map.json`](../src/lib/atongx-map.json) is the data-driven contract:

| Layer | Consumer |
| --- | --- |
| Physical button | `bindings[].button` |
| linux `EV_KEY` | `bindings[].keys[].name` + `.code` |
| HID node | `bindings[].keys[].iface` (`keyboard` / `consumer` / `system`) |
| Zappe action | `bindings[].action` (`AtongxAction`) |
| Webview leftover | `bindings[].keys[].webCodes` (`KeyboardEvent.code`) |

TypeScript: [`src/lib/remoteMap.ts`](../src/lib/remoteMap.ts) (`classifyAtongx`, `classifyAtongxEvkey`).

Rust: [`src-tauri/src/atongx_map.rs`](../src-tauri/src/atongx_map.rs) + evdev listener in [`src-tauri/src/atongx.rs`](../src-tauri/src/atongx.rs).

Appliance capture: [`packaging/appliance/capture-atongx.sh`](../packaging/appliance/capture-atongx.sh).

## Why evdev (not global shortcuts)

On zappe-tv the dongle enumerates as a USB HID composite:

| `/dev/input/by-id/` | Typical node | Role |
| --- | --- | --- |
| `usb-XING_WEI_2.4G_USB_USB_Composite_Device-if02-event-kbd` | event3 | Boot keyboard — D-pad, OK, PAGE, DEL |
| `…-if03-event-mouse` | event4 | Air-mouse `REL_*` / `BTN_*` (not an `AtongxAction`) |
| `…-event-if03` (Consumer Control) | event5 | Back, Home, Play/Pause, Menu, VOL, Mute, Mic |
| System Control | event6 | Power |

`BrowserBack`, `BrowserHome`, `MediaPlayPause`, and `ContextMenu` **do not register** as Tauri global shortcuts under gamescope. Those usages are standard Consumer Control keys (`KEY_BACK` / `KEY_HOMEPAGE` / `KEY_PLAYPAUSE` / `KEY_COMPOSE`). The evdev thread reads them, optionally **grabs** the consumer + system nodes so Chrome inside the nest does not navigate away, and emits `atongx-action`.

D-pad + OK stay **ungrabbed** on the keyboard node so the focused player still receives them. Nest D-pad / pointer focus is a sibling; subscribe to `atongx-action` (`dispatch: "focus"`) if you need the same codes in-process.

Laptop fallback still registers a short list of shortcuts that actually work (`Escape`, `Home`, `Delete`, F-keys, volume). It does **not** register the four broken consumer codes.

Disable: `ZAPPE_ATONGX_EVDEV=0`. Disable grab: `ZAPPE_ATONGX_GRAB=0`.

## Button → KEY_* → action

Confidence:

- **hid** — standard USB HID → linux keycode. Expected on this dongle class (VID `2320` / PID `0912`, G10S-like ATONGX / XING WEI).
- **alias** — extra code we accept (other firmwares, leftover webview keys).
- **needs_device** — must confirm on zappe-tv with the capture script.

`dispatch: global` is handled in Rust even when the guide is hidden. `dispatch: focus` is classified and emitted only.

| Button | linux `KEY_*` | EV_KEY | iface | `AtongxAction` | dispatch | Confidence |
| --- | --- | --- | --- | --- | --- | --- |
| D-pad ↑ | `KEY_UP` | 103 | keyboard | `up` | focus | hid |
| D-pad ↓ | `KEY_DOWN` | 108 | keyboard | `down` | focus | hid |
| D-pad ← | `KEY_LEFT` | 105 | keyboard | `left` | focus | hid |
| D-pad → | `KEY_RIGHT` | 106 | keyboard | `right` | focus | hid |
| OK (orange ring) | `KEY_ENTER` | 28 | keyboard | `ok` | focus | hid |
| OK aliases | `KEY_SELECT` / `KEY_OK` | 353 / 352 | keyboard | `ok` | focus | alias |
| Back | **`KEY_BACK`** | **158** | consumer | `back` | global | hid (BrowserBack) |
| Back alias | `KEY_ESC` | 1 | keyboard | `back` | global | alias |
| Home | **`KEY_HOMEPAGE`** | **172** | consumer | `home` | global | hid (BrowserHome) |
| Home alias | `KEY_HOME` | 102 | keyboard | `home` | global | alias |
| Menu | **`KEY_COMPOSE`** | **127** | consumer | `menu` | global | hid (ContextMenu) |
| Menu | `KEY_MENU` | 139 | consumer | `menu` | global | hid |
| Menu aliases | `KEY_CONTEXT_MENU` / `KEY_F1` | 438 / 59 | consumer / keyboard | `menu` | global | alias / **needs_device** |
| Play / Pause | **`KEY_PLAYPAUSE`** | **164** | consumer | `playpause` | global | hid (MediaPlayPause) |
| Play aliases | `KEY_PLAY` / `KEY_SPACE` | 207 / 57 | consumer / keyboard | `playpause` | global | alias |
| PAGE up | `KEY_PAGEUP` | 104 | keyboard | `pageUp` | focus | hid |
| PAGE down | `KEY_PAGEDOWN` | 109 | keyboard | `pageDown` | focus | hid |
| Mic (red) | `KEY_SEARCH` | 217 | consumer | `voice` | global | **needs_device** |
| Mic | `KEY_VOICECOMMAND` | 582 | consumer | `voice` | global | **needs_device** |
| Mic aliases | `KEY_F8` / `KEY_F9` / `KEY_RED` | 66 / 67 / 398 | keyboard / consumer | `voice` | global | **needs_device** |
| VOL + | `KEY_VOLUMEUP` | 115 | consumer | `volumeUp` | global | hid |
| VOL − | `KEY_VOLUMEDOWN` | 114 | consumer | `volumeDown` | global | hid |
| Mute | `KEY_MUTE` | 113 | consumer | `mute` | global | hid |
| Power | `KEY_POWER` | 116 | system | `power` | global | hid |
| Power alias | `KEY_SLEEP` | 142 | system | `power` | global | alias |
| Air-mouse toggle | `KEY_F2` / `F6` / `F7` / `F10` | 60 / 64 / 65 / 68 | keyboard | `pointer` | global | **needs_device** |
| DEL | `KEY_DELETE` | 111 | keyboard | `delete` | global | hid |
| DEL alias | `KEY_BACKSPACE` | 14 | keyboard | `delete` | global | alias |

What the actions do (unchanged product meaning):

| Action | Behavior |
| --- | --- |
| `power` | Stop playback and return to the guide. Never shuts the box down. |
| `playpause` | Space into the Chrome nest, or Space into mpv for OTA |
| `pointer` | Toggles `tv-pointer-on` (show/hide CSS cursor) |
| `up` `down` `left` `right` | Guide focus (or the focused nest / mpv) |
| `ok` | Activate focused tile |
| `home` | Return to guide / end playback |
| `back` / `delete` | Hide nest / stop OTA |
| `menu` | Open Connect (phone) |
| `pageUp` `pageDown` | Jump a shelf row |
| `voice` | Whisper pt-BR (`voice_listen`) |
| `volumeUp` `volumeDown` | `pactl set-sink-volume` ±5% |
| `mute` | `pactl set-sink-mute toggle` |

The frontend can still classify leftover webview keys. Prefer the Rust `atongx-action` event when the guide is hidden.

## On-device verification (zappe-tv)

CI does not have the dongle. After plugging it in:

```bash
# 1. Confirm nodes
ls -l /dev/input/by-id | grep -i xing
cat /proc/bus/input/devices

# 2. Permissions (once)
sudo cp packaging/appliance/99-atongx.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG input "$USER"   # then re-login

# 3. Inventory KEY bitfields, then press each button
./packaging/appliance/capture-atongx.sh --list
./packaging/appliance/capture-atongx.sh --prompt
```

`--prompt` walks `atongx-map.json` in order and prints `KEY_* (code) → action` plus `MSC_SCAN` (useful later for udev hwdb). `--all` listens to every evdev node for a future remote.

### Confirm or patch these first

1. **Mic** — which of `KEY_SEARCH` (217), `KEY_VOICECOMMAND` (582), `KEY_F8`, `KEY_F9`, `KEY_RED` actually fires, and on which node.
2. **Menu** — `KEY_COMPOSE` (127) vs `KEY_MENU` (139) vs `KEY_F1`. ContextMenu failing as a shortcut already points at 127.
3. **Pointer toggle** — often **no EV_KEY** (gyro on/off is local). If capture is silent, do not invent a code; leave `pointer` as a CSS fallback.
4. **Back / Home / Play** — should be 158 / 172 / 164 on Consumer Control. If a board sends `KEY_ESC` / `KEY_HOME` / `KEY_SPACE` on if02 instead, the aliases already cover it.
5. **Power** — `KEY_POWER` (116) on System Control (event6). Confirm it is not also `KEY_SLEEP`.
6. **USB ids** — map assumes vendor `2320` product `0912` (linux-hardware.org XING WEI composite). If `lsusb` differs, update `device.usb` in the JSON; name matching (`XING WEI`, `ATONGX`) still works.

Paste the capture transcript back into `atongx-map.json` (`confidence: "hid"` for codes you saw, drop ones you did not). Rust and the guide pick up the file on the next build.

## Voice (red mic)

Grammar: [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts).

Examples (pt-BR, short): `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo`.

Runtime: `ZAPPE_WHISPER_BIN` + `arecord`. `ZAPPE_VOICE_FAKE=abrir netflix` for tests.

## Branding (not this map)

Official feras TV lockup (tan dog **Beto**, tuxedo cat **Lek**, beige circle, lowercase “feras”) lives only under `~/.local/share/zappe/` — never git. See `branding.json` + `logo.png`. Do not invent a substitute mark.
