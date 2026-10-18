#!/usr/bin/env bash
# Um gamescope só. A unit de sistema zappe-kiosk.service (tty1, seat0)
# chama ~/bin/zappe-kiosk.sh, que chama este script. O Chrome nasce dentro
# do mesmo gamescope. Não há segundo gamescope.
#
# O loop relança o app se ele sair. A unit também tem Restart=always.
#
# TODO: captura contínua da tela via PipeWire (portal de screencast).
# O espelho HDMI baseado em gpu-screen-recorder, em clipes de 2s, foi removido.
set -u

# HOME e USER vêm da unit de sistema (User= explícito). O runtime do
# usuário sai do PAM; se a sessão não exportou, usa o uid real.
if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
  export XDG_RUNTIME_DIR="/run/user/$(id -u)"
fi
if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" && -S "${XDG_RUNTIME_DIR}/bus" ]]; then
  export DBUS_SESSION_BUS_ADDRESS="unix:path=${XDG_RUNTIME_DIR}/bus"
fi

export PATH="${HOME}/bin:/usr/local/bin:${PATH}"

DATA="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"

while true; do
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

  gamescope -W "${WIDTH}" -H "${HEIGHT}" -f -- zappe || true
  sleep 1
done
