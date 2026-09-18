import { useEffect, useState } from "react";
import { toast } from "sonner";
import { BrandMark } from "@/components/BrandMark";
import { useBrand } from "@/components/BrandingProvider";
import { Button } from "@/components/ui/button";
import { api, type GuideFocus, type SetupState } from "@/lib/tauri";

type Step = "browser" | "onepassword" | "accounts";

const noopFocus: GuideFocus = { shelf_id: "apps", index: 0 };

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

  const goHome = async (opts?: { accountsDone?: boolean }) => {
    const patch = {
      ...setup,
      browser_ack: true,
      accounts_done: opts?.accountsDone ?? setup.accounts_done,
      completed: true,
    };
    await persist(patch);
    await api.completeSetup();
    onComplete();
  };

  const nextFromBrowser = async () => {
    const status = await api.chromeStatus();
    if (!status.available) {
      toast.error("Install Google Chrome or Chromium", {
        description:
          "Arch / Omarchy: sudo pacman -S google-chrome or chromium. Or set CHROME_PATH.",
      });
      return;
    }
    await persist({ ...setup, browser_ack: true });
    setStep("onepassword");
  };

  const skip1Password = async () => {
    await persist({ ...setup, onepassword_skipped: true });
    setStep("accounts");
  };

  const open1Password = async () => {
    try {
      await api.open1Password(noopFocus);
      toast.message("Install 1Password in the Chrome nest, then press Back to return.");
    } catch (e) {
      toast.error(String(e));
    }
  };

  const openAccounts = async () => {
    try {
      await api.openApp("netflix", noopFocus);
      toast.message("Sign in inside the Chrome nest. Back returns here — then Continue to Home.");
    } catch (e) {
      toast.error(String(e));
    }
  };

  return (
    <div className="tv-page items-center justify-center text-center">
      <div className="max-w-4xl space-y-[var(--tv-space-3)]">
        <BrandMark size="lg" className="justify-center" />
        <h1 className="tv-display">Welcome to {brand.name}</h1>
        <p className="tv-body text-muted-foreground">
          Streaming plays in a gamescope nest wrapping Google Chrome (your Zappe
          profile). Live TV appears on Home when{" "}
          <code className="tv-caption">channels.conf</code> is found — no extra setup step.
        </p>
      </div>

      {step === "browser" && (
        <div className="mt-[var(--tv-space-4)] space-y-[var(--tv-space-3)]">
          <h2 className="tv-title">Browser required</h2>
          <p className="tv-body mx-auto max-w-3xl text-muted-foreground">
            Zappe launches a dedicated Chrome profile inside gamescope — no CDP,
            no automation flags. Netflix Continue Watching is harvested through
            the accessibility tree into this guide.
          </p>
          <p className="tv-caption">
            {chromeOk ? "Chrome detected — you can continue." : "Chrome not detected yet."}
          </p>
          <div className="flex flex-wrap justify-center gap-[var(--tv-space-2)]">
            <Button size="lg" onClick={() => void nextFromBrowser()}>
              Continue
            </Button>
            {chromeOk && (
              <Button size="lg" variant="secondary" onClick={() => void goHome()}>
                Skip to Home
              </Button>
            )}
          </div>
        </div>
      )}

      {step === "onepassword" && (
        <div className="mt-[var(--tv-space-4)] space-y-[var(--tv-space-3)]">
          <h2 className="tv-title">1Password (optional)</h2>
          <p className="tv-body mx-auto max-w-3xl text-muted-foreground">
            Open the extension store in Zappe Chrome. Zappe never reads your vault or cookies.
          </p>
          <div className="flex flex-wrap justify-center gap-[var(--tv-space-2)]">
            <Button size="lg" onClick={() => void open1Password()}>
              Open extension store
            </Button>
            <Button size="lg" variant="secondary" onClick={() => void skip1Password()}>
              Skip
            </Button>
            <Button size="lg" variant="ghost" onClick={() => void goHome()}>
              Continue to Home
            </Button>
          </div>
        </div>
      )}

      {step === "accounts" && (
        <div className="mt-[var(--tv-space-4)] space-y-[var(--tv-space-3)]">
          <h2 className="tv-title">Sign in to your accounts (optional)</h2>
          <p className="tv-body mx-auto max-w-3xl text-muted-foreground">
            Sign in inside the Chrome nest once per service so harvest can see
            Continue Watching. Or go straight to Home — TV aberta and Canais
            stay on the guide when your channel list is present.
          </p>
          <div className="flex flex-wrap justify-center gap-[var(--tv-space-2)]">
            <Button size="lg" onClick={() => void goHome({ accountsDone: true })}>
              Continue to Home
            </Button>
            <Button size="lg" variant="secondary" onClick={() => void openAccounts()}>
              Open Netflix in the nest
            </Button>
          </div>
        </div>
      )}
    </div>
  );
}
