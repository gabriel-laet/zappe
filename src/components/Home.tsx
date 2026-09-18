import { useCallback, useEffect, useMemo, useState } from "react";
import { toast } from "sonner";
import { BrandMark } from "@/components/BrandMark";
import { ShelfSkeleton, SkeletonTiles } from "@/components/ShelfSkeleton";
import { Button } from "@/components/ui/button";
import { useBranding } from "@/hooks/useBranding";
import { cn } from "@/lib/utils";
import { primaryOtaChannel } from "@/lib/otaDisplay";
import {
  api,
  onCatalogChanged,
  onFocusRestore,
  type CatalogShelf,
  type CatalogView,
  type GuideFocus,
  type OtaChannel,
} from "@/lib/tauri";

type Tile = {
  id: string;
  title: string;
  subtitle?: string;
  serviceId?: string;
  url?: string;
  ota?: { channel: string; conf: string };
  kind: "app" | "catalog" | "empty" | "teach" | "ota" | "sync" | "busy";
};

type Shelf = {
  id: string;
  title: string;
  tiles: Tile[];
};

const APP_TILES: Tile[] = [
  { id: "netflix", title: "Netflix", serviceId: "netflix", kind: "app" },
  { id: "prime", title: "Prime Video", serviceId: "prime", kind: "app" },
  { id: "disney", title: "Disney+", serviceId: "disney", kind: "app" },
  { id: "youtube", title: "YouTube", serviceId: "youtube", kind: "app" },
];

const EMPTY_CATALOG: CatalogView = {
  shelves: [],
  teach: { active: false, skill_id: null, message: null },
};

function otaTiles(channels: OtaChannel[]): Tile[] {
  return channels.map((c) => ({
    id: `ota-${c.name}`,
    title: c.name,
    kind: "ota",
    ota: { channel: c.name, conf: c.source },
  }));
}

function continueTiles(
  shelf?: CatalogShelf,
  teachActive?: boolean,
  harvesting?: boolean,
): Tile[] {
  if (harvesting || shelf?.status === "harvesting") {
    return [
      {
        id: "continue-harvesting",
        title: "Syncing…",
        subtitle: shelf?.message ?? "Reading Chrome a11y",
        kind: "busy",
      },
    ];
  }
  if (!shelf || shelf.status === "empty") {
    return [
      {
        id: "continue-empty",
        title: "Nothing yet",
        subtitle: "Sign in to Netflix, then Sync",
        kind: "empty",
      },
      {
        id: "continue-sync",
        title: "Sync Netflix",
        subtitle: "Harvest Continue Watching",
        kind: "sync",
      },
    ];
  }
  if (shelf.status === "teach" || shelf.status === "stale" || shelf.status === "error") {
    return [
      {
        id: "continue-teach",
        title: teachActive ? "Waiting…" : "Teach me",
        subtitle: shelf.message ?? "Skill stale — finish the path with the air mouse",
        kind: "teach",
        serviceId: shelf.skill_id,
      },
      {
        id: "continue-sync",
        title: "Retry sync",
        subtitle: "Run harvest again",
        kind: "sync",
      },
    ];
  }
  const rows: Tile[] = shelf.rows.map((row, i) => ({
    id: `cw-${i}-${row.title}`,
    title: row.title,
    subtitle: "Netflix",
    serviceId: row.service,
    url: row.href ?? undefined,
    kind: "catalog",
  }));
  rows.push({
    id: "continue-sync",
    title: "Sync",
    subtitle: "Refresh from Chrome",
    kind: "sync",
  });
  return rows;
}

type HomeProps = {
  initialCatalog?: CatalogView;
  initialOta?: OtaChannel[];
};

