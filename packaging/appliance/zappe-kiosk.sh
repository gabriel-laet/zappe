#!/usr/bin/env bash
# Minimal living-room kiosk wrapper. PATH prefers the user-local binary
# installed by zappe-update.sh. Does not touch chrome-profile.
set -euo pipefail

export PATH="${HOME}/bin:/usr/local/bin:${PATH}"
exec gamescope -e -f -- zappe
