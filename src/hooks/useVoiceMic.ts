import { useEffect, useState } from "react";
import { pickOtaChannel } from "@/lib/otaDisplay";
import { api, onVoiceArm, onVoiceRelease, type GuideFocus } from "@/lib/tauri";
import { atongx } from "@/lib/remoteMap";
import { parseVoicePtBr, type VoiceIntent } from "@/lib/voicePtBr";

const noopFocus: GuideFocus = { shelf_id: "apps", index: 0 };
const TAP_MS = 350;
const TAP_LISTEN_MS = 4000;
const MAX_LISTEN_MS = 6500;

export function useVoiceMic(enabled: boolean) {
  const [message, setMessage] = useState<string | null>(null);

  useEffect(() => {
    if (!enabled) return;

    let busy = false;
    let listening = false;
    let startedAt = 0;
    let tapTimer: number | undefined;
    let maxTimer: number | undefined;
    let hideTimer: number | undefined;

    const hideLater = () => {
      window.clearTimeout(hideTimer);
      hideTimer = window.setTimeout(() => setMessage(null), 2400);
    };

    const applyIntent = async (intent: VoiceIntent | null, fallback: string) => {
      if (intent?.type === "home") {
        await api.remoteHome();
        setMessage("Início");
      } else if (intent?.type === "back") {
        await api.remoteBack();
        setMessage("Voltar");
      } else if (intent?.type === "playpause") {
        await api.remotePlayPause();
        setMessage("Play / pause");
      } else if (intent?.type === "open") {
        await api.openApp(intent.service, noopFocus);
        setMessage(intent.service);
      } else if (intent?.type === "volume") {
        await api.remoteVolume(intent.delta);
        setMessage(intent.delta > 0 ? "Volume +" : "Volume −");
      } else if (intent?.type === "mute") {
        await api.remoteMute();
        setMessage("Mudo");
      } else if (intent?.type === "sync") {
        setMessage("Sincronizando…");
        const out = await api.harvestNow();
        setMessage(out.message || "Sync");
      } else if (intent?.type === "connect") {
        await api.remoteMenu();
        setMessage("Connect");
      } else if (intent?.type === "ota") {
        const status = await api.otaStatus().catch(() => null);
        const channels = status?.channels ?? [];
        if (status?.error && channels.length === 0) {
          setMessage(status.error);
        } else {
          const hit = pickOtaChannel(channels, intent.query);
          if (hit) {
            await api.playOta(hit.name, hit.source, noopFocus);
            setMessage(hit.name);
          } else {
            setMessage(status?.error ?? `Canal ${intent.query} não está no channels.conf`);
          }
        }
      } else {
        setMessage(fallback);
      }
    };

    const finish = async () => {
      if (!listening || busy) return;
      busy = true;
      listening = false;
      window.clearTimeout(tapTimer);
      window.clearTimeout(maxTimer);
      setMessage("…");
      try {
        const out = await api.voiceEnd();
        const intent = out.transcript ? parseVoicePtBr(out.transcript) : null;
        await applyIntent(intent, out.message);
      } catch (err) {
        setMessage(
          String(err).includes("invoke")
            ? "Whisper pt-BR — botão vermelho no controle"
            : String(err),
        );
      }
      busy = false;
      hideLater();
    };

    const start = async () => {
      if (listening || busy) return;
      listening = true;
      startedAt = Date.now();
      window.clearTimeout(hideTimer);
      window.clearTimeout(maxTimer);
      setMessage("Ouvindo…");
      try {
        const out = await api.voiceBegin();
        if (!out.armed) {
          listening = false;
          setMessage(out.message);
          hideLater();
          return;
        }
        if (out.transcript) {
          listening = false;
          const intent = parseVoicePtBr(out.transcript);
          await applyIntent(intent, out.message);
          hideLater();
          return;
        }
      } catch (err) {
        listening = false;
        setMessage(
          String(err).includes("invoke")
            ? "Whisper pt-BR — botão vermelho no controle"
            : String(err),
        );
        hideLater();
        return;
      }
      maxTimer = window.setTimeout(() => {
        void finish();
      }, MAX_LISTEN_MS);
    };

    const onRelease = () => {
      if (!listening || busy) return;
      const held = Date.now() - startedAt;
      window.clearTimeout(tapTimer);
      if (held < TAP_MS) {
        tapTimer = window.setTimeout(() => {
          void finish();
        }, Math.max(200, TAP_LISTEN_MS - held));
        return;
      }
      void finish();
    };

    const onKey = (e: KeyboardEvent) => {
      if (!atongx.isVoice(e)) return;
      e.preventDefault();
      if (e.type === "keyup") {
        onRelease();
        return;
      }
      if (e.repeat) return;
      void start();
    };

    window.addEventListener("keydown", onKey);
    window.addEventListener("keyup", onKey);
    let unlistenArm: (() => void) | undefined;
    let unlistenRelease: (() => void) | undefined;
    void onVoiceArm(() => {
      void start();
    }).then((fn) => {
      unlistenArm = fn;
    });
    void onVoiceRelease(() => {
      onRelease();
    }).then((fn) => {
      unlistenRelease = fn;
    });
    return () => {
      window.removeEventListener("keydown", onKey);
      window.removeEventListener("keyup", onKey);
      window.clearTimeout(tapTimer);
      window.clearTimeout(maxTimer);
      window.clearTimeout(hideTimer);
      unlistenArm?.();
      unlistenRelease?.();
    };
  }, [enabled]);

  return message;
}
