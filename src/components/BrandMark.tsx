import { cn } from "@/lib/utils";
import { useBrand } from "@/components/BrandingProvider";

type BrandMarkProps = {
  size?: "md" | "lg";
  className?: string;
};

export function BrandMark({ size = "md", className }: BrandMarkProps) {
  const brand = useBrand();
  const art = brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "F";

  return (
    <div className={cn("flex items-center", className)}>
      {art ? (
        <img
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
