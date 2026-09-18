import { useCallback, useEffect, useMemo, useState } from "react";
import { toast } from "sonner";
import { BrandMark } from "@/components/BrandMark";
import { ConnectPhone } from "@/components/ConnectPhone";
import { ShelfSkeleton } from "@/components/ShelfSkeleton";
import { Button } from "@/components/ui/button";
import { cn } from "@/lib/utils";
import { primaryOtaChannel } from "@/lib/otaDisplay";
import { atongx } from "@/lib/remoteMap";
import {
  api,
  onCatalogChanged,
  onFocusRestore,
  onGuideMenu,
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
  kind: "app" | "catalog" | "empty" | "teach" | "ota" | "sync" | "connect";
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

function otaTiles(channels: OtaChannel[]): Tile[] {
  return channels.map((c) => ({
    id: `ota-${c.name}`,
    title: c.name,
    kind: "ota",
    ota: { channel: c.name, conf: c.source },
  }));
}

function continueTiles(shelf?: CatalogShelf, teachActive?: boolean): Tile[] {
  if (!shelf || shelf.status === "empty") {
    return [
      {
        id: "continue-empty",
        title: "Nothing yet",
        subtitle: "Connect from your phone, then Sync",
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
  if (shelf.status === "harvesting") {
    return [
      {
        id: "continue-harvesting",
        title: "Syncing…",
        subtitle: shelf.message ?? "Reading Chrome a11y",
        kind: "empty",
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

export function Home() {
  const [otaChannels, setOtaChannels] = useState<OtaChannel[]>([]);
  const [otaReady, setOtaReady] = useState(false);
  const [catalogReady, setCatalogReady] = useState(false);
  const [catalog, setCatalog] = useState<CatalogView>({
    shelves: [],
    teach: { active: false, skill_id: null, message: null },
  });
  const [focus, setFocus] = useState<GuideFocus>({ shelf_id: "apps", index: 0 });
  const [connectOpen, setConnectOpen] = useState(false);

  const hasOta = otaChannels.length > 0;
  const primary = primaryOtaChannel(otaChannels);
  const continueShelf = catalog.shelves.find((s) => s.id === "continue");
  const harvesting = continueShelf?.status === "harvesting";
  const needsConnect =
    catalogReady &&
    (!continueShelf ||
      continueShelf.status === "empty" ||
      (continueShelf.status === "ok" && continueShelf.rows.length === 0));

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
    return [
      { id: "apps", title: "Apps", tiles: apps },
      ...(needsConnect
        ? [
            {
              id: "connect",
              title: "Connect",
              tiles: [
                {
                  id: "connect-phone",
                  title: "Connect account",
                  subtitle: "zappe-tv.local",
                  kind: "connect" as const,
                },
              ],
            },
          ]
        : []),
      ...(catalogReady
        ? [
            {
              id: "continue",
              title: continueShelf?.title ?? "Continue watching",
              tiles: continueTiles(continueShelf, catalog.teach.active),
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
    catalogReady,
    needsConnect,
  ]);

  useEffect(() => {
    void api
      .listOtaChannels()
      .then(setOtaChannels)
      .catch(() => setOtaChannels([]))
      .finally(() => setOtaReady(true));
    void api
      .getCatalog()
      .then(setCatalog)
      .catch(() => undefined)
      .finally(() => setCatalogReady(true));
    // Never auto-harvest on mount — that steals focus into the Netflix nest.
    // Opt in only for debugging: ZAPPE_AUTO_HARVEST=1
    void api.autoHarvestEnabled().then((on) => {
      if (on) {
        void api.harvestNow().catch((e) => toast.message(String(e)));
      }
    });
    let unlistenFocus: (() => void) | undefined;
    let unlistenCatalog: (() => void) | undefined;
    void onFocusRestore((f) => setFocus(f)).then((fn) => {
      unlistenFocus = fn;
    });
    void onCatalogChanged(setCatalog).then((fn) => {
      unlistenCatalog = fn;
    });
    let unlistenMenu: (() => void) | undefined;
    void onGuideMenu(() => setConnectOpen(true)).then((fn) => {
      unlistenMenu = fn;
    });
    const onDomMenu = () => setConnectOpen(true);
    window.addEventListener("zappe-menu", onDomMenu);
    return () => {
      unlistenFocus?.();
      unlistenCatalog?.();
      unlistenMenu?.();
      window.removeEventListener("zappe-menu", onDomMenu);
    };
  }, []);

  const shelfIndex = shelves.findIndex((s) => s.id === focus.shelf_id);
  const currentShelf = shelves[shelfIndex] ?? shelves[0];

  const activate = useCallback(
    async (tile: Tile) => {
      const f = { shelf_id: focus.shelf_id, index: focus.index };
      const handoff = tile.kind === "app" || tile.kind === "catalog" || tile.kind === "ota";
      if (handoff) document.body.classList.add("tv-handoff");
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
          const out = await api.harvestNow();
          if (out.status === "ok" || out.status === "empty" || out.status === "harvesting") {
            toast.message(out.message);
          } else {
            toast.error(out.message);
          }
        } else if (tile.kind === "teach") {
          const skillId = tile.serviceId ?? "netflix.continue_watching.v1";
          const state = await api.beginTeach(skillId);
          toast.message(state.message ?? "Teach-mode waiting.");
        } else if (tile.kind === "connect") {
          setConnectOpen(true);
        } else if (tile.kind === "empty") {
          toast.message("Connect from your phone at zappe-tv.local, then Sync.");
        }
      } catch (e) {
        document.body.classList.remove("tv-handoff");
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
      if (connectOpen) return;
      if (atongx.isUp(e)) {
        e.preventDefault();
        move(-1, 0);
      } else if (atongx.isDown(e)) {
        e.preventDefault();
        move(1, 0);
      } else if (atongx.isLeft(e)) {
        e.preventDefault();
        move(0, -1);
      } else if (atongx.isRight(e)) {
        e.preventDefault();
        move(0, 1);
      } else if (atongx.isActivate(e)) {
        e.preventDefault();
        const tile = currentShelf.tiles[focus.index];
        if (tile) void activate(tile);
      } else if (atongx.isPageUp(e)) {
        e.preventDefault();
        move(-1, 0);
      } else if (atongx.isPageDown(e)) {
        e.preventDefault();
        move(1, 0);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [activate, connectOpen, currentShelf, focus.index, move]);

  if (connectOpen) {
    return <ConnectPhone onClose={() => setConnectOpen(false)} />;
  }

  return (
    <div className="tv-page tv-guide">
      <header className="mb-[var(--tv-space-4)] flex items-center justify-between gap-[var(--tv-space-3)]">
        <BrandMark />
        {catalog.teach.active && (
          <p className="tv-caption max-w-xl text-right">{catalog.teach.message}</p>
        )}
      </header>

      <div className="tv-scroll">
        {shelves.map((shelf) => {
          const showHarvest = shelf.id === "continue" && harvesting;
          return (
            <section key={shelf.id} className="tv-shelf" aria-label={shelf.title}>
              <h2 className="tv-shelf-title">{shelf.title}</h2>
              {showHarvest && <div className="tv-progress" aria-hidden />}
              <div className="shelf-scroll">
                {shelf.tiles.map((tile, index) => {
                  const focused =
                    focus.shelf_id === shelf.id && focus.index === index;
                  const wide = shelf.id === "continue" || tile.kind === "catalog";
                  return (
                    <Button
                      key={tile.id}
                      variant="secondary"
                      size="tile"
                      className={cn(
                        "shrink-0",
                        wide && "tv-tile-wide",
                        focused && "focus-tile",
                      )}
                      ref={(node) => {
                        if (focused && node) {
                          node.scrollIntoView({
                            behavior: "smooth",
                            inline: "center",
                            block: "center",
                          });
                        }
                      }}
                      onClick={() => {
                        setFocus({ shelf_id: shelf.id, index });
                        void activate(tile);
                      }}
                    >
                      <span className="tv-tile-title">{tile.title}</span>
                      {tile.subtitle && (
                        <span className="tv-tile-sub">{tile.subtitle}</span>
                      )}
                    </Button>
                  );
                })}
              </div>
            </section>
          );
        })}
        {!catalogReady && (
          <ShelfSkeleton title="Continue watching" count={4} wide />
        )}
        {!otaReady && <ShelfSkeleton title="Canais" count={3} />}
      </div>
    </div>
  );
}
