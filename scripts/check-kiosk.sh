#!/usr/bin/env bash
# Kiosk is one gamescope. No gpu-screen-recorder mirror.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
kiosk="${root}/packaging/appliance/zappe-kiosk.sh"
update="${root}/packaging/appliance/zappe-update.sh"

grep -q 'gamescope' "${kiosk}"
grep -q '1920' "${kiosk}"
grep -q '1080' "${kiosk}"
grep -q 'alsa_card.pci-0000_00_1f.3' "${kiosk}"
grep -q 'output:hdmi-stereo' "${kiosk}"
grep -q 'PipeWire' "${kiosk}"
if grep -vE '^[[:space:]]*#' "${kiosk}" "${update}" | grep -q 'gpu-screen-recorder'; then
  echo "check-kiosk: gpu-screen-recorder must stay removed" >&2
  exit 1
fi
if [[ -e "${root}/packaging/appliance/zappe-companion.service" ]]; then
  echo "check-kiosk: companion unit still present" >&2
  exit 1
fi
if [[ ! -f "${root}/packaging/appliance/zappe-kiosk.service" ]]; then
  echo "check-kiosk: missing zappe-kiosk.service" >&2
  exit 1
fi
if ! grep -q 'Restart=always' "${root}/packaging/appliance/zappe-kiosk.service"; then
  echo "check-kiosk: system unit must Restart=always" >&2
  exit 1
fi
if ! grep -q 'PAMName=login' "${root}/packaging/appliance/zappe-kiosk.service"; then
  echo "check-kiosk: system unit must join seat0 via PAMName=login" >&2
  exit 1
fi
unit="${root}/packaging/appliance/zappe-kiosk.service"
if grep -vE '^[[:space:]]*#' "${unit}" | grep -q '%h\|%U'; then
  echo "check-kiosk: system unit must not use %h or %U" >&2
  exit 1
fi
if ! grep -q 'Environment=HOME=/home/YOUR_USER' "${unit}" \
  || ! grep -q 'Environment=USER=YOUR_USER' "${unit}" \
  || ! grep -q 'ExecStart=/home/YOUR_USER/bin/zappe-kiosk.sh' "${unit}" \
  || ! grep -q '^User=YOUR_USER$' "${unit}"; then
  echo "check-kiosk: system unit must pin YOUR_USER, HOME and USER" >&2
  exit 1
fi
if ! grep -q 'XDG_RUNTIME_DIR' "${kiosk}"; then
  echo "check-kiosk: kiosk script must set the user runtime dir" >&2
  exit 1
fi
if ! grep -q 'while true' "${kiosk}"; then
  echo "check-kiosk: kiosk script must relaunch zappe" >&2
  exit 1
fi
if ! grep -q 'sudo -n systemctl restart' "${update}"; then
  echo "check-kiosk: updater must restart the system unit" >&2
  exit 1
fi
echo "check-kiosk: ok"
