export const DEFAULT_BRAND_NAME = "Zappe";
export const DEFAULT_ACCENT = "#E85A1B";
export const FERAS_PREVIEW_NAME = "feras TV";
export const FERAS_PREVIEW_ACCENT = "#C4A574";
export const DEFAULT_BACKGROUND = "#000000";
export const DEFAULT_IDLE_TIMEOUT = 120;

export type Branding = {
  name: string;
  accent: string;
  tagline: string | null;
  logo_data_url: string | null;
  splash_data_url: string | null;
  idle_data_url: string | null;
  idle_mode: "screensaver" | "off";
  idle_timeout_seconds: number;
  idle_animation: string;
  theme_style: string;
  theme_background: string;
  theme_focus: string;
  source: "default" | "user";
};

export const DEFAULT_BRANDING: Branding = {
  name: DEFAULT_BRAND_NAME,
  accent: DEFAULT_ACCENT,
  tagline: null,
  logo_data_url: null,
  splash_data_url: null,
  idle_data_url: null,
  idle_mode: "screensaver",
  idle_timeout_seconds: DEFAULT_IDLE_TIMEOUT,
  idle_animation: "soft-breathe",
  theme_style: "apple-tv",
  theme_background: DEFAULT_BACKGROUND,
  theme_focus: "subtle-scale",
  source: "default",
};

export function applyBrandingCss(branding: Branding) {
  const root = document.documentElement;
  root.style.setProperty("--brand-accent", branding.accent);
  root.style.setProperty("--color-primary", branding.accent);
  root.style.setProperty("--color-ring", branding.accent);
  root.style.setProperty("--color-background", branding.theme_background);
  root.dataset.theme = branding.theme_style;
  root.dataset.focus = branding.theme_focus;
  document.title = branding.name;
}

export function normalizeBranding(raw: Partial<Branding> | null | undefined): Branding {
  if (!raw) return DEFAULT_BRANDING;
  return {
    ...DEFAULT_BRANDING,
    ...raw,
    tagline: raw.tagline ?? null,
    logo_data_url: raw.logo_data_url ?? null,
    splash_data_url: raw.splash_data_url ?? null,
    idle_data_url: raw.idle_data_url ?? raw.logo_data_url ?? null,
    idle_mode: raw.idle_mode === "off" ? "off" : "screensaver",
    idle_timeout_seconds: Math.min(
      3600,
      Math.max(15, raw.idle_timeout_seconds ?? DEFAULT_IDLE_TIMEOUT),
    ),
  };
}

export function ferasPreviewBranding(base: Branding = DEFAULT_BRANDING): Branding {
  return {
    ...base,
    name: FERAS_PREVIEW_NAME,
    accent: FERAS_PREVIEW_ACCENT,
    source: "user",
  };
}

/** Vite-only: read ~/.local/share/zappe via the preview middleware (file URLs, not 1.4MB base64). */
export async function loadDevDataDirBranding(): Promise<Branding | null> {
  if (!import.meta.env.DEV) return null;
  try {
    const res = await fetch("/__zappe_branding__/preview");
    if (!res.ok) return null;
    return normalizeBranding(await res.json());
  } catch {
    return null;
  }
}
