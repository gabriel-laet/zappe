# Samsung UN43TU7020

O mini-PC controla a TV pela rede. Volume, mudo, força e entrada HDMI usam o Device Connect, o websocket do controle da TV (na sala, o mesmo pareamento do SmartThings). Não há token de nuvem no app.

```text
ATONGX 115 / 114 / 113 / 116
  → src/samsung.cpp
  → ws://192.168.3.6:8001/api/v2/channels/samsung.remote.control
  → KEY_VOLUP / KEY_VOLDOWN / KEY_MUTE / KEY_POWER
```

A porta segura `wss://:8002` é a segunda tentativa. O nome no pareamento é **Zappe**. O token volta no evento `ms.channel.connect` e é gravado em `~/.local/share/zappe/samsung.json` com permissão `0600`. Não commitar esse arquivo.

## Configurar

```bash
mkdir -p ~/.local/share/zappe
cp packaging/appliance/examples/samsung.json ~/.local/share/zappe/samsung.json
chmod 600 ~/.local/share/zappe/samsung.json
```

Edite o arquivo na máquina (não no git):

| Campo | Função |
| --- | --- |
| `host` | IP da TV. O exemplo usa `192.168.3.6`. |
| `port` | `8001`. |
| `securePort` | `8002`. |
| `name` | Nome no Device Connect. Padrão `Zappe`. |
| `token` | Gravado pelo app depois que a TV aceita o Zappe. Não inventar. |
| `mac` | MAC da TV, para Wake-on-LAN. Vazio desliga o WoL. |
| `cec` | Vazio tenta `/dev/cec0`. `off` não manda HDMI-CEC. |

Na TV, aceite **Zappe** em Gerenciador de dispositivos / Device Connect. Sem isso o websocket responde `unauthorized` e o app avisa na tela.

O MAC está em Configurações → Rede → Status da conexão, ou no roteador. Formato `aa:bb:cc:dd:ee:ff`. Wake-on-LAN precisa estar ligado na TV (rede em standby).

Variáveis cobrem o arquivo: `ZAPPE_SAMSUNG_HOST`, `ZAPPE_SAMSUNG_PORT`, `ZAPPE_SAMSUNG_SECURE_PORT`, `ZAPPE_SAMSUNG_NAME`, `ZAPPE_SAMSUNG_TOKEN`, `ZAPPE_SAMSUNG_MAC`, `ZAPPE_SAMSUNG_CEC`. `ZAPPE_SAMSUNG_DISABLE=1` desliga o controle. Um `wakeOnPower` antigo no JSON é ignorado: o Power do controle agora sempre alterna a TV, quando o controle não está desligado.

## Power

A tecla 116 liga ou desliga a Samsung. O app continua com `EVIOCGRAB` no nó que tem `KEY_POWER` e não tem setas, então o logind não vê a tecla e não desliga o mini-PC.

O plano, depois de um `GET http://host:8001/api/v2/`:

| Estado | O que sai |
| --- | --- |
| Ligada | Só `KEY_POWER` no Device Connect (desliga). |
| Standby, ready ou sleep | `KEY_POWER`, e também Wake-on-LAN se `mac` existir, e `cec-ctl --image-view-on` se o device CEC existir. |
| Sem resposta | As mesmas tentativas de ligar. O `KEY_POWER` falha em silêncio se o WoL ou o CEC já foram pedidos. |
| Rede ok, estado desconhecido | Só `KEY_POWER`, para não brigar com o WoL. |

Desligar não usa CEC nem WoL. Ligar não manda standby.

O Beelink da sala pode não ter `/dev/cec0`. Nesse caso o CEC é pulado. Se o device existir, o comando é `cec-ctl -d /dev/cec0 --playback --to 0 --image-view-on`. Sem o binário `cec-ctl`, a linha de status avisa e o resto segue.

## Volume, mudo e HDMI

Volume e mudo são `KEY_VOLUP`, `KEY_VOLDOWN` e `KEY_MUTE`. Se a TV não responder a essas três, o app mostra um toast e tenta `pactl` no sink padrão. Força e HDMI não caem no Pulse.

A entrada não usa `KEY_SOURCE` (isso só circula a fonte). Ajustes manda `KEY_HDMI1`, `KEY_HDMI2` ou `KEY_HDMI3`. A UN43TU7020 tem três HDMI.

O volume numérico não vem no websocket. Com a TV ligada o app pergunta o UPnP `GetVolume` na porta `9197`. Se a TV não responder, Ajustes mostra **indisponível**.

## Ajustes

Menu → Ajustes → seção **TV**:

| Linha | Controle |
| --- | --- |
| Estado | OK consulta de novo (ligada/desligada e volume). |
| Ligar / desligar | OK alterna, o mesmo plano da tecla Power. |
| Volume | Esquerda e direita. |
| Mudo | OK. |
| Entrada HDMI | Laterais escolhem 1–3, OK envia. |
