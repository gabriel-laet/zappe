import type { OtaChannel } from "@/lib/tauri";

function foldName(name: string): string {
  return name
    .toLowerCase()
    .normalize("NFD")
    .replace(/\p{M}/gu, "")
    .replace(/[^\p{L}\p{N}\s]/gu, " ")
    .replace(/\s+/g, " ")
    .trim();
}

function channelBaseKey(name: string): string {
  let s = foldName(name);
  for (const token of [" hdtv", " hd", " fhd", " sd", " uhd", " 4k"]) {
    if (s.endsWith(token)) {
      s = s.slice(0, -token.length).trim();
    }
  }
  for (const prefix of ["tv ", "rede "]) {
    if (s.startsWith(prefix)) {
      s = s.slice(prefix.length).trim();
    }
  }
  return s;
}

function hdScore(name: string): number {
  const l = foldName(name);
  if (l.includes("hdtv") || l.includes(" fhd") || l.endsWith(" hd") || l.includes(" hd ")) {
    return 0;
  }
  if (l.includes("hd")) return 1;
  if (l.includes(" sd")) return 3;
  return 2;
}

/** First channel for the Apps-row shortcut (HD-first list from backend). */
export function primaryOtaChannel(channels: OtaChannel[]): OtaChannel | null {
  return channels[0] ?? null;
}

/** Voice / couch query → listed channel. Mirrors Rust `resolve_channel_name`. */
export function pickOtaChannel(
  channels: OtaChannel[],
  query: string,
): OtaChannel | null {
  const req = query.trim();
  if (!req || channels.length === 0) return null;

  const exact = channels.find((c) => c.name === req);
  if (exact) return exact;

  const reqFold = foldName(req);
  const ci = channels.find((c) => foldName(c.name) === reqFold);
  if (ci) return ci;

  const reqKey = channelBaseKey(req);
  const ranked = channels.filter((c) => {
    const key = channelBaseKey(c.name);
    const folded = foldName(c.name);
    return (
      key === reqKey ||
      (reqKey.length >= 3 && key.includes(reqKey)) ||
      folded.includes(reqFold) ||
      reqFold.includes(folded)
    );
  });
  if (ranked.length === 0) return null;

  ranked.sort((a, b) => {
    const aKey = channelBaseKey(a.name);
    const bKey = channelBaseKey(b.name);
    const aFold = foldName(a.name);
    const bFold = foldName(b.name);
    const score = (key: string, folded: string) => {
      if (key === reqKey) return 0;
      if (folded === reqFold) return 1;
      if (folded.startsWith(reqFold) || key.startsWith(reqKey)) return 2;
      return 3;
    };
    return (
      score(aKey, aFold) - score(bKey, bFold) ||
      hdScore(a.name) - hdScore(b.name) ||
      aFold.localeCompare(bFold)
    );
  });
  return ranked[0] ?? null;
}
