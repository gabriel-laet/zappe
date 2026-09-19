import assert from "node:assert/strict";
import { describe, it } from "node:test";
import { parseVoicePtBr, VOICE_LEXICON } from "./voicePtBr.ts";

describe("parseVoicePtBr", () => {
  it("documents the couch lexicon", () => {
    assert.ok(VOICE_LEXICON.some((line) => /Netflix/i.test(line)));
    assert.ok(VOICE_LEXICON.some((line) => /Globo/i.test(line)));
    assert.ok(VOICE_LEXICON.some((line) => /sincronizar/i.test(line)));
  });

  it("maps apps, transport, and volume", () => {
    assert.deepEqual(parseVoicePtBr("abrir Netflix"), { type: "open", service: "netflix" });
    assert.deepEqual(parseVoicePtBr("abre a netflix"), { type: "open", service: "netflix" });
    assert.deepEqual(parseVoicePtBr("Netflix"), { type: "open", service: "netflix" });
    assert.deepEqual(parseVoicePtBr("prime video"), { type: "open", service: "prime" });
    assert.deepEqual(parseVoicePtBr("abrir Disney+"), { type: "open", service: "disney" });
    assert.deepEqual(parseVoicePtBr("You Tube"), { type: "open", service: "youtube" });
    assert.deepEqual(parseVoicePtBr("voltar"), { type: "back" });
    assert.deepEqual(parseVoicePtBr("volta"), { type: "back" });
    assert.deepEqual(parseVoicePtBr("início"), { type: "home" });
    assert.deepEqual(parseVoicePtBr("tela inicial"), { type: "home" });
    assert.deepEqual(parseVoicePtBr("ir para o guia"), { type: "home" });
    assert.deepEqual(parseVoicePtBr("volume mais"), { type: "volume", delta: 1 });
    assert.deepEqual(parseVoicePtBr("aumentar o volume"), { type: "volume", delta: 1 });
    assert.deepEqual(parseVoicePtBr("volume menos"), { type: "volume", delta: -1 });
    assert.deepEqual(parseVoicePtBr("abaixa o volume"), { type: "volume", delta: -1 });
    assert.deepEqual(parseVoicePtBr("mudo"), { type: "mute" });
    assert.deepEqual(parseVoicePtBr("silêncio"), { type: "mute" });
    assert.deepEqual(parseVoicePtBr("pausar"), { type: "playpause" });
    assert.deepEqual(parseVoicePtBr("continuar"), { type: "playpause" });
    assert.deepEqual(parseVoicePtBr("play"), { type: "playpause" });
  });

  it("maps Brazilian TV aberta names onto OTA queries", () => {
    assert.deepEqual(parseVoicePtBr("ir para Globo"), { type: "ota", query: "globo" });
    assert.deepEqual(parseVoicePtBr("canal SBT"), { type: "ota", query: "sbt" });
    assert.deepEqual(parseVoicePtBr("Record"), { type: "ota", query: "record" });
    assert.deepEqual(parseVoicePtBr("vai pra Band"), { type: "ota", query: "band" });
    assert.deepEqual(parseVoicePtBr("passa para a Cultura"), { type: "ota", query: "cultura" });
    assert.deepEqual(parseVoicePtBr("canal da globo"), { type: "ota", query: "globo" });
  });

  it("maps sync and connect without treating them as chat", () => {
    assert.deepEqual(parseVoicePtBr("sincronizar"), { type: "sync" });
    assert.deepEqual(parseVoicePtBr("sync"), { type: "sync" });
    assert.deepEqual(parseVoicePtBr("atualizar"), { type: "sync" });
    assert.deepEqual(parseVoicePtBr("conectar"), { type: "connect" });
    assert.deepEqual(parseVoicePtBr("telefone"), { type: "connect" });
  });

  it("rejects chatter the couch grammar does not own", () => {
    assert.equal(parseVoicePtBr("conte uma piada"), null);
    assert.equal(parseVoicePtBr("qual o clima"), null);
    assert.equal(parseVoicePtBr(""), null);
    assert.equal(parseVoicePtBr("   "), null);
  });
});
