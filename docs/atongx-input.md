# ATONGX air mouse — input contract

Living-room box: **no physical keyboard**. Couch input is this remote plus a phone at `zappe-tv.local` (device-code login is the next PR).

Source of truth: [`src/lib/remoteMap.ts`](../src/lib/remoteMap.ts) (`classifyAtongx`). This is Zappe HID mapping — not a Samsung TV API.

When the guide is hidden (nest / mpv), the same actions are registered as **global shortcuts** (`src-tauri/src/atongx.rs`) so Home / Back / Play-Pause / Menu / Mic / VOL / Mute / pointer still do something. D-pad + OK stay with the focused player.

| Button on device | Action | This PR |
| --- | --- | --- |
| Power | `power` | Stop playback and return to the guide. Never shuts the box down. |
| Play / Pause | `playpause` | Space into the Chrome nest, or Space into mpv for OTA |
| Mouse-cursor toggle | `pointer` | Toggles `tv-pointer-on` (show/hide CSS cursor) |
| D-pad ↑ ↓ ← → | `up` `down` `left` `right` | Guide focus |
| OK (orange ring) | `ok` | Activate focused tile |
| Home | `home` | Return to guide / end playback |
| Back | `back` | Hide nest / stop OTA |
| Menu | `menu` | Open Connect (phone) |
| PAGE up / down | `pageUp` `pageDown` | Jump a shelf row |
| Mic (red) | `voice` | Whisper pt-BR (`voice_listen`) |
| VOL + / − | `volumeUp` `volumeDown` | `pactl set-sink-volume` ±5% |
| DEL | `delete` | Same as Back (no on-TV typing) |
| Mute | `mute` | `pactl set-sink-mute toggle` |

## Voice (red mic)

Grammar: [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts).

Examples (pt-BR, short): `abrir Netflix`, `voltar`, `volume mais`, `mudo`, `ir para Globo`.

Runtime: `ZAPPE_WHISPER_BIN` + `arecord`. `ZAPPE_VOICE_FAKE=abrir netflix` for tests.

## Branding (not this map)

Official feras TV lockup (tan dog **Beto**, tuxedo cat **Lek**, beige circle, lowercase “feras”) lives only under `~/.local/share/zappe/` — never git. See `branding.json` + `logo.png`. Do not invent a substitute mark.
