import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const host = process.env.TAURI_DEV_HOST;
const BRAND_PREFIX = "/__zappe_branding__";

function brandingDataDir() {
  return process.env.ZAPPE_DATA_DIR || path.join(os.homedir(), ".local/share/zappe");
}

function mimeFor(file: string) {
  switch (path.extname(file).toLowerCase()) {
    case ".png":
      return "image/png";
    case ".jpg":
    case ".jpeg":
      return "image/jpeg";
    case ".webp":
      return "image/webp";
    case ".gif":
      return "image/gif";
    case ".json":
      return "application/json";
    default:
      return "application/octet-stream";
  }
}

/** Vite-only: serve ~/.local/share/zappe artwork without stuffing 1.4MB into JS. */
function zappeBrandingPreview(): Plugin {
  return {
    name: "zappe-branding-preview",
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const url = req.url?.split("?")[0] ?? "";
        if (!url.startsWith(BRAND_PREFIX)) {
          next();
          return;
        }

        const dir = brandingDataDir();
        const sendFile = (file: string) => {
          if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
            res.statusCode = 404;
            res.end();
            return;
          }
          res.setHeader("Content-Type", mimeFor(file));
          res.setHeader("Cache-Control", "no-store");
          fs.createReadStream(file).pipe(res);
        };

        if (url === `${BRAND_PREFIX}/preview`) {
          const logoFile = path.join(dir, "logo.png");
          const splashFile = path.join(dir, "splash.png");
          const hasLogo = fs.existsSync(logoFile);
          const hasSplash = fs.existsSync(splashFile);
          let name = "feras TV";
          let accent = "#C4A574";
          try {
            const raw = JSON.parse(
              fs.readFileSync(path.join(dir, "branding.json"), "utf8"),
            ) as { name?: string; accent?: string };
            if (typeof raw.name === "string" && raw.name.trim()) name = raw.name.trim();
            if (typeof raw.accent === "string" && raw.accent.trim()) {
              accent = raw.accent.trim();
            }
          } catch {
            /* preview defaults */
          }
          res.setHeader("Content-Type", "application/json");
          res.setHeader("Cache-Control", "no-store");
          res.end(
            JSON.stringify({
              name,
              accent,
              tagline: null,
              logo_data_url: hasLogo ? `${BRAND_PREFIX}/logo.png` : null,
              splash_data_url: hasSplash
                ? `${BRAND_PREFIX}/splash.png`
                : hasLogo
                  ? `${BRAND_PREFIX}/logo.png`
                  : null,
              idle_data_url: hasLogo ? `${BRAND_PREFIX}/logo.png` : null,
              idle_mode: "screensaver",
              idle_timeout_seconds: 120,
              idle_animation: "soft-breathe",
              theme_style: "apple-tv",
              theme_background: "#000000",
              theme_focus: "subtle-scale",
              source: "user",
            }),
          );
          return;
        }

        if (url === `${BRAND_PREFIX}/logo.png`) {
          sendFile(path.join(dir, "logo.png"));
          return;
        }
        if (url === `${BRAND_PREFIX}/splash.png`) {
          const splash = path.join(dir, "splash.png");
          sendFile(fs.existsSync(splash) ? splash : path.join(dir, "logo.png"));
          return;
        }

        res.statusCode = 404;
        res.end();
      });
    },
  };
}

export default defineConfig({
  plugins: [react(), tailwindcss(), zappeBrandingPreview()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: "ws",
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      ignored: ["**/src-tauri/**"],
    },
  },
  envPrefix: ["VITE_", "TAURI_"],
  build: {
    target: process.env.TAURI_PLATFORM === "windows" ? "chrome105" : "safari13",
    minify: !process.env.TAURI_DEBUG ? "esbuild" : false,
    sourcemap: !!process.env.TAURI_DEBUG,
  },
});
