# Zappe — protótipo Qt Quick

Guia de sala em **Qt 6 Quick / QML**, no visual de uma tela inicial de TV: fundo desfocado que acompanha o item focado, fileiras horizontais de capas, card em foco com escala, sombra e brilho, rolagem com mola e relógio no canto.

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

Só D-pad:

| Tecla | Ação |
| --- | --- |
| Setas | Move o foco entre capas e fileiras. Para na ponta. |
| Enter / Return / Select | Abre o título focado. |
| Esc / Back / Backspace | Fecha o título e volta para as fileiras. |

Esc na home não encerra o processo. Feche a janela pelo compositor.

## Catálogo

O formato é o de `~/.local/share/zappe/catalog.json` (`src-tauri/src/catalog.rs`): `shelves[]` com `id`, `skill_id`, `title`, `status`, `message`, `rows[]`. Cada row tem `title`, `service`, `href`, `artwork`, `skill_id`.

Ordem de leitura:

1. `--catalog ARQUIVO`
2. `ZAPPE_CATALOG`
3. `$ZAPPE_DATA_DIR/catalog.json`, ou `~/.local/share/zappe/catalog.json` se o arquivo existir
4. `catalog.json` ao lado do binário
5. o exemplo embutido em `qt-guide/data/catalog.json`

O exemplo traz três fileiras (Continue watching, Library, Set aside) com títulos originais. Capas sem `artwork` são geradas na hora: gradientes e formas, sem logotipo de terceiros. Um `artwork` preenchido pode ser caminho absoluto, `file://` ou relativo ao JSON / ao data dir (png, jpg, webp, gif).

Fileiras `continue`, `live`, `apps` e `canais` usam card 16:9. As demais usam capa retrato.

## Branding

Lido em runtime, no mesmo formato de `packaging/appliance/examples/branding.json` e `src-tauri/src/branding.rs`:

```text
${ZAPPE_DATA_DIR:-~/.local/share/zappe}/branding.json
```

Nome, `accent` (também `accentColor` / `accent_color`), `logo`, `splash`, `tagline`, `theme.background`, `theme.focusRing` (`subtle-scale` usa escala 1.08; outro token usa 1.12) e os campos de `idle`. Logo e splash são caminhos relativos a esse diretório, ou absolutos. O logo do canto vira um recorte circular.

Se o arquivo não existir, estiver inválido ou o nome vier vazio, o nome é **Zappe**, o acento `#E85A1B` e o fundo `#000000`. Nenhum nome de marca pessoal entra no repositório: o arquivo de verdade fica só na máquina.

## O que este protótipo não faz

Não reproduz vídeo, não faz harvest e não fala com o nest. Enter só abre uma ficha do título. O desfoque do fundo é uma placa pré-calculada (a capa reduzida e borrada uma vez), para a iGPU não pagar um blur por frame. Sombra e brilho também são texturas únicas, recoloridas pela cor da marca.
