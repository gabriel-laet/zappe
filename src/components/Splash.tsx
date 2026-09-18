import { BrandArt } from "@/components/BrandArt";
import { useBrand } from "@/components/BrandingProvider";

export function Splash() {
  const brand = useBrand();
  const art = brand.splash_data_url ?? brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "Z";

  return (
    <div className="splash-screen" role="status" aria-live="polite">
      <div className="splash-inner">
        {art ? (
          <BrandArt src={art} alt={brand.name} className="splash-art" />
        ) : (
          <>
            <span className="splash-mark" aria-hidden>
              {letter}
            </span>
            <h1 className="splash-name">{brand.name}</h1>
          </>
        )}
      </div>
    </div>
  );
}
