// The divider between the editor and the preview. A splitter rather than a
// fixed half: the two panes want different amounts of room depending on the
// screen, and the user knows which.

import { el } from "./dom";

const MIN_PANE = 280;

export function splitter(container: HTMLElement): HTMLElement {
  const handle = el("div", { className: "splitter", attrs: { role: "separator", "aria-orientation": "vertical" } });
  handle.addEventListener("pointerdown", (start) => {
    start.preventDefault();
    handle.setPointerCapture(start.pointerId);
    const bounds = container.getBoundingClientRect();
    const move = (event: PointerEvent): void => {
      const left = Math.max(MIN_PANE, Math.min(bounds.width - MIN_PANE, event.clientX - bounds.left));
      container.style.gridTemplateColumns = `${left}px 6px 1fr`;
    };
    const stop = (): void => {
      handle.removeEventListener("pointermove", move);
      handle.removeEventListener("pointerup", stop);
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", stop);
  });
  return handle;
}
