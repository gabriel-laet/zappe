# ATONGX air mouse — input contract

Living-room box: **no physical keyboard**. Couch input is this remote plus a phone at `zappe-tv.local` (device-code login is the next PR).

Source of truth: [`src/lib/remoteMap.ts`](../src/lib/remoteMap.ts) (`classifyAtongx`). Next PR should fill the **stub** rows without changing the action names.

| Button on device | Action | This PR |
| --- | --- | --- |
| Power | `power` | Stub (`remote_power`) — do not power-off yet |
| Play / Pause | `playpause` | HID play/pause in the nest / mpv |
| Mouse-cursor toggle | `pointer` | Toggles `tv-pointer-on` (show/hide CSS cursor). Real air-mouse HID is next |
| D-pad ↑ ↓ ← → | `up` `down` `left` `right` | Guide focus |
| OK (orange ring) | `ok` | Activate focused tile |
| Home | `home` | Return to guide / end playback |
| Back | `back` | Hide nest / stop OTA |
| Menu | `menu` | Stub (`remote_menu`) |
| PAGE up / down | `pageUp` `pageDown` | Jump a shelf row |
| Mic (red) | `voice` | Whisper pt-BR stub (`voice_listen`) |
| VOL + / − | `volumeUp` `volumeDown` | Stub (`remote_volume`) |
| DEL | `delete` | Same as Back (no on-TV typing) |
| Mute | `mute` | Stub (`remote_mute`) |

## Voice (red mic) — next PR completes STT

This PR: button is reserved, grammar lives in [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts).

Examples (pt-BR, short): `abrir Netflix`, `pausar`, `voltar`, `canal Globo`.

Runtime: `ZAPPE_WHISPER_BIN` + `arecord`. `ZAPPE_VOICE_FAKE=abrir netflix` for tests.

## Branding (not this map)

Feras TV mark (tan dog **Beto**, tuxedo cat **Lek**) lives only under `~/.local/share/zappe/` — never git. See `branding.json` + `logo.png`.
