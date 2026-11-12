// No File Left Behind for the web: the list of unsaved work as a custom element, <unsaved-list>.
// One file, no dependencies, no build step. Rule numbers refer to SPEC.md, version 0.3.
//
//   import "./unsaved-list.js";
//   const result = await document.querySelector("unsaved-list").ask(docs, { save, pick, noun, onShow });
//
// The file has two parts. `Closing` is the reference model from scenarios/reference/model.mjs,
// copied in unchanged so this file stands alone and passes the same scenarios. `UnsavedList` is the
// window: it draws the model's snapshot, passes clicks and keys in, and answers the model's
// requests by calling the app's `pick` and `save`.

// ---- The model -----------------------------------------------------------------------------------
// It holds the documents one action is closing and answers one question at a time: what should the
// window show now, and what is the application being asked to do? It never touches a file. When it
// needs one it sets `request`:
//   { type: "pick", id, why: "never-saved" | "read-only" }   show a save picker for this document
//   { type: "save", id, path }                              write this document to this path
// and is answered with picked(path) / pickCancelled() or saveDone() / saveFailed(reason).
// Every input returns true when it was accepted and false when it was ignored (a button that is
// disabled or waiting).

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
    this.run.failed = true; this.run.queue = [];        // 3.1.3: a cancelled picker stops the run
    this.request = null; this.#finishRun(); return true;
  }

  saveDone() {
    if (!this.request || this.request.type !== "save") return false;
    const d = this.#doc(this.request.id);
    d.path = this.request.path; d.readOnly = false; d.state = "saved"; d.savedTo = d.path; d.note = null;
    this.run.queue.shift(); this.request = null; this.#next(); return true;
  }

  saveFailed(reason) {
    if (!this.request || this.request.type !== "save") return false;
    this.#doc(this.request.id).note = { kind: "failed", text: reason }; // 3.4: the row says why
    this.run.failed = true;
    this.run.queue.shift(); this.request = null; this.#next(); return true; // 3.1.3: the others still save
  }

  // A document in the set changed while the window was open (6.5).
  docChanged(id) {
    const d = this.#doc(id);
    if (this.outcome || !d || !d.open) return false;
    if (this.request && this.request.id === id) return true; // being saved now: the save takes its latest state
    if (d.state === "clean") d.ticked = true;                // a new row joins the list, ticked (2.4)
    d.state = "unsaved"; d.savedTo = null; return true;
  }

  // ---- how a run of saves goes -----------------------------------------------------------------
  #start(queue, mode) {
    for (const id of queue) this.#doc(id).note = null;
    this.run = { queue: queue.slice(), mode, failed: false, target: mode === "row" ? queue[0] : null };
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
    if (!failed) this.#proceed();   // 3.1.2. Otherwise 3.1.1: nothing closes and the window stays.
  }

  // 4.3: when no unsaved row is left, the action carries on.
  #lastRowGone() { if (this.#unsaved().length === 0) this.#proceed(); }

  #proceed() {
    for (const d of this.docs) { if (d.open && d.state === "unsaved") d.lost = true; d.open = false; }
    this.request = null; this.run = null; this.outcome = "proceeded";
  }
}

// ---- Words -----------------------------------------------------------------------------------------
// Every visible or spoken string, so an app can translate the window by passing `strings` to ask().
// Entries that need a value are functions.
export const STRINGS = {
  title: (count, noun) => `Save changes to ${count} ${count === 1 ? noun.one : noun.other}?`, // 2.1
  singleTitle: (name) => `Save changes to “${name}”?`,                                           // 1.2
  help: (noun) => `Ticked ${noun.other} are saved; the rest close without saving.`,              // 2.6
  notSavedYet: "Not saved yet",                                                                  // 2.2
  saveSelected: "Save Selected",
  dontSave: "Don't Save",
  cancel: "Cancel",
  save: "Save",
  discard: "Discard",
  tickLabel: (name) => `Save ${name}`,                                                           // 5.3
  saveLabel: (name) => `Save ${name}`,
  discardLabel: (name) => `Discard ${name}`,
  show: "Show this document",
  saving: "Saving…",                                                                             // 6.2
  saved: "Saved",                                                                                // 3.1.1
  readOnly: "The original file is read-only, so saving asks for a new place.",                   // 6.1
  cancelled: "Not saved: you cancelled choosing where.",
  failed: (reason) => reason,                                                                    // 3.4
  unknownFailure: "The save failed, and the app gave no reason.",
  announce: (name, text) => `${name}: ${text}`,
  nothingClosed: "Nothing was closed.",                                                          // 3.1.1
};

