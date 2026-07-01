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

export const collections = { blog };
