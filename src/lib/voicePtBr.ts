export type VoiceIntent =
  | { type: "home" }
  | { type: "back" }
  | { type: "playpause" }
  | { type: "open"; service: "netflix" | "prime" | "disney" | "youtube" }
  | { type: "ota"; query: string };

function fold(text: string): string {
  return text
    .toLowerCase()
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .replace(/[^\p{L}\p{N}\s]/gu, " ")
    .replace(/\s+/g, " ")
    .trim();
}

/** Constrained pt-BR grammar for the red mic. Not a chat model. */
export function parseVoicePtBr(text: string): VoiceIntent | null {
  const t = fold(text);
  if (!t) return null;
  if (/\b(inicio|home|guia|principal)\b/.test(t)) return { type: "home" };
  if (/\b(voltar|volta|retornar|back)\b/.test(t)) return { type: "back" };
  if (/\b(pausar|pause|parar|stop)\b/.test(t)) return { type: "playpause" };
  if (/\b(continuar|play|reproduzir|tocar)\b/.test(t)) return { type: "playpause" };
  if (/\b(abrir|abre|abre a|abrir a)?\s*netflix\b/.test(t) || /\bnetflix\b/.test(t)) {
    return { type: "open", service: "netflix" };
  }
  if (/\b(prime|amazon)\b/.test(t)) return { type: "open", service: "prime" };
  if (/\b(disney|disneyplus)\b/.test(t)) return { type: "open", service: "disney" };
  if (/\b(youtube|you tube)\b/.test(t)) return { type: "open", service: "youtube" };
  const canal = t.match(/\bcanal\s+(.+)$/) ?? t.match(/\b(globo|sbt|record|band|cultura)\b/);
  if (canal) {
    const query = (canal[1] ?? canal[0]).trim();
    if (query) return { type: "ota", query };
  }
  return null;
}
