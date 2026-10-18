# Zappe

Guia de sala em **Qt 6 Quick**. Roda num gamescope só, em 1080p, no mini-PC. O Google Chrome abre Netflix, YouTube, Prime Video, HBO Max, Apple TV, Disney+ e Globoplay no mesmo gamescope, controlado por CDP. A TV aberta continua em `dvbv5-zap | mpv`.

O nome padrão é **Zappe**. Marca, logo e splash vêm de um arquivo na máquina, nunca do git.

## Arquitetura

```text
zappe-kiosk.service   (systemd de sistema, tty1, seat0)
  └─ gamescope 1920×1080
       ├─ zappe          guia Qt Quick
       └─ google-chrome  --kiosk --remote-debugging-port=9222
            perfil ~/.local/share/zappe/chrome-profile
```

Não há segundo gamescope. O Chrome é filho do app e herda o `WAYLAND_DISPLAY` / `DISPLAY` do gamescope.

| Peça | Função |
| --- | --- |
| Guia | Qt Quick. Fileiras, ficha, menu, ajustes. |
| Chrome | Um perfil, kiosk, porta CDP 9222. |
| Roteiros | `data/logins.json`. URL, seletores e passos que pedem e-mail, senha, código ou perfil. |
| Pedido de dado | Overlay na tela e, se configurado, POST num webhook. Sem URL, o webhook fica desligado. |
| TV aberta | `channels.conf`, HD primeiro, 1Seg fora. |
| Controle | evdev do ATONGX. `event4` (air mouse) com grab exclusivo. |
| Samsung | Power, volume, mudo, HDMI e estado da UN43TU7020. |
| Voz | Whisper pt-BR, modelo base e, se não houver, tiny. |
| Áudio | PipeWire `output:hdmi-stereo` no card `alsa_card.pci-0000_00_1f.3`. |
| Update | `zappe-update` recompila com CMake e reinicia a unit de sistema `zappe-kiosk.service`. |

## O que saiu

A UI Tauri/React, o companion (QR / device code, `zappe-companion.service`) e o harvest da Netflix (AT-SPI, skills, `zappe-harvest`) não fazem mais parte do app. O espelho HDMI por `gpu-screen-recorder` em clipes de 2s também não. Captura contínua fica como TODO via PipeWire, no `zappe-kiosk.sh`.

## Dependências (Arch)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  qt6-base qt6-declarative qt6-websockets qt6-wayland qt6-5compat \
  mesa gamescope \
  google-chrome \
  pipewire pipewire-pulse \
  alsa-utils
```

Opcionais:

- `ttf-inter` — a interface usa Inter quando a família existe.
- `dvbv5-tools` e `mpv` — TV aberta.
- `socat` — pause do mpv pelo socket.
- Whisper — `packaging/appliance/install-whisper.sh` (modelo `ggml-base.bin`; para o menor, aponte `ZAPPE_WHISPER_MODEL` em `ggml-tiny.bin`).

O usuário do kiosk precisa dos grupos `video`, `input` e `render`.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

O binário é `build/zappe`. `--self-check` confere o catálogo de exemplo, a marca **Zappe**, o mapa do controle, os sete roteiros, o filtro de TV aberta, a voz, o perfil HDMI e os argumentos do Chrome.

## Rodar

```bash
./build/zappe
```

Janela 1920×1080, sem fullscreen:

```bash
ZAPPE_AUDIO_DISABLE=1 ./build/zappe --windowed
```

`ZAPPE_AUDIO_DISABLE=1` evita trocar o perfil de áudio da máquina de desenvolvimento. No aparelho o padrão é forçar o HDMI.

`ZAPPE_FPS=1` mostra o contador. `ZAPPE_SOFTWARE=1` usa o backend de software do Qt Quick.

## Deploy no aparelho

O gamescope sobe numa **unit de sistema**, no tty1, dentro da sessão do usuário no seat0. Sem essa sessão o PipeWire do usuário não aparece e o HDMI fica mudo. O `getty@tty1` fica mascarado.

```bash
sudo usermod -aG video,input,render "$USER"
loginctl enable-linger "$USER"

