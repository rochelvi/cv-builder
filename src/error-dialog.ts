// The message box for a failed command: a heading saying what could not be
// done, and the reason Rust gave.

import { el } from "./dom";

export function showError(heading: string, reason: string): Promise<void> {
  return new Promise((resolve) => {
    const ok = el("button", { className: "primary", attrs: { type: "button" } }, "OK");
    const dialog = el(
      "dialog",
      { className: "error-dialog" },
      el("h3", {}, "CV Builder"),
      el("p", {}, heading),
      el("p", { className: "reason" }, reason),
      el("div", { className: "buttons" }, ok),
    );
    const close = (): void => {
      dialog.close();
    };
    ok.addEventListener("click", close);
    dialog.addEventListener("close", () => {
      dialog.remove();
      resolve();
    });
    document.body.appendChild(dialog);
    dialog.showModal();
    ok.focus();
  });
}
