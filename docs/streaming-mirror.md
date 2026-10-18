# Espelho dos streamings

O Chrome não aparece para navegar. Fora do Play ele é uma janela real num Xvfb (`:99`, ou `ZAPPE_CATALOG_DISPLAY`), no mesmo perfil de sempre (`~/.local/share/zappe/chrome-profile`), com `--window-size=1920,1080` e `--ozone-override-screen-size=1920,1080`. O processo não herda o `WAYLAND_DISPLAY` do gamescope e usa `--ozone-platform=x11`. `--disable-gpu` evita disputar o DRM com o gamescope. Não usa `--headless=new`: esse modo anuncia `HeadlessChrome` e a Netflix e o YouTube não montam o catálogo. Também não usa janela fora da tela no gamescope: o compositor ignora a posição, dá foco e a janela cobre o guia, com abas, barra de endereço e o balão “Restore pages?”. `--hide-crash-restore-bubble` entra em toda invocação. O user-agent é o de um Chrome de janela, com `--disable-blink-features=AutomationControlled`.

O perfil do Chrome tem lock. No Play o app fecha o Chrome do Xvfb e sobe outro com `--kiosk` no `DISPLAY` e no `WAYLAND_DISPLAY` do gamescope. Ao sair do vídeo, o kiosk fecha e o catálogo volta para o Xvfb. Não dá para mover a janela de um display para o outro pelo CDP. O Xvfb fica no ar entre um Play e o seguinte. Sem o binário `Xvfb` (`xorg-server-xvfb` no Arch) o catálogo não abre, em vez de cair no gamescope.

Cada streaming tem a própria aba. A fila é serial: o próximo serviço só começa depois que o atual contou os títulos do feed e, se o feed veio vazio, do DOM. `PUT /json/new?about:blank` cria o target, o websocket dessa aba recebe `Page.enable` e `Network.enable`, e só então `Page.navigate` abre a home. O histórico do YouTube reusa a aba do YouTube. Login e uma URL aberta na hora ganham outra aba e não navegam a do catálogo. A espera de cada home chega a 8 segundos enquanto ainda há resposta de feed em voo.

O processo do Chrome fica pronto antes da porta de depuração. O app espera `/json/version` antes da primeira aba. Cada passo sai no stderr (`zappe: cdp nova aba`, `zappe: espelho …`). Se a home não abre em 20 segundos, a fileira deixa o “Atualizando…” e mostra um card de erro. OK nesse card é “Tentar de novo”.

## Adaptadores

`data/mirrors.json` descreve cada serviço com o que a TV mostrou em outubro de 2026: URL da home, URL de busca (`{q}`), `feeds` dos endpoints JSON/GraphQL e os seletores de DOM que ainda existem. Um serviço novo entra nesse JSON com `kind` `generic`. Netflix (`kind` `netflix`) e YouTube (`kind` `youtube`) também normalizam a URL de reprodução no código:

- `/title/80100172` e `/browse?jbv=80100172` na Netflix viram `https://www.netflix.com/watch/80100172`
- `youtu.be/…` e `/shorts/…` viram `https://www.youtube.com/watch?v=…`

A home do YouTube é `https://www.youtube.com/`. `/feed/subscriptions` nesta conta não tem `ytd-rich-item-renderer`. O histórico (`/feed/history`) entra em seguida, como continuar assistindo. A busca continua em `/results`.

Prime Video, HBO Max, Apple TV, Disney+ e Globoplay usam o extrator genérico, com os caminhos de Play do próprio serviço (`/detail/` no Prime, `/video/watch` e `/show/` na Max, `/episode/` na Apple, `/browse/entity` na Disney, `/v/` e `/ao-vivo/` no Globoplay). No Prime, título com “Watch with”, Paramount+, Cinemax, MGM+, Starz e canais parecidos sai da fileira. Onde o card não é a URL de vídeo, `playClick` aperta o botão Assistir/Retomar da página.

