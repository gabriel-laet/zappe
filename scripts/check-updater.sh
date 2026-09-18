#!/usr/bin/env bash
# Refuse to regress the appliance updater onto `cargo build --release`
# (that leaves cfg(dev) → http://localhost:1420).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
script="${root}/packaging/appliance/zappe-update.sh"

if grep -E '^[[:space:]]*cargo[[:space:]]+build[[:space:]]+--release' "${script}"; then
  echo "check-updater: ${script} still runs cargo build --release" >&2
  exit 1
fi
if ! grep -qE '^[[:space:]]*npm run tauri -- build --no-bundle[[:space:]]*$' "${script}"; then
  echo "check-updater: ${script} must invoke: npm run tauri -- build --no-bundle" >&2
  exit 1
fi
echo "check-updater: ok"
