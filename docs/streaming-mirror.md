# Espelho dos streamings

O Chrome não aparece para navegar. Fora do Play ele sobe sem janela (`--headless=new`), no mesmo perfil de sempre (`~/.local/share/zappe/chrome-profile`). O guia pede ao CDP as respostas JSON/GraphQL da página e desenha fileiras, capas e continuar assistindo. O DOM entra quando a rede não traz título reproduzível; perfis e busca por seletor saem dessa leitura. O Chrome só vem para a frente no Play, em tela cheia.

## Adaptadores

`data/mirrors.json` descreve cada serviço com o que a TV mostrou em outubro de 2026: URL da home, URL de busca (`{q}`), `feeds` dos endpoints JSON/GraphQL e os seletores de DOM que ainda existem. Um serviço novo entra nesse JSON com `kind` `generic`. Netflix (`kind` `netflix`) e YouTube (`kind` `youtube`) também normalizam a URL de reprodução no código:

- `/title/80100172` na Netflix vira `https://www.netflix.com/watch/80100172`
- `youtu.be/…` e `/shorts/…` viram `https://www.youtube.com/watch?v=…`

A home do YouTube desta conta vem vazia. O guia lê inscrições (`/feed/subscriptions`) e o histórico (`/feed/history`), que entra como continuar assistindo. A busca continua em `/results`.

Prime Video, HBO Max, Apple TV, Disney+ e Globoplay usam o extrator genérico, com os caminhos de Play do próprio serviço (`/detail/` no Prime, `/video/watch` e `/show/` na Max, `/episode/` na Apple, `/play/` na Disney, `/v/` e `/ao-vivo/` no Globoplay). No Prime, título com “Watch with”, Paramount+, Cinemax, MGM+, Starz e canais parecidos sai da fileira. Onde o card não é a URL de vídeo, `playClick` aperta o botão Assistir/Retomar da página.

Seletores vistos na TV: Netflix usa `h2` e `[class*=card]` com `a[href*='/title/']` (`.lolomoRow` não está mais na UI). Prime usa `[data-testid*=carousel]` e `[data-testid*=card]`. Max usa `[data-testid*=tile]`. Apple usa `[class*=shelf]` e `h2`. Disney usa `[data-testid*=tile]` e `a[href*='/play/']`. Globoplay usa `[class*=carousel]` e `[class*=card]`.

Endpoints: `www.netflix.com/graphql`; `youtubei/v1/browse`, `/guide` e `/search`; `primevideo.com/api/enrichItemMetadata`; `api.hbomax.com/cms/routes` e `/cms/collections`; `disney.api.edge.bamgrid.com/explore`; `multicloud-jarvis.globo.com/graphql` e `multicloud-mystique.globo.com`. O catálogo da Apple TV veio no HTML, não num JSON estável.

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
| Player | Espaço no vídeo | play/pause no vídeo | fecha o Chrome e o guia reaparece |

No player o nó do teclado fica exclusivo. Setas, OK e play/pause saem como `Input.dispatchKeyEvent` (`rawKeyDown`, `keyDown`, `keyUp`). OK e play/pause são Espaço (32): Enter pausa em uns serviços e não faz nada em outros. A seta esquerda é `ArrowLeft` (37), dez segundos para trás na maior parte. Na Disney+ as setas são irregulares, então ←/→ mudam `video.currentTime` em 10s. No Globoplay ao vivo as teclas não chegam sem foco: o app clica no `<video>` e controla play/pause e o tempo por script. O vídeo escolhido é o que tem DRM (`mediaKeys`); no Prime o trailer mudo fica de fora.

Voltar não manda Escape. Escape só sai do player no Prime e na Apple TV, e ainda deixa a página por baixo. Voltar fecha o Chrome com `Browser.close` e o guia volta para a frente. Em seguida o Chrome sobe de novo sem janela (`--headless=new`) para continuar lendo os catálogos. A janela kiosk existe só enquanto o vídeo está aberto: no gamescope, minimizar não tira a tela branca de `about:blank`.

## Capas e atualização

Cada URL de capa vai para `~/.cache/…/art/covers/<sha1>.img`. Arquivo com menos de 12 horas é reusado. O que falta baixa em segundo plano; até lá o guia usa uma capa gerada. A varredura dos serviços recomeça a cada 20 minutos e não roda enquanto o player está aberto.

`ZAPPE_MIRROR_DISABLE=1` não abre o Chrome. Sem o binário, o guia segue sem essas fileiras.

Busca por voz: «buscar dark na netflix».
