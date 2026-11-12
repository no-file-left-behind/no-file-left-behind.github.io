# `<unsaved-list>`: No File Left Behind for the web

A custom element that asks about unsaved documents when a web app closes several at once: one
list, a row each, every row ticked, and nothing closes if a save fails. It conforms to
[No File Left Behind 0.4.0](../../SPEC.md) for the rules it owns (see [What it does not do](#what-it-does-not-do)).

It is one file, [`unsaved-list.js`](unsaved-list.js): an ES module with no dependencies and no build
step. Copy it into your app. [`example.html`](example.html) is a small pretend app that uses it; serve
the folder over HTTP to try it (browsers don't load modules from `file://`), for example
`python3 -m http.server` in this folder, then open `http://localhost:8000/example.html`.

## Usage

```html
<unsaved-list></unsaved-list>
<script type="module">
  import "./unsaved-list.js";
  const list = document.querySelector("unsaved-list");

  async function quit() {
    const docs = openDocuments.map((d) => ({ id: d.id, name: d.title, path: d.file, dirty: d.changed, readOnly: d.locked }));
    const result = await list.ask(docs, {
      save: (doc, path, signal) => writeFile(doc.id, path, { signal }), // resolve when saved, throw to fail
      pick: (doc, why) => askWhereToSave(doc.name, why),                // a path, or null if cancelled
      noun: { one: "drawing", other: "drawings" },
      onShow: (doc) => bringToFront(doc.id),
    });
    if (result.outcome === "proceeded") reallyQuit();
  }
</script>
```

## The API

### `list.ask(docs, options)` → `Promise<result>`

`docs` are the documents the action closes, in the order the user sees them (tab order, window
order), and only those (rule 1.3). Each is an object with:

| Field | Meaning |
|---|---|
| `id` | anything unique within this call; the index is used if it is missing |
| `name` | what the user calls it; name never-saved ones "Untitled", "Untitled 2" (6.3) |
| `path` | where it lives, shown under the name; `null` if it was never saved |
| `dirty` | `true` if it has unsaved changes; the others are closed without a row (1.4) |
| `readOnly` | `true` if its file can't be written (read-only or moved), so saving asks where (6.1) |

The objects you pass are the ones handed back to `save`, `pick` and `onShow`, so they can carry
anything else your app needs.

Options:

| Option | Meaning |
|---|---|
| `save(doc, path, signal)` | Your save. Return a promise: resolved means saved, rejected means failed, and the error's `message` is shown on the row, so write it for the user ("The disk is full. Free some space and try again.", rule 3.4). `signal` is an `AbortSignal` that fires if the user presses Cancel while this save is running (6.2): stop, and leave the original file as it was. Write to a temporary file and move it over the original only when it is complete. |
| `pick(doc, why)` | Your save picker, for a document that was never saved (`why` is `"never-saved"`) or whose file can't be written (`"read-only"`). Return a path, or `null` (or throw) if the user cancels. The picker must name the document (3.1). If it is a `<dialog>` in the page, open it with `showModal()` so it sits above the list. |
| `noun` | Your word for a document, `{ one, other }`. Default `{ one: "document", other: "documents" }`. Used in the title and the help line (2.1). |
| `onShow(doc)` | Called when the user clicks a row's name: bring that document to the front (2.2). Without it, names are plain text. |
| `onChange(snapshot)` | Called after every change while the window is open, with the same picture `snapshot()` gives. Use it to close a document the moment its own row is answered: its id appears in `snapshot.closed` (4.1, 4.2). |
| `strings` | Replace any of the window's words, for translation. See `STRINGS` in the file for every key; entries that take a value are functions, such as `title: (count, noun) => …`. |
| `singlePrompt` | `true` (the default): with exactly one unsaved document, the element shows the familiar single prompt, "Save changes to “Report”?" with Don't Save, Cancel and Save (1.2). `false`: `ask` shows nothing and resolves at once with `outcome: "single"` and `doc` set to that document, so your app can show its own prompt. |

The result:

| Field | Meaning |
|---|---|
| `outcome` | `"proceeded"`: carry on with the quit or close; every listed document is closed. `"stopped"`: the user cancelled; stop the action. `"single"`: only with `singlePrompt: false`, see above. |
| `closed` | ids of the documents to close now. With `"stopped"` these are the rows answered on their own before Cancel (3.3). |
| `open` | ids of the documents that stay open |
| `saved` | `{ id: path }` for every document saved in this window, including ones saved to a new place |
| `lost` | ids closed with unsaved changes, by the user's choice (Don't Save, Discard, or left unticked) |
| `abandoned` | ids whose save was abandoned by Cancel (6.2) |

With no unsaved documents, `ask` resolves at once with `"proceeded"` and shows nothing.

### Other members

- `list.changed(id)`: tell the window a document in the set changed while it is open, such as an
  autosave or a background edit (6.5). A row that had saved needs saving again; a clean document
  that gains changes gets a row, ticked.
- `list.snapshot()`: what the window shows, as in the [scenarios](../../scenarios/README.md), or `null` when closed.
- `list.press(input, id, on)`: answer it from a script or a test (8.1), with the same inputs as
  the buttons: `"saveSelected"`, `"dontSave"`, `"cancel"`, `"rowSave"`, `"rowDiscard"`, or
  `"tick"` with `on`. Ignored inputs return `false`, as with a disabled button.
- `list.open`: `true` while it is asking.
- `Closing` (exported): the window-less model, the same as [`scenarios/reference/model.mjs`](../../scenarios/reference/model.mjs).

## Keys and screen readers

Enter is Save Selected (or Save in the single prompt), unless a button has focus, which then does
its own job. Escape is Cancel. Tab goes through each row's tick box, name, Save and Discard, then
the window's buttons, and wraps round inside the window. Focus returns to where it was when the
window closes. Each row is a group named after its document, its tick box, Save and Discard say
the name ("Save Report", "Discard Report"), the window is named by its title, and a failed or
cancelled save is announced.

## Theming

The window is a native `<dialog>` inside the element's shadow root. It follows the system's light
or dark setting; add `scheme="light"` or `scheme="dark"` to the element to fix one.

Colours, set on the element or anything above it:
`--nflb-panel`, `--nflb-text`, `--nflb-muted`, `--nflb-line`, `--nflb-hover`, `--nflb-accent`,
`--nflb-accent-text`, `--nflb-danger`, `--nflb-ok`, `--nflb-backdrop`. Setting one replaces it in
both schemes, so set them inside your own `prefers-color-scheme` rule if they should differ.
Shape: `--nflb-font`, `--nflb-radius`, `--nflb-width` (default 560px).

For anything else, style the parts with `::part()`: `dialog`, `title`, `help`, `rows`, `row`,
`tick`, `name`, `where`, `note` (also `note-bad` and `note-good`), `footer`, `button`, `primary`,
`save-selected`, `dont-save`, `cancel`, `row-save`, `row-discard`.

```css
unsaved-list { --nflb-accent: #7a3cff; --nflb-radius: 4px; }
unsaved-list::part(title) { font-size: 1.25rem; }
```

## What it does not do

- **Save files.** Your `save` writes; your `pick` asks where. The element never touches a file,
  and it can't make a save atomic (6.2): that is up to `save`.
- **Decide what to list.** Pass only the documents this action closes (1.3), named so that equal
  names can be told apart (6.3).
- **Handle a forced quit** (6.4). A browser tab being closed can't wait for an answer; keep a
  recoverable copy of unsaved work instead. A `beforeunload` prompt can't show this list.
- **Offer the old one-prompt-per-document behaviour** (7.1). That preference belongs to your app.
- **Have a close button.** Escape is Cancel. If you add one, make it Cancel (2.3).

## Tests

```
node implementations/web/test/scenarios.mjs   # the scenarios, against the Closing in unsaved-list.js
node implementations/web/test/browser.mjs     # the window, in headless Chromium
```

The browser test needs `playwright-core` and a Chromium. Point it at them with `NFLB_PLAYWRIGHT`
(the `playwright-core` folder) and `NFLB_CHROMIUM` (the browser binary); `NFLB_SHOTS` chooses
where its screenshots go.
