import { useEffect, useState } from "react";
import { Toaster } from "sonner";
import { BrandingProvider } from "@/components/BrandingProvider";
import { Home } from "@/components/Home";
import { Screensaver } from "@/components/Screensaver";
import { SetupWizard } from "@/components/SetupWizard";
import { Splash } from "@/components/Splash";
import { VoiceListen } from "@/components/VoiceListen";
import { useIdle } from "@/hooks/useIdle";
import { useRemote } from "@/hooks/useRemote";
import { useVoiceMic } from "@/hooks/useVoiceMic";
import {
  applyBrandingCss,
  DEFAULT_BRANDING,
  normalizeBranding,
  type Branding,
} from "@/lib/branding";
import { api } from "@/lib/tauri";

const SPLASH_MIN_MS = 900;

/** Vite-only: `?guide=1` Home, `?brand=feras` name/theme, `?splash=1` hold splash, `?idle=3` screensaver in 3s. */
function webPreview(): { forceGuide: boolean; branding: Branding | null; idleSec: number | null } {
  if (!import.meta.env.DEV || typeof window === "undefined") {
    return { forceGuide: false, branding: null, idleSec: null };
  }
  const params = new URLSearchParams(window.location.search);
  const branding =
    params.get("brand") === "feras" || params.get("brand") === "example"
      ? {
          ...DEFAULT_BRANDING,
          name: "Feras TV",
          source: "user" as const,
        }
      : null;
  const idleRaw = params.get("idle");
  const idleSec = idleRaw ? Number(idleRaw) : null;
  return {
    forceGuide: params.get("guide") === "1",
    branding,
    idleSec: idleSec && idleSec > 0 ? idleSec : null,
  };
}

export default function App() {
  const [branding, setBranding] = useState<Branding>(DEFAULT_BRANDING);
  const [brandingReady, setBrandingReady] = useState(false);
  const [setupReady, setSetupReady] = useState(false);
  const [setupDone, setSetupDone] = useState(false);
  const [minSplashDone, setMinSplashDone] = useState(false);

  useEffect(() => {
    if (!import.meta.env.DEV) {
      document.documentElement.classList.add("tv-living-room");
    }
    applyBrandingCss(DEFAULT_BRANDING);
    const timer = window.setTimeout(() => setMinSplashDone(true), SPLASH_MIN_MS);
    return () => window.clearTimeout(timer);
  }, []);

  useEffect(() => {
    const preview = webPreview();
    void api
      .getBranding()
      .then((next) => {
        const resolved = normalizeBranding(next);
        setBranding(resolved);
        applyBrandingCss(resolved);
      })
      .catch((err) => {
        console.warn("branding: falling back to defaults", err);
        const fallback = normalizeBranding(preview.branding ?? DEFAULT_BRANDING);
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
  const voiceMessage = useVoiceMic(!booting);

  const previewIdle = webPreview().idleSec;
  const idleMs =
    previewIdle != null
      ? previewIdle * 1000
      : branding.idle_timeout_seconds * 1000;
  const idleOn =
    !booting && branding.idle_mode === "screensaver" && idleMs > 0;
  const screensaver = useIdle(idleMs, idleOn);

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
      {voiceMessage && <VoiceListen message={voiceMessage} />}
      {screensaver && <Screensaver />}
    </BrandingProvider>
  );
}
