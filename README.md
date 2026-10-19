# Zappe

Guia de sala em **Qt 6 Quick**. Roda num gamescope só, em 1080p, no mini-PC. O Google Chrome abre Netflix, YouTube, Prime Video, HBO Max, Apple TV, Disney+ e Globoplay no mesmo gamescope, controlado por CDP. A TV aberta continua em `dvbv5-zap | mpv`.

O nome padrão é **Zappe**. Marca, logo e splash vêm de um arquivo na máquina, nunca do git.

## Arquitetura

```text
zappe-kiosk.service   (systemd de sistema, tty1, seat0)
  └─ gamescope 1920×1080
       └─ zappe          guia Qt Quick
            ├─ Xvfb :99
            │    └─ google-chrome   catálogo (não entra no gamescope)
            └─ no Play: google-chrome --kiosk neste gamescope
                 perfil ~/.local/share/zappe/chrome-profile
```

Não há segundo gamescope. O gamescope ignora posição de janela e mostra o que tem foco, então o Chrome do catálogo não pode nascer nesse display. O app abre um Xvfb (`:99`, ou `ZAPPE_CATALOG_DISPLAY`) e aponta só esse Chrome para lá, com user-agent normal. No Play o app fecha essa instância — o perfil não aceita duas ao mesmo tempo — e sobe o Chrome `--kiosk` no display do gamescope. Ao sair do vídeo, o catálogo volta para o Xvfb. Se esse Chrome ou o Xvfb caem, o app relança os dois com espera (1s, dobrando até 30s) e registra o código ou o sinal da saída.

| Peça | Função |
| --- | --- |
| Guia | Qt Quick. Fileiras, ficha, menu, ajustes. |
| Chrome | Um perfil, porta CDP 9222. Catálogo num Xvfb; kiosk no gamescope só no Play. |
| Roteiros | `data/logins.json`. URL, seletores e passos que pedem e-mail, senha, código ou perfil. |
| Pedido de dado | Overlay na tela e, se configurado, POST num webhook. Sem URL, o webhook fica desligado. |
| TV aberta | `channels.conf`, HD primeiro. 1-seg só fica se não houver fullseg. |
| Controle | evdev do ATONGX. `event4` (air mouse) com grab exclusivo. |
| Samsung | Power, volume, mudo, HDMI e estado da UN43TU7020. |
| Voz | Whisper pt-BR no Vulkan, modelo small q8, servidor residente. |
| Áudio | PipeWire `output:hdmi-stereo` no card `alsa_card.pci-0000_00_1f.3`. |
| Update | `zappe-update` recompila com CMake e reinicia a unit de sistema `zappe-kiosk.service`. |

## O que saiu

A UI Tauri/React, o companion (QR / device code, `zappe-companion.service`) e o harvest da Netflix (AT-SPI, skills, `zappe-harvest`) não fazem mais parte do app. O espelho HDMI por `gpu-screen-recorder` em clipes de 2s também não. Captura contínua fica como TODO via PipeWire, no `zappe-kiosk.sh`.

## Dependências (Arch)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  qt6-base qt6-declarative qt6-websockets qt6-wayland qt6-5compat qt6-imageformats \
  mesa gamescope \
  xorg-server-xvfb \
  google-chrome \
  pipewire pipewire-pulse \
  alsa-utils \
  vulkan-intel spirv-headers vulkan-headers glslang ffmpeg
```

Opcionais:

- `ttf-inter` — a interface usa Inter quando a família existe.
- `dvbv5-tools` e `mpv` — TV aberta.
- `socat` — pause do mpv pelo socket.
- Whisper — `packaging/appliance/install-whisper.sh` (Vulkan, `ggml-small-q8_0.bin`). O script tira o drop-in que fixava `ggml-small.bin`.

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

Na unit de sistema, `%h` vira `/root` e `%U` vira `0`. O `ExecStart` não existe e o systemd fica em 203/EXEC. O arquivo do repo usa o placeholder `YOUR_USER` e `Environment=HOME` / `Environment=USER` com caminho fixo. O `sed` abaixo troca o placeholder pelo usuário da TV antes de habilitar.

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

Fora do Play o Chrome é uma janela real num Xvfb, não no gamescope. `--headless=new` anuncia `HeadlessChrome` e a Netflix e o YouTube não montam o catálogo. Posição `-3000,-3000` também não serve: o gamescope ignora a posição, dá foco à janela e ela cobre o guia, com abas e o balão “Restore pages?”. O user-agent é o de um Chrome normal, com `--ozone-platform=x11`, `--disable-gpu` (não disputa o DRM com o gamescope), `--disable-blink-features=AutomationControlled` e `--hide-crash-restore-bubble`. `--ozone-override-screen-size=1920,1080` acompanha `--window-size=1920,1080`.

```bash
Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp -ac
DISPLAY=:99 WAYLAND_DISPLAY= \
google-chrome-stable \
  --user-data-dir="$HOME/.local/share/zappe/chrome-profile" \
  --remote-debugging-port=9222 \
  --remote-allow-origins=* \
  --user-agent="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/154.0.0.0 Safari/537.36" \
  --disable-blink-features=AutomationControlled \
  --ozone-platform=x11 \
  --disable-gpu \
  --hide-crash-restore-bubble \
  --ozone-override-screen-size=1920,1080 \
  --window-size=1920,1080 \
  --mute-audio \
  about:blank