export function Home({ initialCatalog, initialOta }: HomeProps) {
  const brand = useBranding();
  const [otaChannels, setOtaChannels] = useState<OtaChannel[]>(initialOta ?? []);
  const [otaLoading, setOtaLoading] = useState(initialOta === undefined);
  const [catalog, setCatalog] = useState<CatalogView>(initialCatalog ?? EMPTY_CATALOG);
  const [catalogLoading, setCatalogLoading] = useState(initialCatalog === undefined);
  const [syncing, setSyncing] = useState(false);
  const [focus, setFocus] = useState<GuideFocus>({ shelf_id: "apps", index: 0 });

  const hasOta = otaChannels.length > 0;
  const primary = primaryOtaChannel(otaChannels);
  const continueShelf = catalog.shelves.find((s) => s.id === "continue");
  const harvesting = syncing || continueShelf?.status === "harvesting";

  const shelves: Shelf[] = useMemo(() => {
    const apps: Tile[] = [...APP_TILES];
    if (primary) {
      apps.push({
        id: "tv-aberta",
        title: "TV aberta",
        subtitle: primary.name,
        kind: "ota",
        ota: { channel: primary.name, conf: primary.source },
      });
    }
    const canaisShelf: Shelf = {
      id: "canais",
      title: "Canais",
      tiles: otaTiles(otaChannels),
    };
    const continueReady = !catalogLoading || Boolean(continueShelf);
    return [
      { id: "apps", title: "Apps", tiles: apps },
      ...(continueReady
        ? [
            {
              id: "continue",
              title: continueShelf?.title ?? "Continue watching",
              tiles: continueTiles(continueShelf, catalog.teach.active, harvesting),
            },
          ]
        : []),
      ...(hasOta ? [canaisShelf] : []),
    ];
  }, [
    otaChannels,
    hasOta,
    primary,
    continueShelf,
    catalog.teach.active,
    catalogLoading,
    harvesting,
  ]);

  useEffect(() => {
    void api
      .listOtaChannels()
      .then(setOtaChannels)
      .catch(() => undefined)
      .finally(() => setOtaLoading(false));
    void api
      .getCatalog()
      .then(setCatalog)
      .catch(() => undefined)
      .finally(() => setCatalogLoading(false));
    // Never auto-harvest on mount — that steals focus into the Netflix nest.
    // Opt in only for debugging: ZAPPE_AUTO_HARVEST=1
    void api.autoHarvestEnabled().then((on) => {
      if (on) {
        setSyncing(true);
        void api
          .harvestNow()
          .catch((e) => toast.message(String(e)))
          .finally(() => setSyncing(false));
      }
    });
    let unlistenFocus: (() => void) | undefined;
    let unlistenCatalog: (() => void) | undefined;
    void onFocusRestore((f) => setFocus(f)).then((fn) => {
      unlistenFocus = fn;
    });
    void onCatalogChanged((next) => {
      setCatalog(next);
      setCatalogLoading(false);
    }).then((fn) => {
      unlistenCatalog = fn;
    });
    return () => {
      unlistenFocus?.();
      unlistenCatalog?.();
    };
  }, []);

  const shelfIndex = shelves.findIndex((s) => s.id === focus.shelf_id);
  const currentShelf = shelves[shelfIndex] ?? shelves[0];

  useEffect(() => {
    const len = currentShelf?.tiles.length ?? 0;
    if (len > 0 && focus.index >= len) {
      setFocus((f) => ({ ...f, index: len - 1 }));
    }
  }, [currentShelf, focus.index]);

  const activate = useCallback(
    async (tile: Tile) => {
      const f = { shelf_id: focus.shelf_id, index: focus.index };
      try {
        if (tile.kind === "app" && tile.serviceId) {
          await api.openApp(tile.serviceId, f);
        } else if (tile.kind === "catalog") {
          if (tile.url && tile.serviceId) {
            await api.openChromeUrl(tile.serviceId, tile.url, tile.title, f);
          } else {
            await api.openApp(tile.serviceId ?? "netflix", f);
          }
        } else if (tile.kind === "ota" && tile.ota) {
          await api.playOta(tile.ota.channel, tile.ota.conf, f);
        } else if (tile.kind === "sync") {
          toast.message("Harvesting Continue Watching from Chrome…");
          setSyncing(true);
          try {
            const out = await api.harvestNow();
            toast.message(out.message);
          } finally {
            setSyncing(false);
          }
        } else if (tile.kind === "teach") {
          const skillId = tile.serviceId ?? "netflix.continue_watching.v1";
          const state = await api.beginTeach(skillId);
          toast.message(state.message ?? "Teach-mode waiting.");
        } else if (tile.kind === "busy") {
          toast.message(tile.subtitle ?? "Still syncing…");
        } else if (tile.kind === "empty") {
          toast.message("Sign into Netflix in the player nest, then Sync.");
        }
      } catch (e) {
        toast.error(String(e));
      }
    },
    [focus],
  );

  const move = useCallback(
    (dRow: number, dCol: number) => {
      let si = shelfIndex >= 0 ? shelfIndex : 0;
      let ti = focus.index;
      if (dRow !== 0) {
        si = Math.max(0, Math.min(shelves.length - 1, si + dRow));
        ti = Math.min(ti, (shelves[si]?.tiles.length ?? 1) - 1);
      }
      if (dCol !== 0) {
        const len = shelves[si]?.tiles.length ?? 1;
        ti = (ti + dCol + len) % len;
      }
      setFocus({ shelf_id: shelves[si].id, index: ti });
    },
    [focus.index, shelfIndex, shelves],
  );

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.code === "ArrowUp") {
        e.preventDefault();
        move(-1, 0);
      } else if (e.code === "ArrowDown") {
        e.preventDefault();
        move(1, 0);
      } else if (e.code === "ArrowLeft") {
        e.preventDefault();
        move(0, -1);
      } else if (e.code === "ArrowRight") {
        e.preventDefault();
        move(0, 1);
      } else if (e.code === "Enter" || e.code === "NumpadEnter") {
        e.preventDefault();
        const tile = currentShelf.tiles[focus.index];
        if (tile) void activate(tile);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [activate, currentShelf, focus.index, move]);

  const showContinueSkeleton = catalogLoading && !continueShelf;
  const showCanaisSkeleton = otaLoading;

  return (
    <div
      className="flex h-full flex-col bg-background"
      style={{ padding: "var(--tv-page-y) var(--tv-page-x)" }}
    >
      <header className="mb-8 flex items-end justify-between gap-8">
        <div className="space-y-2">
          <BrandMark size="header" />
          <h1 className="text-5xl font-semibold tracking-tight">{brand.tagline}</h1>
        </div>
        <div className="max-w-xl space-y-2 text-right text-lg text-muted-foreground">
          {harvesting && (
            <p className="status-line justify-end">
              <span className="status-dot" aria-hidden />
              Syncing Continue Watching
            </p>
          )}
          {showCanaisSkeleton && (
            <p className="status-line justify-end">
              <span className="status-dot" aria-hidden />
              Loading channels
            </p>
          )}
          {catalog.teach.active && <p>{catalog.teach.message}</p>}
        </div>
      </header>

      <div className="flex-1 space-y-10 overflow-y-auto pb-12">
        {shelves.map((shelf) => (
          <section key={shelf.id} aria-label={shelf.title}>
            <h2 className="mb-4 text-2xl text-muted-foreground">{shelf.title}</h2>
            <div className="shelf-scroll">
              {shelf.tiles.map((tile, index) => {
                const focused =
                  focus.shelf_id === shelf.id && focus.index === index;
                const poster = tile.kind === "catalog" || tile.kind === "busy";
                return (
                  <Button
                    key={tile.id}
                    variant="secondary"
                    size={poster ? "poster" : "tile"}
                    className={cn(
                      "shrink-0 bg-card text-card-foreground",
                      focused && "focus-tile",
                      tile.kind === "busy" && "brand-breathe",
                    )}
                    onClick={() => {
                      setFocus({ shelf_id: shelf.id, index });
                      void activate(tile);
                    }}
                  >
                    <span className="text-3xl leading-tight font-semibold">
                      {tile.title}
                    </span>
                    {tile.subtitle && (
                      <span className="mt-2 text-lg text-muted-foreground">
                        {tile.subtitle}
                      </span>
                    )}
                  </Button>
                );
              })}
              {shelf.id === "continue" && harvesting && (
                <SkeletonTiles count={3} kind="poster" />
              )}
            </div>
          </section>
        ))}
        {showContinueSkeleton && (
          <ShelfSkeleton title="Continue watching" kind="poster" count={4} />
        )}
        {showCanaisSkeleton && <ShelfSkeleton title="Canais" count={5} />}
      </div>
    </div>
  );
}
