import { defineConfig } from "vite";

// The page is served to the Tauri window only: a fixed port the configuration
// points at, nothing exposed beyond this machine, and no inline script or style
// in the build output, which the CSP would refuse anyway.
export default defineConfig({
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    host: "localhost",
    watch: { ignored: ["**/src-tauri/**", "**/target/**", "**/build/**"] },
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
    target: "es2022",
    assetsInlineLimit: 0,
    modulePreload: { polyfill: false },
    sourcemap: false,
  },
});
