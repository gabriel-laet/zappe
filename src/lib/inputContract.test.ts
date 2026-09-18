import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  companionStatusLabel,
  COMPANION_HOST,
  GUIDE_SOURCES,
  isCompanionConnected,
  qrSvgMarkup,
} from "./companion.ts";
import { classifyAtongx } from "./remoteMap.ts";
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

describe("companion connect", () => {
  it("keeps login on the phone host", () => {
    assert.equal(COMPANION_HOST, "zappe-tv.local");
    assert.equal(isCompanionConnected("connected"), true);
    assert.equal(isCompanionConnected("waiting"), false);
    assert.equal(companionStatusLabel({
      code: "W7K2MQ4P",
      display_code: "W7K2-MQ4P",
      status: "waiting",
      message: "Open this on your phone.",
      public_url: "http://zappe-tv.local",
      claim_url: "http://zappe-tv.local/c/W7K2MQ4P?s=netflix",
      qr_svg: "<svg></svg>",
      host: "zappe-tv.local",
      port: 80,
      expires_in_secs: 60,
      nest_hidden: true,
      accounts_connected: false,
      source: "netflix",
      source_label: "Netflix",
      source_wired: true,
      harvest_skill: "netflix.continue_watching.v1",
      sources: [
        { id: "netflix", label: "Netflix", wired: true, connected: false },
        { id: "prime", label: "Prime Video", wired: false, connected: false },
      ],
    }), "Waiting for Netflix");
    assert.equal(qrSvgMarkup("<svg><rect/></svg>"), "<svg><rect/></svg>");
    assert.equal(qrSvgMarkup("<svg></svg><script>alert(1)</script>"), "<svg></svg>");
    assert.deepEqual(
      GUIDE_SOURCES.map((s) => s.id),
      ["netflix", "prime", "disney", "youtube"],
    );
    assert.equal(GUIDE_SOURCES[0].wired, true);
    assert.equal(GUIDE_SOURCES[1].wired, false);
  });
});
