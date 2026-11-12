# Scenarios

The specification's rules as data, so an implementation in any language or toolkit can be checked
the same way. [`scenarios.json`](scenarios.json) holds the cases; [`reference/model.mjs`](reference/model.mjs)
is a small reference model that passes all of them:

```
node scenarios/reference/run.mjs
```

## The idea

The pattern splits in two. The **rules** (what is listed, what each button does, what happens when a
save fails) are the same everywhere. The **window** (rows, focus, keys, the save picker) belongs to
a toolkit. The scenarios test the rules, with no window involved.

An implementation keeps a small model of one closing action:

- It is given the documents the action closes, in order: `id`, `name`, `path` (null if never
  saved), `dirty`, `readOnly`.
- The window draws the model's **snapshot** and passes the user's clicks in as **inputs**.
- The model never touches a file. When it needs one it raises a **request**, and the application
  answers it.

| Request | The application does | and answers |
|---|---|---|
| `{ "type": "pick", "id", "why": "never-saved" \| "read-only" }` | shows a save picker that names the document | `picked(path)` or `pickCancelled()` |
| `{ "type": "save", "id", "path" }` | writes the document to that path | `saveDone()` or `saveFailed(reason)` |

Inputs from the window: `tick(id, on)`, `saveSelected()` (also Save in the single prompt),
`dontSave()`, `cancel()` (also Escape and the close button), `rowSave(id)`, `rowDiscard(id)`.
From the application: the four answers above, and `docChanged(id)` when a document in the set
changes while the window is open. An input that arrives when its button would be disabled or
waiting is **ignored**, and changes nothing.

## A scenario

```json
{
  "id": "failed-save-nothing-closes",
  "rules": ["3.1.1", "3.4"],
  "title": "A save fails: nothing closes, the list stays, the row says why, the saved one shows as saved",
  "docs": [ { "id": "bracket", "name": "Bracket", "path": "~/Designs/Bracket.sc", "dirty": true }, ... ],
  "steps": [
    { "do": "saveSelected" },
    { "do": "saveDone" },
    { "do": "saveFailed", "reason": "The disk is full" },
    { "expect": { "prompt": "list", "closed": [], "rows": [ { "id": "bracket", "state": "saved" }, ... ] } }
  ]
}
```

- A `do` step is one input, with its arguments beside it (`id`, `on`, `path`, `reason`). With
  `"ignored": true` the input must be ignored; without it, it must be accepted.
- An `expect` step is a **partial** picture of the snapshot: every field it names must match.
  Objects match field by field; arrays must have the same length and order, each element matched
  the same partial way.

## The snapshot

| Field | Meaning |
|---|---|
| `prompt` | `"list"`, `"single"` (one unsaved document, rule 1.2) or `"none"` (the window is gone) |
| `count` | unsaved documents still to answer: the number in the title (2.1) |
| `rows` | one per listed document, in order: `id`, `name`, `where` (path or null for "Not saved yet"), `readOnly`, `ticked`, `state` (`"unsaved"`, `"saving"`, `"saved"`), `note` (null, or `{ "kind": "failed" \| "cancelled", "text" }`) |
| `request` | what the application is being asked to do, or null |
| `outcome` | null while asking; `"proceeded"` (the quit or close goes ahead) or `"stopped"` (Cancel) |
| `open`, `closed` | ids of the action's documents that are still open, or closed |
| `saved` | id to path, for every document whose changes were saved |
| `lost` | ids closed with unsaved changes, by the user's choice |
| `abandoned` | ids whose save was cancelled part-way (6.2): no half-written file may be left |

## Using it in an implementation

Port `reference/model.mjs` (about 150 lines) or write your own, then load `scenarios.json` in your
tests and run every scenario. The implementations in [`../implementations`](../implementations) all
do this. What the scenarios can't check is the window itself: for that, use the
[checklist](../CHECKLIST.md).
