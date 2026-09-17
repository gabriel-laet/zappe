import type { OtaChannel } from "@/lib/tauri";

/** First channel for the Apps-row shortcut (HD-first list from backend). */
export function primaryOtaChannel(channels: OtaChannel[]): OtaChannel | null {
  return channels[0] ?? null;
}
