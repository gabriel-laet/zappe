import { useEffect } from "react";
import { api } from "@/lib/tauri";

export function useRemote(enabled: boolean) {
  useEffect(() => {
    if (!enabled) return;

    const onKey = (e: KeyboardEvent) => {
      const code = e.code;
      if (
        code === "Escape" ||
        code === "BrowserBack" ||
        code === "Backspace"
      ) {
        e.preventDefault();
        void api.remoteBack();
        return;
      }
      if (code === "Home" || code === "BrowserHome") {
        e.preventDefault();
        void api.remoteHome();
        return;
      }
      if (code === "Space" || code === "MediaPlayPause") {
        e.preventDefault();
        void api.remotePlayPause();
      }
    };

    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [enabled]);
}
