#!/usr/bin/env bash
# The appliance updater builds with CMake and restarts zappe-kiosk.service.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
script="${root}/packaging/appliance/zappe-update.sh"

if grep -nE 'npm run tauri|cargo build --release|zappe-companion' "${script}"; then
  echo "check-updater: updater still mentions Tauri, cargo release, or the companion" >&2
  exit 1
fi
if ! grep -q 'cmake --build' "${script}"; then
  echo "check-updater: updater must run cmake --build" >&2
  exit 1
fi
if ! grep -q 'zappe-kiosk.service' "${script}"; then
  echo "check-updater: updater must restart zappe-kiosk.service" >&2
  exit 1
fi
if ! grep -q 'sudo -n systemctl restart' "${script}"; then
  echo "check-updater: updater must use sudo -n systemctl restart when the system unit exists" >&2
  exit 1
fi
echo "check-updater: ok"
