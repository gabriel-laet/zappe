import { useEffect, useState } from "react";

const ACTIVITY = [
  "keydown",
  "pointermove",
  "pointerdown",
  "wheel",
  "touchstart",
] as const;

/** True after `ms` of no remote / pointer activity. Any input clears it instantly. */
export function useIdle(ms: number, enabled: boolean): boolean {
  const [idle, setIdle] = useState(false);

  useEffect(() => {
    if (!enabled || ms <= 0) {
      setIdle(false);
      return;
    }
    let timer = 0;
    const bump = () => {
      setIdle(false);
      window.clearTimeout(timer);
      timer = window.setTimeout(() => setIdle(true), ms);
    };
    ACTIVITY.forEach((evt) =>
      window.addEventListener(evt, bump, { passive: true, capture: true }),
    );
    bump();
    return () => {
      window.clearTimeout(timer);
      ACTIVITY.forEach((evt) =>
        window.removeEventListener(evt, bump, { capture: true } as EventListenerOptions),
      );
    };
  }, [enabled, ms]);

  return idle;
}
