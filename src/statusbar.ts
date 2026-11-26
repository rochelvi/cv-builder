// The status line: what just happened on the left, the build in the corner.

import { el } from "./dom";

export class StatusBar {
  readonly element: HTMLElement;
  private readonly message: HTMLElement;

  constructor(displayVersion: string) {
    this.message = el("span", { className: "status-message" });
    this.element = el("footer", { className: "statusbar" }, this.message, el("span", { className: "version" }, displayVersion));
  }

  set(text: string): void {
    this.message.textContent = text;
  }

  get text(): string {
    return this.message.textContent ?? "";
  }
}
