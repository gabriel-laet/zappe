import { useEffect } from "react";
import { Button } from "@/components/ui/button";
import { classifyAtongx } from "@/lib/remoteMap";
import { cn } from "@/lib/utils";

export const COMPANION_HOST = "zappe-tv.local";

export function ConnectPhone({ onClose }: { onClose: () => void }) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const action = classifyAtongx(e);
      if (action === "back" || action === "delete" || action === "ok") {
        e.preventDefault();
        e.stopPropagation();
        onClose();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [onClose]);

  return (
    <div className="tv-page tv-scroll items-center text-center" role="dialog" aria-modal>
      <h1 className="tv-display">Connect account</h1>
      <p className="tv-body mt-[var(--tv-space-2)] max-w-4xl text-muted-foreground">
        This TV has no keyboard. Sign in on your phone — never type a password
        on the couch.
      </p>

      <p className="tv-caption mt-[var(--tv-space-3)]">On your phone, open</p>
      <p className="tv-host">{COMPANION_HOST}</p>

      <div className="tv-qr-stub" aria-hidden>
        <span>QR</span>
      </div>
      <p className="tv-body max-w-3xl text-muted-foreground">
        A Netflix-style device code and QR will show here when the phone
        companion ships. Until then, this screen is the reminder — no on-TV
        typing.
      </p>

      <Button
        size="lg"
        className={cn("mt-[var(--tv-space-3)] focus-tile")}
        onClick={onClose}
      >
        Back
      </Button>
    </div>
  );
}
