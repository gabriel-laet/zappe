#!/usr/bin/env bash
# Um gamescope só, lançado pelo autologin do tty1 (via zappe-kiosk.service).
# O app Qt é o cliente. O Chrome nasce dentro do mesmo gamescope, herdando
# WAYLAND_DISPLAY / DISPLAY. Não há segundo gamescope.
#
# TODO: captura contínua da tela via PipeWire (portal de screencast).
# O espelho HDMI baseado em gpu-screen-recorder, em clipes de 2s, foi removido.
set -euo pipefail

export PATH="${HOME}/bin:/usr/local/bin:${PATH}"

DATA="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"
WIDTH=1920
HEIGHT=1080
if [[ -f "${DATA}/display.json" ]] && grep -q '"4K"' "${DATA}/display.json"; then
  WIDTH=3840
  HEIGHT=2160
fi

CARD="${ZAPPE_HDMI_CARD:-alsa_card.pci-0000_00_1f.3}"
PROFILE="${ZAPPE_HDMI_PROFILE:-output:hdmi-stereo}"
if command -v pactl >/dev/null 2>&1; then
  pactl set-card-profile "${CARD}" "${PROFILE}" || true
fi

exec gamescope -W "${WIDTH}" -H "${HEIGHT}" -f -- zappe
