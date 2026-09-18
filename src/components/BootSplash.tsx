import { BrandMark } from "@/components/BrandMark";
import { useBranding } from "@/hooks/useBranding";

export function BootSplash() {
  const brand = useBranding();
  return (
    <div className="boot-splash" role="status" aria-live="polite">
      <div className="splash-rise">
        <BrandMark size="splash" className="brand-breathe flex-col" />
        <p className="text-2xl text-muted-foreground">{brand.tagline}</p>
        <div className="splash-bar" aria-hidden />
      </div>
      <span className="sr-only">Loading {brand.name}</span>
    </div>
  );
}