const NOUN = { one: "document", other: "documents" };

// ---- Colours ---------------------------------------------------------------------------------------
// Each has a light and a dark default; an app overrides one with --nflb-<name> on the element or above.
const COLOURS = {
  panel: ["#ffffff", "#22252c"], text: ["#1d2129", "#e7e9ee"], muted: ["#69707d", "#9aa1ad"],
  line: ["#d9dce1", "#383c45"], hover: ["#f4f5f7", "#2b2f37"], accent: ["#2f6fde", "#4a8bf2"],
  "accent-text": ["#ffffff", "#ffffff"], danger: ["#c4302b", "#ef6b66"], ok: ["#1f8a4c", "#4cc283"],
  backdrop: ["rgba(20, 24, 31, 0.45)", "rgba(0, 0, 0, 0.55)"],
};
const scheme = (i) => Object.entries(COLOURS).map(([k, v]) => `--_${k}: var(--nflb-${k}, ${v[i]});`).join(" ");

const CSS = `
:host { display: contents; ${scheme(0)} color-scheme: light; }
@media (prefers-color-scheme: dark) { :host(:not([scheme="light"])) { ${scheme(1)} color-scheme: dark; } }
:host([scheme="dark"]) { ${scheme(1)} color-scheme: dark; }
* { box-sizing: border-box; }
dialog {
  padding: 0; margin: auto; border: 1px solid var(--_line); border-radius: var(--nflb-radius, 12px);
  background: var(--_panel); color: var(--_text); font: var(--nflb-font, 15px/1.45 system-ui, -apple-system, "Segoe UI", sans-serif);
  width: min(var(--nflb-width, 560px), calc(100vw - 32px)); max-width: none;
  max-height: calc(100vh - 32px); max-height: calc(100dvh - 32px);   /* 2.5: the window fits on screen */
  box-shadow: 0 12px 40px rgba(0, 0, 0, 0.3); outline: none;
}
dialog[open] { display: flex; flex-direction: column; }
dialog::backdrop { background: var(--nflb-backdrop, rgba(20, 24, 31, 0.45)); }
h2 { font-size: 1.13em; line-height: 1.3; margin: 0; padding: 16px 18px 6px; overflow-wrap: anywhere; }
p { margin: 0; padding: 0 18px 10px; font-size: 0.87em; color: var(--_muted); overflow-wrap: anywhere; }
.rows { overflow-y: auto; padding: 0 10px 6px; min-height: 0; flex: 0 1 auto; }   /* 2.5: rows scroll, buttons stay */
.single .rows, .single .help, .list .single-where, .list .single-note { display: none; }
.single-note:empty { display: none; }
.row { display: grid; grid-template-columns: auto minmax(0, 1fr) auto; gap: 2px 10px; align-items: start;
  padding: 8px; border-radius: 8px; }
.row:hover { background: var(--_hover); }
.row input { margin: 3px 0 0; width: 16px; height: 16px; accent-color: var(--_accent); }
.name { font: inherit; font-weight: 600; color: inherit; background: none; border: none; padding: 0; margin: 0;
  text-align: left; overflow-wrap: anywhere; min-width: 0; }
button.name { cursor: pointer; border-radius: 3px; }
button.name:hover:not(:disabled) { text-decoration: underline; }
.where, .note { grid-column: 2; font-size: 0.87em; color: var(--_muted); overflow-wrap: anywhere; }
.bad { color: var(--_danger); }
.good { color: var(--_ok); }
.acts { grid-row: 1; grid-column: 3; display: flex; gap: 6px; }
footer { display: flex; flex-wrap: wrap; gap: 8px; padding: 12px 14px; border-top: 1px solid var(--_line); flex: none; }
footer .spacer { flex: 1; }
button { font: inherit; padding: 6px 14px; border-radius: 6px; border: 1px solid var(--_line);
  background: var(--_panel); color: var(--_text); cursor: pointer; }
button.primary { background: var(--_accent); border-color: var(--_accent); color: var(--_accent-text); font-weight: 600; }
button.small { padding: 2px 10px; font-size: 0.87em; }
.acts button:disabled, footer button:disabled { opacity: 0.5; cursor: default; }
button.name:disabled { cursor: default; }
button:focus-visible, input:focus-visible { outline: 2px solid var(--_accent); outline-offset: 2px; }
.announce { position: absolute; width: 1px; height: 1px; overflow: hidden; clip-path: inset(50%); white-space: nowrap; }
/* Narrow screens: a row's own buttons go under its name rather than squeezing it. */
@media (max-width: 440px) {
  .acts { grid-row: auto; grid-column: 2; order: 1; margin-top: 4px; }
  footer { padding: 10px; }
  footer .spacer { display: none; }
  footer button { flex: 1 1 auto; padding: 6px 10px; }
}
@media (forced-colors: active) { button.primary { border-width: 2px; } }
`;

