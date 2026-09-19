import { useEffect, useState } from "react";
import { cn } from "@/lib/utils";

type BrandArtProps = {
  src: string;
  alt?: string;
  className?: string;
};

/**
 * Render the appliance logo URL only. Never invent a letter mark while a
 * logo URL exists — including the wait before decode and a failed load.
 * Header/splash CSS clips the image to a circle; do not paint a plate here.
 */
export function BrandArt({ src, alt = "", className }: BrandArtProps) {
  const [failed, setFailed] = useState(false);

  useEffect(() => {
    setFailed(false);
  }, [src]);

  if (!src || failed) {
    return <div className={cn("tv-brand-art", className)} aria-hidden />;
  }

  return (
    <img
      src={src}
      alt={alt}
      className={cn("tv-brand-art", className)}
      onError={() => setFailed(true)}
    />
  );
}
