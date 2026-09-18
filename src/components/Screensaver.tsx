import { BrandArt } from "@/components/BrandArt";
import { useBrand } from "@/components/BrandingProvider";

export function Screensaver() {
  const brand = useBrand();
  const art = brand.idle_data_url ?? brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "Z";

  return (
    <div className="screensaver" role="presentation">
      <div className="screensaver-vignette" />
      <div className="screensaver-drift">
        {art ? (
          <span className="screensaver-badge-frame">
            <BrandArt src={art} alt="" className="screensaver-badge" />
          </span>
        ) : (
          <span className="screensaver-mark" aria-hidden>
            {letter}
          </span>
        )}
      </div>
    </div>
  );
}
