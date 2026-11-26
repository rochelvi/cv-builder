// The right pane: one A4 page at a time, drawn as SVG from what Rust laid out.
//
// The browser does not lay out a single character. Every one is placed at the
// x the layout engine computed from the bundled font's advance widths - the
// same font, served with the page, that the PDF embeds - so the preview wraps,
// breaks and spaces exactly as the export does.

import type { PreviewDoc, PreviewPage } from "./api";
import { el, replaceChildren, svg } from "./dom";

/** Margin around the sheet, in pixels. */
const PAGE_GAP = 16;
const MIN_ZOOM = 50;
const MAX_ZOOM = 300;

/** One page as an SVG in points; the caller decides how big it is shown. */
export function pageSvg(page: PreviewPage, doc: PreviewDoc): SVGSVGElement {
  const root = svg("svg", {
    class: "page",
    viewBox: `0 0 ${doc.width} ${doc.height}`,
    preserveAspectRatio: "xMinYMin meet",
  });
  root.appendChild(svg("rect", { x: 0, y: 0, width: doc.width, height: doc.height, fill: page.background }));
  for (const line of page.lines) {
    root.appendChild(
      svg("line", {
        x1: line.x1,
        y1: line.y1,
        x2: line.x2,
        y2: line.y2,
        stroke: line.color,
        "stroke-width": line.width,
      }),
    );
  }
  for (const run of page.runs) {
    const text = svg("text", {
      x: run.xs.join(" "),
      y: run.y,
      "font-size": run.size,
      "font-weight": run.bold ? 700 : 400,
      fill: run.color,
    });
    text.textContent = run.text;
    root.appendChild(text);
  }
  return root;
}

export class Preview {
  readonly element: HTMLElement;
  private readonly sheet: HTMLElement;
  private doc: PreviewDoc | null = null;
  private pageIndex = 0;
  private zoomPercent = 100;

  constructor(private readonly onState: () => void) {
    this.sheet = el("div", { className: "sheet" });
    this.element = el("div", { className: "preview" }, this.sheet);
    // Ctrl+wheel zooms, as in every document viewer; a plain wheel scrolls.
    this.element.addEventListener(
      "wheel",
      (event) => {
        if (!event.ctrlKey) return;
        event.preventDefault();
        this.setZoom(this.zoomPercent + (event.deltaY < 0 ? 10 : -10));
      },
      { passive: false },
    );
  }

  get page(): number {
    return this.pageIndex;
  }

  get pageCount(): number {
    return Math.max(1, this.doc?.pages.length ?? 0);
  }

  get zoom(): number {
    return this.zoomPercent;
  }

  get document(): PreviewDoc | null {
    return this.doc;
  }

  show(doc: PreviewDoc): void {
    this.doc = doc;
    // A shorter document than before keeps the reader on its last page.
    this.pageIndex = Math.min(this.pageIndex, Math.max(0, doc.pages.length - 1));
    this.draw();
    this.onState();
  }

  setPage(index: number): void {
    const last = Math.max(0, (this.doc?.pages.length ?? 1) - 1);
    this.pageIndex = Math.max(0, Math.min(last, index));
    this.element.scrollTop = 0;
    this.draw();
    this.onState();
  }

  setZoom(percent: number): void {
    this.zoomPercent = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, Math.round(percent)));
    this.resize();
    this.onState();
  }

  /** The whole page height in view. */
  zoomToFit(): void {
    if (!this.doc) return;
    const available = this.element.clientHeight - 2 * PAGE_GAP;
    if (available <= 0) return;
    this.setZoom(Math.floor((available / this.doc.height) * 100));
  }

  private draw(): void {
    const page = this.doc?.pages[this.pageIndex];
    if (!this.doc || !page) {
      replaceChildren(this.sheet);
      return;
    }
    replaceChildren(this.sheet, pageSvg(page, this.doc));
    this.resize();
  }

  private resize(): void {
    if (!this.doc) return;
    const scale = this.zoomPercent / 100;
    this.sheet.style.width = `${Math.ceil(this.doc.width * scale)}px`;
    this.sheet.style.height = `${Math.ceil(this.doc.height * scale)}px`;
  }
}
