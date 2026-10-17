# Zappe — protótipo Qt Quick

Guia de sala em **Qt 6 Quick / QML**, no visual de uma tela inicial de TV: fundo desfocado que acompanha o item focado, fileiras horizontais de capas, card em foco com escala, sombra, brilho e um tilt leve, rolagem com mola e relógio no canto. A fileira **Ao vivo** desenha uma prévia (gradiente e ruído), o **Menu** abre a barra lateral, e Enter abre a ficha do título.

Este diretório é um protótipo. O app Tauri continua sendo o guia do produto. Nada aqui altera `src/` nem `src-tauri/`.

Alvo: mini-PC **Intel N150** (iGPU Alder Lake-N), Arch Linux, 1080p, **Wayland/EGL** ou **`-platform eglfs`** direto na DRM/KMS.

## Dependências (Arch)

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf \
  qt6-base qt6-declarative qt6-wayland qt6-5compat \
  mesa
```

Opcional:

- `ttf-inter` — a interface usa Inter quando a família está instalada; sem ela, cai para uma sans do sistema.
- `qt6-imageformats` — só se alguma capa em `artwork` for WebP.

`qt6-base` no Arch inclui o plugin **eglfs** (DRM/KMS). `qt6-wayland` é o plugin Wayland, que usa EGL. `qt6-5compat` fornece `Qt5Compat.GraphicalEffects` (cantos arredondados das capas).

Para o modo eglfs, o usuário que abre a DRM precisa estar nos grupos `video`, `input` e `render`:

```bash
sudo usermod -aG video,input,render "$USER"
```

## Build

```bash
cmake -S qt-guide -B qt-guide/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build qt-guide/build
ctest --test-dir qt-guide/build --output-on-failure
```

O teste `--self-check` confere o JSON de exemplo, o fallback de branding para **Zappe** e a geração de uma capa.

## Rodar

```bash
./qt-guide/build/zappe-qt-guide
```

A janela abre em tela cheia. Num desktop, para uma janela 1920×1080:

```bash
./qt-guide/build/zappe-qt-guide --windowed
```

`ZAPPE_WINDOWED=1` faz o mesmo. O cursor some de propósito (sala). `ZAPPE_CURSOR=1` o mostra.

### Wayland (Hyprland e outros)

```bash
QT_QPA_PLATFORM=wayland ./qt-guide/build/zappe-qt-guide
```

O scene graph fica em **OpenGL**. No Wayland isso passa por EGL, que é o caminho da iGPU do N150.

### DRM / KMS, sem compositor (`eglfs`)

Num TTY em que nenhum compositor esteja com a DRM master (troque de TTY ou pare o display manager):

```bash
QT_QPA_PLATFORM=eglfs \
QT_QPA_EGLFS_INTEGRATION=eglfs_kms \
QT_QPA_EGLFS_ALWAYS_SET_MODE=1 \
  ./qt-guide/build/zappe-qt-guide -platform eglfs
