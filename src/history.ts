// Undo and redo as whole-document snapshots rather than a list of edits: the
// resume serialises losslessly, and a snapshot cannot fall out of step with
// the form the way a replayed operation can.

/** Deep enough to undo a whole editing session. */
const DEPTH = 50;

export class History {
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private baseline = "";

  /** A new document starts a new history: undoing across an Open would put
   *  the previous resume back under the new file's name. */
  reset(state: string): void {
    this.undoStack = [];
    this.redoStack = [];
    this.baseline = state;
  }

  /** Records `state` if it differs from the last one. A burst of keystrokes
   *  collapses into one step; a call that changed nothing records nothing.
   *  Editing after an undo abandons the future that was undone away. */
  record(state: string): void {
    if (state === this.baseline) return;
    this.undoStack.push(this.baseline);
    if (this.undoStack.length > DEPTH) this.undoStack.shift();
    this.baseline = state;
    this.redoStack = [];
  }

  /** The state to go back to, or null when there is none. */
  undo(): string | null {
    const state = this.undoStack.pop();
    if (state === undefined) return null;
    this.redoStack.push(this.baseline);
    this.baseline = state;
    return state;
  }

  redo(): string | null {
    const state = this.redoStack.pop();
    if (state === undefined) return null;
    this.undoStack.push(this.baseline);
    this.baseline = state;
    return state;
  }

  get canUndo(): boolean {
    return this.undoStack.length > 0;
  }

  get canRedo(): boolean {
    return this.redoStack.length > 0;
  }
}
