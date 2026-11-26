// Puts the window together and runs it: the toolbar, the editor, the preview
// and the status line, the timers, the keyboard, and the commands each button
// sends to Rust. Each screen lives in a file of its own; this one only wires
// them up.

import "./styles.css";
import * as api from "./api";
import type { Cv, Opened, ThemeMode } from "./api";
import { el, append } from "./dom";
import { Editor } from "./editor/editor";
import { showError } from "./error-dialog";
import { History } from "./history";
import { Preview } from "./preview";
import { printDocument } from "./print";
import { splitter } from "./splitter";
import { StatusBar } from "./statusbar";
import { applyThemeMode } from "./theme";
import { Toolbar } from "./toolbar";

/** Quiet time before the preview redraws, in ms. */
const REFRESH_DELAY = 120;
/** A separate, slower debounce for undo: otherwise every pause between two
 *  words would be its own step. */
const SNAPSHOT_DELAY = 500;
/** The recovery snapshot costs a file write; losing at most half a minute of
 *  work to a power cut is the trade. */
const AUTOSAVE_INTERVAL = 30_000;

async function start(): Promise<void> {
  const [info, defaults, presets, mode] = await Promise.all([
    api.appInfo(),
    api.defaults(),
    api.presets(),
    api.themeMode(),
  ]);
  applyThemeMode(mode);

  const history = new History();
  let dirty = false;
  let refreshTimer = 0;
  let snapshotTimer = 0;
  let renderSequence = 0;

  const status = new StatusBar(info.displayVersion);
  const preview = new Preview(() => toolbar.setPreviewState(preview.page, preview.pageCount, preview.zoom));
  const editor = new Editor(defaults, presets, () => edited());

  const setDirty = (value: boolean): void => {
    if (dirty === value) return;
    dirty = value;
    void api.setDirty(value);
  };

  const refreshPreview = async (): Promise<void> => {
    window.clearTimeout(refreshTimer);
    // Answers can overtake each other; only the newest request is drawn.
    const sequence = ++renderSequence;
    try {
      const doc = await api.render(editor.collect());
      if (sequence === renderSequence) preview.show(doc);
    } catch (error) {
      status.set(api.errorText(error));
    }
  };

  const takeSnapshot = (): void => {
    window.clearTimeout(snapshotTimer);
    history.record(JSON.stringify(editor.collect()));
    toolbar.setHistory(history.canUndo, history.canRedo);
  };

  function edited(): void {
    setDirty(true);
    window.clearTimeout(refreshTimer);
    refreshTimer = window.setTimeout(() => void refreshPreview(), REFRESH_DELAY);
    window.clearTimeout(snapshotTimer);
    snapshotTimer = window.setTimeout(takeSnapshot, SNAPSHOT_DELAY);
  }

  const load = (opened: Opened): void => {
    editor.setCv(opened.cv);
    window.clearTimeout(snapshotTimer);
    history.reset(JSON.stringify(editor.collect()));
    toolbar.setHistory(false, false);
    dirty = opened.dirty;
    status.set(opened.status);
    void refreshPreview();
  };

  const applySnapshot = (state: string): void => {
    editor.setCv(JSON.parse(state) as Cv);
    setDirty(true);
    toolbar.setHistory(history.canUndo, history.canRedo);
    void refreshPreview();
  };

  // Commands run one at a time: a second click while a dialog is open waits
  // for the first to finish instead of opening a second dialog.
  let queue: Promise<void> = Promise.resolve();
  const run = (failure: string, task: () => Promise<void>): void => {
    queue = queue.then(async () => {
      try {
        await task();
      } catch (error) {
        await showError(failure, api.errorText(error));
      }
    });
  };

  const opening = (call: (cv: Cv) => Promise<Opened | null>) => () =>
    run("Не удалось открыть файл:", async () => {
      const opened = await call(editor.collect());
      if (opened) load(opened);
    });

  const saving = (call: (cv: Cv) => Promise<string | null>, failure: string) => () =>
    run(failure, async () => {
      const message = await call(editor.collect());
      if (message === null) return;
      if (call !== api.exportPdf) dirty = false; // Rust cleared its own flag
      status.set(message);
    });

  const actions = {
    newDocument: opening(api.newDocument),
    open: opening(api.openDialog),
    openRecent: (index: number) => opening((cv) => api.openRecent(index, cv))(),
    openPending: opening(api.openPending),
    save: saving(api.save, "Не удалось сохранить:"),
    saveAs: saving(api.saveAs, "Не удалось сохранить:"),
    exportPdf: saving(api.exportPdf, "Не удалось записать PDF:"),
    print: () =>
      run("Не удалось начать печать.", async () => {
        const doc = await api.render(editor.collect());
        if (doc.pages.length === 0) return;
        await printDocument(doc);
        status.set("Отправлено на печать");
      }),
    undo: () => {
      // Whatever was typed in the last half second is not recorded yet; without
      // this the first Ctrl+Z would throw it away instead of undoing it.
      takeSnapshot();
      const state = history.undo();
      if (state === null) {
        status.set("Отменять нечего");
        return;
      }
      applySnapshot(state);
      status.set("Отменено");
    },
    redo: () => {
      const state = history.redo();
      if (state === null) {
        status.set("Возвращать нечего");
        return;
      }
      applySnapshot(state);
      status.set("Возвращено");
    },
    close: () =>
      run("Не удалось сохранить:", async () => {
        await api.closeWindow(editor.collect());
      }),
  };

  const toolbar = new Toolbar({
    ...actions,
    recentFiles: () => api.recentFiles(),
    clearRecent: () => void api.clearRecent(),
    setThemeMode: (value: ThemeMode) => {
      applyThemeMode(value);
      void api.setThemeMode(value);
    },
    previousPage: () => preview.setPage(preview.page - 1),
    nextPage: () => preview.setPage(preview.page + 1),
    zoomOut: () => preview.setZoom(preview.zoom - 10),
    zoomIn: () => preview.setZoom(preview.zoom + 10),
    zoomToFit: () => preview.zoomToFit(),
  });
  toolbar.setThemeMode(mode);
  toolbar.setHistory(false, false);

  const panes = el("main", { className: "panes" });
  append(panes, editor.element, splitter(panes), preview.element);
  append(document.body, toolbar.element, panes, status.element);

  // The keyboard of the earlier versions. Undo and redo are the document's,
  // also inside a text field, as with the Win32 accelerators.
  window.addEventListener("keydown", (event) => {
    if (!(event.ctrlKey || event.metaKey) || event.altKey) return;
    const key = event.key.toLowerCase();
    const shift = event.shiftKey;
    const commands: Record<string, (() => void) | undefined> = {
      n: actions.newDocument,
      o: actions.open,
      s: shift ? actions.saveAs : actions.save,
      e: actions.exportPdf,
      p: actions.print,
      z: shift ? actions.redo : actions.undo,
      y: actions.redo,
      q: actions.close,
      "=": () => preview.setZoom(preview.zoom + 10),
      "+": () => preview.setZoom(preview.zoom + 10),
      "-": () => preview.setZoom(preview.zoom - 10),
    };
    const command = commands[key] ?? commandByCode(event.code, shift, actions);
    if (!command) return;
    event.preventDefault();
    command();
  });

  window.setInterval(() => {
    if (dirty) void api.writeRecovery(editor.collect()).catch(() => undefined);
  }, AUTOSAVE_INTERVAL);

  await api.onPendingFiles(() => actions.openPending());
  await api.onCloseRequested(() => actions.close());

  load(await api.startup());
}

/** The same shortcuts on a non-Latin keyboard layout, by physical key. */
function commandByCode(
  code: string,
  shift: boolean,
  actions: Record<"newDocument" | "open" | "save" | "saveAs" | "exportPdf" | "print" | "undo" | "redo" | "close", () => void>,
): (() => void) | undefined {
  switch (code) {
    case "KeyN":
      return actions.newDocument;
    case "KeyO":
      return actions.open;
    case "KeyS":
      return shift ? actions.saveAs : actions.save;
    case "KeyE":
      return actions.exportPdf;
    case "KeyP":
      return actions.print;
    case "KeyZ":
      return shift ? actions.redo : actions.undo;
    case "KeyY":
      return actions.redo;
    case "KeyQ":
      return actions.close;
    default:
      return undefined;
  }
}

void start();