```

O binário também liga `QT_QPA_EGLFS_HIDECURSOR` quando detecta eglfs. Não há decoração de janela: a superfície cobre o modo do conector.

Se a tela ficar preta, confira `dmesg` / o grupo `video` e se outro processo ainda segura `/dev/dri/card0`.

### Contador de FPS

```bash
ZAPPE_FPS=1 ./qt-guide/build/zappe-qt-guide
```

Qualquer valor diferente de vazio, `0`, `false` ou `off` liga o contador. Ele mede frames apresentados (`frameSwapped`), no canto inferior esquerdo. `ZAPPE_FPS` ausente deixa o contador desligado.

`ZAPPE_SOFTWARE=1` força o backend de software do Qt Quick. Serve para um grab sem GPU. No N150 deixe isso desligado.

## Teclas

Só D-pad, no mesmo mapa do controle da sala. A tecla Menu é `KEY_COMPOSE` (`Qt.Key_Multi_key`) ou `KEY_MENU` (`Qt.Key_Menu`). Home do controle (`Qt.Key_Home` / `Qt.Key_HomePage`) faz o mesmo que Back.

| Tecla | Ação |
| --- | --- |
| Setas | Move o foco entre capas, fileiras, botões da ficha, episódios, itens do menu ou linhas de Ajustes. Para na ponta. |
| Enter / Return / Select | Abre a ficha, dispara o botão focado, ou fecha o menu na seção atual. |
| Esc / Back / Backspace / Home | Fecha o menu, depois a ficha, depois volta para Início. Na home não faz nada. |
| Menu (`KEY_COMPOSE` ou `KEY_MENU`) | Abre ou fecha o menu lateral (Início, Ao vivo, Streaming, Ajustes). |
| Volume Up / Down / Mute | Mostra o HUD de volume. Não altera áudio de verdade. |

Esc na home não encerra o processo. Feche a janela pelo compositor.

No menu, cima e baixo trocam a seção na hora (o fundo acompanha). Esquerda, direita ou Enter fecham o menu nessa seção.

Na ficha, esquerda e direita movem entre **Assistir**, **Trailer** e **Minha lista**. Baixo entra na fileira de episódios. Enter mostra um toast — não há reprodução. **Minha lista** alterna um botão com mola e vale só nesta sessão.

Em Ajustes, esquerda e direita (ou Enter) trocam **1080p / 4K** ou ligam a marca do aparelho. A resolução é só desenho: a janela não muda de modo.

## Catálogo

O formato é o de `~/.local/share/zappe/catalog.json` (`src-tauri/src/catalog.rs`): `shelves[]` com `id`, `skill_id`, `title`, `status`, `message`, `rows[]`. Cada row tem `title`, `service`, `href`, `artwork`, `skill_id`.

Ordem de leitura:

1. `--catalog ARQUIVO`
2. `ZAPPE_CATALOG`
3. `$ZAPPE_DATA_DIR/catalog.json`, ou `~/.local/share/zappe/catalog.json` se o arquivo existir
4. `catalog.json` ao lado do binário
5. o exemplo embutido em `qt-guide/data/catalog.json`

O exemplo traz quatro fileiras (Ao vivo, Continue watching, Library, Set aside) com títulos originais. Capas sem `artwork` são geradas na hora: gradientes e formas, sem logotipo de terceiros. Um `artwork` preenchido pode ser caminho absoluto, `file://` ou relativo ao JSON / ao data dir (png, jpg, webp, gif).

Fileiras `continue`, `live`, `ao-vivo`, `apps` e `canais` usam card 16:9. As demais usam capa retrato. A fileira `live` / `ao-vivo` / `aovivo` marca os cards como canal: a prévia é um ruído em tile mais duas manchas de cor, desenhados por cima da capa, com o selo **AO VIVO** pulsando. Não há decodificador de vídeo. O ruído se move no canal focado e nos dois vizinhos; os outros cards ficam numa textura parada. A prévia pausa com a ficha, o menu ou Ajustes abertos.

O menu lateral não captura a cena. O fundo que aparece atrás dele já é a placa desfocada da capa, escurecida, enquanto o conteúdo entra à direita. O card focado fica de frente; os vizinhos da mesma fileira inclinam alguns graus em Y, e a mola zera o ângulo quando o card encosta na margem.

## Branding

Lido em runtime, no mesmo formato de `packaging/appliance/examples/branding.json` e `src-tauri/src/branding.rs`:

```text
${ZAPPE_DATA_DIR:-~/.local/share/zappe}/branding.json
```

Nome, `accent` (também `accentColor` / `accent_color`), `logo`, `splash`, `tagline`, `theme.background`, `theme.focusRing` (`subtle-scale` usa escala 1.08; outro token usa 1.12) e os campos de `idle`. Logo e splash são caminhos relativos a esse diretório, ou absolutos. O logo do canto vira um recorte circular.

Se o arquivo não existir, estiver inválido ou o nome vier vazio, o nome é **Zappe**, o acento `#E85A1B` e o fundo `#000000`. Nenhum nome de marca pessoal entra no repositório: o arquivo de verdade fica só na máquina.

## O que este protótipo não faz

Não reproduz vídeo, não faz harvest e não fala com o nest. Assistir, Trailer e os episódios só mostram um toast. O volume não mexe no mixer. 1080p/4K não troca o modo DRM. O desfoque do fundo é uma placa pré-calculada (a capa reduzida e borrada uma vez), para a iGPU não pagar um blur por frame. Sombra, brilho e o ruído da prévia também são texturas únicas.
