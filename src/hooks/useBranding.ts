import { createContext, useContext } from "react";
import { DEFAULT_BRANDING, type BrandingView } from "@/lib/branding";

export const BrandingContext = createContext<BrandingView>(DEFAULT_BRANDING);

export function useBranding(): BrandingView {
  return useContext(BrandingContext);
}
