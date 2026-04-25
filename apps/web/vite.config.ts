import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig(({ command }) => ({
	base: command === "build" ? (process.env.VITE_BASE ?? "/sr-web/") : "/",
	plugins: [react(), tailwindcss()],
	server: {
		port: 5173,
	},
}));
