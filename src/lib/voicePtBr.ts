export type VoiceApp = "netflix" | "prime" | "disney" | "youtube";

export type VoiceIntent =
  | { type: "home" }
  | { type: "back" }
  | { type: "playpause" }
  | { type: "open"; service: VoiceApp }
  | { type: "ota"; query: string }
  | { type: "volume"; delta: number }
  | { type: "mute" }
  | { type: "sync" }
  | { type: "connect" };

/** Couch phrases the red-mic overlay understands (pt-BR). Not a chat model. */
export const VOICE_LEXICON = [
  "início / início / home / guia / tela inicial",
  "voltar / volta / retornar",
  "volume mais / aumentar o volume",
  "volume menos / diminuir o volume / abaixa o volume",
  "mudo / silêncio / sem som",
  "Netflix / Prime / Disney / YouTube",
  "play / pausar / continuar",
  "Globo / Record / SBT / Band / Cultura (e “ir para …”, “canal …”)",
  "sincronizar / sync / atualizar",
  "conectar / telefone",
] as const;

export function foldVoiceText(text: string): string {
  return text
    .toLowerCase()
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .replace(/[^\p{L}\p{N}\s]/gu, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function stripArticle(q: string): string {
  return q.replace(/^(a|o|as|os|na|no|nas|nos|da|do|das|dos|uma|um)\s+/, "").trim();
}

/** Constrained pt-BR grammar for the red mic. Unknown utterances are rejected. */
export function parseVoicePtBr(text: string): VoiceIntent | null {
  const t = foldVoiceText(text);
  if (!t) return null;

  if (/\b(mudo|mutar|unmute|desmutar|silencio|sem\s+som|tira\s+o\s+mudo)\b/.test(t)) {
    return { type: "mute" };
  }
  if (
    /\b(volume\s+(mais|aumentar|alto)|aumentar\s+o?\s*volume|aumenta\s+o?\s*volume|mais\s+alto|sobe\s+o?\s*volume)\b/.test(
      t,
    )
  ) {
    return { type: "volume", delta: 1 };
  }
  if (
    /\b(volume\s+(menos|diminuir|baixo)|diminuir\s+o?\s*volume|diminui\s+o?\s*volume|abaixa\s+o?\s*volume|mais\s+baixo|desce\s+o?\s*volume)\b/.test(
      t,
    )
  ) {
    return { type: "volume", delta: -1 };
  }
  if (/\b(sincronizar|sincroniza|sync|atualizar|atualiza|colheita|harvest)\b/.test(t)) {
    return { type: "sync" };
  }
  if (/\b(conectar|conecta|connect|telefone|celular)\b/.test(t)) {
    return { type: "connect" };
  }
  if (/\bnetflix\b/.test(t)) return { type: "open", service: "netflix" };
  if (/\b(prime|amazon)\b/.test(t)) return { type: "open", service: "prime" };
  if (/\b(disneyplus|disney\s+plus|disney)\b/.test(t)) {
    return { type: "open", service: "disney" };
  }
  if (/\b(youtube|you\s+tube)\b/.test(t)) return { type: "open", service: "youtube" };
  if (/\b(pausar|pause|parar|stop|continuar|play|reproduzir|tocar)\b/.test(t)) {
    return { type: "playpause" };
  }
  if (/\b(voltar|volta|retornar|retorna|back)\b/.test(t)) return { type: "back" };

  const go = t.match(
    /\b(?:ir\s+para|vai\s+para|vai\s+pra|passa\s+para|passa\s+pra|troca\s+para|troca\s+pra|poe|ponha|coloca|abrir|abre|assiste|assistir|canal)\s+(.+)$/,
  );
  if (go) {
    const query = stripArticle(go[1] ?? "");
    if (/^(inicio|home|guia|principal|tela\s+inicial)$/.test(query)) {
      return { type: "home" };
    }
    if (query) return { type: "ota", query };
  }

  const bare = t.match(/\b(globo|sbt|record|band|cultura)\b/);
  if (bare?.[1]) return { type: "ota", query: bare[1] };

  if (/\b(inicio|home|guia|principal|tela\s+inicial)\b/.test(t)) {
    return { type: "home" };
  }
  return null;
}
