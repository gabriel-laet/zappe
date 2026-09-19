# Samsung TV volume (Device Connect)

The Beelink has **no HDMI-CEC** (`/dev/cec0` missing). Volume +/− / Mute from the ATONGX air mouse must reach the Samsung living-room TV the same way its own remote does: **Wi-Fi Device Connect** (websocket), not PulseAudio on the mini-PC.

```text
ATONGX VOL / Mute
    → evdev / global shortcut / voice
    → src-tauri/src/samsung.rs
    → ws://TV:8001/api/v2/channels/samsung.remote.control
         KEY_VOLUP / KEY_VOLDOWN / KEY_MUTE
    → on failure: pactl on zappe-tv (HDMI PCM), one toast + log
```

Power stays **return to the guide**. We never send `KEY_POWER` or `KEY_SOURCE` (those would blank HDMI or switch inputs). Nest Back / Home / D-pad / OTA are unchanged.

## Configure on the appliance

Host defaults to the living-room TV (`192.168.3.6:8001`). Override without putting a token in git:

| Source (later wins) | Path / variable |
| --- | --- |
| File | `~/.local/share/zappe/samsung.json` |
| File | `~/.config/zappe/samsung.json` |
| Env | `ZAPPE_SAMSUNG_HOST` `ZAPPE_SAMSUNG_PORT` `ZAPPE_SAMSUNG_SECURE_PORT` |
| Env | `ZAPPE_SAMSUNG_TOKEN` `ZAPPE_SAMSUNG_NAME` |
| Disable | `ZAPPE_SAMSUNG_DISABLE=1` (pactl only) |

`$ZAPPE_DATA_DIR` replaces `~/.local/share/zappe`. Example (no token):

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/samsung.json ~/.local/share/zappe/samsung.json
# edit host if the TV is not 192.168.3.6
```

```json
{
  "host": "192.168.3.6",
  "port": 8001,
  "securePort": 8002,
  "name": "Zappe"
}
```

A successful pair **writes `token` into that file** (mode `0600`). Do not commit a live token. Prefer the file or a systemd user Environment= — never the repo.

## Pair Device Connect

1. TV and `zappe-tv` on the same LAN. TV must be on (not a blank HDMI wake).
2. Confirm the API: `curl -sS --max-time 2 http://192.168.3.6:8001/api/v2/ | head`
3. Restart the kiosk (`systemctl --user restart zappe.service`). Logs: `Samsung TV at 192.168.3.6:8001`.
4. On the TV, a **Device Connect / allow this device** popup for **Zappe**. Accept it with the Samsung remote.
5. If there is no popup: **Settings → General → External Device Manager → Device Connection Manager** (wording varies: Smart Hub → Device Connect, or Connections → Expert Settings). Allow the client; delete a stale “Zappe” entry and retry.
6. Token lands in `~/.local/share/zappe/samsung.json`. `journalctl --user -u zappe.service` should say `Samsung Device Connect token saved` or `ready`.

Port **8001** (`ws://`) is tried first; **8002** (`wss://`, self-signed) is the fallback for Tizen sets that disabled plaintext.

## Verify from the couch

1. `journalctl --user -u zappe.service -f`
2. Press **VOL +** on the ATONGX. The **Samsung on-screen volume** should move (same slider as the TV remote). Log: `ATONGX volume+ via samsung`.
3. **Mute** should show the TV mute icon. Log: `ATONGX mute via samsung`.
4. `pactl get-sink-volume @DEFAULT_SINK@` should **not** change when Samsung works.
5. Back / Home still leave Netflix; D-pad still drives Chrome; OTA play is unchanged.

### Fallback (TV off / unpaired)

If Device Connect fails, volume uses Pulse on the Beelink (HDMI PCM). The guide shows **one** toast naming the TV and PulseAudio; later presses stay quiet about the fallback (45s cooldown before another Samsung attempt). Log: `ATONGX volume+ Samsung failed … falling back to pactl`.

```bash
# force pactl while debugging
ZAPPE_SAMSUNG_DISABLE=1
```

## Manual key check (optional)

After pairing, any websocket client that speaks Device Connect can send `KEY_VOLUP`. The kiosk already does this; a one-off `websocat` is not required.
