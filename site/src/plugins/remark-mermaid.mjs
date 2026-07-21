// Turn ```mermaid fenced blocks into <pre class="mermaid"> so the client-side
// mermaid runtime renders them, instead of the syntax highlighter escaping them.
//
// Accessibility: a rendered Mermaid diagram is an SVG a screen reader cannot
// interpret. When the block is immediately followed by a caption paragraph
// starting with `**Figure N — …**`, the two are wrapped into a single
// <figure>/<figcaption> pair and the diagram is exposed as
// role="img" aria-labelledby="<caption id>". The caption stays *visible* — it
// documents the diagram for sighted readers too, which is what keeps it
// maintained. Without a caption we fall back to a generic label.
import { visit } from "unist-util-visit";

/** Does this mdast node look like a `**Figure N — …**` caption paragraph? */
function isFigureCaption(node) {
  if (!node || node.type !== "paragraph") return false;
  const first = node.children?.[0];
  if (!first || first.type !== "strong") return false;
  const text = (first.children ?? []).map((c) => c.value ?? "").join("");
  return /^Figure\b/i.test(text.trim());
}

export default function remarkMermaid() {
  return (tree) => {
    // Collect first, splice after: mutating children mid-visit is unsafe.
    const hits = [];
    visit(tree, "code", (node, index, parent) => {
      if (node.lang === "mermaid" && parent && typeof index === "number") {
        hits.push({ node, index, parent });
      }
    });

    // Number ids in document order, but splice back-to-front so the earlier
    // indices stay valid.
    hits.forEach((h, i) => (h.id = `figcap-${i + 1}`));
    for (const { node, index, parent, id } of hits.reverse()) {
      const caption = parent.children[index + 1];
      const hasCaption = isFigureCaption(caption);

      const pre = hasCaption
        ? `<pre class="mermaid" role="img" aria-labelledby="${id}">${node.value}</pre>`
        : `<pre class="mermaid" role="img" aria-label="Diagramme technique (description non fournie)">${node.value}</pre>`;

      if (!hasCaption) {
        parent.children[index] = { type: "html", value: pre };
        continue;
      }

      // <figure> + diagram + <figcaption> wrapping the (still markdown) caption.
      parent.children.splice(
        index,
        2,
        { type: "html", value: `<figure class="lpl-figure" role="group" aria-labelledby="${id}">` },
        { type: "html", value: pre },
        { type: "html", value: `<figcaption id="${id}">` },
        caption,
        { type: "html", value: `</figcaption></figure>` },
      );
    }
  };
}
