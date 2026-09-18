export type BrandingView = {
  name: string;
  wordmark: string;
  tagline: string;
  accent: string;
  logo_src: string | null;
};

export const DEFAULT_BRANDING: BrandingView = {
  name: "Zappe",
  wordmark: "Zappe",
  tagline: "Home",
  accent: "oklch(0.72 0.14 45)",
  logo_src: null,
};

function viteField(value: unknown): string | undefined {
  if (typeof value !== "string") return undefined;
  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed : undefined;
}

/** Optional Vite-time overrides for `npm run dev:web` / preview. */
export function viteBrandingPatch(): Partial<BrandingView> {
  const env = import.meta.env;
  return {
    name: viteField(env.VITE_ZAPPE_BRAND_NAME),
    wordmark: viteField(env.VITE_ZAPPE_BRAND_WORDMARK),
    tagline: viteField(env.VITE_ZAPPE_BRAND_TAGLINE),
    accent: viteField(env.VITE_ZAPPE_BRAND_ACCENT),
    logo_src: viteField(env.VITE_ZAPPE_BRAND_LOGO) ?? undefined,
  };
}

export function mergeBranding(
  base: BrandingView,
  patch: Partial<BrandingView> | null | undefined,
): BrandingView {
  if (!patch) return base;
  const name = patch.name?.trim() || base.name;
  const wordmark = patch.wordmark?.trim() || (patch.name?.trim() ? name : base.wordmark);
  return {
    name,
    wordmark,
    tagline: patch.tagline?.trim() || base.tagline,
    accent: patch.accent?.trim() || base.accent,
    logo_src: patch.logo_src === undefined ? base.logo_src : patch.logo_src,
  };
}

export function applyBranding(brand: BrandingView) {
  const root = document.documentElement;
  root.dataset.brand = brand.name;
  document.title = brand.name;
  if (brand.accent) {
    root.style.setProperty("--color-primary", brand.accent);
    root.style.setProperty("--color-ring", brand.accent);
    root.style.setProperty("--brand-accent", brand.accent);
  }
}
