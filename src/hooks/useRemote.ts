import { useEffect } from "react";
import { toast } from "sonner";
import { api } from "@/lib/tauri";
import { classifyAtongx } from "@/lib/remoteMap";

function stub(label: string, run: () => Promise<string>) {
  void run()
    .then(() => toast.message(label))
    .catch(() => toast.message(`${label} — próximo`));
}

export function useRemote(enabled: boolean) {
  useEffect(() => {
    if (!enabled) return;

    const onKey = (e: KeyboardEvent) => {
      const action = classifyAtongx(e);
      if (!action || action === "voice") return;
      if (
        action === "up" ||
        action === "down" ||
        action === "left" ||
        action === "right" ||
        action === "ok" ||
        action === "pageUp" ||
        action === "pageDown"
      ) {
        return;
      }

      e.preventDefault();
      if (action === "back" || action === "delete") {
        void api.remoteBack();
        return;
      }
      if (action === "home") {
        void api.remoteHome();
        return;
      }
      if (action === "playpause") {
        void api.remotePlayPause();
        return;
      }
      if (action === "pointer") {
        document.documentElement.classList.toggle("tv-pointer-on");
        const on = document.documentElement.classList.contains("tv-pointer-on");
        toast.message(on ? "Ponteiro" : "Controle");
        return;
      }
      if (action === "volumeUp") {
        stub("Volume +", () => api.remoteVolume(1));
        return;
      }
      if (action === "volumeDown") {
        stub("Volume −", () => api.remoteVolume(-1));
        return;
      }
      if (action === "mute") {
        stub("Mudo", () => api.remoteMute());
        return;
      }
      if (action === "menu") {
        stub("Menu", () => api.remoteMenu());
        return;
      }
      if (action === "power") {
        stub("Power", () => api.remotePower());
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [enabled]);
}
