/**
 * ATONGX air-mouse contract (living-room remote).
 *
 * Every physical button has a named action in the guide / nest. Volume and
 * mute call `pactl`. Menu opens Connect. Power returns to the guide and
 * never shuts the box down. Pointer toggles the CSS cursor on the guide
 * and a real nest cursor while Chrome is playing (see nest-input.md).
 *
 * Cheap ATONGX HID maps vary; we accept common KeyboardEvent.code aliases.
 */

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

const SETS: Record<AtongxAction, ReadonlySet<string>> = {
  up: new Set(["ArrowUp"]),
  down: new Set(["ArrowDown"]),
  left: new Set(["ArrowLeft"]),
  right: new Set(["ArrowRight"]),
  ok: new Set(["Enter", "NumpadEnter", "Select"]),
  back: new Set(["Escape", "BrowserBack", "GoBack", "Back"]),
  home: new Set(["Home", "BrowserHome"]),
  menu: new Set(["ContextMenu", "F1", "LaunchApp1"]),
  playpause: new Set(["Space", "MediaPlayPause", "MediaPlay", "MediaPause"]),
  pageUp: new Set(["PageUp"]),
  pageDown: new Set(["PageDown"]),
  voice: new Set(["ColorF0Red", "F9", "F8", "Red"]),
  volumeUp: new Set(["AudioVolumeUp", "VolumeUp", "XF86AudioRaiseVolume"]),
  volumeDown: new Set(["AudioVolumeDown", "VolumeDown", "XF86AudioLowerVolume"]),
  mute: new Set(["AudioVolumeMute", "VolumeMute", "XF86AudioMute"]),
  power: new Set(["Power", "PowerOff", "Sleep", "XF86PowerOff"]),
  pointer: new Set(["F2", "F6", "F7"]),
  delete: new Set(["Delete", "Backspace"]),
};

function codeOf(e: KeyboardEvent): string {
  return e.code || e.key;
}

export function classifyAtongx(e: KeyboardEvent): AtongxAction | null {
  const code = codeOf(e);
  if (e.key === "ColorF0Red" || e.keyCode === 403) return "voice";
  for (const [action, codes] of Object.entries(SETS) as [AtongxAction, ReadonlySet<string>][]) {
    if (codes.has(code) || codes.has(e.key)) return action;
  }
  return null;
}

export const atongx = {
  classify: classifyAtongx,
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

/** Human table for README / next-PR wiring. */
export const ATONGX_BUTTONS: { action: AtongxAction; button: string; status: "wired" | "stub" }[] =
  [
    { action: "power", button: "Power", status: "wired" },
    { action: "playpause", button: "Play / Pause", status: "wired" },
    { action: "pointer", button: "Air-mouse cursor toggle", status: "wired" },
    { action: "up", button: "D-pad Up", status: "wired" },
    { action: "down", button: "D-pad Down", status: "wired" },
    { action: "left", button: "D-pad Left", status: "wired" },
    { action: "right", button: "D-pad Right", status: "wired" },
    { action: "ok", button: "OK (center / orange ring)", status: "wired" },
    { action: "home", button: "Home", status: "wired" },
    { action: "back", button: "Back", status: "wired" },
    { action: "menu", button: "Menu", status: "wired" },
    { action: "pageUp", button: "PAGE up", status: "wired" },
    { action: "pageDown", button: "PAGE down", status: "wired" },
    { action: "voice", button: "Mic (red)", status: "wired" },
    { action: "volumeUp", button: "VOL +", status: "wired" },
    { action: "volumeDown", button: "VOL −", status: "wired" },
    { action: "delete", button: "DEL", status: "wired" },
    { action: "mute", button: "Mute", status: "wired" },
  ];
