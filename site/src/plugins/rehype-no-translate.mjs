// Mark code, maths and diagrams as non-translatable.
//
// The book is written in French, so Chrome/Edge/Safari offer to machine-translate
// it for non-French readers — which is exactly what we want for the prose, and
// exactly what we do NOT want for anything else. Translating a KaTeX subtree
// rewrites the spans it uses for glyph positioning and visibly mangles the
// formula; translating identifiers turns `Fixed32` into prose. `translate="no"`
// is the standard opt-out and is honoured by every engine that offers
// translation.
//
// Must run AFTER rehype-katex, so the .katex wrappers already exist.
import { visit } from "unist-util-visit";

const SKIP_TAGS = new Set(["code", "pre", "kbd", "samp", "var"]);
const SKIP_CLASSES = ["katex", "katex-display", "mermaid"];

function hasSkipClass(node) {
  const cls = node.properties?.className;
  if (!cls) return false;
  const list = Array.isArray(cls) ? cls : String(cls).split(/\s+/);
  return list.some((c) => SKIP_CLASSES.includes(c));
}

export default function rehypeNoTranslate() {
  return (tree) => {
    visit(tree, "element", (node) => {
      if (SKIP_TAGS.has(node.tagName) || hasSkipClass(node)) {
        node.properties = { ...node.properties, translate: "no" };
      }
    });
  };
}
