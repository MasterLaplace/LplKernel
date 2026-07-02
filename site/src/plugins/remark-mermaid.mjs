// Turn ```mermaid fenced blocks into <pre class="mermaid"> so the client-side
// mermaid runtime renders them, instead of the syntax highlighter escaping them.
import { visit } from "unist-util-visit";

export default function remarkMermaid() {
  return (tree) => {
    visit(tree, "code", (node, index, parent) => {
      if (node.lang === "mermaid" && parent && typeof index === "number") {
        parent.children[index] = {
          type: "html",
          value: `<pre class="mermaid">${node.value}</pre>`,
        };
      }
    });
  };
}
