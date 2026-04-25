import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// `base` is the public path the built site is served from. GitHub Pages
// project sites serve at `https://<user>.github.io/<repo>/`, so we need
// `/sr-web/` baked into the asset URLs for prod. Local dev keeps `/`.
export default defineConfig(({ command }) => ({
	base: command === "build" ? (process.env.VITE_BASE ?? "/sr-web/") : "/",
	plugins: [react()],
	server: {
		port: 5173,
	},
}));
