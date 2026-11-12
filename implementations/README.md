# Implementations

Ready-made versions of the list, one per toolkit. You don't have to use any of them: the pattern
is the [specification](../SPEC.md), and an app conforms by meeting its rules however it is built.
These exist to save you the work, and to show one way of doing it.

| Toolkit | Folder | What it is |
|---|---|---|
| Web | [`web/`](web/) | `<unsaved-list>`, a custom element in one file with no dependencies. The [demo](https://no-file-left-behind.github.io/demo/) runs on it. |
| egui (Rust) | [`egui/`](egui/) | A crate with the rules in a model that needs no egui, and an immediate-mode window for egui 0.37. |
| Qt 6 Widgets (C++) | [`qt/`](qt/) | `nflb::UnsavedListDialog`, a `QDialog`, with the rules in a QtCore-only model. |

Each one keeps the rules in a model with no window in it, passes the [scenarios](../scenarios/),
and says in its README what it leaves to your app. All of it is [0BSD](../LICENSE-code): copy it,
change it, no credit needed.

Want to add a toolkit? Port the [reference model](../scenarios/reference/model.mjs), run the
scenarios against it in your test framework, build the window, and open a pull request.
