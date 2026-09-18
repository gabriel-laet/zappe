import { useEffect, useState } from "react";
import { toast } from "sonner";
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
    <div className="flex h-full flex-col items-center justify-center gap-10 bg-background px-12 text-center">
      <div className="max-w-2xl space-y-4">
        <h1 className="text-4xl font-semibold tracking-tight">Welcome to Zappe</h1>
        <p className="text-xl text-muted-foreground">
          Streaming plays in a gamescope nest wrapping Google Chrome (your Zappe
          profile). Live TV appears on Home when{" "}
          <code className="text-base">channels.conf</code> is found — no extra setup step.
        </p>
      </div>

      {step === "browser" && (
        <div className="space-y-6">
          <h2 className="text-2xl">Browser required</h2>
          <p className="max-w-lg text-muted-foreground">
            Zappe launches a dedicated Chrome profile inside gamescope — no CDP,
            no automation flags. Netflix Continue Watching is harvested through
            the accessibility tree into this guide.
          </p>
          <p className="text-sm text-muted-foreground">
            {chromeOk ? "Chrome detected — you can continue." : "Chrome not detected yet."}
          </p>
          <div className="flex flex-wrap justify-center gap-4">
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
        <div className="space-y-6">
          <h2 className="text-2xl">1Password (optional)</h2>
          <p className="max-w-lg text-muted-foreground">
            Open the extension store in Zappe Chrome. Zappe never reads your vault or cookies.
          </p>
          <div className="flex flex-wrap justify-center gap-4">
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
        <div className="space-y-6">
          <h2 className="text-2xl">Sign in to your accounts (optional)</h2>
          <p className="max-w-lg text-muted-foreground">
            Sign in inside the Chrome nest once per service so harvest can see
            Continue Watching. Or go straight to Home — TV aberta and Canais
            stay on the guide when your channel list is present.
          </p>
          <div className="flex flex-wrap justify-center gap-4">
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
