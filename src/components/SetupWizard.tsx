import { useCallback, useEffect, useMemo, useState } from "react";
import { toast } from "sonner";
import { BrandMark } from "@/components/BrandMark";
import { useBrand } from "@/components/BrandingProvider";
import { COMPANION_HOST } from "@/components/ConnectPhone";
import { Button } from "@/components/ui/button";
import { useTvChoice } from "@/hooks/useTvChoice";
import { cn } from "@/lib/utils";
import { api, type SetupState } from "@/lib/tauri";

type Step = "browser" | "phone";

const emptySetup: SetupState = {
  completed: false,
  browser_ack: false,
  onepassword_skipped: false,
  accounts_done: false,
};

export function SetupWizard({ onComplete }: { onComplete: () => void }) {
  const brand = useBrand();
  const [step, setStep] = useState<Step>("browser");
  const [setup, setSetup] = useState<SetupState>(emptySetup);
  const [chromeOk, setChromeOk] = useState(false);

  useEffect(() => {
    void api.getSetupState().then(setSetup);
    void api.chromeStatus().then((s) => setChromeOk(s.available));
  }, []);

  const persist = async (patch: SetupState) => {
    setSetup(patch);
    await api.updateSetup(patch);
  };

  const goHome = useCallback(async () => {
    const patch = {
      ...setup,
      browser_ack: true,
      onepassword_skipped: true,
      completed: true,
    };
    await persist(patch);
    await api.completeSetup();
    onComplete();
  }, [onComplete, setup]);

  const nextFromBrowser = useCallback(async () => {
    const status = await api.chromeStatus();
    if (!status.available) {
      toast.error("Chrome is not on this TV yet", {
        description:
          "Install on the appliance (not from the couch): sudo pacman -S google-chrome — or set CHROME_PATH.",
      });
      return;
    }
    await persist({ ...setup, browser_ack: true });
    setStep("phone");
  }, [setup]);

  const browserActions = useMemo(() => {
    const actions = [{ id: "continue", label: "Continue", run: () => void nextFromBrowser() }];
    if (chromeOk) {
      actions.push({ id: "skip", label: "Skip to Home", run: () => void goHome() });
    }
    return actions;
  }, [chromeOk, goHome, nextFromBrowser]);

  const phoneActions = useMemo(
    () => [{ id: "home", label: "Continue to Home", run: () => void goHome() }],
    [goHome],
  );

  const actions = step === "browser" ? browserActions : phoneActions;
  const onActivate = useCallback(
    (index: number) => {
      actions[index]?.run();
    },
    [actions],
  );
  const focus = useTvChoice(actions.length, onActivate);

  return (
    <div className="tv-page tv-scroll items-center text-center">
      <div className="max-w-4xl space-y-[var(--tv-space-2)]">
        <BrandMark className="justify-center" />
        <h1 className="tv-display">Welcome to {brand.name}</h1>
        <p className="tv-body text-muted-foreground">
          This TV is remote- and phone-only — no keyboard. Streaming plays in a
          gamescope nest wrapping Chrome. Live TV appears on Home when{" "}
          <code className="tv-caption">channels.conf</code> is found.
        </p>
      </div>

      {step === "browser" && (
        <div className="mt-[var(--tv-space-3)] space-y-[var(--tv-space-2)]">
          <h2 className="tv-title">Browser on this TV</h2>
          <p className="tv-body mx-auto max-w-3xl text-muted-foreground">
            Chrome must already be installed on the appliance. You will not type
            passwords here. Accounts are connected from your phone.
          </p>
          <p className="tv-caption">
            {chromeOk ? "Chrome detected — you can continue." : "Chrome not detected yet."}
          </p>
        </div>
      )}

      {step === "phone" && (
        <div className="mt-[var(--tv-space-3)] space-y-[var(--tv-space-2)]">
          <h2 className="tv-title">Connect account (phone)</h2>
          <p className="tv-body mx-auto max-w-3xl text-muted-foreground">
            On the couch, open your phone — not a password field on the TV.
          </p>
          <p className="tv-caption">On your phone, open</p>
          <p className="tv-host">{COMPANION_HOST}</p>
          <div className="mx-auto tv-qr-stub" aria-hidden>
            <span>QR</span>
          </div>
          <p className="tv-caption mx-auto max-w-3xl">
            Netflix-style device code + QR land in a follow-up (phone companion).
            This step is the placeholder so setup never asks you to type.
          </p>
        </div>
      )}

      <div className="mt-[var(--tv-space-3)] flex flex-wrap justify-center gap-[var(--tv-space-2)]">
        {actions.map((action, i) => (
          <Button
            key={action.id}
            size="lg"
            variant={i === 0 ? "default" : "secondary"}
            className={cn(focus === i && "focus-tile")}
            onClick={action.run}
          >
            {action.label}
          </Button>
        ))}
      </div>
    </div>
  );
}
