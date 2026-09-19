# Phone companion — couch login (every Apps source)

The living-room box has **no keyboard**. Sign-in happens on a phone. HDMI stays
on the Zappe Connect screen (device code + QR). Nested Chrome is never shown
for typing — same invisible path as harvest (PR #14): **no second gamescope**,
`--start-minimized`, off-screen window, then hide + raise the guide.

Companion URL: **`http://zappe-tv.local`** (mDNS). If port 80 cannot bind, the
process falls back to **`:8780`** and the TV shows that host:port.

## Couch steps

1. On the TV, open **Connect account** (Home shelf, setup, or ATONGX **Menu**).
2. D-pad **Left / Right** to pick a service: Netflix, Prime Video, Disney+,
   YouTube. Back dismisses Connect.
3. Scan the QR or open `http://zappe-tv.local` and type the code.
4. On the phone, sign in to that service (password, email code, or Google).
5. The TV shows **Connected**. For Netflix it then **Syncs Continue Watching**.
6. HDMI never left the guide.

Opening Connect **does not** launch Chrome. Chrome starts only after the phone
submits, using the shared background nest.

## Sources

| Source | Adapter | After login |
| --- | --- | --- |
| **Netflix** | Wired (reference). Login URL + `NetflixId` cookies + harvest `netflix.continue_watching.v1`. | Refresh Continue Watching. |
| **Prime Video** | Stub: same UX, `primevideo.com`, heuristic cookies (`at-main` / `x-main`). | Marks connected. No harvest skill yet. |
| **Disney+** | Stub: `disneyplus.com/identity/login`, heuristic cookies. | Marks connected. |
| **YouTube** | Stub: Google login → youtube.com, `LOGIN_INFO` / `SAPISID`. | Marks connected. |
| Jellyfin | Adapter exists, hidden from Connect (not on the Apps shelf). | — |
| Globoplay | Not an Apps source here (Globo is OTA). | — |

Adding the next source is: login URL + cookie needles + optional harvest skill
in `src-tauri/src/companion/sources.rs`. No new TV UX.

## How the nest stays invisible (login **and** harvest)

| Step | Behavior |
| --- | --- |
| Launch | Shared Chrome profile, **no gamescope**, **no `-f`**. |
| Flags | `--start-minimized --window-size=1280,720 --window-position=-32000,-32000` |
| After spawn | Hide nest + raise the guide (harvest: `keep_guide_after_harvest`; login: `keep_hidden`). |
| Fail closed | Nest open/hide failure does **not** retry-spam. Harvest and login share one background slot; a second Chrome is not spawned. |
| Auto-harvest | Off unless `ZAPPE_AUTO_HARVEST=1`. Storms (AT-SPI timeout, no Chrome on the bus, nest kill) back off 30s → 2m → 10m → 1h and disable after 3 failures. |
| Typing | `inject_text_quiet` / `inject_key_quiet` — never `focus_nest_on_host`. |
| Optional | `ZAPPE_LOGIN_BACKEND=xvfb` for a virtual X display. |
| Never | Chrome `--headless`, CDP, `--enable-automation`. |

`ZAPPE_LOGIN_FAKE=1` marks the selected source connected without Chrome (tests).

## Appliance

```bash
mkdir -p ~/.config/systemd/user
cp packaging/appliance/zappe-companion.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now zappe-companion.service
```

The unit binds **port 80** with `CAP_NET_BIND_SERVICE`. The kiosk wants this
unit; if :80 is up, the guide polls it instead of starting a second server.

## API (LAN)

| Path | Role |
| --- | --- |
| `GET /` | Phone landing |
| `GET /c/{code}?s=netflix` | QR claim + login form for that source |
| `GET /api/tv` | TV session (code, QR, selected source, per-source status) |
| `POST /api/tv/source` | `{ "source": "prime" }` |
| `GET /api/sources` | Adapter list |
| `POST /api/claim` | Pair the device code |
| `POST /api/login` | Hidden login (`source`, email, password, provider) |
| `POST /api/factor` | Extra email / 2FA code |
| `GET /health` | `ok` |

Per-source flags: `~/.local/share/zappe/auth-sources.json`.
Passwords are not stored.
