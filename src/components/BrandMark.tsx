import { useBranding } from "@/hooks/useBranding";
import { cn } from "@/lib/utils";

type BrandMarkProps = {
  size?: "header" | "splash" | "setup";
  className?: string;
};

const logoClass = {
  header: "brand-logo h-12 w-12",
  setup: "brand-logo h-16 w-16",
  splash: "brand-logo h-24 w-24",
} as const;

const wordmarkClass = {
  header: "brand-wordmark text-base",
  setup: "brand-wordmark text-xl",
  splash: "brand-wordmark text-3xl",
} as const;

export function BrandMark({ size = "header", className }: BrandMarkProps) {
  const brand = useBranding();
  return (
    <div className={cn("flex items-center gap-4", className)}>
      {brand.logo_src ? (
        <img
          src={brand.logo_src}
          alt=""
          className={logoClass[size]}
        />
      ) : null}
      <p className={wordmarkClass[size]}>{brand.wordmark}</p>
    </div>
  );
}