```

No Play o app encerra esse processo e sobe outro com `--kiosk`, no `DISPLAY` / `WAYLAND_DISPLAY` do gamescope, no mesmo perfil. São sempre uma instância de cada vez.

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

Voltar nunca encerra o app. `KEY_BACK` (evdev 158, `XF86Back`), Escape e Del voltam um nível: na home não fazem nada, numa tela interna fecham essa tela, no catálogo de um streaming voltam para a lista de apps. No canal ao vivo, Voltar fecha o mpv e devolve o foco ao card que estava no ar. Home (`KEY_HOMEPAGE` 172 e `KEY_HOME` 102) volta para a grade e deixa o foco no primeiro card, também no meio de uma fileira ou saindo do canal. No player do espelho, os dois fecham o Chrome e o guia volta para a frente, mesmo com o Chrome em foco. Para injetar o Voltar com xdotool, mande `XF86Back` para a janela do app. Ver [`docs/atongx-input.md`](docs/atongx-input.md).

## Controle

Mapa em `data/atongx-map.json`. O app grava o mesmo contrato.

| Tecla | Código | Ação |
| --- | --- | --- |
| Power | 116 | liga/desliga a Samsung |
| Play / Pause | 164 | play/pause |
| Setas | 103, 108, 105, 106 | foco |
| OK | 28 | ativar |
| Voltar | 158 | um passo; no canal, o card que estava no ar |
| Home | 172, 102 | grade, foco no primeiro card |
| Del / Escape | 14, 1 | voltar um nível |
| Menu | 127 | menu |
| Page Up / Down | 104, 109 | troca de fileira |
| Volume | 115, 114 | Samsung |
| Mute | 113 | Samsung |
| Mic | 582 | Whisper |
| Números | 2–11, 71–83 | sintonia (7.1, 13). O ponto é 52 ou 83 |

O nó `event4` do air mouse recebe `EVIOCGRAB`. O cursor desse nó é descartado. O nó da tecla Power também: o logind não desliga o mini-PC. Detalhe em [`docs/atongx-input.md`](docs/atongx-input.md) e [`docs/samsung-tv.md`](docs/samsung-tv.md).

IP, token e MAC da TV ficam em `~/.local/share/zappe/samsung.json`. O exemplo sem segredo está em `packaging/appliance/examples/samsung.json`.

Em Ajustes → TV, **Testar TV** abre o diagnóstico: ligação `ws` ou `wss`, token presente ou não, último erro, volume, mudo e força. Cada tecla do controle vira uma linha (tecla recebida, ação, comando na Samsung, resposta e latência) na tela e no `kiosk.log` — uma linha por tecla. Reparar apaga o token e pede Allow de novo. Voltar sai só dessa tela. O valor do token não entra no log. Com token gravado, um tempo esgotado não pede Allow; a espera da tecla é de 5 segundos.

O logo da rede, quando o nome ou o virtual casa, é um arquivo em `~/.local/share/zappe/logos/{id}.png`. Se não houver arquivo, o app gera o wordmark e guarda ali. Globo, SBT, Record, Band, Cultura, RedeTV, TV Brasil, Gazeta, NGT e Novo Tempo entram nessa lista.

## TV aberta

Procura `~/tv/channels.conf`, `~/.config/zappe/channels.conf` e `~/.local/share/zappe/channels.conf`. `ZAPPE_OTA_CHANNELS` substitui a lista (caminhos separados por `:`). Um serviço 1-seg (o nome ou `ISDBT_LAYER_ENABLED = 1`, só a camada A) sai quando o mesmo canal tem fullseg. Se a Band só veio assim, ela permanece na fileira e o log diz que o fullseg não foi gravado. Sem nenhuma seção da Band, o log diz uma vez que a varredura não gravou a Band: o virtual é 13.1 e o físico em São Paulo continua o 23 (527 MHz), não o 13. Digitar 13 oferece OK para um `dvbv5-scan` só nessa frequência; Voltar cancela. Se o 23 não devolve serviço, o próximo OK varre a faixa de UHF 14 a 51. Os serviços novos entram no `channels.conf` sem repetir o nome. `Globo`, `Record` e `SBT` caem na variante HD quando ela existe. A fileira Ao vivo do catálogo é trocada por esses canais quando o arquivo existe.

O número virtual vem de `LOGICAL_CHANNEL_NUMBER`, `VCHANNEL` ou `LCN`, senão de um número no nome. Sem número no arquivo, os majors habituais entram: Band 13.1, Record 7.1, Globo 5.1, SBT 4.1, Cultura 2.1, RedeTV 9.1. Digitar no controle (13, ou 7.1) mostra o número na tela e sintoniza depois de pouco mais de um segundo. Voltar apaga o número antes de sair da tela.

Com o canal em foco, a capa atualiza na hora. Se esse canal já está no ar, é um `screenshot-to-file` e o vídeo não para. Se outro canal está no ar, o sintonizador não é trocado. Se está livre, um `dvbv5-zap` curto grava um TS, o `ffmpeg` tira um frame e o guia continua na frente. O mesmo TS entrega o EPG. A gravação usa `dvbv5-zap -P`, então entram o SDT (PID 0x11), o EIT (0x12) e, no ISDB, 0x26 e 0x27, não só áudio e vídeo. EIT presente/seguinte (e a grade, se o presente não vier) e o nome no SDT. O log `ota: epg` sai sempre, com os PIDs e as tabelas lidas, mesmo quando o título vem vazio. O guia e a tela de sintonia mostram "Agora" e "A seguir".

Sem frame, a capa é o logo da rede. O app procura `~/.local/share/zappe/logos/{id}.png` (ou `.jpg` / `.webp`, e a pasta `ota-logos`) e, se o arquivo existe, desenha ele inteiro na cor da rede. Sem arquivo, gera o wordmark, grava em `logos/{id}.png` e usa esse arquivo — o log deixa de dizer `arquivo=nenhum`. Globo, SBT, Record, Band, Cultura, RedeTV, TV Brasil, Gazeta, NGT e Novo Tempo têm marca. O virtual 5.1, 4.1, 7.1, 13.1, 2.1, 9.1, 11.1 e 3.1 escolhe a marca quando o nome não cita a rede. O granulado não cobre o logo.

OK num canal ao vivo sintoniza na hora: o card aperta e um overlay mostra o nome, a arte, um spinner e uma barra de até 10 segundos. A imagem do mpv esconde o overlay. Se falhar ou estourar o tempo, o overlay explica e oferece OK para tentar de novo e Voltar para fechar.

Sem um frame gravado, o card é o monograma da rede ou, fora dessa lista, o nome do canal num degradê, não o desenho genérico das outras capas. Enquanto o mpv já está no ar, o app pede um `screenshot-to-file` pelo socket IPC (8 segundos depois da imagem, e de novo a cada 3 minutos). O JPEG fica em `~/.local/share/zappe/ota-frames/` e vale por 12 horas. Isso não abre outro sintonizador e não pausa o vídeo. Quando o card usa esse frame, o granulado e as manchas de cor saem de cima da foto. Na próxima vez que o guia mostra o canal, a capa é esse frame.

Voltar no canal fecha o mpv e devolve o guia no card que estava no ar, mesmo com o mpv em foco. O app trata Back (evdev 158) e Escape (evdev 1) como sair do canal. Enquanto o canal está no ar, o app também captura no X o keycode 166 (`XF86Back`), então `xdotool key 166` fecha o mpv mesmo com ele em foco. O `input.conf` do mpv usa `BACK` e `GO_BACK` (o nome que o mpv dá a essa tecla; `XF86Back` ele ignora) e o keycode `0x21FF26`. Quando o processo termina, o guia volta para a frente.

O Chrome não é a interface de navegar. Ele fica atrás do guia, com o perfil já logado, e um adaptador por streaming (`data/mirrors.json`) lê as respostas JSON do serviço; o DOM só entra se a rede não trouxer títulos. O Qt desenha fileiras, capas, continuar assistindo, perfis e busca. A capa prefere a imagem que o Chrome já baixou e, em seguida, um GET direto do processo do app (user-agent de Chrome, `Accept` com JPEG na frente e `Referer` da home do serviço). URL com `{width}`, `{w}x{h}{c}.{f}` ou `%7Bwidth%7D` recebe um tamanho real antes do GET. WebP e AVIF viram JPEG pelo plugin do Qt ou pelo `ffmpeg` (no GET direto o AVIF passa por um arquivo temporário, porque o pipe não sonda o formato). O arquivo `.jpg` vira `file://` no card. O card visível ainda pode virar um recorte. O log é uma linha de artes e uma linha `capas N/M direto, cards K` com o primeiro `file://`. As fileiras de cada serviço são relidas ao abrir e a cada 45 minutos (`ZAPPE_MIRROR_REFRESH_MIN`, entre 30 e 60). Capa com path novo é baixada; a query assinada reusa o JPEG. Arquivo `.img` antigo e JPEG sem uso há mais de 14 dias saem do cache. A ficha de um título do espelho mostra o nome, a capa e a sinopse quando o DOM tem uma; não inventa episódios nem avisa que o título está fora do catálogo local. Abrir um streaming não mostra as fileiras do catálogo local. Foco, scroll e troca de tela usam as durações de `qml/Motion.qml` (OutCubic, sem spring). Só o Play abre a URL de reprodução e traz o Chrome para a frente. Se o adaptador quebra e existe `~/.local/share/zappe/llm.json` com `url`, o app pede um reparo, valida nas amostras capturadas e versiona o resultado em `adapters/`. Sem essa URL, nada chama IA. Detalhe em [`docs/streaming-mirror.md`](docs/streaming-mirror.md).