install -D -m 755 packaging/appliance/zappe-kiosk.sh "$HOME/bin/zappe-kiosk.sh"
# ou um wrapper: exec "$HOME/src/zappe/packaging/appliance/zappe-kiosk.sh"

sudo cp packaging/appliance/zappe-kiosk.service /etc/systemd/system/zappe-kiosk.service
sudo sed -i "s/YOUR_USER/$USER/g" /etc/systemd/system/zappe-kiosk.service
sudo systemctl daemon-reload
sudo systemctl mask getty@tty1.service
sudo systemctl enable --now zappe-kiosk.service
```

O updater reinicia com `sudo -n systemctl restart zappe-kiosk.service` quando essa unit existe. Sem senha:

```bash
sudo tee /etc/sudoers.d/zappe >/dev/null <<EOF
$USER ALL=(root) NOPASSWD: /usr/bin/systemctl restart zappe-kiosk.service, /usr/bin/systemctl daemon-reload
EOF
sudo chmod 440 /etc/sudoers.d/zappe
```

O timer de update continua na sessão do usuário:

```bash
mkdir -p ~/.config/systemd/user
cp packaging/appliance/zappe-update.service \
   packaging/appliance/zappe-update.timer \
   ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now zappe-update.timer
```

Se a unit de sistema não existir, `zappe-tty1.sh` (autologin do tty1) ainda sobe a unit de usuário `zappe-kiosk.user.service`. O padrão do repo é a unit de sistema.

O script do kiosk lê `~/.local/share/zappe/display.json`: sem arquivo, ou com `1080p`, o gamescope fica em 1920×1080. `"resolution": "4K"` sobe em 3840×2160 no próximo início. Se o `zappe` sair, o script abre de novo; se o script sair, `Restart=always` sobe a unit outra vez.

Áudio, antes do gamescope e de novo no app:

```bash
pactl set-card-profile alsa_card.pci-0000_00_1f.3 output:hdmi-stereo
```

## Chrome e roteiros de login

O app lança:

```bash
google-chrome-stable \
  --user-data-dir="$HOME/.local/share/zappe/chrome-profile" \
  --remote-debugging-port=9222 \
  --remote-allow-origins=* \
  --kiosk \
  about:blank
