// @ts-check
import { defineConfig } from "astro/config";
import tailwindcss from "@tailwindcss/vite";
import remarkMath from "remark-math";
import rehypeKatex from "rehype-katex";
import remarkMermaid from "./src/plugins/remark-mermaid.mjs";

// GitHub Pages project site served from the `gh-pages` branch of LplKernel.
// If you later attach a custom domain, drop `base` and update `site`.
export default defineConfig({
  site: "https://masterlaplace.github.io",
  base: "/LplKernel",
  trailingSlash: "ignore",
  markdown: {
    remarkPlugins: [remarkMath, remarkMermaid],
    rehypePlugins: [rehypeKatex],
    shikiConfig: { theme: "github-dark" },
  },
  vite: {
    plugins: [tailwindcss()],
  },
});
