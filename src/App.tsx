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
    void api
      .getBranding()
      .then((next) => {
        setBranding(next);
        applyBrandingCss(next);
      })
      .catch((err) => {
        console.warn("branding: falling back to defaults", err);
        setBranding(DEFAULT_BRANDING);
        applyBrandingCss(DEFAULT_BRANDING);
      })
      .finally(() => setBrandingReady(true));
  }, []);

  useEffect(() => {
    void api
      .getSetupState()
      .then((s) => {
        setSetupDone(s.completed);
        setSetupReady(true);
      })
      .catch(() => setSetupReady(true));
  }, []);

  const booting = !brandingReady || !setupReady || !minSplashDone;
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
