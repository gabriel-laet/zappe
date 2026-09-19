/** Events emitted by the Tauri Samsung / pactl volume path (`tv-control`). */

export type TvControlVia = "samsung" | "pactl";

export type TvControlEvent = {
  action: string;
  via: TvControlVia;
  message: string;
  first_fallback: boolean;
};

export function tvControlLabel(action: string): string {
  if (action === "volume+") return "Volume +";
  if (action === "volume-") return "Volume −";
  if (action === "mute") return "Mudo";
  return action;
}

/** Prefer the one-shot fallback copy; otherwise the short couch label. */
export function tvControlToast(e: TvControlEvent): string {
  if (e.first_fallback && e.via === "pactl" && e.message.trim()) {
    return e.message;
  }
  return e.message.trim() || tvControlLabel(e.action);
}
