import { cn } from "@/lib/utils";

export function ShelfSkeleton({
  title,
  count = 4,
  wide = false,
}: {
  title?: string;
  count?: number;
  wide?: boolean;
}) {
  return (
    <section className="tv-shelf" aria-busy="true" aria-label={title ?? "Loading"}>
      {title ? <h2 className="tv-shelf-title">{title}</h2> : null}
      <div className="tv-progress" aria-hidden />
      <div className="shelf-scroll">
        {Array.from({ length: count }, (_, i) => (
          <div
            key={i}
            className={cn("tv-skeleton-tile", wide && "tv-tile-wide")}
          />
        ))}
      </div>
    </section>
  );
}
