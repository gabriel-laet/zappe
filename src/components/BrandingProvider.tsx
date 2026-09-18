import { createContext, useContext, useEffect, type ReactNode } from "react";
import {
  applyBrandingCss,
  DEFAULT_BRANDING,
  type Branding,
} from "@/lib/branding";

const BrandingContext = createContext<Branding>(DEFAULT_BRANDING);

export function BrandingProvider({
  branding,
  children,
}: {
  branding: Branding;
  children: ReactNode;
}) {
  useEffect(() => {
    applyBrandingCss(branding);
  }, [branding]);

  return (
    <BrandingContext.Provider value={branding}>
      {children}
    </BrandingContext.Provider>
  );
}

export function useBrand(): Branding {
  return useContext(BrandingContext);
}
