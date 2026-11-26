// The "Разделы" block: every section of the page, with a check box to show it,
// the heading it prints, and ↑ ↓ for the running order.

import type { SectionRef } from "../api";
import { el, replaceChildren } from "../dom";
import { SECTION_NAMES } from "../text";
import { checkbox, field, toolButton, moveItem } from "./widgets";

export function sectionsBlock(sections: SectionRef[], changed: () => void): HTMLElement {
  const rows = el("div", { className: "rows" });
  const render = (): void => {
    replaceChildren(
      rows,
      ...sections.map((section, index) =>
        el(
          "div",
          { className: "row section-row" },
          checkbox(SECTION_NAMES[section.id] ?? section.id, section.enabled, (on) => {
            section.enabled = on;
            changed();
          }),
          field(section.label, "Заголовок на странице", (text) => {
            section.label = text;
            changed();
          }),
          el(
            "span",
            { className: "tools" },
            toolButton("↑", "Выше", () => {
              if (moveItem(sections, index, -1)) {
                render();
                changed();
              }
            }),
            toolButton("↓", "Ниже", () => {
              if (moveItem(sections, index, 1)) {
                render();
                changed();
              }
            }),
          ),
        ),
      ),
    );
  };
  render();
  return rows;
}
