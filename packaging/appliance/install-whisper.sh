#!/usr/bin/env bash
# One-time Whisper pt-BR install for the living-room box.
# Downloads ggml-small.bin and builds whisper-cli if it is not already on PATH.
set -euo pipefail

DATA="${ZAPPE_DATA_DIR:-${HOME}/.local/share/zappe}"
WHISPER_DIR="${DATA}/whisper"
BIN_DIR="${HOME}/bin"
MODEL="${ZAPPE_WHISPER_MODEL:-${WHISPER_DIR}/ggml-small.bin}"
MODEL_URL="${ZAPPE_WHISPER_MODEL_URL:-https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin}"
SRC_DIR="${WHISPER_DIR}/src"

mkdir -p "${WHISPER_DIR}" "${BIN_DIR}"

if ! command -v arecord >/dev/null 2>&1; then
  echo "install-whisper: arecord missing. On Arch: sudo pacman -S alsa-utils" >&2
  exit 1
fi

if [[ ! -s "${MODEL}" ]]; then
  echo "install-whisper: downloading $(basename "${MODEL}") → ${MODEL}"
  curl -fL --retry 3 --retry-delay 2 -o "${MODEL}.part" "${MODEL_URL}"
  mv "${MODEL}.part" "${MODEL}"
else
  echo "install-whisper: model already at ${MODEL}"
fi

pick_bin() {
  if [[ -n "${ZAPPE_WHISPER_BIN:-}" && -x "${ZAPPE_WHISPER_BIN}" ]]; then
    printf '%s\n' "${ZAPPE_WHISPER_BIN}"
    return
  fi
  if command -v whisper-cli >/dev/null 2>&1; then
    command -v whisper-cli
    return
  fi
  if [[ -x "${BIN_DIR}/whisper-cli" ]]; then
    printf '%s\n' "${BIN_DIR}/whisper-cli"
    return
  fi
  if [[ -x "${WHISPER_DIR}/whisper-cli" ]]; then
    printf '%s\n' "${WHISPER_DIR}/whisper-cli"
    return
  fi
  return 1
}

if BIN="$(pick_bin)"; then
  echo "install-whisper: binary ${BIN}"
else
  if ! command -v cmake >/dev/null 2>&1 || ! command -v git >/dev/null 2>&1; then
    echo "install-whisper: no whisper-cli, and cmake/git missing to build whisper.cpp." >&2
    echo "  Arch: sudo pacman -S cmake git gcc make" >&2
    exit 1
  fi
  echo "install-whisper: building whisper.cpp → ${SRC_DIR}"
  if [[ ! -d "${SRC_DIR}/.git" ]]; then
    git clone --depth 1 https://github.com/ggerganov/whisper.cpp "${SRC_DIR}"
  fi
  cmake -S "${SRC_DIR}" -B "${SRC_DIR}/build" -DWHISPER_SDL2=OFF
  cmake --build "${SRC_DIR}/build" -j"$(nproc)" --target whisper-cli
  built="${SRC_DIR}/build/bin/whisper-cli"
  if [[ ! -x "${built}" ]]; then
    built="$(find "${SRC_DIR}/build" -name whisper-cli -type f -executable | head -n1)"
  fi
  if [[ -z "${built}" || ! -x "${built}" ]]; then
    echo "install-whisper: build finished but whisper-cli was not found" >&2
    exit 1
  fi
  install -m 0755 "${built}" "${BIN_DIR}/whisper-cli"
  BIN="${BIN_DIR}/whisper-cli"
  echo "install-whisper: installed ${BIN}"
fi

cat <<EOF

Whisper pt-BR is ready.

  binary: ${BIN}
  model:  ${MODEL}

Add to ~/.config/systemd/user/zappe.service.d/whisper.conf if PATH is tight:

  [Service]
  Environment=ZAPPE_WHISPER_BIN=${BIN}
  Environment=ZAPPE_WHISPER_MODEL=${MODEL}

Then: systemctl --user daemon-reload && systemctl --user restart zappe.service

Cloud stays off unless you also set ZAPPE_WHISPER_URL.
See docs/voice.md
EOF
