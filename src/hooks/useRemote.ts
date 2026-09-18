import { useEffect } from "react";
import { toast } from "sonner";
import { api } from "@/lib/tauri";
import { classifyAtongx } from "@/lib/remoteMap";

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
        void api
          .remoteVolume(1)
          .then(() => toast.message("Volume +"))
          .catch(() => toast.message("Volume +"));
        return;
      }
      if (action === "volumeDown") {
        void api
          .remoteVolume(-1)
          .then(() => toast.message("Volume −"))
          .catch(() => toast.message("Volume −"));
        return;
      }
      if (action === "mute") {
        void api
          .remoteMute()
          .then(() => toast.message("Mudo"))
          .catch(() => toast.message("Mudo"));
        return;
      }
      if (action === "menu") {
        window.dispatchEvent(new Event("zappe-menu"));
        void api.remoteMenu().catch(() => undefined);
        return;
      }
      if (action === "power") {
        void api
          .remotePower()
          .then((out) => {
            toast.message(out === "power:home" ? "Início" : "Power — o aparelho continua ligado");
          })
          .catch(() => toast.message("Power — o aparelho continua ligado"));
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [enabled]);
}
