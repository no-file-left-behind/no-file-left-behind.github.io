// No File Left Behind: the reference model. The pattern's rules with no window attached.
//
// It holds the documents one action is closing and answers one question at a time: what should the
// window show now, and what is the application being asked to do? A window (in any toolkit) draws
// `snapshot()` and passes clicks in; the application does the real saving and reports back.
//
// The model never touches a file. When it needs one it sets `request`:
//   { type: "pick", id, why: "never-saved" | "read-only" }   show a save picker for this document
//   { type: "save", id, path }                              write this document to this path
// and the application answers with picked(path) / pickCancelled() or saveDone() / saveFailed(reason).
//
// Every input returns true when it was accepted and false when it was ignored (a button that is
// disabled or waiting). Rule numbers refer to SPEC.md.

export class Closing {
  // docs: the documents this action closes, in the order the user sees them (1.3, 2.2).
  // Each: { id, name, path (null if never saved), dirty, readOnly }.
  constructor(docs) {
    this.docs = docs.map((d) => ({
      id: d.id, name: d.name, path: d.path ?? null, readOnly: !!d.readOnly,
      state: d.dirty ? "unsaved" : "clean", // clean | unsaved | saved (saved by this window, still open)
      open: true, ticked: !!d.dirty,          // 2.4: every row starts ticked
      note: null,                             // { kind: "failed" | "cancelled", text }
      savedTo: null, lost: false,
    }));
    this.request = null;   // what the application is being asked to do
    this.run = null;       // the saves in progress: { queue, mode: "selected" | "row" | "single", failed }
    this.outcome = null;   // null while asking; "proceeded" (the action goes ahead) or "stopped" (3.3)
    this.abandoned = [];   // saves cancelled part-way; the application must not leave half a file (6.2)
    const unsaved = this.docs.filter((d) => d.state === "unsaved").length;
    this.single = unsaved === 1;               // 1.2: one unsaved document keeps the single prompt
    if (unsaved === 0) this.#proceed();        // nothing to ask
  }

  // ---- what the window shows -------------------------------------------------------------------
  #listed() { return this.docs.filter((d) => d.open && d.state !== "clean"); }
  #unsaved() { return this.#listed().filter((d) => d.state === "unsaved"); }
  #doc(id) { return this.docs.find((d) => d.id === id); }

  snapshot() {
    const listed = this.outcome ? [] : this.#listed();
    return {
      prompt: this.outcome ? "none" : this.single && listed.length === 1 ? "single" : "list", // 1.1, 1.2
      count: this.outcome ? 0 : this.#unsaved().length,                                       // 2.1
      rows: listed.map((d) => ({                                                              // 2.2, 1.4
        id: d.id, name: d.name, where: d.path, readOnly: d.readOnly, ticked: d.state === "unsaved" && d.ticked,
        state: this.request && this.request.type === "save" && this.request.id === d.id ? "saving" : d.state,
        note: d.note,
      })),
      request: this.request,
      outcome: this.outcome,
      open: this.docs.filter((d) => d.open).map((d) => d.id),
      closed: this.docs.filter((d) => !d.open).map((d) => d.id),
      saved: Object.fromEntries(this.docs.filter((d) => d.savedTo).map((d) => [d.id, d.savedTo])),
      lost: this.docs.filter((d) => d.lost).map((d) => d.id), // closed with unsaved changes, by the user's choice
      abandoned: this.abandoned.slice(),
    };
  }

  // ---- the window's buttons --------------------------------------------------------------------
  #idle() { return !this.outcome && !this.request; }