Seletores medidos na TV com o catálogo logado: a capa da Netflix é `a[href^="/browse?jbv=<id>"]`, com o nome em `aria-label` e um `img` dentro (cerca de 105 links). `.title-card-container`, `.slider-item`, `.lolomoRow` e `a[href*="/watch/"]` não aparecem. A Disney usa `[data-testid="set-item"]` e `a[href*="/browse/entity"]` (75 de cada). Prime, Max, Apple e Globoplay já devolviam títulos pelo DOM (34, 33, 27 e 43). Netflix, YouTube e Disney devolviam 0 com os seletores antigos. O feed JSON, nessa passagem, veio com 0 títulos nos sete serviços; o DOM é o que preenche o guia. Se a fileira do seletor não existe, o script ainda lê os links do `linkSelector` direto no documento.

Endpoints: `www.netflix.com/graphql`; `youtubei/v1/browse`, `/guide` e `/search`; `primevideo.com/api/enrichItemMetadata`; `api.hbomax.com/cms/routes` e `/cms/collections`; `disney.api.edge.bamgrid.com/explore`; `multicloud-jarvis.globo.com/graphql` e `multicloud-mystique.globo.com`. O catálogo da Apple TV veio no HTML, não num JSON estável.

Cada `feeds` diz quais URLs interessam (`urlContains`) e quais chaves do JSON são título, link, capa e nome da fileira. Se o link não for `http`, `hrefTemplate` monta a URL com `{href}`. O app guarda até 8 corpos JSON, com 48 KB cada. Os extratores de `data/mirrors.json` rodam sempre, com ou sem `llm.json`. O stderr conta os títulos:

```text
zappe: espelho netflix feed 12 títulos em 2 corpos
zappe: espelho netflix feed 12 títulos em https://www.netflix.com/graphql
zappe: espelho netflix extrator DOM de mirrors.json
zappe: espelho netflix dom 0 títulos login=0
```

Se o feed já trouxe título reproduzível, o guia usa isso e não raspa o HTML. Senão, o script de DOM devolve `profiles`, `shelves` e `needsLogin`. `needsLogin` abre o pedido de login que já existia, sem tirar o Qt da frente, e não dispara aprendizado. Feed e DOM vazios é que chegam no aprendizado.

## Aprendizado

Em operação normal o app não chama IA. A chamada só existe depois que a rede e o DOM falham na home do serviço (fileira vazia, seletor ausente ou nenhum Play) e existe um endpoint configurado.

A configuração fica fora do git, em `~/.local/share/zappe/llm.json` (ou `$ZAPPE_DATA_DIR/llm.json`). Sem arquivo, ou com `url` vazia, o aprendizado fica desligado. Os extratores estáticos já rodaram; o log deixa isso explícito (`extrator estático sem catálogo (feed N, dom N); aprendizado desligado`) e o guia mostra a fileira vazia. Exemplo sem chave: `packaging/appliance/examples/llm.json`.

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

Voltar no player não manda Escape. Escape só sai do player no Prime e na Apple TV, e ainda deixa a página por baixo. Voltar fecha o Chrome com `Browser.close` e o guia volta para a frente. Enquanto o kiosk está aberto, o app também segura qualquer nó evdev que emita `KEY_BACK` ou backspace, não só o teclado com setas. Sem isso o X entrega `XF86Back` para o Chrome focado e a kiosk cai numa `about:blank` branca. Se o frame principal voltar para `about:blank` depois do vídeo ter aberto, o app fecha o kiosk e devolve o guia. Em seguida o Chrome do catálogo sobe de novo no Xvfb. A janela kiosk existe só enquanto o vídeo está aberto, e só no display do gamescope.

O catálogo (Xvfb) não navega para URL de playback (`/watch`, `/player`, `watch?v=`). Esse navigate só roda quando o Chrome já está na face de player, no gamescope. A queda do CDP do Xvfb, no meio do `present()`, não reanexa no processo que está morrendo: espera o `ready()` do kiosk novo.

Numa view de streaming (as fileiras no Qt, antes do Play), Voltar do controle é `KEY_BACK` (evdev 158), keysym `XF86Back`. Escape não é essa tecla. `xdotool key Escape` cai na janela que estiver focada; se for o Chrome, o site consome a tecla e o guia não fecha. O alvo é a janela do app, cujo título é o nome da marca (o padrão é Zappe):

