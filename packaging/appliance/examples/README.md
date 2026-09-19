# Appliance examples

Files here are **samples**. They are not loaded by Zappe at runtime.

Remote capture (KEY bitfields + Mic/Pointer press) lives next to this folder: [`../zappe-atongx-capture`](../zappe-atongx-capture). See [`docs/atongx-input.md`](../../../docs/atongx-input.md).

[`xing-wei-devices.txt`](xing-wei-devices.txt) is a **synthetic** `/proc/bus/input/devices` dump for the parser (KEY bits encoded from `atongx-map.json`). It is not a live zappe-tv capture.

## Custom branding (not in git)

Zappe reads branding from the **user data dir**, never from this repository:

- Linux XDG (default): `~/.local/share/zappe/branding.json`
- Override: `$ZAPPE_DATA_DIR/branding.json`

Optional artwork sits next to that file (`logo.png`) or uses an absolute path.

Put your own `logo.png` + `branding.json` on the appliance. **Do not commit that PNG.**

### Drop branding on the living-room box

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/branding.json ~/.local/share/zappe/branding.json
cp /path/to/your-logo.png ~/.local/share/zappe/logo.png

systemctl --user restart zappe.service
```

If `branding.json` is missing, the guide uses in-code defaults (`Zappe` + Apple TV–black chrome). Invalid JSON → log and fall back.

### `branding.json` schema

| Field | Required | Description |
| --- | --- | --- |
| `name` | no | Product name (e.g. `Zappe` or `My TV`). Default `Zappe`. |
| `accent` | no | CSS color. Default `#E85A1B`. |
| `logo` | no | Image path relative to the data dir, or absolute. `png` / `jpg` / `webp` / `gif`, max 8 MiB. Header clips the image to a circle; prefer a transparent PNG so no white plate remains. |
| `splash` | no | Splash image (defaults to `logo`). Shown `object-fit: contain`. |
| `idle.mode` | no | `screensaver` (default) or `off`. |
| `idle.asset` | no | Screensaver image (defaults to `logo`). |
| `idle.timeoutSeconds` | no | Idle seconds before screensaver (default 120, clamp 15–3600). |
| `idle.animation` | no | `soft-breathe` (default). |
| `theme.style` | no | `apple-tv` (default). |
| `theme.background` | no | CSS color, typically `#000000`. |
| `theme.focusRing` | no | `subtle-scale` (default). |

Unknown keys (`_example`, `_comment`) are ignored.

## Samsung TV (Device Connect)

Volume / mute on the ATONGX go to the living-room Samsung over Wi-Fi. Copy the sample (no token) and pair on the TV — see [`docs/samsung-tv.md`](../../../docs/samsung-tv.md).

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/samsung.json ~/.local/share/zappe/samsung.json
# allow "Zappe" in Device Connect Manager; the kiosk writes token into that file
systemctl --user restart zappe.service
```

Never commit a live `token`. `ZAPPE_SAMSUNG_HOST` / `ZAPPE_SAMSUNG_TOKEN` override the file. `ZAPPE_SAMSUNG_DISABLE=1` forces PulseAudio.

ATONGX Power stays **return to the guide** unless you set `"wakeOnPower": true` (or `ZAPPE_SAMSUNG_WAKE_ON_POWER=1`). Optional `"mac"` / `ZAPPE_SAMSUNG_MAC` enables Wake-on-LAN when the TV is fully off. See [`docs/samsung-tv.md`](../../../docs/samsung-tv.md).
