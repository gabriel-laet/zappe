#!/usr/bin/env bash
# Whisper pt-BR with Vulkan for the living-room box (Intel N150 / Mesa).
# Puts whisper-server and whisper-cli in ~/.local/share/zappe/whisper/bin
# and downloads models/ggml-small-q8_0.bin. The server keeps the model loaded.
set -euo pipefail

DATA="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"
WHISPER_DIR="${ZAPPE_WHISPER_DIR:-${DATA}/whisper}"
BIN_DIR="${WHISPER_DIR}/bin"
MODEL_DIR="${WHISPER_DIR}/models"
# The kiosk drop-in used to pin the f16 ggml-small.bin. That file is slower
# and must not replace the q8 download.
case "${ZAPPE_WHISPER_MODEL:-}" in
  *ggml-small.bin)
    echo "install-whisper: ignoring ZAPPE_WHISPER_MODEL=${ZAPPE_WHISPER_MODEL}; using q8_0" >&2
    unset ZAPPE_WHISPER_MODEL
    ;;
esac
MODEL="${ZAPPE_WHISPER_MODEL:-${MODEL_DIR}/ggml-small-q8_0.bin}"
MODEL_URL="${ZAPPE_WHISPER_MODEL_URL:-https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q8_0.bin}"
SRC_DIR="${WHISPER_DIR}/src"
LINK_DIR="${HOME}/bin"
SYSTEM_DROPIN="${ZAPPE_WHISPER_SYSTEM_DROPIN:-/etc/systemd/system/zappe-kiosk.service.d/whisper.conf}"
USER_DROPIN="${ZAPPE_WHISPER_USER_DROPIN:-${HOME}/.config/systemd/user/zappe-kiosk.service.d/whisper.conf}"

