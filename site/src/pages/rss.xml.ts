// RSS feed for the blog / papers section. Hand-rolled for the same reason as
// the sitemap: no extra dependency for a fixed-shape XML document.
import type { APIRoute } from "astro";
import { getCollection } from "astro:content";

const SITE = "https://masterlaplace.github.io";
const BASE = "/LplKernel";

const escape = (s: string) =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");

export const GET: APIRoute = async () => {
  const posts = (await getCollection("blog"))
    .filter((p) => !p.data.draft)
    .sort((a, b) => b.data.date.valueOf() - a.data.date.valueOf());

  const items = posts
    .map(
      (p) => `    <item>
      <title>${escape(p.data.title)}</title>
      <link>${SITE}${BASE}/blog/${p.id}</link>
      <guid isPermaLink="true">${SITE}${BASE}/blog/${p.id}</guid>
      <description>${escape(p.data.description)}</description>
      <pubDate>${p.data.date.toUTCString()}</pubDate>
    </item>`,
    )
    .join("\n");

  const body = `<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0">
  <channel>
    <title>The Laplace Project — Blog &amp; Papers</title>
    <link>${SITE}${BASE}/blog</link>
    <description>Systems papers, technical postmortems, and benchmarks from building the Laplace stack.</description>
    <language>en</language>
${items}
  </channel>
</rss>
`;

  return new Response(body, { headers: { "Content-Type": "application/xml; charset=utf-8" } });
};
