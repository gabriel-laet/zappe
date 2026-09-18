/** ATONGX / ATONG-style living-room remote. No keyboard required. */

const BACK = new Set([
  "Escape",
  "Backspace",
  "BrowserBack",
  "GoBack",
  "Back",
]);
const HOME = new Set(["Home", "BrowserHome"]);
const ACTIVATE = new Set(["Enter", "NumpadEnter", "Select"]);
const PLAY = new Set([
  "Space",
  "MediaPlayPause",
  "MediaPlay",
  "MediaPause",
]);
const VOICE = new Set(["ColorF0Red", "F9", "F8"]);
const MENU = new Set(["ContextMenu", "F1", "AudioVolumeMute"]);

function codeOf(e: KeyboardEvent): string {
  return e.code || e.key;
}

export const atongx = {
  isLeft: (e: KeyboardEvent) => codeOf(e) === "ArrowLeft",
  isRight: (e: KeyboardEvent) => codeOf(e) === "ArrowRight",
  isUp: (e: KeyboardEvent) => codeOf(e) === "ArrowUp",
  isDown: (e: KeyboardEvent) => codeOf(e) === "ArrowDown",
  isActivate: (e: KeyboardEvent) => ACTIVATE.has(codeOf(e)),
  isBack: (e: KeyboardEvent) => BACK.has(codeOf(e)),
  isHome: (e: KeyboardEvent) => HOME.has(codeOf(e)),
  isPlayPause: (e: KeyboardEvent) => PLAY.has(codeOf(e)),
  isVoice: (e: KeyboardEvent) =>
    VOICE.has(codeOf(e)) || e.key === "ColorF0Red" || e.keyCode === 403,
  isMenu: (e: KeyboardEvent) => MENU.has(codeOf(e)),
};
