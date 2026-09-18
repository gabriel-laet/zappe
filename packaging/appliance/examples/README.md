# Appliance examples

Files here are **samples**. They are not loaded by Zappe at runtime.

## Custom branding (not in git)

Zappe reads branding from the **user data dir**, never from this repository:

- Linux XDG (default): `~/.local/share/zappe/branding.json`
- Override: `$ZAPPE_DATA_DIR/branding.json`

Optional artwork sits next to that file (`logo.png`) or uses an absolute path.

The official feras TV lockup (tan dog **Beto**, tuxedo cat **Lek**, beige circle, lowercase “feras”, “— TV —”) stays on the appliance. Pet names are not UI copy. **Do not commit that PNG, and do not invent a substitute mark.**

### Drop branding on the living-room box

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/branding.json ~/.local/share/zappe/branding.json
cp /path/to/feras-tv-logo.png ~/.local/share/zappe/logo.png

systemctl --user restart zappe.service
```

If `branding.json` is missing, the guide uses in-code defaults (`Zappe` + Apple TV–black chrome). Invalid JSON → log and fall back.

### `branding.json` schema

| Field | Required | Description |
| --- | --- | --- |
| `name` | no | Product name (e.g. `feras TV`). No pet names. Default `Zappe`. |
| `accent` | no | Warm beige from the official art (`#C4A574`). |
| `logo` | no | Image path relative to the data dir, or absolute. `png` / `jpg` / `webp` / `gif`, max 8 MiB (~1.4MB official PNG is fine). Header clips the circular badge; prefer a transparent PNG so no white plate remains. |
| `splash` | no | Splash image (defaults to `logo`). Shown `object-fit: contain`. |
| `idle.mode` | no | `screensaver` (default) or `off`. |
| `idle.asset` | no | Screensaver image (defaults to `logo`). |
| `idle.timeoutSeconds` | no | Idle seconds before screensaver (default 120, clamp 15–3600). |
| `idle.animation` | no | `soft-breathe` (default). |
| `theme.style` | no | `apple-tv` (default). |
| `theme.background` | no | CSS color, typically `#000000`. |
| `theme.focusRing` | no | `subtle-scale` (default). |

Unknown keys (`_example`, `_comment`) are ignored. No taglines on the feras TV mark.

## Samsung TV (Device Connect)

Volume / mute on the ATONGX go to the living-room Samsung over Wi-Fi. Copy the sample (no token) and pair on the TV — see [`docs/samsung-tv.md`](../../../docs/samsung-tv.md).

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/samsung.json ~/.local/share/zappe/samsung.json
# allow "Zappe" in Device Connect Manager; the kiosk writes token into that file
systemctl --user restart zappe.service
```

Never commit a live `token`. `ZAPPE_SAMSUNG_HOST` / `ZAPPE_SAMSUNG_TOKEN` override the file. `ZAPPE_SAMSUNG_DISABLE=1` forces PulseAudio.
