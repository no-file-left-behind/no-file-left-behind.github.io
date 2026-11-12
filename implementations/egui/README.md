# No File Left Behind for egui

The reference implementation of [No File Left Behind](../../SPEC.md) for
[egui](https://github.com/emilk/egui) 0.37. When an app closes several documents at once (quitting,
Close All, closing a window), it asks about the unsaved ones once, in one list, and closes nothing
if a save fails.

It conforms to No File Left Behind 0.4.0 for the rules the [scenarios](../../scenarios/) check: all
37 pass against it. The window covers the rest of sections 2 to 6 that a list can cover; what is
left to the app is listed under [What it does not do](#what-it-does-not-do).

## Two parts

- **`model`**: `Closing`, the rules as a small state machine. No egui in it, and usable without the
  `egui` feature. It never touches a file: when it needs one it asks the app, and waits for the
  answer.
- **`window`** (feature `egui`, on by default): `UnsavedList`, which draws a `Closing` as a modal
  each frame and passes the user's clicks in.

## Usage

A sketch of the app's side:

```rust
use no_file_left_behind::{Closing, Doc, Outcome, Request, UnsavedList};

// The user chose Quit: start a closing with every document it closes, in tab order.
let docs = app.tabs.iter().map(|t| {
    Doc::new(t.id, &t.name, t.path.clone())
        .dirty(t.modified)
        .read_only(t.read_only)
});
app.closing = Some(Closing::new(docs));

// Then, every frame:
if let Some(closing) = &mut app.closing {
    let response = UnsavedList::new("drawing", "drawings").show(ctx, closing);
    if let Some(id) = response.show_document {
        app.bring_to_front(id); // the user clicked a row's name
    }

    match closing.poll_request() {
        Some(Request::Pick { id, why }) => app.open_save_picker(id, why),
        Some(Request::Save { id, path }) => app.start_save(id, path),
        None => {}
    }

    for id in closing.closed() {
        app.close_tab(id); // a row's Save or Discard closes its document straight away
    }
    match closing.outcome() {
        Some(Outcome::Proceeded) => app.finish_quitting(),
        Some(Outcome::Stopped) => app.closing = None,
        None => {}
    }
}
```

The ids are the app's own keys (a tab index, a slot-map key, a `u64`): the model is generic over
them, so every request and row hands back the key the app already uses.

## The request and answer loop

The model raises one request at a time, and keeps it until it is answered:

| Request | The app does | and answers |
|---|---|---|
| `Request::Pick { id, why }` | shows a save picker that names the document | `picked(path)` or `pick_cancelled()` |
| `Request::Save { id, path }` | writes the document to that path | `save_done()` or `save_failed(reason)` |

`poll_request()` hands each request out once, so an app that checks every frame starts each
picker or save exactly once. `request()` shows the current one without taking it.

Nothing has to happen within the frame. A native file dialog can run on another thread, and a
save can take as long as it takes: until the answer arrives the row shows "Saving…", and every
button except Cancel waits (rule 6.2). If the user presses Cancel during a save, the save is
abandoned: the model stops waiting for it and lists its id in `abandoned()`. The app should write
to a temporary file and replace the original only when the model still wants that save, so an
abandoned or failed save never leaves half a file. The demo shows the pattern: it checks that
`request()` is still that save before it reports `save_done()`.

When the user picks a place, `picked(path)` turns the request into a save to that path, so the
next `poll_request()` asks for the write.

## Features

- The list with the count in its title, one row per unsaved document in order, each with a tick
  box, its name (clicking it asks the app to show that document), where it lives or "Not saved
  yet", and its own Save and Discard.
- The line saying that unticked documents close without saving.
- Save Selected as the default button, Don't Save less prominent, and Cancel.
- Enter is Save Selected, unless a button has focus; Escape is Cancel. Tab goes through each row
  (tick box, name, Save, Discard) and then the buttons, and stays inside the window. Focus goes
  back where it was after Cancel.
- Screen readers: the window is an alert dialog named by its title, each row is a group named after
  its document, and the tick box and buttons name the document ("Save Report", "Discard Report").
- A failure shown on its row in the app's words, a read-only file's reason shown before anything
  is pressed, and "Saving…" on the row being written.
- A long list scrolls inside the window; the buttons stay visible and the window fits the screen.
- The single-document prompt for one unsaved document. Turn it off with
  `.single_prompt(false)` to keep the app's own; the model still answers it.
- Every word can be replaced through `Strings`, for translation. Colours come from the app's egui
  visuals.

## What it does not do

These belong to the app:

- **Choosing the documents.** The app passes in the documents the action closes (1.3), named so
  that same-named ones can be told apart (6.3: "Untitled", "Untitled 2").
- **Saving and picking.** The model asks; the app writes files, shows the picker, and keeps a
  failed or abandoned save from replacing the original (6.2).
- **Telling the model about changes** while the list is open: call `doc_changed(id)` (6.5).
- **Quitting when the system asks** (6.4), and the preference to keep the old prompts (7.1).
- **Scripting** (8.1): the model's inputs and `snapshot()` are the way in.

## Trying it

```
cargo run --example demo
cargo test
```

The demo is a pretend app with four documents: one never saved, one with a slow save. Switches
on each make a save fail or mark a file read-only. Its save picker is a small egui window, standing
in for the platform's file dialog.

egui 0.37 needs Rust 1.95 or later.
