// The editor's two kinds of list: a list of plain strings (bullets, soft
// skills, lab items) and a list of cards (jobs, skill groups, education).
//
// Each owns the array it edits. Typing changes an item in place and only
// reports the change, so the field keeps its focus; adding, moving and
// removing rebuild the list and then report.

import { el, replaceChildren } from "../dom";
import { addButton, field, rowTools } from "./widgets";

export function stringList(items: string[], placeholder: string, changed: () => void): HTMLElement {
  const rows = el("div", { className: "rows" });
  const structural = (): void => {
    render();
    changed();
  };
  const render = (): void => {
    replaceChildren(
      rows,
      ...items.map((value, index) =>
        el(
          "div",
          { className: "row" },
          field(value, placeholder, (text) => {
            items[index] = text;
            changed();
          }),
          rowTools(items, index, structural),
        ),
      ),
    );
  };
  render();
  return el(
    "div",
    { className: "string-list" },
    rows,
    addButton("+ Добавить", () => {
      items.push("");
      structural();
      const inputs = rows.querySelectorAll("input");
      inputs[inputs.length - 1]?.focus();
    }),
  );
}

/** A column of cards with ↑ ↓ ✕ above each and an add button below. */
export function cardList<T>(
  items: T[],
  addLabel: string,
  create: () => T,
  card: (item: T, changed: () => void) => Node[],
  changed: () => void,
): HTMLElement {
  const column = el("div", { className: "cards" });
  const structural = (): void => {
    render();
    changed();
  };
  const render = (): void => {
    replaceChildren(
      column,
      ...items.map((item, index) =>
        el("div", { className: "card" }, el("div", { className: "card-tools" }, rowTools(items, index, structural)), ...card(item, changed)),
      ),
    );
  };
  render();
  return el(
    "div",
    { className: "card-list" },
    column,
    addButton(addLabel, () => {
      items.push(create());
      structural();
    }),
  );
}