# Drop Environment= lines that pin the f16 model or the CLI. If nothing else
# remains, delete the file. sudo -n failure must not abort the install.
dropin_changed=0
fix_whisper_dropin() {
  local file="$1"
  [[ -f "${file}" ]] || return 0
  if ! grep -qE 'ZAPPE_WHISPER_(MODEL|BIN)=' "${file}"; then
    return 0
  fi
  local tmp
  tmp="$(mktemp)"
  grep -vE '^[[:space:]]*Environment=.*ZAPPE_WHISPER_(MODEL|BIN)=' "${file}" > "${tmp}" || true
  local ok=0
  if ! grep -qE '^[[:space:]]*[A-Za-z][A-Za-z0-9]*=' "${tmp}"; then
    if [[ "${file}" == /etc/* ]]; then
      if sudo -n rm -f "${file}"; then
        ok=1
      fi
    elif rm -f "${file}"; then
      ok=1
    fi
    if [[ "${ok}" == "1" ]]; then
      echo "install-whisper: removed ${file}"
      dropin_changed=1
    else
      echo "install-whisper: could not remove ${file}; run: sudo rm -f ${file}" >&2
    fi
  else
    if [[ "${file}" == /etc/* ]]; then
      if sudo -n cp "${tmp}" "${file}"; then
        ok=1
      fi
    elif cp "${tmp}" "${file}"; then
      ok=1
    fi
    if [[ "${ok}" == "1" ]]; then
      echo "install-whisper: updated ${file}"
      dropin_changed=1
    else
      echo "install-whisper: could not update ${file}; run: sudo rm -f ${file}" >&2
    fi
  fi
  rm -f "${tmp}"
}

fix_dropin_dir() {
  local dir="$1"
  [[ -d "${dir}" ]] || return 0
  local file
  for file in "${dir}"/*.conf; do
    [[ -f "${file}" ]] || continue
    fix_whisper_dropin "${file}"
  done
}

fix_dropin_dir "$(dirname "${SYSTEM_DROPIN}")"
fix_dropin_dir "$(dirname "${USER_DROPIN}")"
if [[ "${ZAPPE_WHISPER_DROPIN_TEST:-}" == "1" ]]; then
  exit 0
fi
if [[ "${dropin_changed}" == "1" ]]; then
  if sudo -n systemctl daemon-reload; then
    echo "install-whisper: systemctl daemon-reload"
  else
    echo "install-whisper: sudo -n systemctl daemon-reload failed; run: sudo systemctl daemon-reload" >&2
  fi
  if systemctl --user daemon-reload; then
    echo "install-whisper: systemctl --user daemon-reload"
  else
    echo "install-whisper: systemctl --user daemon-reload failed" >&2
  fi
fi

mkdir -p "${BIN_DIR}" "${MODEL_DIR}" "${LINK_DIR}"

if command -v pacman >/dev/null 2>&1; then
  if sudo -n true >/dev/null 2>&1; then
    if ! sudo -n pacman -S --needed --noconfirm \
      vulkan-intel spirv-headers vulkan-headers glslang \
      cmake git base-devel alsa-utils ffmpeg curl; then
      echo "install-whisper: pacman failed; continuing if vulkan-intel and spirv-headers are already installed" >&2
    fi
  else
    echo "install-whisper: sudo -n unavailable; vulkan-intel and spirv-headers must already be installed" >&2
  fi
else
  echo "install-whisper: pacman not found; install vulkan-intel spirv-headers vulkan-headers glslang cmake git base-devel alsa-utils ffmpeg" >&2
fi

if ! command -v arecord >/dev/null 2>&1; then
  echo "install-whisper: arecord missing (alsa-utils)" >&2
  exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "install-whisper: ffmpeg missing; whisper-server --convert needs it" >&2
fi

if [[ ! -s "${MODEL}" ]]; then
  echo "install-whisper: downloading ggml-small-q8_0 → ${MODEL}"
  if ! curl -fL --retry 3 --retry-delay 2 -o "${MODEL}.part" "${MODEL_URL}"; then
    rm -f "${MODEL}.part"
    echo "install-whisper: model download failed" >&2
    exit 1
  fi
  mv "${MODEL}.part" "${MODEL}"
else
  echo "install-whisper: model already at ${MODEL}"
fi

need_build=0
if [[ ! -x "${BIN_DIR}/whisper-cli" || ! -x "${BIN_DIR}/whisper-server" ]]; then
  need_build=1
fi
if [[ "${ZAPPE_WHISPER_REBUILD:-}" == "1" ]]; then
  need_build=1
fi

copy_built() {
  local name="$1"
  local built="${SRC_DIR}/build/bin/${name}"
  if [[ ! -x "${built}" ]]; then
    built="$(find "${SRC_DIR}/build" -name "${name}" -type f -executable | head -n1 || true)"
  fi
  if [[ -z "${built}" || ! -x "${built}" ]]; then
    echo "install-whisper: ${name} was not produced" >&2
    return 1
  fi
  install -m 0755 "${built}" "${BIN_DIR}/${name}"
}

if [[ "${need_build}" == "0" ]]; then
  echo "install-whisper: binaries already in ${BIN_DIR}"
else
  if ! command -v cmake >/dev/null 2>&1 || ! command -v git >/dev/null 2>&1; then
    echo "install-whisper: cmake/git missing; cannot build whisper.cpp with GGML_VULKAN" >&2
    exit 1
  fi
  echo "install-whisper: building whisper.cpp with GGML_VULKAN → ${SRC_DIR}"
  if [[ ! -d "${SRC_DIR}/.git" ]]; then
    git clone --depth 1 https://github.com/ggml-org/whisper.cpp "${SRC_DIR}"
  fi
  cmake -S "${SRC_DIR}" -B "${SRC_DIR}/build" \
    -DGGML_VULKAN=1 \
    -DWHISPER_SDL2=OFF \
    -DBUILD_SHARED_LIBS=OFF
  cmake --build "${SRC_DIR}/build" -j"$(nproc)" --target whisper-cli whisper-server
  copy_built whisper-cli
  copy_built whisper-server
  echo "install-whisper: installed ${BIN_DIR}/whisper-cli and whisper-server"
fi

ln -sfn "${BIN_DIR}/whisper-cli" "${LINK_DIR}/whisper-cli"
ln -sfn "${BIN_DIR}/whisper-server" "${LINK_DIR}/whisper-server"

cat <<EOF

Whisper pt-BR (Vulkan) is ready.

  server: ${BIN_DIR}/whisper-server
  cli:    ${BIN_DIR}/whisper-cli
  model:  ${MODEL}

The app keeps whisper-server resident on ggml-small-q8_0 and falls back
to whisper-cli with -l pt -nt -t 4 -bs 1 -bo 1 and -ac sized to the clip.
A drop-in that pinned ggml-small.bin or ZAPPE_WHISPER_BIN is removed.
Set ZAPPE_WHISPER_SERVER=off to force the CLI.
Set ZAPPE_WHISPER_REBUILD=1 to compile again.
See docs/voice.md
EOF