  tick(id, on) {
    const d = this.#doc(id);
    if (!this.#idle() || !d || !d.open || d.state !== "unsaved") return false;
    if (this.snapshot().prompt === "single") return false; // the single prompt has no tick box
    d.ticked = !!on; return true;                       // 4.4: only this row changes
  }

  // Save Selected (3.1), or Save in the single prompt.
  saveSelected() {
    if (!this.#idle()) return false;
    const queue = this.#unsaved().filter((d) => d.ticked).map((d) => d.id); // row order
    return this.#start(queue, this.snapshot().prompt === "single" ? "single" : "selected");
  }

  // Don't Save (3.2): everything listed closes without saving.
  dontSave() {
    if (!this.#idle()) return false;
    this.#proceed(); return true;
  }

  // Cancel, Escape, or the window's close button (3.3, 2.3, 5.1).
  cancel() {
    if (this.outcome) return false;
    if (this.request && this.request.type === "pick") return false; // the picker is in front; answer it first
    if (this.request) this.abandoned.push(this.request.id);          // 6.2: abandon the save in progress
    this.request = null; this.run = null; this.outcome = "stopped";
    return true;
  }

  // ---- a row's own buttons (section 4) ---------------------------------------------------------
  rowSave(id) {
    const d = this.#doc(id);
    if (!this.#idle() || !d || !d.open || d.state !== "unsaved") return false;
    return this.#start([id], "row");
  }

  rowDiscard(id) {
    const d = this.#doc(id);
    if (!this.#idle() || !d || !d.open || d.state !== "unsaved") return false;
    d.open = false; d.lost = true;                      // 4.2
    this.#lastRowGone(); return true;
  }

  // ---- the application's answers ---------------------------------------------------------------
  picked(path) {
    if (!this.request || this.request.type !== "pick") return false;
    this.request = { type: "save", id: this.request.id, path }; return true;
  }

  pickCancelled() {
    if (!this.request || this.request.type !== "pick") return false;
    this.#doc(this.request.id).note = { kind: "cancelled", text: null };
    this.run.failed = true;                             // 3.1.3: a cancelled picker stops the run
    this.request = null; this.#finishRun(); return true;
  }

  saveDone() {
    if (!this.request || this.request.type !== "save") return false;
    const d = this.#doc(this.request.id);
    d.path = this.request.path; d.readOnly = false; d.savedTo = d.path; d.note = null;
    d.state = this.run.changed ? "unsaved" : "saved";   // 6.5: changed while it was being saved, so not all of it is saved
    this.run.changed = false;
    this.run.queue.shift(); this.request = null; this.#next(); return true;
  }

  saveFailed(reason) {
    if (!this.request || this.request.type !== "save") return false;
    this.#doc(this.request.id).note = { kind: "failed", text: reason }; // 3.4: the row says why
    this.run.failed = true; this.run.changed = false;
    this.run.queue.shift(); this.request = null; this.#next(); return true; // 3.1.3: the others still save
  }

  // A document in the set changed while the window was open (6.5).
  docChanged(id) {
    const d = this.#doc(id);
    if (this.outcome || !d || !d.open) return false;
    if (this.request && this.request.type === "save" && this.request.id === id) { this.run.changed = true; return true; } // told on saveDone
    if (d.state === "clean") d.ticked = true;                // a new row joins the list, ticked (2.4)
    d.state = "unsaved"; d.savedTo = null; return true;
  }

  // ---- how a run of saves goes -----------------------------------------------------------------
  #start(queue, mode) {
    for (const id of queue) this.#doc(id).note = null;
    this.run = { queue: queue.slice(), mode, failed: false, changed: false, target: mode === "row" ? queue[0] : null };
    this.#next(); return true;
  }

  #next() {
    if (this.run.queue.length === 0) return this.#finishRun();
    const d = this.#doc(this.run.queue[0]);
    // 3.1: ask where only for a document with no file to write to; 6.1: or whose file can't be written.
    if (d.path === null) this.request = { type: "pick", id: d.id, why: "never-saved" };
    else if (d.readOnly) this.request = { type: "pick", id: d.id, why: "read-only" };
    else this.request = { type: "save", id: d.id, path: d.path };
  }

  #finishRun() {
    const { mode, failed, target } = this.run;
    this.run = null;
    if (mode === "row") {
      // 4.1: a row that saved closes and leaves the list; one that didn't stays, with its reason.
      const d = this.#doc(target);
      if (d.state === "saved") d.open = false;
      return this.#lastRowGone();
    }
    // 3.1.2: everything ticked has saved, so the action goes ahead. Otherwise 3.1.1: nothing closes and the
    // window stays. A document that changed during the run is unsaved again and ticked, so it holds the
    // window open too: it must not close unasked (6.5).
    if (!failed && !this.#unsaved().some((d) => d.ticked)) this.#proceed();
  }

  // 4.3: when no unsaved row is left, the action carries on.
  #lastRowGone() { if (this.#unsaved().length === 0) this.#proceed(); }

  #proceed() {
    for (const d of this.docs) { if (d.open && d.state === "unsaved") d.lost = true; d.open = false; }
    this.request = null; this.run = null; this.outcome = "proceeded";
  }
}
