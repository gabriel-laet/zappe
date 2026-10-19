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
if grep -qE 'systemctl (--user )?cat' "${script}"; then
  echo "check-updater: updater must not detect units with systemctl cat (root-only drop-ins make it fail)" >&2
  exit 1
fi
if ! grep -q 'systemctl show -p LoadState' "${script}"; then
  echo "check-updater: updater must detect the system unit with systemctl show -p LoadState" >&2
  exit 1
fi
if ! grep -q 'install-whisper.sh' "${script}"; then
  echo "check-updater: updater must call install-whisper.sh" >&2
  exit 1
fi
whisper="${root}/packaging/appliance/install-whisper.sh"
for token in GGML_VULKAN vulkan-intel spirv-headers ggml-small-q8_0 zappe-kiosk.service.d whisper.conf; do
  if ! grep -q "${token}" "${whisper}"; then
    echo "check-updater: install-whisper.sh missing ${token}" >&2
    exit 1
  fi
done

tmp="$(mktemp -d)"
mkdir -p "${tmp}/system" "${tmp}/user"
cat > "${tmp}/system/whisper.conf" <<'EOF'
[Service]
Environment=ZAPPE_WHISPER_MODEL=ggml-small.bin
Environment=ZAPPE_WHISPER_BIN=~/bin/whisper-cli
EOF
cat > "${tmp}/user/whisper.conf" <<'EOF'
[Service]
Environment=ZAPPE_WHISPER_MODEL=/home/x/ggml-small.bin
Environment=ZAPPE_KEEP=1
EOF
cat > "${tmp}/user/other.conf" <<'EOF'
[Service]
Environment=ZAPPE_OTHER=1
EOF
ZAPPE_WHISPER_DROPIN_TEST=1 \
  ZAPPE_WHISPER_SYSTEM_DROPIN="${tmp}/system/whisper.conf" \
  ZAPPE_WHISPER_USER_DROPIN="${tmp}/user/whisper.conf" \
  bash "${whisper}"
if [[ -e "${tmp}/system/whisper.conf" ]]; then
  echo "check-updater: f16 drop-in should be removed" >&2
  rm -rf "${tmp}"
  exit 1
fi
if grep -q 'ZAPPE_WHISPER_MODEL' "${tmp}/user/whisper.conf"; then
  echo "check-updater: f16 model pin should be stripped" >&2
  rm -rf "${tmp}"
  exit 1
fi
if ! grep -q 'ZAPPE_KEEP=1' "${tmp}/user/whisper.conf"; then
  echo "check-updater: other settings should stay" >&2
  rm -rf "${tmp}"
  exit 1
fi
if ! grep -q 'ZAPPE_OTHER=1' "${tmp}/user/other.conf"; then
  echo "check-updater: unrelated drop-in changed" >&2
  rm -rf "${tmp}"
  exit 1
fi
rm -rf "${tmp}"
echo "check-updater: ok"