Uma fileira antiga do harvest com `status` `error`, `teach` ou `harvesting`, ou com texto de AT-SPI (`no Chrome/Chromium application on the AT-SPI bus`), sai do catálogo e não aparece no guia. `Continue watching` com títulos de verdade continua.

## Voz

Segurar o mic 582 grava com `arecord` e transcreve no `whisper-server` (Vulkan, modelo carregado). Sem o servidor, cai no `whisper-cli` com os mesmos flags. O `-ac` segue o tamanho do clipe. Silêncio nas pontas sai antes da transcrição, e uma palavra próxima de um comando conhecido é corrigida (`Almento` vira `aumenta`). `ZAPPE_VOICE_FAKE="abrir netflix"` pula o microfone e entrega o texto ao soltar. Ver [`docs/voice.md`](docs/voice.md).

## Marca

`~/.local/share/zappe/branding.json`, ou `$ZAPPE_DATA_DIR`. Sem arquivo, nome **Zappe**, acento `#E85A1B`, fundo `#000000`. Exemplo em `packaging/appliance/examples/branding.json`. Não commitar logo.

## Update

`zappe-update.timer` olha `origin/main` a cada 20 minutos. Árvore suja não é atualizada. O build é `cmake --build`. O binário vai para `~/bin/zappe`. Em seguida roda `install-whisper.sh`; se o Vulkan ou o download falhar, o app atualiza mesmo assim. Se existir a unit de sistema `zappe-kiosk.service`, o restart é `sudo -n systemctl restart zappe-kiosk.service`. Sem ela, reinicia a unit de usuário. O perfil do Chrome não é tocado.

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
| `ZAPPE_MIRROR_DISABLE` | não abre o Chrome em segundo plano |
| `ZAPPE_LLM_URL` | endpoint de chat completions; vazio desliga o aprendizado |
| `ZAPPE_LLM_KEY` | chave só no cabeçalho; não vai para o git |
| `ZAPPE_LLM_MODEL` | modelo do endpoint (padrão `gpt-4o-mini`) |
| `ZAPPE_LLM_FALLBACK_MODEL` | segunda tentativa se o pedido ou o adaptador falha |
| `ZAPPE_LLM_SESSION_HEADER` | nome do cabeçalho de sessão (`x-opencode-session`); o valor é `zappe-<serviço>-<yyyymmdd>` |
| `ZAPPE_LLM_USER_AGENT` | User-Agent; vazio manda `zappe/<versão>` |
| `ZAPPE_LOGIN_WEBHOOK` | URL do webhook de login |
| `ZAPPE_HDMI_CARD` / `ZAPPE_HDMI_PROFILE` | perfil PipeWire |
| `ZAPPE_AUDIO_DISABLE` | não chama `pactl` |
| `ZAPPE_HID_DISABLE` | não lê evdev |
| `ZAPPE_OTA_CHANNELS` | lista de `channels.conf` |
| `ZAPPE_SAMSUNG_HOST` | IP da TV (padrão `192.168.3.6`) |
| `ZAPPE_SAMSUNG_DISABLE` | não fala com a TV |
| `ZAPPE_WHISPER_BIN` | só o whisper-cli de reserva; não troca o servidor |
| `ZAPPE_WHISPER_MODEL` | arquivo do modelo; `ggml-small.bin` cede ao q8 |
| `ZAPPE_WHISPER_SERVER` | binário do servidor, ou `off` para só o CLI |
| `ZAPPE_WHISPER_PORT` | porta local do servidor (padrão 8178) |
| `ZAPPE_WHISPER_URL` | URL de um whisper-server que já está no ar |
| `ZAPPE_WHISPER_DIR` | pasta com `bin/` e `models/` |
| `ZAPPE_VOICE_FAKE` | transcrição falsa, entregue ao soltar o botão |
| `ZAPPE_VOICE_SECONDS` | teto do botão segurado (2–8, padrão 8) |

## Captura de tela

Não há espelho HDMI. O comentário no `zappe-kiosk.sh` marca o TODO: captura contínua por PipeWire, no lugar dos clipes de 2s.
