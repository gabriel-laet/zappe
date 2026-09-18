import { cn } from "@/lib/utils";
import { BrandArt } from "@/components/BrandArt";
import { useBrand } from "@/components/BrandingProvider";

type BrandMarkProps = {
  size?: "md" | "lg";
  className?: string;
};

export function BrandMark({ size = "md", className }: BrandMarkProps) {
  const brand = useBrand();
  const art = brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "Z";

  return (
    <div className={cn("flex items-center", className)}>
      {art ? (
        <BrandArt
          src={art}
          alt={brand.name}
          className={cn("tv-logo", size === "lg" && "tv-logo-lg")}
        />
      ) : (
        <>
          <span
            className={cn("tv-logo-mark", size === "lg" && "tv-logo-mark-lg")}
            aria-hidden
          >
            {letter}
          </span>
          <p className={cn("tv-wordmark", size === "lg" && "tv-wordmark-lg")}>
            {brand.name}
          </p>
        </>
      )}
    </div>
  );
}
