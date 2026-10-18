#!/usr/bin/env bash
# Chamado pelo autologin do tty1. O processo do gamescope fica no
# zappe-kiosk.service para o zappe-update conseguir reiniciá-lo.
set -euo pipefail

if [[ "$(tty 2>/dev/null || true)" != "/dev/tty1" ]]; then
  exit 0
fi

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
UNIT_DIR="${HOME}/.config/systemd/user"
if [[ ! -f "${UNIT_DIR}/zappe-kiosk.service" ]]; then
  mkdir -p "${UNIT_DIR}"
  cp "$(dirname "$0")/zappe-kiosk.service" "${UNIT_DIR}/zappe-kiosk.service"
  systemctl --user daemon-reload || true
fi

if systemctl --user start zappe-kiosk.service; then
  exec sleep infinity
fi

exec "$(dirname "$0")/zappe-kiosk.sh"
