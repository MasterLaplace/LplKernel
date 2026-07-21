// Hand-rolled sitemap. Astro has an official integration, but it would be a new
// dependency (and a new CI install step) for ~30 lines of XML we can emit from
// the collections we already read.
import type { APIRoute } from "astro";
import { getCollection } from "astro:content";

const SITE = "https://masterlaplace.github.io";
const BASE = "/LplKernel";

export const GET: APIRoute = async () => {
  const [blog, book] = await Promise.all([getCollection("blog"), getCollection("book")]);

  const urls: { loc: string; lastmod?: string; priority: string }[] = [
    { loc: `${BASE}`, priority: "1.0" },
    { loc: `${BASE}/projects`, priority: "0.8" },
    { loc: `${BASE}/roadmap`, priority: "0.8" },
    { loc: `${BASE}/book`, priority: "0.9" },
    { loc: `${BASE}/blog`, priority: "0.9" },
    ...blog.map((p) => ({
      loc: `${BASE}/blog/${p.id}`,
      lastmod: p.data.date.toISOString().slice(0, 10),
      priority: "0.7",
    })),
    ...book.map((c) => ({ loc: `${BASE}/book/${c.data.slug}`, priority: "0.6" })),
  ];

  const body = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
${urls
  .map(
    (u) =>
      `  <url><loc>${SITE}${u.loc}</loc>` +
      (u.lastmod ? `<lastmod>${u.lastmod}</lastmod>` : "") +
      `<priority>${u.priority}</priority></url>`,
  )
  .join("\n")}
</urlset>
`;

  return new Response(body, { headers: { "Content-Type": "application/xml; charset=utf-8" } });
};
