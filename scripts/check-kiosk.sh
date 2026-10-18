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
echo "check-kiosk: ok"
