import { useEffect, useState } from "react";
import { Toaster } from "sonner";
import { BrandingProvider } from "@/components/BrandingProvider";
import { Home } from "@/components/Home";
import { SetupWizard } from "@/components/SetupWizard";
import { Splash } from "@/components/Splash";
import { useRemote } from "@/hooks/useRemote";
import {
  applyBrandingCss,
  DEFAULT_BRANDING,
  type Branding,
} from "@/lib/branding";
import { api } from "@/lib/tauri";

const SPLASH_MIN_MS = 900;

/** Vite-only preview: `?guide=1` skips first-run; `?brand=example` uses sample wordmark/accent; `?splash=1` holds the boot screen. */
function webPreview(): { forceGuide: boolean; branding: Branding | null } {
  if (!import.meta.env.DEV || typeof window === "undefined") {
    return { forceGuide: false, branding: null };
  }
  const params = new URLSearchParams(window.location.search);
  const branding =
    params.get("brand") === "example"
      ? {
          ...DEFAULT_BRANDING,
          name: "Living Room",
          accent: "#E8A84C",
          tagline: "What are we watching?",
          source: "user" as const,
        }
      : null;
  return { forceGuide: params.get("guide") === "1", branding };
}

export default function App() {
  const [branding, setBranding] = useState<Branding>(DEFAULT_BRANDING);
  const [brandingReady, setBrandingReady] = useState(false);
  const [setupReady, setSetupReady] = useState(false);
  const [setupDone, setSetupDone] = useState(false);
  const [minSplashDone, setMinSplashDone] = useState(false);

  useEffect(() => {
    applyBrandingCss(DEFAULT_BRANDING);
    const timer = window.setTimeout(() => setMinSplashDone(true), SPLASH_MIN_MS);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    const preview = webPreview();
    void api
      .getBranding()
      .then((next) => {
        setBranding(next);
        applyBrandingCss(next);
      })
      .catch((err) => {
        console.warn("branding: falling back to defaults", err);
        const fallback = preview.branding ?? DEFAULT_BRANDING;
        setBranding(fallback);
        applyBrandingCss(fallback);
      })
      .finally(() => setBrandingReady(true));
  }, []);

  useEffect(() => {
    const preview = webPreview();
    void api
      .getSetupState()
      .then((s) => {
        setSetupDone(s.completed);
        setSetupReady(true);
      })
      .catch(() => {
        setSetupDone(preview.forceGuide);
        setSetupReady(true);
      });
  }, []);

  const holdSplash =
    import.meta.env.DEV &&
    typeof window !== "undefined" &&
    new URLSearchParams(window.location.search).get("splash") === "1";
  const booting = !brandingReady || !setupReady || !minSplashDone || holdSplash;
  useRemote(!booting);

  return (
    <BrandingProvider branding={branding}>
      <Toaster theme="dark" position="top-center" richColors />
      {booting ? (
        <Splash />
      ) : setupDone ? (
        <Home />
      ) : (
        <SetupWizard onComplete={() => setSetupDone(true)} />
      )}
    </BrandingProvider>
  );
}
