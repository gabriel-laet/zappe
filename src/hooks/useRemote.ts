import { useEffect } from "react";
import { api } from "@/lib/tauri";
import { atongx } from "@/lib/remoteMap";

export function useRemote(enabled: boolean) {
  useEffect(() => {
    if (!enabled) return;

    const onKey = (e: KeyboardEvent) => {
      if (atongx.isVoice(e)) return;
      if (atongx.isBack(e)) {
        e.preventDefault();
        void api.remoteBack();
        return;
      }
      if (atongx.isHome(e)) {
        e.preventDefault();
        void api.remoteHome();
        return;
      }
      if (atongx.isPlayPause(e)) {
        e.preventDefault();
        void api.remotePlayPause();
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [enabled]);
}
