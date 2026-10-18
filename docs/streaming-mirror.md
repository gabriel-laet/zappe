# Espelho dos streamings

O Chrome não aparece para navegar. Ele sobe oculto, atrás do guia Qt, no mesmo perfil de sempre (`~/.local/share/zappe/chrome-profile`). O guia pede ao CDP as respostas JSON/GraphQL da página e desenha fileiras, capas e continuar assistindo. O DOM entra quando a rede não traz título reproduzível; perfis e busca por seletor saem dessa leitura. O Chrome só vem para a frente no Play, em tela cheia.

## Adaptadores

`data/mirrors.json` descreve cada serviço: URL da home, URL de busca (`{q}`), um `feeds` de rede e os seletores de DOM. Um serviço novo entra nesse JSON com `kind` `generic`. Netflix (`kind` `netflix`) e YouTube (`kind` `youtube`) também normalizam a URL de reprodução no código:

- `/title/80100172` na Netflix vira `https://www.netflix.com/watch/80100172`
- `youtu.be/…` e `/shorts/…` viram `https://www.youtube.com/watch?v=…`

Prime Video, HBO Max, Apple TV, Disney+ e Globoplay usam o extrator genérico. Um link só é Play se o caminho tiver `/watch`, `/play`, `/player`, `/video` ou `/v/`.

Cada `feeds` diz quais URLs interessam (`urlContains`) e quais chaves do JSON são título, link, capa e nome da fileira. Se o link não for `http`, `hrefTemplate` monta a URL com `{href}`. O app guarda até 8 corpos JSON, com 48 KB cada. Se sair pelo menos um título reproduzível, o guia usa isso e não raspa o HTML. Senão, o script de DOM devolve `profiles`, `shelves` e `needsLogin`. `needsLogin` abre o pedido de login que já existia, sem tirar o Qt da frente, e não dispara aprendizado.

## Aprendizado

Em operação normal o app não chama IA. A chamada só existe depois que a rede e o DOM falham na home do serviço (fileira vazia, seletor ausente ou nenhum Play) e existe um endpoint configurado.

A configuração fica fora do git, em `~/.local/share/zappe/llm.json` (ou `$ZAPPE_DATA_DIR/llm.json`). Sem arquivo, ou com `url` vazia, o aprendizado fica desligado e o guia só mostra a fileira vazia. Exemplo sem chave: `packaging/appliance/examples/llm.json`.

```json
{
  "url": "https://api.openai.com/v1/chat/completions",
  "model": "gpt-4o-mini",
  "key": "coloque-a-chave-so-nesta-maquina"
}
```

`url` é o endpoint completo, no formato de chat completions. `ZAPPE_LLM_URL`, `ZAPPE_LLM_KEY` e `ZAPPE_LLM_MODEL` substituem o arquivo. A chave vai no cabeçalho `Authorization` e não entra no JSON do pedido.

Quando o adaptador falha, o app tira um retrato: DOM enxuto (tag, classe, link, rótulo), as amostras de rede e um JPEG da página. O modelo devolve um adaptador. Antes de gravar, o app roda esse adaptador nas amostras capturadas. Só salva se saírem pelo menos 4 títulos com capa `http` e link de Play. Resposta só com seletor de DOM, ou feed que não extrai essas amostras, é descartada. Há uma tentativa por serviço a cada processo.

O arquivo vigente é `~/.local/share/zappe/adapters/<id>.json`. Ele substitui o serviço de mesmo `id` que veio no pacote. A versão anterior vai para `adapters/history/<id>/<versão>.json` e permanece lá. Se o arquivo aprendido falha de novo, o app restaura a versão histórica imediatamente anterior, uma vez por processo, e tenta extrair de novo. Apagar `adapters/<id>.json` volta ao adaptador do pacote. Netflix e YouTube conservam o `kind`, para a URL de Play continuar a mesma.

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
