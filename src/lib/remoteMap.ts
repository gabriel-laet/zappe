/**
 * ATONGX / XING WEI air-mouse contract (living-room remote).
 *
 * Source of truth: [`atongx-map.json`](./atongx-map.json) — physical button →
 * linux KEY_* / EV_KEY → `AtongxAction`. The guide still classifies
 * KeyboardEvent.code when a key reaches the webview. Under gamescope the
 * Consumer Control keys (Back / Home / Play-Pause / Menu) do not register as
 * Tauri global shortcuts; Rust reads those from evdev instead.
 *
 * This is Zappe HID mapping — not a Samsung TV API.
 */

import rawMap from "./atongx-map.json" with { type: "json" };

export type AtongxAction =
  | "up"
  | "down"
  | "left"
  | "right"
  | "ok"
  | "back"
  | "home"
  | "menu"
  | "playpause"
  | "pageUp"
  | "pageDown"
  | "voice"
  | "volumeUp"
  | "volumeDown"
  | "mute"
  | "power"
  | "pointer"
  | "delete";

export type AtongxDispatch = "global" | "focus";
export type AtongxIface = "keyboard" | "mouse" | "consumer" | "system";
export type AtongxConfidence = "hid" | "alias" | "needs_device";

export type AtongxKey = {
  name: string;
  code: number;
  iface: AtongxIface;
  confidence: AtongxConfidence;
  webCodes: string[];
};

export type AtongxBinding = {
  button: string;
  action: AtongxAction;
  dispatch: AtongxDispatch;
  note?: string;
  keys: AtongxKey[];
};

export type AtongxMap = {
  version: number;
  device: {
    product: string;
    aliases: string[];
    usb: { vendor: string; product: string };
    matchNames: string[];
    nodes: {
      id: string;
      byId: string;
      eventHint: string;
      iface: AtongxIface;
      grab: boolean;
      note: string;
    }[];
  };
  bindings: AtongxBinding[];
};

export const ATONGX_MAP = rawMap as AtongxMap;
export const ATONGX_BINDINGS: readonly AtongxBinding[] = ATONGX_MAP.bindings;

const ACTIONS = new Set<string>(ATONGX_BINDINGS.map((b) => b.action));

function isAction(value: string): value is AtongxAction {
  return ACTIONS.has(value);
}

const WEB_TO_ACTION = new Map<string, AtongxAction>();
const EVKEY_TO_ACTION = new Map<number, AtongxAction>();
const KEYNAME_TO_ACTION = new Map<string, AtongxAction>();

for (const binding of ATONGX_BINDINGS) {
  KEYNAME_TO_ACTION.set(binding.action, binding.action);
  for (const key of binding.keys) {
    if (!EVKEY_TO_ACTION.has(key.code)) {
      EVKEY_TO_ACTION.set(key.code, binding.action);
    }
    KEYNAME_TO_ACTION.set(key.name, binding.action);
    KEYNAME_TO_ACTION.set(key.name.replace(/^KEY_/, ""), binding.action);
    for (const web of key.webCodes) {
      if (!WEB_TO_ACTION.has(web)) {
        WEB_TO_ACTION.set(web, binding.action);
      }
    }
  }
}

function codeOf(e: KeyboardEvent): string {
  return e.code || e.key;
}

/** Classify a DOM key event (guide / Connect, or leftover HID → webview). */
export function classifyAtongx(e: KeyboardEvent): AtongxAction | null {
  const byCode = WEB_TO_ACTION.get(codeOf(e));
  if (byCode) return byCode;
  const byKey = WEB_TO_ACTION.get(e.key);
  if (byKey) return byKey;
  // Chrome TV leftover: ColorF0Red used keyCode 403. Linux KEY_RED is 398.
  if (e.keyCode === 403) return "voice";
  if (typeof e.keyCode === "number" && e.keyCode > 0) {
    const byLegacy = classifyAtongxEvkey(e.keyCode);
    if (byLegacy) return byLegacy;
  }
  return null;
}

/** Classify a linux EV_KEY code (the evdev / capture-script path). */
export function classifyAtongxEvkey(code: number): AtongxAction | null {
  return EVKEY_TO_ACTION.get(code) ?? null;
}

/** Classify `KEY_BACK`, `BACK`, or an `AtongxAction` name. */
export function classifyAtongxKeyName(name: string): AtongxAction | null {
  const trimmed = name.trim();
  if (isAction(trimmed)) return trimmed;
  return KEYNAME_TO_ACTION.get(trimmed) ?? KEYNAME_TO_ACTION.get(trimmed.toUpperCase()) ?? null;
}

export function bindingForAction(action: AtongxAction): AtongxBinding | undefined {
  return ATONGX_BINDINGS.find((b) => b.action === action);
}

export const atongx = {
  classify: classifyAtongx,
  classifyEvkey: classifyAtongxEvkey,
  classifyKeyName: classifyAtongxKeyName,
  isLeft: (e: KeyboardEvent) => classifyAtongx(e) === "left",
  isRight: (e: KeyboardEvent) => classifyAtongx(e) === "right",
  isUp: (e: KeyboardEvent) => classifyAtongx(e) === "up",
  isDown: (e: KeyboardEvent) => classifyAtongx(e) === "down",
  isActivate: (e: KeyboardEvent) => classifyAtongx(e) === "ok",
  isBack: (e: KeyboardEvent) => classifyAtongx(e) === "back" || classifyAtongx(e) === "delete",
  isHome: (e: KeyboardEvent) => classifyAtongx(e) === "home",
  isPlayPause: (e: KeyboardEvent) => classifyAtongx(e) === "playpause",
  isVoice: (e: KeyboardEvent) => classifyAtongx(e) === "voice",
  isMenu: (e: KeyboardEvent) => classifyAtongx(e) === "menu",
  isPageUp: (e: KeyboardEvent) => classifyAtongx(e) === "pageUp",
  isPageDown: (e: KeyboardEvent) => classifyAtongx(e) === "pageDown",
};

/** Human table for README / capture prompts. */
export const ATONGX_BUTTONS: {
  action: AtongxAction;
  button: string;
  status: "wired" | "stub";
  dispatch: AtongxDispatch;
}[] = ATONGX_BINDINGS.map((b) => ({
  action: b.action,
  button: b.button,
  status: "wired" as const,
  dispatch: b.dispatch,
}));
