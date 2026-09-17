import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

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
};

export type OtaChannel = { name: string; source: string };

export type PlaybackStatus = {
  surface: "idle" | "chrome" | "ota";
  focus: GuideFocus | null;
};

export const api = {
  getSetupState: () => invoke<SetupState>("get_setup_state"),
  updateSetup: (patch: SetupState) => invoke<void>("update_setup", { patch }),
  completeSetup: () => invoke<void>("complete_setup"),
  chromeStatus: () => invoke<ChromeStatus>("chrome_status"),
  otaEnabled: () => invoke<boolean>("ota_enabled"),
  listOtaChannels: () => invoke<OtaChannel[]>("list_ota_channels"),
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
