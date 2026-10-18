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
if ! grep -q 'install-whisper.sh' "${script}"; then
  echo "check-updater: updater must call install-whisper.sh" >&2
  exit 1
fi
whisper="${root}/packaging/appliance/install-whisper.sh"
for token in GGML_VULKAN vulkan-intel spirv-headers ggml-small-q8_0; do
  if ! grep -q "${token}" "${whisper}"; then
    echo "check-updater: install-whisper.sh missing ${token}" >&2
    exit 1
  fi
done
echo "check-updater: ok"
