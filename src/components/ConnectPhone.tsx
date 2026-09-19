import { useCallback, useEffect, useRef, useState } from "react";
import { toast } from "sonner";
import { Button } from "@/components/ui/button";
import {
  COMPANION_HOST,
  companionStatusLabel,
  EMPTY_SESSION,
  isCompanionConnected,
  qrSvgMarkup,
  sessionSources,
} from "@/lib/companion";
import { atongx } from "@/lib/remoteMap";
import { api, type CompanionSession } from "@/lib/tauri";
import { cn } from "@/lib/utils";

export { COMPANION_HOST };

export function ConnectVisual({
  session,
  onSelectSource,
}: {
  session: CompanionSession;
  onSelectSource?: (id: string) => void;
}) {
  const qr = qrSvgMarkup(session.qr_svg);
  const hostLabel =
    session.port && session.port !== 80
      ? `${session.host}:${session.port}`
      : session.host || COMPANION_HOST;
  const sources = sessionSources(session);

  return (
    <>
      <div className="tv-source-row" role="listbox" aria-label="Account">
        {sources.map((src) => (
          <button
            key={src.id}
            type="button"
            role="option"
            aria-selected={src.id === session.source}
            className={cn(
              "tv-source-chip",
              src.id === session.source && "tv-source-chip-on",
            )}
            onClick={() => onSelectSource?.(src.id)}
          >
            {src.label}
            {src.connected ? " · on" : src.wired ? "" : " · soon"}
          </button>
        ))}
      </div>
      <p className="tv-caption mt-[var(--tv-space-2)]">On your phone, open</p>
      <p className="tv-host">{hostLabel}</p>
      <p className="tv-device-code" aria-label="Device code">
        {session.display_code}
      </p>
      {qr ? (
        <div
          className="tv-qr"
          aria-hidden
          dangerouslySetInnerHTML={{ __html: qr }}
        />
      ) : (
        <div className="tv-qr-stub" aria-hidden>
          <span>QR</span>
        </div>
      )}
      <p className="tv-kicker">{companionStatusLabel(session)}</p>
      <p className="tv-body max-w-3xl text-muted-foreground">{session.message}</p>
    </>
  );
}

export function ConnectPhone({
  onClose,
  onConnected,
  source,
}: {
  onClose: () => void;
  onConnected?: () => void;
  source?: string;
}) {
  const [session, setSession] = useState<CompanionSession>(EMPTY_SESSION);

  useEffect(() => {
    let cancelled = false;
    const tick = async () => {
      try {
        const next = await api.companionSession();
        if (!cancelled) setSession(next);
      } catch {
        /* web preview / companion starting */
      }
    };
    void (async () => {
      try {
        const next = source
          ? await api.companionSelectSource(source)
          : await api.companionSession();
        if (!cancelled) setSession(next);
      } catch {
        if (!cancelled) setSession(EMPTY_SESSION);
      }
    })();
    const id = window.setInterval(() => void tick(), 1500);
    return () => {
      cancelled = true;
      window.clearInterval(id);
    };
  }, [source]);

  const harvestedForStatus = useRef<string | null>(null);

  useEffect(() => {
    if (!isCompanionConnected(session.status)) {
      harvestedForStatus.current = null;
      return;
    }
    // catalog-changed used to recreate onClose and re-enter this effect,
    // stacking harvest_now + Chrome. One attempt per connected session.
    if (harvestedForStatus.current === session.status) return;
    harvestedForStatus.current = session.status;
    let timer = 0;
    const run = async () => {
      onConnected?.();
      if (session.harvest_skill) {
        try {
          const out = await api.harvestNow(session.harvest_skill);
          toast.message(out.message);
        } catch (err) {
          toast.message(String(err));
        }
      }
      timer = window.setTimeout(onClose, 1400);
    };
    void run();
    return () => window.clearTimeout(timer);
  }, [onClose, onConnected, session.harvest_skill, session.status]);

  const pickSource = useCallback(async (id: string) => {
    try {
      setSession(await api.companionSelectSource(id));
    } catch {
      setSession((s) => ({ ...s, source: id, source_label: id }));
    }
  }, []);

  const cycleSource = useCallback(
    (dir: number) => {
      const sources = sessionSources(session);
      if (!sources.length) return;
      const i = Math.max(0, sources.findIndex((s) => s.id === session.source));
      const next = sources[(i + dir + sources.length) % sources.length];
      void pickSource(next.id);
    },
    [pickSource, session],
  );

  const dismiss = useCallback(() => {
    onClose();
  }, [onClose]);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (atongx.isLeft(e)) {
        e.preventDefault();
        e.stopPropagation();
        cycleSource(-1);
        return;
      }
      if (atongx.isRight(e)) {
        e.preventDefault();
        e.stopPropagation();
        cycleSource(1);
        return;
      }
      if (
        e.code === "Escape" ||
        e.code === "BrowserBack" ||
        e.code === "Backspace" ||
        e.code === "Enter" ||
        e.code === "NumpadEnter"
      ) {
        e.preventDefault();
        e.stopPropagation();
        dismiss();
      }
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [cycleSource, dismiss]);

  return (
    <div className="tv-page tv-scroll items-center text-center" role="dialog" aria-modal>
      <h1 className="tv-display">Connect account</h1>
      <p className="tv-body mt-[var(--tv-space-2)] max-w-4xl text-muted-foreground">
        Pick a service, then scan the QR. Sign-in stays on your phone — Chrome
        stays hidden. Netflix is ready tonight; the others use the same path.
      </p>
      <ConnectVisual session={session} onSelectSource={(id) => void pickSource(id)} />
      <Button
        size="lg"
        className={cn("mt-[var(--tv-space-3)] focus-tile")}
        onClick={dismiss}
      >
        Back
      </Button>
    </div>
  );
}
