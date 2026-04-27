import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig(({ command }) => ({
	base: command === "build" ? (process.env.VITE_BASE ?? "/sr-web/") : "/",
	plugins: [react(), tailwindcss()],
	// Stamp every build with a fresh token so we can cache-bust the static
	// wasm/data files (which keep fixed names) on each deploy.
	define: {
		__SR_BUILD_ID__: JSON.stringify(
			process.env.VITE_BUILD_ID ?? String(Date.now()),
		),
	},
	server: {
		port: 5173,
	},
}));
