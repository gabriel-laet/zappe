import { useEffect, useState } from "react";
import { Toaster } from "sonner";
import { BootSplash } from "@/components/BootSplash";
import { Home } from "@/components/Home";
import { SetupWizard } from "@/components/SetupWizard";
import { BrandingContext } from "@/hooks/useBranding";
import { useRemote } from "@/hooks/useRemote";
import {
  applyBranding,
  DEFAULT_BRANDING,
  mergeBranding,
  viteBrandingPatch,
  type BrandingView,
} from "@/lib/branding";
import { api, type CatalogView, type OtaChannel } from "@/lib/tauri";

const SPLASH_MIN_MS = 1200;
const BOOT_CAP_MS = 4000;

const EMPTY_CATALOG: CatalogView = {
  shelves: [],
  teach: { active: false, skill_id: null, message: null },
};

function previewFlag(value: string): boolean {
  return (
    Boolean(import.meta.env.DEV) &&
    new URLSearchParams(window.location.search).get("preview") === value
  );
}

function settle<T>(promise: Promise<T>, fallback: T, ms = BOOT_CAP_MS): Promise<T> {
  return new Promise((resolve) => {
    const timer = window.setTimeout(() => resolve(fallback), ms);
    promise
      .then((value) => {
        window.clearTimeout(timer);
        resolve(value);
      })
      .catch(() => {
        window.clearTimeout(timer);
        resolve(fallback);
      });
  });
}

export default function App() {
  const [ready, setReady] = useState(false);
  const [setupDone, setSetupDone] = useState(false);
  const [branding, setBranding] = useState<BrandingView>(
    mergeBranding(DEFAULT_BRANDING, viteBrandingPatch()),
  );
  const [initialCatalog, setInitialCatalog] = useState<CatalogView>(EMPTY_CATALOG);
  const [initialOta, setInitialOta] = useState<OtaChannel[]>([]);

  useEffect(() => {
    applyBranding(branding);
  }, [branding]);

  useEffect(() => {
    let cancelled = false;
    const minSplash = new Promise<void>((resolve) => {
      window.setTimeout(resolve, SPLASH_MIN_MS);
    });

    void (async () => {
      const [completed, remote, catalog, ota] = await Promise.all([
        settle(
          api.getSetupState().then((s) => s.completed),
          previewFlag("home"),
        ),
        settle(api.getBranding(), DEFAULT_BRANDING),
        settle(api.getCatalog(), EMPTY_CATALOG),
        settle(api.listOtaChannels(), []),
        minSplash,
      ]);
      if (cancelled) return;
      setBranding(
        mergeBranding(mergeBranding(DEFAULT_BRANDING, remote), viteBrandingPatch()),
      );
      setSetupDone(completed);
      setInitialCatalog(catalog);
      setInitialOta(ota);
      if (!previewFlag("splash")) {
        setReady(true);
      }
    })();

    return () => {
      cancelled = true;
    };
  }, []);

  useRemote(ready);

  if (!ready) {
    return (
      <BrandingContext.Provider value={branding}>
        <BootSplash />
      </BrandingContext.Provider>
    );
  }

  return (
    <BrandingContext.Provider value={branding}>
      <Toaster
        theme="dark"
        position="top-center"
        richColors
        toastOptions={{ className: "tv-toast" }}
      />
      {setupDone ? (
        <Home initialCatalog={initialCatalog} initialOta={initialOta} />
      ) : (
        <SetupWizard onComplete={() => setSetupDone(true)} />
      )}
    </BrandingContext.Provider>
  );
}
