import { useEffect, useState } from "react";
import { api, type GuideFocus } from "@/lib/tauri";
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
        } else if (intent?.type === "ota") {
          const channels = await api.listOtaChannels().catch(() => []);
          const hit = channels.find((c) =>
            c.name.toLowerCase().includes(intent.query.toLowerCase()),
          );
          if (hit) {
            await api.playOta(hit.name, hit.source, noopFocus);
            setMessage(hit.name);
          } else {
            setMessage(`Canal ${intent.query} — próximo`);
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
    return () => window.removeEventListener("keydown", onKey);
  }, [enabled]);

  return message;
}