const HTML = `
<dialog part="dialog" role="alertdialog" aria-modal="true" aria-labelledby="title" aria-describedby="help" tabindex="-1">
  <h2 id="title" part="title"></h2>
  <p id="help" class="help" part="help"></p>
  <p id="single-where" class="single-where" part="where"></p>
  <p class="single-note" part="note"></p>
  <div class="rows" part="rows"></div>
  <footer part="footer">
    <button class="dont-save" part="button dont-save"></button>
    <span class="spacer"></span>
    <button class="cancel" part="button cancel"></button>
    <button class="primary save-selected" part="button primary save-selected"></button>
  </footer>
  <div class="announce" role="alert"></div>
</dialog>`;

// The element we must treat as the page's focus, looking through open shadow roots.
function deepActive() {
  let el = document.activeElement;
  while (el && el.shadowRoot && el.shadowRoot.activeElement) el = el.shadowRoot.activeElement;
  return el;
}

let rowCount = 0; // ids for aria-describedby, unique within the shadow root

const Base = globalThis.HTMLElement ?? class {}; // so the model can be imported where there is no DOM

export class UnsavedList extends Base {
  #root; #dialog; #title; #help; #rowsBox; #singleWhere; #singleNote; #dontSave; #cancel; #primary; #announce;
  #m = null;          // the Closing being asked about, or null
  #opts = null;
  #docs = new Map();  // id → the app's own document object, handed back to save, pick and onShow
  #rows = new Map();  // id → { el, tick, name, save, discard, where, notes }
  #resolve = null;
  #pending = null;    // the request the app is answering now
  #abort = null;      // the AbortController for the save in progress
  #busy = false;
  #focusBefore = null; // what had focus when a run of saves began
  #returnTo = null;    // what had focus when the window opened (5.2)

  constructor() {
    super();
    this.#root = this.attachShadow({ mode: "open" });
    this.#root.innerHTML = `<style>${CSS}</style>${HTML}`;
    const $ = (s) => this.#root.querySelector(s);
    this.#dialog = $("dialog"); this.#title = $("h2"); this.#help = $(".help"); this.#rowsBox = $(".rows");
    this.#singleWhere = $(".single-where"); this.#singleNote = $(".single-note");
    this.#dontSave = $(".dont-save"); this.#cancel = $(".cancel"); this.#primary = $(".save-selected"); this.#announce = $(".announce");

    this.#primary.addEventListener("click", () => this.#input("saveSelected"));
    this.#dontSave.addEventListener("click", () => this.#input("dontSave"));
    this.#cancel.addEventListener("click", () => this.#input("cancel"));
    // Escape arrives as the dialog's cancel event (5.1). We close the window ourselves, once the model agrees.
    this.#dialog.addEventListener("cancel", (e) => { e.preventDefault(); this.#input("cancel"); });
    // If the browser closes the dialog anyway, it is a Cancel; while a picker is up it can't be, so reopen.
    this.#dialog.addEventListener("close", () => {
      if (!this.#m || this.#m.outcome || this.#dialog.open) return; // open again: a new ask has begun
      if (!this.#input("cancel") && this.#m && !this.#dialog.open) this.#dialog.showModal();
    });
    this.#dialog.addEventListener("keydown", (e) => this.#key(e));
  }

