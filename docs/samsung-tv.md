# Samsung UN43TU7020

O mini-PC não usa HDMI-CEC para volume. Volume +, volume − e mudo do ATONGX vão por Device Connect, o websocket que o controle da TV usa.

```text
ATONGX 115 / 114 / 113
  → src/samsung.cpp
  → ws://192.168.3.6:8001/api/v2/channels/samsung.remote.control
  → KEY_VOLUP / KEY_VOLDOWN / KEY_MUTE
```

Se a TV não responder, o app mostra um toast e tenta `pactl` no sink padrão. A porta segura `wss://:8002` é a segunda tentativa.

O nome enviado no pareamento é **Zappe**. O token volta no evento `ms.channel.connect` e é gravado em `~/.local/share/zappe/samsung.json` com permissão `0600`. Não commitar esse arquivo.

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/samsung.json ~/.local/share/zappe/samsung.json
```

| Variável | Padrão |
| --- | --- |
| `ZAPPE_SAMSUNG_HOST` | `192.168.3.6` |
| `ZAPPE_SAMSUNG_PORT` | `8001` |
| `ZAPPE_SAMSUNG_SECURE_PORT` | `8002` |
| `ZAPPE_SAMSUNG_NAME` | `Zappe` |
| `ZAPPE_SAMSUNG_TOKEN` | vazio (o arquivo manda) |
| `ZAPPE_SAMSUNG_DISABLE` | desligado |

Na TV, aceite **Zappe** em Gerenciador de dispositivos / Device Connect.

A tecla Power do controle (código 116) é ignorada. O app não envia `KEY_POWER` nem Wake-on-LAN. O campo `wakeOnPower` pode existir num `samsung.json` antigo; o app Qt não lê esse campo.
