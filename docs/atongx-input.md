# Controle ATONGX

O app lê `/dev/input` do dongle XING WEI / ATONGX. O contrato está em [`data/atongx-map.json`](../data/atongx-map.json) e em `src/keymap.cpp`. Os dois são conferidos no `--self-check`.

| Botão | Código | Ação |
| --- | --- | --- |
| Power | 116 | liga/desliga a Samsung |
| Play / Pause | 164 | play/pause no Chrome ou no mpv |
| Cima, baixo, esquerda, direita | 103, 108, 105, 106 | foco no guia |
| OK | 28 | ativar |
| Home | 158 | voltar (não fecha o app) |
| Menu | 127 | menu lateral |
| Page Up / Down | 104, 109 | fileira anterior / seguinte |
| Volume + / − | 115, 114 | Samsung |
| Mute | 113 | Samsung |
| Del | 14 | voltar |
| Mic | 582 | Whisper pt-BR |

Power não desliga o mini-PC: o nó da tecla 116 fica com `EVIOCGRAB`, o logind não a recebe, e o app manda o toggle para a Samsung. Ver [`samsung-tv.md`](samsung-tv.md).

Home (158) é voltar. Na home do guia não faz nada. Em Ajustes, ficha ou menu, volta um nível. Com streaming aberto, o app segura esse nó de teclado e consulta o histórico do Chrome: se ainda há página anterior, volta uma; se já está na primeira, fecha o Chrome e volta ao guia. Setas, OK e play/pause seguem para a página. Com a TV aberta, para o mpv e volta ao guia. A tecla não pode encerrar o processo: um `close` da janela é ignorado, e o kiosk abre o app de novo se ele sair.

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
