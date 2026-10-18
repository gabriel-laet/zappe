#!/usr/bin/env bash
# On-device updater for the living-room box.
#
# Polls origin/main, rebuilds the Qt binary with CMake, installs it,
# and restarts zappe-kiosk.service. Never touches chrome-profile.
#
# Env:
#   ZAPPE_SRC       repo checkout (default: ~/src/zappe)
#   ZAPPE_DATA_DIR  state/log dir (default: ~/.local/share/zappe)
#   ZAPPE_UNIT      unit to restart (default: zappe-kiosk.service)
#                   system unit wins: sudo -n systemctl restart
#                   otherwise the user unit, if it exists
set -euo pipefail

ZAPPE_SRC="${ZAPPE_SRC:-${HOME}/src/zappe}"
ZAPPE_DATA_DIR="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"
ZAPPE_UNIT="${ZAPPE_UNIT:-zappe-kiosk.service}"
REMOTE="${ZAPPE_REMOTE:-origin}"
BRANCH="${ZAPPE_BRANCH:-main}"

INSTALLED_SHA="${ZAPPE_DATA_DIR}/installed-sha"
LOG="${ZAPPE_DATA_DIR}/update.log"
LOCK="${ZAPPE_DATA_DIR}/update.lock"
USER_BIN="${HOME}/bin/zappe"
SYSTEM_BIN="/usr/local/bin/zappe"

export PATH="${HOME}/bin:${HOME}/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH}"

mkdir -p "${ZAPPE_DATA_DIR}" "${HOME}/bin"

if [[ -f "${LOG}" ]]; then
  log_bytes="$(wc -c < "${LOG}" || true)"
  if [[ "${log_bytes:-0}" -gt 1048576 ]]; then
    tail -c 524288 "${LOG}" > "${LOG}.tmp" && mv "${LOG}.tmp" "${LOG}"
  fi
fi

log() {
  local ts
  ts="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  printf '%s %s\n' "${ts}" "$*" >> "${LOG}"
}

die() {
  log "ERROR: $*"
  echo "zappe-update: $*" >&2
  exit 1
}

exec 9>"${LOCK}"
if ! flock -n 9; then
  log "another update is already running; exiting"
  exit 0
fi

if [[ ! -d "${ZAPPE_SRC}/.git" ]]; then
  die "not a git repo: ${ZAPPE_SRC} (set ZAPPE_SRC)"
fi

git_c() {
  git -C "${ZAPPE_SRC}" "$@"
}

if ! git_c fetch --quiet "${REMOTE}" "+refs/heads/${BRANCH}:refs/remotes/${REMOTE}/${BRANCH}"; then
  die "git fetch ${REMOTE} ${BRANCH} failed"
fi

if ! remote_sha="$(git_c rev-parse --verify "${REMOTE}/${BRANCH}^{commit}")"; then
  die "cannot resolve ${REMOTE}/${BRANCH}"
fi

installed_sha=""
if [[ -f "${INSTALLED_SHA}" ]]; then
  installed_sha="$(tr -d '[:space:]' < "${INSTALLED_SHA}")"
fi

if [[ -n "${installed_sha}" && "${remote_sha}" == "${installed_sha}" ]]; then
  exit 0
fi

log "update ${installed_sha:-<none>} -> ${remote_sha}"

if ! git_c diff --quiet --ignore-submodules HEAD \
  || ! git_c diff --cached --quiet --ignore-submodules; then
  die "dirty tree in ${ZAPPE_SRC}; refusing to pull (commit or stash locally)"
fi

if git_c show-ref --verify --quiet "refs/heads/${BRANCH}"; then
  git_c checkout --quiet "${BRANCH}"
else
  git_c checkout --quiet -b "${BRANCH}" --track "${REMOTE}/${BRANCH}"
fi

if ! git_c pull --ff-only --quiet "${REMOTE}" "${BRANCH}"; then
  die "git pull --ff-only failed (diverged from ${REMOTE}/${BRANCH}?)"
fi

if ! command -v cmake >/dev/null 2>&1; then
  die "cmake not on PATH"
fi

generator="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
  generator="Ninja"
fi

log "build: cmake -G ${generator} && cmake --build"
cmake -S "${ZAPPE_SRC}" -B "${ZAPPE_SRC}/build" -G "${generator}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${ZAPPE_SRC}/build"

bin=""
for candidate in \
  "${ZAPPE_SRC}/build/zappe" \
  "${ZAPPE_SRC}/build/Release/zappe"; do
  if [[ -x "${candidate}" ]]; then
    bin="${candidate}"
    break
  fi
done
if [[ -z "${bin}" ]]; then
  die "built binary not found under build/zappe"
fi

install -D -m 755 "${bin}" "${USER_BIN}"
log "installed ${bin} -> ${USER_BIN}"

if sudo -n true >/dev/null 2>&1; then
  sudo -n install -m 755 "${bin}" "${SYSTEM_BIN}"
  log "installed ${bin} -> ${SYSTEM_BIN}"
else
  log "sudo -n unavailable; skipped ${SYSTEM_BIN} (kiosk PATH uses ${HOME}/bin)"
fi

mkdir -p "${HOME}/.config/systemd/user" "${HOME}/bin"
if [[ ! -x "${HOME}/bin/zappe-kiosk.sh" && -f "${ZAPPE_SRC}/packaging/appliance/zappe-kiosk.sh" ]]; then
  cat > "${HOME}/bin/zappe-kiosk.sh" <<EOF
#!/bin/bash
exec "${ZAPPE_SRC}/packaging/appliance/zappe-kiosk.sh"
EOF
  chmod 755 "${HOME}/bin/zappe-kiosk.sh"
  log "installed ${HOME}/bin/zappe-kiosk.sh"
fi
if [[ -f "${ZAPPE_SRC}/packaging/appliance/zappe-kiosk.user.service" \
   && ! -f "${HOME}/.config/systemd/user/zappe-kiosk.service" ]]; then
  cp "${ZAPPE_SRC}/packaging/appliance/zappe-kiosk.user.service" \
    "${HOME}/.config/systemd/user/zappe-kiosk.service"
fi
if [[ -f "${ZAPPE_SRC}/packaging/appliance/zappe-update.service" ]]; then
  cp "${ZAPPE_SRC}/packaging/appliance/zappe-update.service" \
    "${HOME}/.config/systemd/user/zappe-update.service"
fi
systemctl --user daemon-reload || true

printf '%s\n' "${remote_sha}" > "${INSTALLED_SHA}"
log "wrote ${INSTALLED_SHA}"

if systemctl cat "${ZAPPE_UNIT}" >/dev/null 2>&1; then
  if sudo -n systemctl restart "${ZAPPE_UNIT}"; then
    log "restarted system ${ZAPPE_UNIT}"
  else
    log "sudo -n systemctl restart ${ZAPPE_UNIT} failed"
  fi
elif systemctl --user cat "${ZAPPE_UNIT}" >/dev/null 2>&1; then
  if systemctl --user restart "${ZAPPE_UNIT}"; then
    log "restarted user ${ZAPPE_UNIT}"
  else
    log "restart user ${ZAPPE_UNIT} failed (ignored; safe if kiosk is not running)"
  fi
else
  log "unit ${ZAPPE_UNIT} not installed; skip restart (expected name: zappe-kiosk.service)"
fi

log "update complete ${remote_sha}"
exit 0
