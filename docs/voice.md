# Voz (Whisper pt-BR)

O botão vermelho do controle é o código evdev **582**. O app grava com `arecord` e transcreve com `whisper-cli -l pt`.

Ordem do modelo, em `~/.local/share/zappe/whisper/`:

1. `ggml-base.bin`
2. `ggml-tiny.bin`
3. variantes `q5_1` desses dois
4. `ggml-small.bin`, se um aparelho antigo ainda tiver esse arquivo

`ZAPPE_WHISPER_MODEL` aponta um arquivo explícito. A instalação baixa o base:

```bash
packaging/appliance/install-whisper.sh
```

Para o modelo menor:

```bash
ZAPPE_WHISPER_MODEL="$HOME/.local/share/zappe/whisper/ggml-tiny.bin" \
ZAPPE_WHISPER_MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin" \
  packaging/appliance/install-whisper.sh
```

## Frases

O gramática é curta, em `src/voice.cpp`.

| Fala | Efeito |
| --- | --- |
| abrir Netflix, YouTube, Prime, HBO Max, Apple TV, Disney+, Globoplay | abre o streaming |
| voltar | sai da reprodução ou volta no guia |
| volume mais / volume menos | Samsung |
| mudo | Samsung |
| ir para Globo, Record, SBT, Band, Cultura | TV aberta |
| play / pausar | play/pause |

`ZAPPE_VOICE_FAKE="abrir netflix"` devolve esse texto sem microfone. `ZAPPE_VOICE_SECONDS` limita a gravação (2–8, padrão 5). `ZAPPE_ALSA_DEVICE` escolhe o `arecord -D`. `ZAPPE_WHISPER_BIN` aponta o `whisper-cli`.
