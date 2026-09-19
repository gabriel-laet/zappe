import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  companionStatusLabel,
  COMPANION_HOST,
  GUIDE_SOURCES,
  isCompanionConnected,
  qrSvgMarkup,
} from "./companion.ts";
import { pickOtaChannel, primaryOtaChannel } from "./otaDisplay.ts";
import {
  ATONGX_BINDINGS,
  ATONGX_MAP,
  classifyAtongx,
  classifyAtongxEvkey,
  classifyAtongxKeyName,
} from "./remoteMap.ts";
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
    assert.equal(classifyAtongx(key("F8")), "voice");
    assert.equal(classifyAtongx(key("Voice")), "voice");
    assert.equal(classifyAtongx(key("MediaRecord")), "voice");
    assert.equal(classifyAtongx(key("ColorF0Red", { key: "ColorF0Red", keyCode: 403 })), "voice");
    assert.equal(classifyAtongx(key("AudioVolumeUp")), "volumeUp");
    assert.equal(classifyAtongx(key("AudioVolumeDown")), "volumeDown");
    assert.equal(classifyAtongx(key("AudioVolumeMute")), "mute");
    assert.equal(classifyAtongx(key("Delete")), "delete");
    assert.equal(classifyAtongx(key("F2")), "pointer");
    assert.equal(classifyAtongx(key("F6")), "pointer");
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
    assert.equal(classifyAtongxEvkey(583), "voice");
    assert.equal(classifyAtongxEvkey(60), "pointer");
    assert.equal(classifyAtongxEvkey(530), "pointer");
    assert.equal(classifyAtongxEvkey(999), null);
  });

  it("accepts KEY_* names from the capture script", () => {
    assert.equal(classifyAtongxKeyName("KEY_BACK"), "back");
    assert.equal(classifyAtongxKeyName("HOMEPAGE"), "home");
    assert.equal(classifyAtongxKeyName("playpause"), "playpause");
    assert.equal(classifyAtongxKeyName("KEY_SEARCH"), "voice");
    assert.equal(classifyAtongxKeyName("KEY_VOICECOMMAND"), "voice");
    assert.equal(classifyAtongxKeyName("KEY_F2"), "pointer");
    assert.equal(classifyAtongxKeyName("KEY_TOUCHPAD_TOGGLE"), "pointer");
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
      "pageUp",
      "pageDown",
      "voice",
      "pointer",
      "volumeUp",
      "mute",
      "power",
    ]) {
      assert.ok(actions.has(needed), `missing ${needed}`);
    }
    const back = ATONGX_BINDINGS.find((b) => b.action === "back");
    assert.equal(back?.keys[0]?.name, "KEY_BACK");
    assert.equal(back?.keys[0]?.code, 158);
  });

  it("locks Mic and Pointer scancodes and documents them", () => {
    const voice = ATONGX_BINDINGS.find((b) => b.action === "voice");
    const pointer = ATONGX_BINDINGS.find((b) => b.action === "pointer");
    assert.equal(voice?.button, "Mic (red)");
    assert.match(voice?.note ?? "", /Locked/);
    assert.match(pointer?.note ?? "", /Locked/);
    const voiceLocked = (voice?.keys ?? []).filter((k) => k.confidence === "locked");
    const pointerLocked = (pointer?.keys ?? []).filter((k) => k.confidence === "locked");
    assert.ok(voiceLocked.some((k) => k.name === "KEY_SEARCH" && k.code === 217));
    assert.ok(voiceLocked.some((k) => k.name === "KEY_VOICECOMMAND" && k.code === 582));
    assert.ok(pointerLocked.some((k) => k.name === "KEY_F2" && k.code === 60));
    assert.ok(pointerLocked.some((k) => k.name === "KEY_TOUCHPAD_TOGGLE" && k.code === 530));
    assert.ok((voice?.keys ?? []).some((k) => k.webCodes.includes("F9")));
    assert.ok((pointer?.keys ?? []).some((k) => k.webCodes.includes("F2")));
    assert.equal(
      ATONGX_BINDINGS.find((b) => b.action === "up")?.dispatch,
      "focus",
      "D-pad must stay focus (not evdev-grabbed)",
    );
    assert.equal(ATONGX_BINDINGS.find((b) => b.action === "ok")?.dispatch, "focus");
    assert.equal(voice?.dispatch, "global");
    assert.equal(pointer?.dispatch, "global");
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
    assert.deepEqual(parseVoicePtBr("Record"), { type: "ota", query: "record" });
    assert.deepEqual(parseVoicePtBr("início"), { type: "home" });
    assert.deepEqual(parseVoicePtBr("pausar"), { type: "playpause" });
    assert.deepEqual(parseVoicePtBr("sincronizar"), { type: "sync" });
    assert.equal(parseVoicePtBr("conte uma piada"), null);
  });
});

describe("pickOtaChannel", () => {
  const channels = [
    { name: "Globo HD", source: "/home/tv/channels.conf" },
    { name: "Record HD", source: "/home/tv/channels.conf" },
    { name: "SBT HD", source: "/home/tv/channels.conf" },
    { name: "TV Cultura", source: "/home/tv/channels.conf" },
  ];

  it("resolves Brazilian couch names onto the HD list", () => {
    assert.equal(pickOtaChannel(channels, "Globo")?.name, "Globo HD");
    assert.equal(pickOtaChannel(channels, "globo")?.name, "Globo HD");
    assert.equal(pickOtaChannel(channels, "Record")?.name, "Record HD");
    assert.equal(pickOtaChannel(channels, "sbt")?.name, "SBT HD");
    assert.equal(pickOtaChannel(channels, "cultura")?.name, "TV Cultura");
    assert.equal(pickOtaChannel(channels, "nope"), null);
    assert.equal(primaryOtaChannel(channels)?.name, "Globo HD");
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
