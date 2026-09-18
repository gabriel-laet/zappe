#!/usr/bin/env bash
# Capture helper for the living-room ATONGX / XING WEI dongle (and the next one).
#
#   ./packaging/appliance/capture-atongx.sh           # inventory + listen
#   ./packaging/appliance/capture-atongx.sh --list    # KEY bitfields only
#   ./packaging/appliance/capture-atongx.sh --prompt  # press each mapped button
#   ./packaging/appliance/capture-atongx.sh --all     # every evdev node
#
# Needs read access to /dev/input/event*. On the appliance:
#   sudo usermod -aG input "$USER"
#   sudo cp packaging/appliance/99-atongx.rules /etc/udev/rules.d/
#   sudo udevadm control --reload-rules && sudo udevadm trigger
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
exec python3 "$ROOT/packaging/appliance/capture-atongx.py" "$@"
