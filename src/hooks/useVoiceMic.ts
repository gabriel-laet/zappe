import { useEffect, useState } from "react";
import { pickOtaChannel } from "@/lib/otaDisplay";
import { api, onVoiceArm, type GuideFocus } from "@/lib/tauri";
import { atongx } from "@/lib/remoteMap";
import { parseVoicePtBr } from "@/lib/voicePtBr";

const noopFocus: GuideFocus = { shelf_id: "apps", index: 0 };

export function useVoiceMic(enabled: boolean) {
  const [message, setMessage] = useState<string | null>(null);

  useEffect(() => {
    if (!enabled) return;

    const run = async () => {
      setMessage("Ouvindo…");
      try {
        const out = await api.voiceListen();
        const intent = out.transcript ? parseVoicePtBr(out.transcript) : null;
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
              setMessage(
                status?.error ?? `Canal ${intent.query} não está no channels.conf`,
              );
            }
          }
        } else {
          setMessage(out.message);
        }
      } catch (err) {
        setMessage(
          String(err).includes("invoke")
            ? "Whisper pt-BR — botão vermelho no controle"
            : String(err),
        );
      }
      window.setTimeout(() => setMessage(null), 2400);
    };

    const onKey = (e: KeyboardEvent) => {
      if (!atongx.isVoice(e)) return;
      e.preventDefault();
      void run();
    };
    window.addEventListener("keydown", onKey);
    let unlistenArm: (() => void) | undefined;
    void onVoiceArm(() => {
      void run();
    }).then((fn) => {
      unlistenArm = fn;
    });
    return () => {
      window.removeEventListener("keydown", onKey);
      unlistenArm?.();
    };
  }, [enabled]);

  return message;
}
