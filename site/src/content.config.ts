import { defineCollection, z } from "astro:content";
import { glob } from "astro/loaders";

const blog = defineCollection({
  loader: glob({ pattern: "**/*.md", base: "./src/content/blog" }),
  schema: z.object({
    title: z.string(),
    description: z.string(),
    date: z.coerce.date(),
    kind: z.enum(["systems-paper", "postmortem", "benchmark", "note"]).default("note"),
    draft: z.boolean().default(false),
  }),
});

const book = defineCollection({
  loader: glob({ pattern: "**/*.md", base: "./src/content/book" }),
  schema: z.object({
    title: z.string(),
    slug: z.string(),
    order: z.number(),
    kind: z.enum(["preamble", "chapter", "annexe", "epilogue"]),
    part: z.string().nullable().default(null),
    num: z.number().nullable().default(null),
  }),
});

export const collections = { blog, book };
