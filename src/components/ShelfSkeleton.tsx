import { cn } from "@/lib/utils";

type ShelfSkeletonProps = {
  title: string;
  count?: number;
  kind?: "tile" | "poster";
};

export function ShelfSkeleton({
  title,
  count = 4,
  kind = "tile",
}: ShelfSkeletonProps) {
  return (
    <section aria-busy="true" aria-label={`${title} loading`}>
      <h2 className="mb-4 text-3xl text-muted-foreground">{title}</h2>
      <div className="shelf-scroll">
        {Array.from({ length: count }, (_, i) => (
          <div
            key={`${title}-${i}`}
            className={cn("tv-skeleton", kind === "poster" ? "tv-poster" : "tv-tile")}
          />
        ))}
      </div>
    </section>
  );
}

export function SkeletonTiles({
  count = 3,
  kind = "tile",
}: {
  count?: number;
  kind?: "tile" | "poster";
}) {
  return (
    <>
      {Array.from({ length: count }, (_, i) => (
        <div
          key={`sk-${i}`}
          className={cn("tv-skeleton", kind === "poster" ? "tv-poster" : "tv-tile")}
          aria-hidden
        />
      ))}
    </>
  );
}
