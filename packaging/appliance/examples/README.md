# Appliance examples

Files here are **samples**. They are not loaded by Zappe at runtime.

## Custom branding (not in git)

Zappe reads branding from the **user data dir**, never from this repository:

- Linux XDG (default): `~/.local/share/zappe/branding.json`
- Override: `$ZAPPE_DATA_DIR/branding.json`

Optional artwork sits next to that file (`logo.png`) or uses an absolute path.

The Feras TV circular badge (tan dog **Beto**, tuxedo cat **Lek**, cream FERAS / orange TV) stays on the appliance. Pet names are not UI copy. **Do not commit that PNG.**

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
| `name` | no | Product name (e.g. `Feras TV`). No pet names. Default `Zappe`. |
| `logo` | no | Image path relative to the data dir, or absolute. `png` / `jpg` / `webp` / `gif`, max 8 MiB. |
| `idle.mode` | no | `screensaver` (default) or `off`. |
| `idle.asset` | no | Screensaver image (defaults to `logo`). |
| `idle.timeoutSeconds` | no | Idle seconds before screensaver (default 120, clamp 15–3600). |
| `idle.animation` | no | `soft-breathe` (default). |
| `theme.style` | no | `apple-tv` (default). |
| `theme.background` | no | CSS color, typically `#000000`. |
| `theme.focusRing` | no | `subtle-scale` (default). |
| `accent` | no | Optional override for the warm orange accent. |

Unknown keys (`_example`, `_comment`) are ignored. No taglines on the Feras TV mark.
