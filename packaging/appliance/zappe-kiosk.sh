#!/usr/bin/env bash
# Minimal living-room kiosk wrapper. PATH prefers the user-local binary
# installed by zappe-update.sh. Does not touch chrome-profile.
# Outer compositor only — the Chrome nest is a second gamescope spawned
# from zappe. Do not add --enable-automation. Input: docs/nest-input.md.
set -euo pipefail

export PATH="${HOME}/bin:/usr/local/bin:${PATH}"
exec gamescope -e -f -- zappe
