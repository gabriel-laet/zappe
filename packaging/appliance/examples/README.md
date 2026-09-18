# Appliance examples

Files here are **samples**. They are not loaded by Zappe at runtime.

## Custom branding (not in git)

Zappe reads branding from the **user data dir**, never from this repository:

- Linux XDG (default): `~/.local/share/zappe/branding.json`
- Override: `$ZAPPE_DATA_DIR/branding.json`

Optional artwork sits next to that file (`logo.png`, `splash.png`) or uses an absolute path.

### Drop branding on the living-room box

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/branding.json ~/.local/share/zappe/branding.json
# edit name / accent / tagline
cp /path/to/your/logo.png ~/.local/share/zappe/logo.png
# optional boot art
cp /path/to/your/splash.png ~/.local/share/zappe/splash.png

# pick up the files
systemctl --user restart zappe.service
# or relaunch `zappe` if you are not using the kiosk unit
```

If `branding.json` is missing, the guide uses in-code defaults (`Zappe` + the built-in accent). If the JSON is invalid, Zappe logs a warning and falls back to those defaults.

### `branding.json` schema

| Field | Required | Description |
| --- | --- | --- |
| `name` | no | Wordmark on splash, Home header, and setup. Max 40 characters. Default `Zappe`. |
| `accent` | no | CSS color for primary buttons and focus rings. Hex (`#E8A84C`), `oklch()`, `rgb()` / `hsl()`, or a simple named color. Invalid values are logged and ignored. Alias: `accentColor`. |
| `logo` | no | Image path relative to the data dir, or absolute. `png` / `jpg` / `webp` / `gif`, max 4 MiB. |
| `splash` | no | Optional boot image (same rules as `logo`). If omitted, the logo or a letter mark is used. |
| `tagline` | no | Line under the wordmark. Max 80 characters. |

Unknown keys (including `_example` / `_comment` on this sample) are ignored.

**Do not** put real household logos or personal artwork in the app source tree. Keep them on the appliance under `~/.local/share/zappe/`.
