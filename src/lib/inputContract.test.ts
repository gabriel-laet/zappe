import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { classifyAtongx } from "./remoteMap.ts";
import { tvControlToast, type TvControlEvent } from "./tvControl.ts";
import { parseVoicePtBr } from "./voicePtBr.ts";

function key(code: string, extra: Partial<KeyboardEvent> = {}): KeyboardEvent {
  return { code, key: extra.key ?? code, keyCode: extra.keyCode ?? 0 } as KeyboardEvent;
}

describe("classifyAtongx", () => {
  it("maps the living-room ATONGX board", () => {
    assert.equal(classifyAtongx(key("ArrowUp")), "up");
    assert.equal(classifyAtongx(key("ArrowDown")), "down");
    assert.equal(classifyAtongx(key("ArrowLeft")), "left");
    assert.equal(classifyAtongx(key("ArrowRight")), "right");
    assert.equal(classifyAtongx(key("Enter")), "ok");
    assert.equal(classifyAtongx(key("Escape")), "back");
    assert.equal(classifyAtongx(key("BrowserBack")), "back");
    assert.equal(classifyAtongx(key("Home")), "home");
    assert.equal(classifyAtongx(key("BrowserHome")), "home");
    assert.equal(classifyAtongx(key("ContextMenu")), "menu");
    assert.equal(classifyAtongx(key("F1")), "menu");
    assert.equal(classifyAtongx(key("Space")), "playpause");
    assert.equal(classifyAtongx(key("MediaPlayPause")), "playpause");
    assert.equal(classifyAtongx(key("PageUp")), "pageUp");
    assert.equal(classifyAtongx(key("PageDown")), "pageDown");
    assert.equal(classifyAtongx(key("F9")), "voice");
    assert.equal(classifyAtongx(key("ColorF0Red", { key: "ColorF0Red", keyCode: 403 })), "voice");
    assert.equal(classifyAtongx(key("AudioVolumeUp")), "volumeUp");
    assert.equal(classifyAtongx(key("AudioVolumeDown")), "volumeDown");
    assert.equal(classifyAtongx(key("AudioVolumeMute")), "mute");
    assert.equal(classifyAtongx(key("Delete")), "delete");
    assert.equal(classifyAtongx(key("F2")), "pointer");
    assert.equal(classifyAtongx(key("Power")), "power");
  });
});

describe("tvControlToast", () => {
  it("uses the Samsung fallback copy once, then the short label", () => {
    const fallback: TvControlEvent = {
      action: "volume+",
      via: "pactl",
      message: "TV Samsung offline (192.168.3.6:8001) — volume no PulseAudio do aparelho.",
      first_fallback: true,
    };
    assert.match(tvControlToast(fallback), /Samsung/);
    assert.match(tvControlToast(fallback), /PulseAudio/);
    assert.equal(
      tvControlToast({ ...fallback, first_fallback: false, message: "Volume +" }),
      "Volume +",
    );
    assert.equal(
      tvControlToast({ action: "mute", via: "samsung", message: "Mudo", first_fallback: false }),
      "Mudo",
    );
  });
});

describe("parseVoicePtBr", () => {
  it("accepts the small couch grammar", () => {
    assert.deepEqual(parseVoicePtBr("abrir Netflix"), { type: "open", service: "netflix" });
    assert.deepEqual(parseVoicePtBr("voltar"), { type: "back" });
    assert.deepEqual(parseVoicePtBr("volume mais"), { type: "volume", delta: 1 });
    assert.deepEqual(parseVoicePtBr("volume menos"), { type: "volume", delta: -1 });
    assert.deepEqual(parseVoicePtBr("mudo"), { type: "mute" });
    assert.deepEqual(parseVoicePtBr("ir para Globo"), { type: "ota", query: "globo" });
    assert.deepEqual(parseVoicePtBr("canal SBT"), { type: "ota", query: "sbt" });
    assert.deepEqual(parseVoicePtBr("início"), { type: "home" });
    assert.deepEqual(parseVoicePtBr("pausar"), { type: "playpause" });
    assert.equal(parseVoicePtBr("conte uma piada"), null);
  });
});
