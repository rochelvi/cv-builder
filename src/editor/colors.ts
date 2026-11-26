// The "Цвета" block: a preset list and a swatch for each of the eight colour
// roles of the document.

import { THEME_ROLES, type Preset, type Theme } from "../api";
import { el } from "../dom";
import { THEME_ROLE_NAMES } from "../text";

const HEX = /^#?([0-9a-fA-F]{6})$/;

/** What the renderer would draw for this value: "#rrggbb", or black for
 *  anything half-typed. */
function swatchColor(value: string): string {
  const match = HEX.exec(value);
  return match ? `#${match[1]!.toLowerCase()}` : "#000000";
}

/** Black or white, whichever stays readable on `hex`. */
function readableOn(hex: string): string {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  const luma = Math.floor((r * 299 + g * 587 + b * 114) / 1000);
  return luma > 140 ? "#000000" : "#ffffff";
}

function sameTheme(a: Theme, b: Theme): boolean {
  return THEME_ROLES.every((role) => a[role] === b[role]);
}

export function colorsBlock(theme: Theme, presets: Preset[], changed: () => void): HTMLElement {
  const select = el("select", { className: "field" });
  // A hand-picked palette matches no preset, and an empty choice says so more
  // honestly than the name of the last preset applied.
  select.appendChild(el("option", { attrs: { value: "", disabled: "", hidden: "" } }));
  presets.forEach((preset, index) => select.appendChild(el("option", { attrs: { value: String(index) } }, preset.name)));

  const syncPreset = (): void => {
    const index = presets.findIndex((preset) => sameTheme(preset.theme, theme));
    select.value = index < 0 ? "" : String(index);
  };

  const swatches = new Map<string, HTMLButtonElement>();
  const paint = (role: keyof Theme): void => {
    const button = swatches.get(role);
    if (!button) return;
    const color = swatchColor(theme[role]);
    button.style.backgroundColor = color;
    button.style.color = readableOn(color);
    button.textContent = theme[role];
  };

  const grid = el("div", { className: "swatches" });
  for (const role of THEME_ROLES) {
    const picker = el("input", { className: "picker", attrs: { type: "color", tabindex: "-1" } });
    picker.addEventListener("change", () => {
      theme[role] = picker.value.toLowerCase();
      paint(role);
      syncPreset();
      changed();
    });
    const button = el("button", {
      className: "swatch",
      attrs: { type: "button" },
      on: {
        click: () => {
          picker.value = swatchColor(theme[role]);
          picker.click();
        },
      },
    });
    swatches.set(role, button);
    paint(role);
    grid.appendChild(el("div", { className: "swatch-row" }, button, picker, el("span", {}, THEME_ROLE_NAMES[role])));
  }

  select.addEventListener("change", () => {
    const preset = presets[Number(select.value)];
    if (!preset) return;
    for (const role of THEME_ROLES) {
      theme[role] = preset.theme[role];
      paint(role);
    }
    changed();
  });
  syncPreset();

  return el("div", { className: "colors" }, el("label", { className: "inline" }, "Пресет", select), grid);
}
