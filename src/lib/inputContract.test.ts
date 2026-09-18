import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  ATONGX_BINDINGS,
  ATONGX_MAP,
  classifyAtongx,
  classifyAtongxEvkey,
  classifyAtongxKeyName,
} from "./remoteMap.ts";
import { parseVoicePtBr } from "./voicePtBr.ts";

function key(code: string, extra: Partial<KeyboardEvent> = {}): KeyboardEvent {
  return { code, key: extra.key ?? code, keyCode: extra.keyCode ?? 0 } as KeyboardEvent;
}

describe("classifyAtongx", () => {
  it("maps leftover webview KeyboardEvent.code values", () => {
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

describe("classifyAtongxEvkey", () => {
  it("maps the XING WEI Consumer Control / keyboard EV_KEY codes", () => {
    assert.equal(classifyAtongxEvkey(103), "up");
    assert.equal(classifyAtongxEvkey(108), "down");
    assert.equal(classifyAtongxEvkey(105), "left");
    assert.equal(classifyAtongxEvkey(106), "right");
    assert.equal(classifyAtongxEvkey(28), "ok");
    assert.equal(classifyAtongxEvkey(158), "back");
    assert.equal(classifyAtongxEvkey(1), "back");
    assert.equal(classifyAtongxEvkey(172), "home");
    assert.equal(classifyAtongxEvkey(164), "playpause");
    assert.equal(classifyAtongxEvkey(127), "menu");
    assert.equal(classifyAtongxEvkey(139), "menu");
    assert.equal(classifyAtongxEvkey(115), "volumeUp");
    assert.equal(classifyAtongxEvkey(114), "volumeDown");
    assert.equal(classifyAtongxEvkey(113), "mute");
    assert.equal(classifyAtongxEvkey(116), "power");
    assert.equal(classifyAtongxEvkey(111), "delete");
    assert.equal(classifyAtongxEvkey(217), "voice");
    assert.equal(classifyAtongxEvkey(582), "voice");
    assert.equal(classifyAtongxEvkey(999), null);
  });

  it("accepts KEY_* names from the capture script", () => {
    assert.equal(classifyAtongxKeyName("KEY_BACK"), "back");
    assert.equal(classifyAtongxKeyName("HOMEPAGE"), "home");
    assert.equal(classifyAtongxKeyName("playpause"), "playpause");
  });
});

describe("atongx-map.json", () => {
  it("is the single living-room contract", () => {
    assert.equal(ATONGX_MAP.device.usb.vendor, "2320");
    assert.equal(ATONGX_MAP.device.usb.product, "0912");
    assert.ok(ATONGX_MAP.device.nodes.some((n) => n.iface === "consumer" && n.grab));
    assert.ok(ATONGX_MAP.device.nodes.some((n) => n.iface === "keyboard" && !n.grab));
    const actions = new Set(ATONGX_BINDINGS.map((b) => b.action));
    for (const needed of [
      "up",
      "down",
      "left",
      "right",
      "ok",
      "back",
      "home",
      "menu",
      "playpause",
      "voice",
      "volumeUp",
      "mute",
      "power",
    ]) {
      assert.ok(actions.has(needed), `missing ${needed}`);
    }
    const back = ATONGX_BINDINGS.find((b) => b.action === "back");
    assert.equal(back?.keys[0]?.name, "KEY_BACK");
    assert.equal(back?.keys[0]?.code, 158);
    const needsDevice = ATONGX_BINDINGS.flatMap((b) =>
      b.keys.filter((k) => k.confidence === "needs_device").map((k) => `${b.action}:${k.name}`),
    );
    assert.ok(needsDevice.includes("voice:KEY_SEARCH"));
    assert.ok(needsDevice.includes("pointer:KEY_F2"));
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
