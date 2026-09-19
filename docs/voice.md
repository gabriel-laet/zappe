# ATONGX red mic — Whisper pt-BR

The living-room air mouse’s **red mic** is a constrained couch remote, not a chat LLM. Hold or tap it: Zappe records from ALSA, runs **local whisper.cpp** in Portuguese, then executes a small pt-BR grammar. The guide stays up (a pill overlay only). Netflix / OTA only leave the guide when you asked for them.

## How a press starts listen

1. ATONGX / XING WEI evdev sees Voice (`KEY_F8` / `KEY_F9` / `KEY_RECORD` / `KEY_VOICECOMMAND` / `KEY_ASSISTANT` / `KEY_DICTATE`). Linux has no `KEY_MIC`; we treat **Voice / `KEY_VOICECOMMAND` (0x246)** as that alias.
2. The grabbed consumer node (and the keyboard node, for Voice only) emits `voice-arm` on press and `voice-release` on release. Compositor shortcuts still register **F8 / F9**. The guide also listens for `keydown` / `keyup` on those codes.
3. `useVoiceMic` calls `voice_begin` (starts `arecord` 16 kHz mono). The overlay shows **Ouvindo…**. Nothing hides the guide.
4. **Hold:** release → `voice_end` (SIGINT `arecord`, then whisper). **Tap** (release &lt; 350 ms): keep recording ~4 s. Hard cap ~6–8 s.
5. Transcript goes through [`src/lib/voicePtBr.ts`](../src/lib/voicePtBr.ts). Known intents run; chatter is a toast only.

In-flight lock + HID debounce stop evdev + JS from double-starting.

## Where the model lives

Default (offline) on the appliance:

| Piece | Path |
| --- | --- |
| Model | `~/.local/share/zappe/whisper/ggml-small.bin` (or `ggml-base.bin`) |
| Binary | `~/bin/whisper-cli` or `$ZAPPE_WHISPER_BIN` |
| Override model | `ZAPPE_WHISPER_MODEL` |

Install once:

```bash
packaging/appliance/install-whisper.sh
systemctl --user restart zappe.service
```

Needs `alsa-utils` (`arecord`) and the kiosk user in the `audio` + `input` groups.

## Lexicon (pt-BR)

| Say | Intent |
| --- | --- |
| `início`, `tela inicial`, `ir para o guia` | Home (no-op if already idle) |
| `voltar`, `volta` | Back / hide nest / stop OTA |
| `volume mais` / `aumentar o volume` | Samsung volume + |
| `volume menos` / `abaixa o volume` | Samsung volume − |
| `mudo`, `silêncio` | Samsung mute |
| `Netflix`, `abrir Netflix` | Open nest |
| `Prime`, `Disney`, `YouTube` | Open that app |
| `play`, `pausar`, `continuar` | Play / pause |
| `Globo`, `Record`, `SBT`, `Band`, `Cultura`, `ir para …`, `canal …` | TV aberta (HD-first `channels.conf`) |
| `sincronizar`, `sync`, `atualizar` | Continue Watching harvest (stays on the guide) |
| `conectar`, `telefone` | Connect (phone) |

Rejected: jokes, weather, anything outside that list.

## Cloud is opt-in

Local whisper.cpp is the default. Cloud is **off** unless you set a URL:

```bash
# OpenAI-compatible multipart POST; expects JSON {"text":"…"}
export ZAPPE_WHISPER_URL=https://api.example/v1/audio/transcriptions
export ZAPPE_WHISPER_TOKEN=sk-…
# optional: ZAPPE_WHISPER_CLOUD_MODEL=whisper-1
```

A wrapper that prints a transcript on stdout: `ZAPPE_WHISPER_KIND=raw` + `ZAPPE_WHISPER_BIN`.

## Rematch the red button (don’t fight pointer capture)

If the pill never appears, the dongle’s Voice scancode is not in the map. Do **not** invent pointer codes here.

```bash
cat /proc/bus/input/devices | less
sudo evtest /dev/input/event5   # consumer — press the red mic
sudo evtest /dev/input/event3   # keyboard node
```

Note `KEY_*` and `(code)`. Then either:

- wait for / merge the parallel “capture mic/pointer codes” change, or
- set `Environment=ZAPPE_HID_VOICE_CODE=582` (example) on `zappe.service` and restart.

Journal: `ATONGX evdev Voice code=…`. Accepts existing Voice / `KEY_MIC` alias plus the env extra.

## Verify on zappe-tv (no live tuner required for grammar)

```bash
# CI / no mic
npm run check:input
cd src-tauri && cargo test --lib voice::tests hid::tests

# On the box, fake a phrase through the real invoke path
ZAPPE_VOICE_FAKE='ir para Globo'   # in the user unit, then restart
# Press the red mic — overlay should show Globo HD (or the channels.conf name)

# Live
journalctl --user -u zappe.service -f
# press/hold red mic → "ATONGX evdev Voice" → "ATONGX voice arecord started" → transcript
```

Manual couch check: stay on Home, say **volume mais** (Samsung OSD, guide stays). Say **sincronizar** (harvest toast, no nest steal). Open Netflix, say **voltar** (nest dies, guide back). Say **ir para Globo** only if the tuner + conf are present.

`ZAPPE_VOICE_FAKE` skips `arecord` and whisper — use it when the model is not installed yet.
