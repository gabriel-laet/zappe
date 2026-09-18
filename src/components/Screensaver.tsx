import { useBrand } from "@/components/BrandingProvider";

export function Screensaver() {
  const brand = useBrand();
  const art = brand.idle_data_url ?? brand.logo_data_url;
  const letter = brand.name.trim().charAt(0) || "F";

  return (
    <div className="screensaver" role="presentation">
      <div className="screensaver-vignette" />
      <div className="screensaver-drift">
        {art ? (
          <img src={art} alt="" className="screensaver-badge" />
        ) : (
          <span className="screensaver-mark" aria-hidden>
            {letter}
          </span>
        )}
      </div>
    </div>
  );
}
