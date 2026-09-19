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
  examplePreviewBranding,
  loadDevDataDirBranding,
  normalizeBranding,
  type Branding,
} from "@/lib/branding";
import { api } from "@/lib/tauri";

const SPLASH_MIN_MS = 900;

/** Vite-only: `?guide=1` Home, `?brand=example` name/theme, `?splash=1` hold splash, `?idle=3` screensaver in 3s. */
function webPreview(): { forceGuide: boolean; wantExample: boolean; idleSec: number | null } {
  if (!import.meta.env.DEV || typeof window === "undefined") {
    return { forceGuide: false, wantExample: false, idleSec: null };
  }
  const params = new URLSearchParams(window.location.search);
  const idleRaw = params.get("idle");
  const idleSec = idleRaw ? Number(idleRaw) : null;
  return {
    forceGuide: params.get("guide") === "1",
    wantExample: params.get("brand") === "example",
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
    void (async () => {
      let resolved = DEFAULT_BRANDING;
      try {
        resolved = normalizeBranding(await api.getBranding());
      } catch (err) {
        console.warn("branding: Tauri invoke unavailable", err);
      }
      const disk = await loadDevDataDirBranding();
      if (disk) {
        resolved = normalizeBranding({
          ...resolved,
          ...disk,
          logo_data_url: resolved.logo_data_url ?? disk.logo_data_url,
          splash_data_url: resolved.splash_data_url ?? disk.splash_data_url,
          idle_data_url: resolved.idle_data_url ?? disk.idle_data_url,
        });
      }
      if (preview.wantExample) {
        resolved = examplePreviewBranding(resolved);
      }
      setBranding(resolved);
      applyBrandingCss(resolved);
      setBrandingReady(true);
    })();
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