```bash
WID=$(xdotool search --name Zappe | head -1)
xdotool key --window "$WID" XF86Back
```

## Capas e atualização

O extrator prefere `currentSrc` (a imagem que o browser de fato carregou) e, se ela for `blob:` ou vazia, cai para `src`, `srcset`, `data-src` e `background-image`. URL que começa com `//` — o Prime manda `//m.media-amazon.com/...` no `srcset` — vira `https:`. Se a imagem está ao lado do link, e não dentro dele, o extrator sobe um nível e olha o irmão que não é outro card. No JSON, `images`, `packshot` e `uri` também entram, inclusive com `//`.

Cada capa vira JPEG em `~/.cache/…/art/covers/<sha1>.jpg`. O sha1 é do host e do path, sem a query: um token novo não cria outro arquivo, e o card casa com a capa mesmo quando a URL assinada mudou. Se o path muda (outro título ou outra arte), o arquivo é outro e o app baixa de novo. JPEG com menos de 12 horas é reusado. Ao mostrar a capa, o app atualiza a data do arquivo.

Arquivo `.img` antigo sai na hora. JPEG que ninguém mostrou há mais de 14 dias também sai. A limpeza roda ao subir e em cada releitura.

Enquanto a página carrega, cada resposta `image/*` grande o bastante é guardada com `Network.getResponseBody`. A URL do card e a da rede casam sem a query. O caminho principal é o processo do app: `QNetworkAccessManager` baixa a URL com o user-agent de Chrome, `Accept` pedindo JPEG antes de WebP, e `Referer` da home do serviço (`https://www.netflix.com/browse`, não o host do CDN). Placeholders `{width}`, `{w}x{h}{c}.{f}` e `%7Bwidth%7D` viram um tamanho real (640×360, JPEG). Se o corpo for WebP ou AVIF, o app decodifica com o plugin do Qt (`qt6-imageformats` no Arch) ou converte com `ffmpeg`. O card recebe `file://` desse JPEG. Se o GET falhar, o log traz o status, o content-type e os magic bytes. Um recorte do card visível ainda cobre o que o GET não trouxer. Até a imagem chegar, o guia usa uma capa gerada.

Na view de um streaming só entram as fileiras lidas agora daquele serviço. O catálogo local, mesmo com o mesmo nome de fileira, fica de fora. Título que começa com «Assistir ao vivo» ou «Ao vivo:», ou cujo link tem `/ao-vivo/`, não entra na fileira: esse card do Globoplay tomava o hero do guia no meio da navegação. Se a fileira é republicada enquanto a capa chega, o foco volta ao mesmo link ou, se o link ganhou query nova, ao mesmo título. Não cai no primeiro card de outra fileira.

O log é uma linha por serviço (`artes N http, M sem url` e `capas N/M direto, cards K`), com o primeiro `file://` e o primeiro erro. `N` é o arquivo gravado; `K` é quantos cards passaram a apontar para ele. Não repete o lote.

Cada serviço é relido ao abrir e de novo a cada 45 minutos. `ZAPPE_MIRROR_REFRESH_MIN` muda esse intervalo e fica preso entre 30 e 60. A releitura não roda enquanto o player está aberto. Se a mesma URL já está na aba, o app passa por `about:blank` antes de navegar de novo: no YouTube um `Page.navigate` repetido não recarrega e o DOM virtualizado devolve um título. Uma leitura sem merge muito menor que a anterior (a guardada tem pelo menos 4 e a nova tem menos da metade) fica de fora.

OK num título local de um serviço do espelho, sem URL de playback, abre o loading daquele serviço em vez da ficha “No catálogo local”. A fileira local que só repete o serviço some da home quando o espelho existe. A fileira “Atualizando…” aparece antes da página do serviço. Um título que já tem URL de playback abre a ficha com o nome, a capa e a sinopse se o card trouxer uma. Essa ficha não lista episódios fictícios e não diz que o guia só navega. Assistir continua mandando o Play.

`ZAPPE_MIRROR_DISABLE=1` não abre o Chrome. Sem o binário, o guia segue sem essas fileiras.

Busca por voz: «buscar dark na netflix».
