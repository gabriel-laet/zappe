import { cn } from "@/lib/utils";
import { useBrand } from "@/components/BrandingProvider";

type BrandMarkProps = {
  size?: "md" | "lg";
  className?: string;
  showTagline?: boolean;
};

export function BrandMark({
  size = "md",
  className,
  showTagline = true,
}: BrandMarkProps) {
  const brand = useBrand();
  const art = brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "Z";

  return (
    <div className={cn("flex items-center", className)}>
      {art ? (
        <img
          src={art}
          alt=""
          className={cn("tv-logo", size === "lg" && "tv-logo-lg")}
        />
      ) : (
        <span
          className={cn("tv-logo-mark", size === "lg" && "tv-logo-mark-lg")}
          aria-hidden
        >
          {letter}
        </span>
      )}
      <div className="min-w-0">
        <p className={cn("tv-wordmark", size === "lg" && "tv-wordmark-lg")}>
          {brand.name}
        </p>
        {showTagline && brand.tagline ? (
          <p className="tv-tagline">{brand.tagline}</p>
        ) : null}
      </div>
    </div>
  );
}
