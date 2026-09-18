export const DEFAULT_BRAND_NAME = "Zappe";
export const DEFAULT_ACCENT = "oklch(0.72 0.14 45)";

export type Branding = {
  name: string;
  accent: string;
  tagline: string | null;
  logo_data_url: string | null;
  splash_data_url: string | null;
  source: "default" | "user";
};

export const DEFAULT_BRANDING: Branding = {
  name: DEFAULT_BRAND_NAME,
  accent: DEFAULT_ACCENT,
  tagline: null,
  logo_data_url: null,
  splash_data_url: null,
  source: "default",
};

export function applyBrandingCss(branding: Branding) {
  const root = document.documentElement;
  root.style.setProperty("--brand-accent", branding.accent);
  root.style.setProperty("--color-primary", branding.accent);
  root.style.setProperty("--color-ring", branding.accent);
  document.title = branding.name;
}