```

`data/logins.json` descreve cada streaming: `homeUrl`, `readySelector` e passos `navigate`, `click` e `ask`. Um `ask` pede `email`, `password`, `code` ou `profile`.

O pedido aparece na tela. Se existir webhook, o app também faz POST:

```json
{"requestId":"…","service":"netflix","field":"email","prompt":"…","secret":false,"source":"zappe"}
```

Configuração, fora do git:

```bash
# ~/.local/share/zappe/login-webhook.json
{ "url": "https://example.invalid/zappe-login", "token": "", "inbox": "login-inbox.json" }
```

`ZAPPE_LOGIN_WEBHOOK` e `ZAPPE_LOGIN_WEBHOOK_TOKEN` cobrem o arquivo. URL vazia deixa o webhook desligado; a tela e o arquivo `login-inbox.json` (`{"requestId","value"}`) continuam valendo. Os seletores são ponto de partida e podem falhar no site real.

Voltar nunca encerra o app. Na home não faz nada. Numa tela interna, volta um nível. Com o Chrome aberto, volta uma página; sem histórico, volta para o guia. Na TV aberta, volta para o guia.

## Controle

Mapa em `data/atongx-map.json`. O app grava o mesmo contrato.

| Tecla | Código | Ação |
| --- | --- | --- |
| Power | 116 | liga/desliga a Samsung |
| Play / Pause | 164 | play/pause |
| Setas | 103, 108, 105, 106 | foco |
| OK | 28 | ativar |
| Home | 158 | voltar (não fecha o app) |
| Menu | 127 | menu |
| Page Up / Down | 104, 109 | troca de fileira |
| Volume | 115, 114 | Samsung |
| Mute | 113 | Samsung |
| Del | 14 | voltar |
| Mic | 582 | Whisper |

O nó `event4` do air mouse recebe `EVIOCGRAB`. O cursor desse nó é descartado. O nó da tecla Power também: o logind não desliga o mini-PC. Detalhe em [`docs/atongx-input.md`](docs/atongx-input.md) e [`docs/samsung-tv.md`](docs/samsung-tv.md).

IP, token e MAC da TV ficam em `~/.local/share/zappe/samsung.json`. O exemplo sem segredo está em `packaging/appliance/examples/samsung.json`.

## TV aberta

Procura `~/tv/channels.conf`, `~/.config/zappe/channels.conf` e `~/.local/share/zappe/channels.conf`. `ZAPPE_OTA_CHANNELS` substitui a lista (caminhos separados por `:`). Nomes 1Seg saem. `Globo`, `Record` e `SBT` caem na variante HD quando ela existe. A fileira Ao vivo do catálogo é trocada por esses canais quando o arquivo existe.

OK num canal ao vivo sintoniza na hora: o card aperta e um overlay mostra o nome, a arte, um spinner e uma barra de até 10 segundos. A imagem do mpv esconde o overlay. Se falhar ou estourar o tempo, o overlay explica e oferece OK para tentar de novo e Voltar para fechar. Abrir um streaming no Chrome usa o mesmo overlay.

Uma fileira antiga do harvest com `status` `error`, `teach` ou `harvesting`, ou com texto de AT-SPI (`no Chrome/Chromium application on the AT-SPI bus`), sai do catálogo e não aparece no guia. `Continue watching` com títulos de verdade continua.

## Voz

Mic 582 grava com `arecord` e transcreve com `whisper-cli -l pt`. A ordem do modelo é `ggml-base.bin`, depois `ggml-tiny.bin`. `ZAPPE_WHISPER_MODEL` aponta outro arquivo. `ZAPPE_VOICE_FAKE="abrir netflix"` pula o microfone. Frases: abrir um streaming, voltar, volume, mudo, ir para Globo / Record / SBT. Ver [`docs/voice.md`](docs/voice.md).

## Marca

`~/.local/share/zappe/branding.json`, ou `$ZAPPE_DATA_DIR`. Sem arquivo, nome **Zappe**, acento `#E85A1B`, fundo `#000000`. Exemplo em `packaging/appliance/examples/branding.json`. Não commitar logo.

## Update

`zappe-update.timer` olha `origin/main` a cada 20 minutos. Árvore suja não é atualizada. O build é `cmake --build`. O binário vai para `~/bin/zappe`. Se existir a unit de sistema `zappe-kiosk.service`, o restart é `sudo -n systemctl restart zappe-kiosk.service`. Sem ela, reinicia a unit de usuário. O perfil do Chrome não é tocado.

```bash
systemctl --user start zappe-update.service
journalctl --user -u zappe-update.service -n 50
```

## Variáveis

| Variável | Efeito |
| --- | --- |
| `ZAPPE_DATA_DIR` | raiz de catálogo, marca, perfil do Chrome, whisper |
| `ZAPPE_CATALOG` | JSON do catálogo |
| `CHROME_PATH` | binário do Chrome |
| `ZAPPE_CDP_PORT` | porta CDP (padrão 9222) |
| `ZAPPE_LOGIN_WEBHOOK` | URL do webhook de login |
| `ZAPPE_HDMI_CARD` / `ZAPPE_HDMI_PROFILE` | perfil PipeWire |
| `ZAPPE_AUDIO_DISABLE` | não chama `pactl` |
| `ZAPPE_HID_DISABLE` | não lê evdev |
| `ZAPPE_OTA_CHANNELS` | lista de `channels.conf` |
| `ZAPPE_SAMSUNG_HOST` | IP da TV (padrão `192.168.3.6`) |
| `ZAPPE_SAMSUNG_DISABLE` | não fala com a TV |
| `ZAPPE_WHISPER_BIN` / `ZAPPE_WHISPER_MODEL` | Whisper |
| `ZAPPE_VOICE_FAKE` | transcrição falsa |

## Captura de tela

Não há espelho HDMI. O comentário no `zappe-kiosk.sh` marca o TODO: captura contínua por PipeWire, no lugar dos clipes de 2s.
