# Controle ATONGX

O app lê `/dev/input` do dongle XING WEI / ATONGX. O contrato está em [`data/atongx-map.json`](../data/atongx-map.json) e em `src/keymap.cpp`. Os dois são conferidos no `--self-check`.

| Botão | Código | Ação |
| --- | --- | --- |
| Power | 116 | liga/desliga a Samsung |
| Play / Pause | 164 | play/pause no Chrome ou no mpv |
| Cima, baixo, esquerda, direita | 103, 108, 105, 106 | foco no guia |
| OK | 28 | ativar |
| Voltar | 158 | um passo (não fecha o app); no canal, volta ao card que estava no ar |
| Home | 172, 102 | primeiro card da grade (não fecha o app) |
| Menu | 127 | menu lateral |
| Page Up / Down | 104, 109 | fileira anterior / seguinte |
| Volume + / − | 115, 114 | Samsung |
| Mute | 113 | Samsung |
| Del | 14 | voltar |
| Mic | 582 | Whisper pt-BR |

Power não desliga o mini-PC: o nó da tecla 116 fica com `EVIOCGRAB`, o logind não a recebe, e o app manda o toggle para a Samsung. Ver [`samsung-tv.md`](samsung-tv.md).

Voltar (158, `KEY_BACK`) dá um passo e não fecha o app. No canal ao vivo, fecha o mpv e devolve o foco ao card que estava no ar. Home (`KEY_HOMEPAGE` 172 e `KEY_HOME` 102) volta para a grade e põe o foco no primeiro card. Escape (1) e Del (14) também voltam um nível e, na home, não mudam o card. No player, o app segura o teclado e qualquer nó que emita `KEY_BACK` (158) ou backspace (14), e manda as outras teclas ao Chrome por CDP (`Input.dispatchKeyEvent`: setas, OK como Espaço, play/pause). Home e Voltar fecham o Chrome (`Browser.close`) e devolvem o guia; não mandam `BrowserBack` para a página. Sem esse grab, o X entrega `XF86Back` ao Chrome focado e a kiosk fica numa `about:blank` branca. Com a TV aberta, o app segura o teclado do controle: Voltar (158) e Escape (evdev 1) fecham o mpv mesmo com ele em foco. Voltar deixa o foco no canal que estava no ar; Home deixa no primeiro card. O keycode X11 166 (`XF86Back`) também é capturado enquanto o canal está no ar, então a tecla injetada com `xdotool` não fica só no mpv. O `input.conf` do mpv trata `ESC`, `BACK` e `GO_BACK` como quit — o mpv não reconhece o nome `XF86Back`. A tecla não pode encerrar o processo do app: um `close` da janela é ignorado, e o kiosk abre o app de novo se ele sair.

## Voltar pelo xdotool

O botão Voltar do controle é `KEY_BACK`, código evdev **158**. Ele volta um passo. No canal ao vivo, o foco volta para o card que estava no ar, não para o primeiro. O Del é evdev 14 e volta um nível. Escape é evdev 1: enquanto o teclado está exclusivo (Chrome ou mpv) o app trata essa tecla como Voltar, para o player em foco não engolir o Escape. `KEY_HOMEPAGE` 172 e `KEY_HOME` 102 são Home e focam o primeiro card.

No X11, evdev 158 vira o keycode **166** e o keysym **`XF86Back`**. O Qt lê isso como `Qt.Key_Back`. O guia trata essa tecla como Voltar: um passo, sem fechar o app.

`xdotool key Escape` não faz isso quando a tecla cai no Chrome. A view de streaming continua aberta, porque o site é que recebe o Escape. Mande `XF86Back` para a janela do app. O título dela é o nome da marca; o padrão é Zappe:

```bash
WID=$(xdotool search --name Zappe | head -1)
xdotool key --window "$WID" XF86Back
```

Se o keysym não existir nessa máquina, o keycode é o mesmo: `xdotool key --window "$WID" 166`.

No player do Chrome, Voltar não é tecla injetada. O app manda `Browser.close` pelo CDP e o guia volta para a frente, mesmo se o Chrome estiver com o foco. Com o teclado exclusivo, Escape também volta; sem o grab, o Escape cai no site. No canal ao vivo, Escape na janela do mpv faz o player sair. `XF86Back` (keycode 166) é capturado pelo app e também está no `input.conf` como `BACK` / `GO_BACK`. Quando o processo termina, o guia volta.

## Air mouse

`event4` é o air mouse. O app abre esse nó (ou o único nó `mouse*` do dongle, se o número mudar) e faz `EVIOCGRAB`. Eventos de cursor são lidos e descartados.

`ZAPPE_HID_DISABLE=1` desliga o leitor. `ZAPPE_HID_NAME` acrescenta um trecho ao nome do device.

O usuário do kiosk precisa do grupo `input`. Regra opcional: `packaging/appliance/99-atongx.rules`.

Para ver o que o dongle emite:

```bash
./packaging/appliance/zappe-atongx-capture --list
```

## Volume

Volume e mudo vão para a Samsung por Device Connect. Ver [`samsung-tv.md`](samsung-tv.md).
