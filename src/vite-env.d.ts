/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_ZAPPE_BRAND_NAME?: string;
  readonly VITE_ZAPPE_BRAND_WORDMARK?: string;
  readonly VITE_ZAPPE_BRAND_TAGLINE?: string;
  readonly VITE_ZAPPE_BRAND_ACCENT?: string;
  readonly VITE_ZAPPE_BRAND_LOGO?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