  // Ask about the documents one action closes. See README.md for the options and the result.
  ask(docs, options = {}) {
    if (this.#m) return Promise.reject(new Error("unsaved-list is already asking about other documents"));
    const opts = {
      singlePrompt: true, ...options,
      noun: { ...NOUN, ...options.noun },
      strings: { ...STRINGS, ...options.strings },
    };
    const list = docs.map((d, i) => ({ ...d, id: d.id ?? i }));
    const m = new Closing(list);
    const first = m.snapshot();
    if (first.outcome) return Promise.resolve(this.#result(first)); // nothing unsaved: no window (1.4)
    this.#docs = new Map(list.map((d, i) => [d.id, docs[i]]));
    if (first.prompt === "single" && !opts.singlePrompt) {
      // The app keeps its own single prompt (1.2): say which document it is about and show nothing.
      return Promise.resolve({ ...this.#result(first), outcome: "single", doc: this.#docs.get(first.rows[0].id) });
    }
    this.#m = m; this.#opts = opts; this.#pending = null; this.#abort = null; this.#busy = false;
    this.#rows.clear(); this.#rowsBox.replaceChildren(); this.#announce.textContent = "";
    this.#returnTo = deepActive();
    return new Promise((resolve) => {
      this.#resolve = resolve;
      this.#render();
      this.#dialog.showModal();
      this.#primary.focus(); // the default button (2.3); the title is read as the window's name (5.4)
      this.#step();
    });
  }

  // The app tells the window that a document in the set changed while it is open (6.5).
  changed(id) {
    if (!this.#m) return false;
    const ok = this.#m.docChanged(id);
    this.#step(); return ok;
  }

  // For scripts and tests (8.1): what the window shows, and the same inputs as its buttons.
  snapshot() { return this.#m ? this.#m.snapshot() : null; }
  press(input, id, on) {
    if (!["saveSelected", "dontSave", "cancel", "rowSave", "rowDiscard", "tick"].includes(input)) throw new Error(`unknown input ${input}`);
    return this.#input(input, id, on);
  }

  get open() { return !!this.#m; }

  // ---- inputs and requests ------------------------------------------------------------------------
  #input(name, ...args) {
    const m = this.#m;
    if (!m) return false;
    const ok = m[name](...args);
    if (ok && name === "cancel" && this.#abort) this.#abort.abort(); // 6.2: abandon the save in progress
    this.#step();
    return ok;
  }

  // After every change: redraw, then either finish, or hand the app the model's next request.
  #step() {
    const m = this.#m;
    if (!m) return;
    const had = this.#root.activeElement;
    const wasBusy = this.#busy;
    this.#busy = !!m.request;
    if (this.#busy && !wasBusy) this.#focusBefore = had;
    this.#render();
    if (this.#opts.onChange) this.#opts.onChange(m.snapshot()); // the app can close a row's document as soon as it is answered (4.1, 4.2)
    if (m.outcome) return this.#finish();
    if (this.#busy && had && had.disabled) this.#dialog.focus(); // keep focus in the window; Enter now does nothing
    if (!this.#busy && wasBusy) this.#afterRun();
    const r = m.request;
    if (!r || r === this.#pending) return;
    this.#pending = r;
    const doc = this.#docs.get(r.id);
    const current = () => this.#m === m && m.request === r; // an answer that comes too late is dropped
    if (r.type === "pick") {
      const pick = this.#opts.pick || (() => null);
      Promise.resolve().then(() => pick(doc, r.why)).then(
        (path) => { if (current()) { path == null ? m.pickCancelled() : m.picked(path); this.#step(); } },
        () => { if (current()) { m.pickCancelled(); this.#step(); } }, // a picker that throws (AbortError) was cancelled
      );
    } else {
      const ac = this.#abort = new AbortController();
      const save = this.#opts.save || (() => { throw new Error("The app gave no way to save."); });
      Promise.resolve().then(() => save(doc, r.path, ac.signal)).then(
        () => { if (current()) { this.#abort = null; m.saveDone(); this.#step(); } },
        (e) => { if (current()) { this.#abort = null; m.saveFailed((e && e.message) || String(e || "") || null); this.#step(); } },
      );
    }
  }

  // A run of saves ended and the window stays (3.1.1, 4.1): say why aloud, and put focus back.
  #afterRun() {
    const S = this.#opts.strings;
    const said = this.#m.snapshot().rows.filter((r) => r.note).map((r) => S.announce(r.name, this.#noteText(r.note)));
    if (said.length) {
      this.#announce.textContent = "";
      const text = [...said, S.nothingClosed].join(" ");
      setTimeout(() => { if (this.#m) this.#announce.textContent = text; }, 50); // a fresh change, so it is read again
    }
    const back = this.#focusBefore;
    this.#focusBefore = null;
    (back && back.isConnected && !back.disabled ? back : this.#primary).focus();
  }

  #finish() {
    const s = this.#m.snapshot();
    const resolve = this.#resolve;
    this.#m = null; this.#resolve = null; this.#pending = null; this.#abort = null; this.#busy = false;
    if (this.#dialog.open) this.#dialog.close();
    // 5.2: focus goes back to where it was, if that is still there.
    const back = this.#returnTo;
    this.#returnTo = null;
    if (back && back.isConnected && typeof back.focus === "function") back.focus();
    resolve(this.#result(s));
  }

  #result(s) {
    return { outcome: s.outcome, saved: s.saved, lost: s.lost, open: s.open, closed: s.closed, abandoned: s.abandoned };
  }

  // ---- keys -----------------------------------------------------------------------------------------
  #key(e) {
    // 5.1: Enter is Save Selected, unless a button has focus, which then does its own job.
    if (e.key === "Enter" && !e.isComposing && !e.altKey && !e.ctrlKey && !e.metaKey) {
      if (e.composedPath()[0] instanceof HTMLButtonElement) return;
      e.preventDefault();
      if (!this.#primary.disabled) this.#input("saveSelected");
      return;
    }
    // 5.2: Tab goes round inside the window and never out of it.
    if (e.key === "Tab") {
      const all = [...this.#root.querySelectorAll("input, button")].filter((el) => !el.disabled && el.getClientRects().length);
      if (!all.length) { e.preventDefault(); return; }
      const at = all.indexOf(this.#root.activeElement);
      if (e.shiftKey && at <= 0) { e.preventDefault(); all[all.length - 1].focus(); }
      else if (!e.shiftKey && (at === -1 || at === all.length - 1)) { e.preventDefault(); all[0].focus(); }
    }
  }

  // ---- drawing --------------------------------------------------------------------------------------
  #noteText(note) {
    const S = this.#opts.strings;
    return note.kind === "cancelled" ? S.cancelled : S.failed(note.text || S.unknownFailure);
  }

  // What a row says under its name: why it will ask where (6.1), and how its save went.
  #notes(r) {
    const S = this.#opts.strings, out = [];
    if (r.readOnly && r.state !== "saved") out.push(["", S.readOnly]);
    if (r.state === "saving") out.push(["", S.saving]);
    else if (r.state === "saved") out.push(["good", S.saved]);
    else if (r.note) out.push(["bad", this.#noteText(r.note)]);
    return out;
  }

  #render() {
    const s = this.#m.snapshot(), S = this.#opts.strings, noun = this.#opts.noun;
    const busy = !!s.request, single = s.prompt === "single";
    this.#dialog.classList.toggle("single", single);
    this.#dialog.classList.toggle("list", !single);
    this.#dialog.setAttribute("aria-describedby", single ? "single-where" : "help");
    this.#dialog.setAttribute("aria-busy", String(busy));
    this.#dontSave.textContent = S.dontSave;
    this.#cancel.textContent = S.cancel;
    this.#primary.textContent = single ? S.save : S.saveSelected;
    // 6.2: while a save runs, every button but Cancel waits. A picker is in front of the window, so it waits too.
    this.#primary.disabled = this.#dontSave.disabled = busy;
    this.#cancel.disabled = !!s.request && s.request.type === "pick";

    if (single) {
      const r = s.rows[0];
      this.#title.textContent = S.singleTitle(r.name);
      this.#singleWhere.textContent = r.where ?? S.notSavedYet;
      const notes = this.#notes(r);
      this.#singleNote.textContent = notes.map((n) => n[1]).join(" ");
      this.#singleNote.className = "single-note" + (notes.some((n) => n[0] === "bad") ? " bad" : "");
    } else {
      this.#title.textContent = S.title(s.count, noun);
      this.#help.textContent = S.help(noun); // 2.6: always shown
    }

    // Rows are kept by id and updated in place, so focus and scroll position survive a redraw.
    const keep = new Set(s.rows.map((r) => r.id));
    for (const [id, row] of this.#rows) if (!keep.has(id)) { row.el.remove(); this.#rows.delete(id); }
    s.rows.forEach((r, i) => {
      const row = this.#rows.get(r.id) || this.#makeRow(r);
      if (this.#rowsBox.children[i] !== row.el) this.#rowsBox.insertBefore(row.el, this.#rowsBox.children[i] || null);
      const answerable = !busy && r.state === "unsaved";
      row.tick.checked = r.ticked;
      row.tick.disabled = row.save.disabled = row.discard.disabled = !answerable;
      if (row.name.tagName === "BUTTON") row.name.disabled = busy;
      row.where.textContent = r.where ?? S.notSavedYet;
      row.el.setAttribute("aria-busy", String(r.state === "saving"));
      const notes = this.#notes(r);
      const ids = [row.where.id];
      row.notes.replaceChildren(...notes.map(([kind, text], k) => {
        const n = document.createElement("span");
        n.className = "note " + kind; n.setAttribute("part", "note" + (kind ? " note-" + kind : "")); n.textContent = text;
        n.id = row.where.id + "-n" + k; ids.push(n.id);
        return n;
      }));
      row.tick.setAttribute("aria-describedby", ids.join(" ")); // where it lives and what happened, read with the tick box
    });
  }

  #makeRow(r) {
    const S = this.#opts.strings;
    const el = document.createElement("div");
    el.className = "row"; el.setAttribute("part", "row");
    el.setAttribute("role", "group"); el.setAttribute("aria-label", r.name); // 5.3: one group, named after its document
    const tick = document.createElement("input");
    tick.type = "checkbox"; tick.setAttribute("part", "tick"); tick.setAttribute("aria-label", S.tickLabel(r.name));
    tick.addEventListener("change", () => { if (!this.#input("tick", r.id, tick.checked)) tick.checked = !tick.checked; });
    // 2.2: clicking the name brings the document to the front, when the app can.
    const show = this.#opts.onShow;
    const name = document.createElement(show ? "button" : "span");
    name.className = "name"; name.setAttribute("part", "name"); name.textContent = r.name;
    if (show) { name.type = "button"; name.title = S.show; name.addEventListener("click", () => show(this.#docs.get(r.id))); }
    const acts = document.createElement("span");
    acts.className = "acts";
    const button = (label, aria, part, input) => {
      const b = document.createElement("button");
      b.className = "small"; b.textContent = label; b.setAttribute("aria-label", aria); b.setAttribute("part", "button " + part);
      b.addEventListener("click", () => this.#input(input, r.id));
      acts.append(b); return b;
    };
    const save = button(S.save, S.saveLabel(r.name), "row-save", "rowSave");          // 4.1
    const discard = button(S.discard, S.discardLabel(r.name), "row-discard", "rowDiscard"); // 4.2
    const where = document.createElement("span");
    where.className = "where"; where.setAttribute("part", "where"); where.id = "where-" + ++rowCount;
    const notes = document.createElement("span");
    notes.style.display = "contents";
    el.append(tick, name, acts, where, notes);
    const row = { el, tick, name, save, discard, where, notes };
    this.#rows.set(r.id, row);
    return row;
  }
}

if (globalThis.customElements && !customElements.get("unsaved-list")) customElements.define("unsaved-list", UnsavedList);
