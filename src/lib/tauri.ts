import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import type { Branding } from "@/lib/branding";

export type GuideFocus = { shelf_id: string; index: number };

export type SetupState = {
  completed: boolean;
  browser_ack: boolean;
  onepassword_skipped: boolean;
  accounts_done: boolean;
};

export type ChromeStatus = {
  available: boolean;
  path: string | null;
  profile: string;
  ready: boolean;
  launched_by_zappe: boolean;
  pids: number[];
  gamescope: string | null;
  using_gamescope: boolean;
};

export type OtaChannel = { name: string; source: string };

export type PlaybackStatus = {
  surface: "idle" | "chrome" | "ota";
  focus: GuideFocus | null;
};

export type ShelfStatus =
  | "ok"
  | "empty"
  | "stale"
  | "error"
  | "teach"
  | "harvesting";

export type CatalogRow = {
  title: string;
  service: string;
  href?: string | null;
  artwork?: string | null;
  skill_id: string;
};

export type CatalogShelf = {
  id: string;
  skill_id: string;
  title: string;
  status: ShelfStatus;
  message?: string | null;
  rows: CatalogRow[];
};

export type TeachView = {
  active: boolean;
  skill_id: string | null;
  message: string | null;
};

export type CatalogView = {
  shelves: CatalogShelf[];
  teach: TeachView;
};

export type VoiceOutcome = {
  armed: boolean;
  transcript: string | null;
  message: string;
};

export type HarvestOutcome = {
  skill_id: string;
  status: ShelfStatus;
  message: string;
  rows: number;
  teach: boolean;
};

export const api = {
  getSetupState: () => invoke<SetupState>("get_setup_state"),
  updateSetup: (patch: SetupState) => invoke<void>("update_setup", { patch }),
  completeSetup: () => invoke<void>("complete_setup"),
  chromeStatus: () => invoke<ChromeStatus>("chrome_status"),
  otaEnabled: () => invoke<boolean>("ota_enabled"),
  listOtaChannels: () => invoke<OtaChannel[]>("list_ota_channels"),
  getCatalog: () => invoke<CatalogView>("get_catalog"),
  getBranding: () => invoke<Branding>("get_branding"),
  voiceListen: () => invoke<VoiceOutcome>("voice_listen"),
  autoHarvestEnabled: () => invoke<boolean>("auto_harvest_enabled"),
  harvestNow: (skillId?: string) =>
    invoke<HarvestOutcome>("harvest_now", { skillId: skillId ?? null }),
  beginTeach: (skillId: string) => invoke<TeachView>("begin_teach", { skillId }),
  cancelTeach: () => invoke<TeachView>("cancel_teach"),
  openApp: (serviceId: string, focus: GuideFocus) =>
    invoke<void>("open_app", { serviceId, focus }),
  openChromeUrl: (
    serviceId: string,
    url: string,
    title: string,
    focus: GuideFocus,
  ) => invoke<void>("open_chrome_url", { serviceId, url, title, focus }),
  playOta: (channel: string, conf: string, focus: GuideFocus) =>
    invoke<void>("play_ota", { channel, conf, focus }),
  playbackStatus: () => invoke<PlaybackStatus>("playback_status"),
  remoteBack: () => invoke<void>("remote_back"),
  remotePlayPause: () => invoke<void>("remote_play_pause"),
  remoteHome: () => invoke<void>("remote_home"),
  open1Password: (focus: GuideFocus) =>
    invoke<void>("open_onepassword_extension", { focus }),
};

export function onFocusRestore(cb: (focus: GuideFocus) => void) {
  return listen<GuideFocus>("focus-restore", (e) => cb(e.payload));
}

export function onPlaybackChanged(cb: (status: PlaybackStatus) => void) {
  return listen<PlaybackStatus>("playback-changed", (e) => cb(e.payload));
}

export function onCatalogChanged(cb: (catalog: CatalogView) => void) {
  return listen<CatalogView>("catalog-changed", (e) => cb(e.payload));
}
