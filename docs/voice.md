# Voz (Whisper pt-BR)

O botão do microfone é o código evdev **582**. Segurar grava; soltar fecha a frase. Enquanto o botão está preso, o app pede uma transcrição parcial cerca de uma vez por segundo e só mostra o texto. O comando roda na passagem final, ao soltar.

O áudio sai do `arecord` em PCM cru (`S16_LE`, 16 kHz, mono). Cada pedido ganha um WAV na hora, porque o cabeçalho de um WAV ainda aberto não fecha. Antes de transcrever, o app corta o silêncio do começo e do fim pelo RMS de janelas de 20 ms. Se não houver fala, o clipe inteiro segue.

## Servidor residente

O `whisper-server` sobe com o app, em Vulkan, e fica com o modelo na memória. A porta local é **8178** (`127.0.0.1` só). O gamescope deixa `ENABLE_GAMESCOPE_WSI=1` no ambiente; essa layer aborta o Whisper (`vkroots.h`, saída 134). O servidor e o `whisper-cli` nascem com `ENABLE_GAMESCOPE_WSI=0`. Cada clipe manda o seu `-ac` no campo `audio_ctx`: `duração_ms / 20 + 64`, preso entre 256 e 1500. Junto vão `-t 4`, `-bs 1` e `-bo 1`.

Se o servidor não responder, o mesmo clipe vai para o `whisper-cli` com `-l pt -nt -t 4 -bs 1 -bo 1 -ac N`.

O log de cada pedido, no stderr (e portanto no `kiosk.log`):

```text
zappe: voz: parcial 1400 ms ac=314 "aumenta o volume"
zappe: voz: final 1500 ms ac=314 "aumenta o volume"
```

## Onde ficam os arquivos

`~/.local/share/zappe/whisper/` (ou `ZAPPE_WHISPER_DIR`):

| Caminho | Conteúdo |
| --- | --- |
| `bin/whisper-server` | servidor Vulkan |
| `bin/whisper-cli` | reserva |
| `models/ggml-small-q8_0.bin` | modelo |

Se o q8 não estiver lá, o app ainda aceita `ggml-small.bin`, `ggml-base.bin`, `ggml-tiny.bin` e as variantes `q5_1`. `ZAPPE_WHISPER_MODEL` aponta um arquivo explícito.

Instalação (baixa o q8, compila o whisper.cpp com `GGML_VULKAN` e pede `vulkan-intel` e `spirv-headers`):

```bash
packaging/appliance/install-whisper.sh
```

O `zappe-update` chama esse script depois do build do app. Se o pacote ou a rede falhar, o app atualiza mesmo assim. `ZAPPE_WHISPER_REBUILD=1` força outra compilação.

`ZAPPE_WHISPER_SERVER=off` não sobe o servidor. `ZAPPE_WHISPER_URL` usa um servidor que já está no ar.

## Frases

A gramática é curta, em `src/voice.cpp`. Uma palavra perto de um comando conhecido é trocada quando a distância é única: até 1 em palavras de 4 letras, até 2 nas maiores. Palavras de até 3 letras (`mudo`, `sbt`, `max`) não mudam. `Almento` vira `aumenta`. `dark` numa busca continua `dark`.

| Fala | Efeito |
| --- | --- |
| abrir Netflix, YouTube, Prime, HBO Max, Apple TV, Disney+, Globoplay | abre o streaming |
| voltar | sai da reprodução ou volta no guia |
| volume mais / aumenta o volume | sobe o volume |
| volume menos | desce o volume |
| mudo | mudo |
| ir para Globo, Record, SBT, Band, Cultura | TV aberta |
| play / pausar | play/pause |
| buscar dark na netflix | busca no streaming |

`ZAPPE_VOICE_FAKE="abrir netflix"` devolve esse texto ao soltar o botão, sem microfone. `ZAPPE_VOICE_SECONDS` é o teto da gravação (2–8, padrão 8); ao estourar, a passagem final roda sozinha. `ZAPPE_ALSA_DEVICE` escolhe o `arecord -D`. `ZAPPE_WHISPER_BIN` aponta o `whisper-cli`. `ZAPPE_WHISPER_PORT` muda a porta (padrão 8178).
