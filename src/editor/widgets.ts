// The small pieces every part of the editor is built from: headings, hints,
// text fields, check boxes and the ↑ ↓ ✕ tool buttons.

import { el } from "../dom";

export function heading(text: string): HTMLElement {
  return el("h2", { className: "heading" }, text);
}

export function hint(text: string): HTMLElement {
  return el("p", { className: "hint" }, text);
}

/** A one-line text field; `onEdit` runs on every keystroke with the new text. */
export function field(value: string, placeholder: string, onEdit: (text: string) => void): HTMLInputElement {
  const input = el("input", {
    className: "field",
    attrs: { type: "text", placeholder, spellcheck: "false" },
    on: { input: () => onEdit(input.value) },
  });
  input.value = value;
  return input;
}

/** A multi-line text field. Line ends come back as plain "\n". */
export function area(value: string, onEdit: (text: string) => void): HTMLTextAreaElement {
  const input = el("textarea", {
    className: "field area",
    attrs: { spellcheck: "false", rows: "6" },
    on: { input: () => onEdit(input.value) },
  });
  input.value = value;
  return input;
}

export function checkbox(label: string, checked: boolean, onToggle: (checked: boolean) => void): HTMLLabelElement {
  const box = el("input", {
    attrs: { type: "checkbox" },
    on: { change: () => onToggle(box.checked) },
  });
  box.checked = checked;
  return el("label", { className: "check" }, box, label);
}

export function toolButton(glyph: string, tip: string, onClick: () => void): HTMLButtonElement {
  return el("button", { className: "tool", title: tip, attrs: { type: "button" }, on: { click: onClick } }, glyph);
}

export function addButton(label: string, onClick: () => void): HTMLButtonElement {
  return el("button", { className: "add", attrs: { type: "button" }, on: { click: onClick } }, label);
}

/** Moves item `index` of `items` by `delta` in place; false when it would
 *  leave the list. */
export function moveItem<T>(items: T[], index: number, delta: number): boolean {
  const target = index + delta;
  if (index < 0 || index >= items.length || target < 0 || target >= items.length) return false;
  const moved = items[index] as T;
  items[index] = items[target] as T;
  items[target] = moved;
  return true;
}

/** The three tools a row or a card of a list carries. */
export function rowTools(items: unknown[], index: number, changed: () => void, removable = true): HTMLElement {
  const tools = el(
    "span",
    { className: "tools" },
    toolButton("↑", "Выше", () => {
      if (moveItem(items, index, -1)) changed();
    }),
    toolButton("↓", "Ниже", () => {
      if (moveItem(items, index, 1)) changed();
    }),
  );
  if (removable) {
    tools.appendChild(
      toolButton("✕", "Удалить", () => {
        items.splice(index, 1);
        changed();
      }),
    );
  }
  return tools;
}
