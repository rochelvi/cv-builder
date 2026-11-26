// The toolbar across the top: file commands, undo and redo, the interface
// theme on the left; page navigation and zoom hugging the right edge.

import type { ThemeMode } from "./api";
import { el, replaceChildren } from "./dom";
import { THEME_MODES } from "./text";

export interface ToolbarActions {
  newDocument(): void;
  open(): void;
  openRecent(index: number): void;
  recentFiles(): Promise<string[]>;
  clearRecent(): void;
  save(): void;
  saveAs(): void;
  exportPdf(): void;
  print(): void;
  undo(): void;
  redo(): void;
  setThemeMode(mode: ThemeMode): void;
  previousPage(): void;
  nextPage(): void;
  zoomOut(): void;
  zoomIn(): void;
  zoomToFit(): void;
}

function button(label: string, title: string, onClick: () => void): HTMLButtonElement {
  return el("button", { className: "bar-button", title, attrs: { type: "button" }, on: { click: onClick } }, label);
}

export class Toolbar {
  readonly element: HTMLElement;
  private readonly undoButton: HTMLButtonElement;
  private readonly redoButton: HTMLButtonElement;
  private readonly prevButton: HTMLButtonElement;
  private readonly nextButton: HTMLButtonElement;
  private readonly pageLabel: HTMLElement;
  private readonly zoomLabel: HTMLElement;
  private readonly themeSelect: HTMLSelectElement;
  private readonly recentMenu: HTMLElement;

  constructor(private readonly actions: ToolbarActions) {
    this.undoButton = button("↶", "Отменить (Ctrl+Z)", () => actions.undo());
    this.redoButton = button("↷", "Вернуть (Ctrl+Y)", () => actions.redo());
    this.prevButton = button("‹", "Предыдущая страница", () => actions.previousPage());
    this.nextButton = button("›", "Следующая страница", () => actions.nextPage());
    this.pageLabel = el("span", { className: "bar-label page-label" }, "Стр. 1 / 1");
    this.zoomLabel = el("span", { className: "bar-label zoom-label" }, "100 %");

    this.themeSelect = el("select", { className: "bar-select", title: "Оформление окна" });
    for (const { mode, name } of THEME_MODES) {
      this.themeSelect.appendChild(el("option", { attrs: { value: mode } }, name));
    }
    this.themeSelect.addEventListener("change", () => actions.setThemeMode(this.themeSelect.value as ThemeMode));

    // Open is a split button: the arrow drops the list of recent files.
    this.recentMenu = el("div", { className: "menu", attrs: { role: "menu" } });
    this.recentMenu.hidden = true;
    const arrow = button("▾", "Недавние файлы", () => void this.toggleRecent());
    const openGroup = el(
      "span",
      { className: "split" },
      button("Открыть…", "Открыть резюме (Ctrl+O)", () => actions.open()),
      arrow,
      this.recentMenu,
    );
    document.addEventListener("mousedown", (event) => {
      if (!openGroup.contains(event.target as Node)) this.recentMenu.hidden = true;
    });

    this.element = el(
      "header",
      { className: "toolbar" },
      button("Новый", "Новое резюме (Ctrl+N)", () => actions.newDocument()),
      openGroup,
      button("Сохранить", "Сохранить (Ctrl+S)", () => actions.save()),
      button("Сохранить как…", "Сохранить как (Ctrl+Shift+S)", () => actions.saveAs()),
      button("Экспорт PDF…", "Экспорт PDF (Ctrl+E)", () => actions.exportPdf()),
      button("Печать…", "Печать (Ctrl+P)", () => actions.print()),
      el("span", { className: "separator" }),
      this.undoButton,
      this.redoButton,
      el("span", { className: "separator" }),
      this.themeSelect,
      el("span", { className: "spacer" }),
      this.prevButton,
      this.pageLabel,
      this.nextButton,
      el("span", { className: "separator" }),
      button("−", "Уменьшить (Ctrl+−)", () => actions.zoomOut()),
      this.zoomLabel,
      button("+", "Увеличить (Ctrl++)", () => actions.zoomIn()),
      button("Вписать", "Вписать страницу в окно", () => actions.zoomToFit()),
    );
  }

  private async toggleRecent(): Promise<void> {
    if (!this.recentMenu.hidden) {
      this.recentMenu.hidden = true;
      return;
    }
    const files = await this.actions.recentFiles();
    const item = (label: string, onClick: (() => void) | null): HTMLButtonElement => {
      const entry = el("button", { className: "menu-item", attrs: { type: "button", role: "menuitem" } }, label);
      if (onClick) {
        entry.addEventListener("click", () => {
          this.recentMenu.hidden = true;
          onClick();
        });
      } else {
        entry.disabled = true;
      }
      return entry;
    };
    if (files.length === 0) {
      replaceChildren(this.recentMenu, item("Пока ничего не открывали", null));
    } else {
      replaceChildren(
        this.recentMenu,
        ...files.map((file, index) => item(`${index + 1}. ${file}`, () => this.actions.openRecent(index))),
        el("hr", {}),
        item("Очистить список", () => this.actions.clearRecent()),
      );
    }
    this.recentMenu.hidden = false;
  }

  setHistory(canUndo: boolean, canRedo: boolean): void {
    this.undoButton.disabled = !canUndo;
    this.redoButton.disabled = !canRedo;
  }

  setPreviewState(page: number, pageCount: number, zoom: number): void {
    this.pageLabel.textContent = `Стр. ${page + 1} / ${pageCount}`;
    this.zoomLabel.textContent = `${zoom} %`;
    this.prevButton.disabled = page <= 0;
    this.nextButton.disabled = page + 1 >= pageCount;
  }

  setThemeMode(mode: ThemeMode): void {
    this.themeSelect.value = mode;
  }
}
