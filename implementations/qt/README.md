# No File Left Behind for Qt Widgets

The list of unsaved work for a Qt 6 Widgets application (C++17, Qt 6.4 or later). When your app
quits or closes several documents at once, it asks about the unsaved ones once, in one list, and
never closes anything if a save fails. It conforms to No File Left Behind 0.3: all 32
[scenarios](../../scenarios/) pass against its model, and the window is tested offscreen.

It has two parts:

- **`nflb::ClosingModel`** (`src/closingmodel.h`): the rules, with no window. It uses QtCore only
  and is a direct port of [`scenarios/reference/model.mjs`](../../scenarios/reference/model.mjs).
- **`nflb::UnsavedListDialog`** (`src/unsavedlistdialog.h`): the window. It draws the model and
  passes clicks in. It also shows the familiar single-document prompt when only one document is
  unsaved (rule 1.2).

Neither touches a file. When a document needs saving, or needs a place to be saved to, the dialog
asks your app.

## Adding it to your app

Either add the folder to your CMake build:

```cmake
add_subdirectory(path/to/no-file-left-behind/implementations/qt nflb)
target_link_libraries(myapp PRIVATE nflb)
```

Built this way it builds only the library. Or copy the four files in `src/` into your project; they
need Qt Widgets and nothing else, and `UnsavedListDialog` needs moc (`CMAKE_AUTOMOC` or your usual
setup).

## Using it

Give the dialog every document the action closes, in the order the user sees them (tab order, window
order), including those with no unsaved changes. It lists only the unsaved ones.

### The short way: a function that saves

```cpp
#include "unsavedlistdialog.h"

bool MainWindow::closeDocuments(const QList<Doc *> &docs)
{
    QList<nflb::UnsavedListDialog::Document> list;
    for (const Doc *d : docs)
        list.append({d->id(), d->name(), d->filePath(), d->isModified(), d->isReadOnly()});

    nflb::UnsavedListDialog dialog(list, this);
    dialog.setNoun(tr("drawing"), tr("drawings"));          // "Save changes to 3 drawings?"
    dialog.setFileFilter(tr("Drawings (*.dwg)"), "dwg");
    dialog.setSaver([this](const QString &id, const QString &path, QString *whyNot) {
        return findDoc(id)->saveTo(path, whyNot);          // false, with a reason the user can act on
    });
    connect(&dialog, &nflb::UnsavedListDialog::showDocumentRequested, this, &MainWindow::bringToFront);

    const bool goAhead = dialog.exec() == QDialog::Accepted;
    for (const QString &id : dialog.closedIds())
        closeDoc(id);                                      // saved, discarded, or closed by Don't Save
    return goAhead;                                        // false: Cancel, or a save didn't happen
}
```

With no other picker given, the dialog asks where to save with `QFileDialog`, titled with the
document's name. `exec()` returns at once, showing nothing, when no document is unsaved.

### The full way: signals, for slow saves and your own file dialog

Connect the requests and answer them when the work is done, now or later:

| The dialog emits | Your app does | and calls |
|---|---|---|
| `saveRequested(id, path)` | writes the document | `saveDone()` or `saveFailed(reason)` |
| `pickRequested(id, name, why)` | shows a save picker naming the document | `picked(path)` or `pickCancelled()` |
| `saveAbandoned(id)` | stops that save; the original file stays intact | nothing |

While a save runs, the row says "Saving…" and only Cancel is available. Cancel then abandons the
save in progress and tells you with `saveAbandoned()`. Write to a temporary file and replace the
original only on success (`QSaveFile` does this), so an abandoned or failed save never leaves half
a file. [`example/main.cpp`](example/main.cpp) does it this way, with a slow save on a timer and a
window-modal `QFileDialog`.

Other things it tells you:

- `showDocumentRequested(id)`: the user clicked a document's name; bring it to the front.
- `documentClosed(id)`: a document closed, either on its own row or when the action went ahead.
  Close it now, or wait and read `closedIds()`.
- Call `documentChanged(id)` if a document changes while the dialog is open (an autosave, a
  background edit). It gets a row, or needs saving again.

After it finishes: `outcome()` (proceeded or stopped), `savedIds()`, `savedPaths()` (for documents
that were never saved, this is where they went), `lostIds()`, `stillOpenIds()`, `closedIds()`.
Scripts and tests can answer it with the same rules as clicks: `setTicked()`, `saveSelected()`,
`dontSave()`, `cancel()`, `rowSave()`, `rowDiscard()`.

### Options

- `setNoun(singular, plural)`: your word for a document, used in the title and the help line.
- `setSinglePromptEnabled(false)`: show a one-row list instead of the single prompt.
- `setPicker(function)`: answer pick requests with your own function instead of `QFileDialog`.

## What it looks like

It follows your app's style and palette: muted text uses the palette's placeholder colour, and a
failure is shown in bold with the style's warning icon rather than a fixed colour. The window's
buttons are a `QDialogButtonBox`, so their order is the platform's. Save Selected is the default
button, Don't Save has the destructive role, and Escape or the window's close button is Cancel. Each
row is a named group for screen readers, and its tick box and buttons carry the document's name
("Save Report.docx", "Discard Report.docx"). Every string goes through `tr()`.

## What it does not do

- It does not save, close or find documents. Your app does that.
- It does not decide when to ask. Your app opens it on quit, Close All, or closing a window with
  several documents, and gives it only those documents (rule 1.3).
- It does not offer the old one-prompt-per-document behaviour (rule 7.1); keep your existing prompt
  for that if you offer the preference.
- It does not handle the system telling the app to quit (rule 6.4). Ask the system for more time
  where your platform allows, and keep a recoverable copy if it can't wait.
- It does not name same-named untitled documents apart ("Untitled", "Untitled 2", rule 6.3): give it
  the names your tabs already show.
- The title's grammar comes from one `tr()` string with your noun in it. Languages whose plurals
  or cases need more than a singular and a plural noun may need a different string.

## Building and testing it here

```
cmake -S implementations/qt -B build/qt -G Ninja
cmake --build build/qt
cd build/qt && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

`tst_scenarios` runs every case in `scenarios/scenarios.json` against `ClosingModel`, matching
snapshots the same partial way as `scenarios/reference/run.mjs`. `tst_dialog` drives the window
with no display. Set `NFLB_SCREENSHOT_DIR` to a folder to have it save pictures of the dialog
there. `build/qt/nflb_example` is the example app.
