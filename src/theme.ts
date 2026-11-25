// The two palettes the interface is painted in, and which of them is in use.
//
// Interface colours only - the eight colours of the document live in the
// resume, are edited by the user and end up in the PDF. These are the values
// both earlier front ends shared, set as CSS custom properties on the root.

import type { ThemeMode } from "./api";

interface Palette {
  window: string; // behind everything
  pane: string; // the form surface
  card: string; // a job / skill group card
  cardEdge: string;
  field: string; // inside an edit box
  text: string;
  subtext: string;
  accent: string;
  previewBack: string; // the desk the page sits on
}

const LIGHT: Palette = {
  window: "#f3f4f6",
  pane: "#ffffff",
  card: "#f8f9fb",
  cardEdge: "#dfe3e8",
  field: "#ffffff",
  text: "#1b1f25",
  subtext: "#606a78",
  accent: "#1e6fd9",
  previewBack: "#5a5f66",
};

const DARK: Palette = {
  window: "#202020",
  pane: "#1b1b1b",
  card: "#2a2b2e",
  cardEdge: "#3d3f44",
  field: "#2d2e31",
  text: "#e6e8ea",
  subtext: "#9aa1ac",
  accent: "#4c9bf5",
  previewBack: "#2a2c2f",
};

const systemDark = window.matchMedia("(prefers-color-scheme: dark)");
let current: ThemeMode = "system";

function paint(): void {
  const dark = current === "dark" || (current === "system" && systemDark.matches);
  const palette = dark ? DARK : LIGHT;
  const root = document.documentElement;
  for (const [name, value] of Object.entries(palette)) {
    root.style.setProperty(`--${name.replace(/[A-Z]/g, (c) => `-${c.toLowerCase()}`)}`, value);
  }
  root.style.colorScheme = dark ? "dark" : "light";
}

/** Switches the interface palette; "system" follows the desktop and repaints
 *  the moment its setting changes. */
export function applyThemeMode(mode: ThemeMode): void {
  current = mode;
  paint();
}

systemDark.addEventListener("change", () => {
  if (current === "system") paint();
});
