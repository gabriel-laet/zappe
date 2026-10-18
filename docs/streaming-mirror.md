# Espelho dos streamings

O Chrome não aparece para navegar. Ele sobe oculto, atrás do guia Qt, no mesmo perfil de sempre (`~/.local/share/zappe/chrome-profile`). O guia lê o DOM por CDP e desenha fileiras, capas, continuar assistindo, perfis e busca. O Chrome só vem para a frente no Play, em tela cheia.

## Adaptadores

`data/mirrors.json` descreve cada serviço: URL da home, URL de busca (`{q}`) e seletores de fileira, título, card, link, capa, progresso e perfil. Um serviço novo entra nesse JSON com `kind` `generic`. Netflix (`kind` `netflix`) e YouTube (`kind` `youtube`) também normalizam a URL de reprodução no código:

- `/title/80100172` na Netflix vira `https://www.netflix.com/watch/80100172`
- `youtu.be/…` e `/shorts/…` viram `https://www.youtube.com/watch?v=…`

Prime Video, HBO Max, Apple TV, Disney+ e Globoplay usam o extrator genérico com os seletores do JSON. Um link só é Play se o caminho tiver `/watch`, `/play`, `/player`, `/video` ou `/v/`.

O script de extração devolve JSON (`profiles`, `shelves`, `needsLogin`). O app transforma isso em fileiras do guia. `needsLogin` abre o pedido de login que já existia, sem tirar o Qt da frente.

## O que o controle faz

| Onde | OK | Play (Assistir) | Voltar |
| --- | --- | --- | --- |
| Card do streaming | abre as fileiras daquele serviço no Qt | — | um nível |
| Título | ficha no Qt | Chrome vai para a URL e fica em tela cheia | fecha a ficha |
| Player | vai ao Chrome | play/pause no Chrome | Chrome volta uma página e o guia reaparece |

No player o nó do teclado fica exclusivo. Setas, OK, play/pause e Voltar saem como `Input.dispatchKeyEvent` (`rawKeyDown`, `keyDown`, `keyUp`). A seta esquerda é `ArrowLeft` (37). Voltar é `BrowserBack` (166). Antes da primeira tecla o app foca o `<video>`.

## Capas e atualização

Cada URL de capa vai para `~/.cache/…/art/covers/<sha1>.img`. Arquivo com menos de 12 horas é reusado. O que falta baixa em segundo plano; até lá o guia usa uma capa gerada. A varredura dos serviços recomeça a cada 20 minutos e não roda enquanto o player está aberto.

`ZAPPE_MIRROR_DISABLE=1` não abre o Chrome. Sem o binário, o guia segue sem essas fileiras.

Busca por voz: «buscar dark na netflix».
