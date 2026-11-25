// Printing: every page of the document, full bleed on A4, through the
// system's print dialog, where the page range is chosen.
//
// The pages are the same SVG the preview draws, at physical size. The template
// background is part of the design and is printed like everything else.

import type { PreviewDoc } from "./api";
import { el } from "./dom";
import { pageSvg } from "./preview";

/** Prints `doc`; resolves once the print dialog has closed. */
export function printDocument(doc: PreviewDoc): Promise<void> {
  const sheets = el("div", { className: "print-only" });
  for (const page of doc.pages) {
    const svg = pageSvg(page, doc);
    svg.setAttribute("width", "210mm");
    svg.setAttribute("height", "297mm");
    sheets.appendChild(el("div", { className: "print-sheet" }, svg));
  }
  document.body.appendChild(sheets);
  document.body.classList.add("printing");

  return new Promise((resolve) => {
    const done = (): void => {
      window.removeEventListener("afterprint", done);
      document.body.classList.remove("printing");
      sheets.remove();
      resolve();
    };
    window.addEventListener("afterprint", done);
    // Fonts must be ready before the page is handed to the printer, or the
    // first sheet goes out in a fallback face.
    void document.fonts.ready.then(() => window.print());
  });
}
