#!/usr/bin/env bash
# On-device updater for the living-room box (hostname zappe-tv, user glaet).
#
# Polls origin/main, rebuilds the zappe binary (no AppImage/deb), installs it,
# and restarts the user kiosk unit. Never touches chrome-profile.
#
# Env:
#   ZAPPE_SRC       repo checkout (default: ~/src/zappe)
#   ZAPPE_DATA_DIR  state/log dir (default: ~/.local/share/zappe)
#   ZAPPE_UNIT      systemd user unit to restart (default: zappe.service)
set -euo pipefail

ZAPPE_SRC="${ZAPPE_SRC:-${HOME}/src/zappe}"
ZAPPE_DATA_DIR="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"
ZAPPE_UNIT="${ZAPPE_UNIT:-zappe.service}"
REMOTE="${ZAPPE_REMOTE:-origin}"
BRANCH="${ZAPPE_BRANCH:-main}"

INSTALLED_SHA="${ZAPPE_DATA_DIR}/installed-sha"
LOG="${ZAPPE_DATA_DIR}/update.log"
LOCK="${ZAPPE_DATA_DIR}/update.lock"
USER_BIN="${HOME}/bin/zappe"
SYSTEM_BIN="/usr/local/bin/zappe"

# systemd --user sessions often have a thin PATH
export PATH="${HOME}/.cargo/bin:${HOME}/bin:${HOME}/.local/bin:/usr/local/bin:/usr/bin:/bin:${PATH}"
if [[ -f "${HOME}/.cargo/env" ]]; then
  # shellcheck disable=SC1091
  . "${HOME}/.cargo/env"
fi

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

# --- fetch + compare -------------------------------------------------------
# Fetch the branch into the remote-tracking ref. A bare `git fetch origin main`
# only writes FETCH_HEAD on some setups and would leave origin/main stale,
# so an already-written installed-sha would skip a real update forever.
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

# Dirty tree: refuse. Never reset --hard / clean / wipe local work.
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

# --- deps + build (binary only; bundler must not gate updates) -------------
# NEVER `cargo build --release` alone. That leaves Tauri's `cfg(dev)` on, so
# the kiosk binary still loads `devUrl` (http://localhost:1420) instead of
# the baked `frontendDist`. The Tauri CLI is what flips production cfg.
#
# `npm run tauri build` (default) also packages AppImage/deb via linuxdeploy
# and can fail after the executable is already linked. Appliance updates
# skip the bundler:
#
#   npm run tauri -- build --no-bundle
cd "${ZAPPE_SRC}"

if [[ -f package-lock.json ]]; then
  npm ci
else
  npm install
fi

if ! command -v npm >/dev/null 2>&1; then
  die "npm not on PATH; cannot run the Tauri CLI"
fi
if ! npm run tauri -- --version >/dev/null; then
  die "Tauri CLI missing (need @tauri-apps/cli via npm run tauri)"
fi

log "build: npm run tauri -- build --no-bundle (not cargo build --release)"
npm run tauri -- build --no-bundle

bin=""
for candidate in \
  "${ZAPPE_SRC}/src-tauri/target/release/zappe" \
  "${ZAPPE_SRC}/target/release/zappe"; do
  if [[ -x "${candidate}" ]]; then
    bin="${candidate}"
    break
  fi
done
if [[ -z "${bin}" ]]; then
  die "built binary not found under src-tauri/target/release/zappe"
fi

# --- install ---------------------------------------------------------------
install -D -m 755 "${bin}" "${USER_BIN}"
log "installed ${bin} -> ${USER_BIN}"

if sudo -n true >/dev/null 2>&1; then
  sudo -n install -m 755 "${bin}" "${SYSTEM_BIN}"
  log "installed ${bin} -> ${SYSTEM_BIN}"
else
  log "sudo -n unavailable; skipped ${SYSTEM_BIN} (kiosk PATH uses ${HOME}/bin)"
fi

printf '%s\n' "${remote_sha}" > "${INSTALLED_SHA}"
log "wrote ${INSTALLED_SHA}"

# Restart is best-effort: unit may be missing, or gamescope/zappe may be down.
if systemctl --user cat "${ZAPPE_UNIT}" >/dev/null 2>&1; then
  if systemctl --user restart "${ZAPPE_UNIT}"; then
    log "restarted ${ZAPPE_UNIT}"
  else
    log "restart ${ZAPPE_UNIT} failed (ignored; safe if kiosk is not running)"
  fi
else
  log "unit ${ZAPPE_UNIT} not installed; skip restart (expected name: zappe.service)"
fi

log "update complete ${remote_sha}"
exit 0
