// @ts-check
import { defineConfig } from "astro/config";
import tailwindcss from "@tailwindcss/vite";

// GitHub Pages project site served from the `gh-pages` branch of LplKernel.
// If you later attach a custom domain, drop `base` and update `site`.
export default defineConfig({
  site: "https://masterlaplace.github.io",
  base: "/LplKernel",
  trailingSlash: "ignore",
  vite: {
    plugins: [tailwindcss()],
  },
});
