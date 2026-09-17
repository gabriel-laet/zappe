import { useEffect, useState } from "react";
import { toast } from "sonner";
import { Button } from "@/components/ui/button";
import { api, type GuideFocus, type SetupState } from "@/lib/tauri";

type Step = "browser" | "onepassword" | "accounts" | "done";

const noopFocus: GuideFocus = { shelf_id: "apps", index: 0 };

export function SetupWizard({ onComplete }: { onComplete: () => void }) {
  const [step, setStep] = useState<Step>("browser");
  const [setup, setSetup] = useState<SetupState | null>(null);
  const [chromeOk, setChromeOk] = useState(false);

  useEffect(() => {
    void api.getSetupState().then(setSetup);
    void api.chromeStatus().then((s) => setChromeOk(s.available));
  }, []);

  const persist = async (patch: SetupState) => {
    setSetup(patch);
    await api.updateSetup(patch);
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
    const patch = {
      ...(setup ?? {
        completed: false,
        browser_ack: false,
        onepassword_skipped: false,
        accounts_done: false,
      }),
      browser_ack: true,
    };
    await persist(patch);
    setStep("onepassword");
  };

  const skip1Password = async () => {
    const patch = {
      ...(setup!),
      onepassword_skipped: true,
    };
    await persist(patch);
    setStep("accounts");
  };

  const open1Password = async () => {
    try {
      await api.open1Password(noopFocus);
      toast.message("Install 1Password in Zappe Chrome, then press Back to return.");
    } catch (e) {
      toast.error(String(e));
    }
  };

  const openAccounts = async () => {
    try {
      await api.openApp("netflix", noopFocus);
      toast.message("Sign in to your services in Zappe Chrome. Back returns here.");
    } catch (e) {
      toast.error(String(e));
    }
  };

  const finish = async () => {
    const patch = { ...(setup!), accounts_done: true, completed: true };
    await persist(patch);
    await api.completeSetup();
    onComplete();
  };

  return (
    <div className="flex h-full flex-col items-center justify-center gap-10 bg-background px-12 text-center">
      <div className="max-w-2xl space-y-4">
        <h1 className="text-4xl font-semibold tracking-tight">Welcome to Zappe</h1>
        <p className="text-xl text-muted-foreground">
          A TV-style guide for streaming in your own Chrome profile — plus live TV when configured.
        </p>
      </div>

      {step === "browser" && (
        <div className="space-y-6">
          <h2 className="text-2xl">Browser required</h2>
          <p className="max-w-lg text-muted-foreground">
            Zappe orchestrates a dedicated Chrome user-data-dir. Netflix, Prime, Disney+, and YouTube
            always play there — never in a webview.
          </p>
          <p className="text-sm text-muted-foreground">
            {chromeOk ? "Chrome detected on this system." : "Chrome not detected yet."}
          </p>
          <Button size="lg" onClick={() => void nextFromBrowser()}>
            Continue
          </Button>
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
          </div>
        </div>
      )}

      {step === "accounts" && (
        <div className="space-y-6">
          <h2 className="text-2xl">Sign in to your accounts</h2>
          <p className="max-w-lg text-muted-foreground">
            Use orchestrated Chrome once per service. Sessions stay in the Zappe profile.
          </p>
          <div className="flex flex-wrap justify-center gap-4">
            <Button size="lg" onClick={() => void openAccounts()}>
              Open Netflix in Chrome
            </Button>
            <Button size="lg" variant="secondary" onClick={() => setStep("done")}>
              I&apos;m already signed in
            </Button>
          </div>
        </div>
      )}

      {step === "done" && (
        <div className="space-y-6">
          <h2 className="text-2xl">You&apos;re set</h2>
          <Button size="lg" onClick={() => void finish()}>
            Go to Home
          </Button>
        </div>
      )}
    </div>
  );
}
