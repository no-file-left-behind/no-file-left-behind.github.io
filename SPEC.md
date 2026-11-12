# No File Left Behind — specification

Version 0.2 (draft). The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are to be read
as described in [RFC 2119](https://www.rfc-editor.org/rfc/rfc2119). [What changed](#changes)
since 0.1 is at the end.

## 1. When it applies

An application **closes several documents at once** when it quits, restarts to install an
update, closes a window with several tabs, or runs a command such as Close All. If two or more
of those documents have unsaved changes, it asks about them **once, in one list**: the
*list of unsaved work*, or *the list* for short.

- **1.1** With two or more unsaved documents in the set being closed, the application MUST show
  the list instead of one prompt per document.
- **1.2** With exactly one unsaved document, the application SHOULD keep its existing
  single-document prompt ("Save changes to *name*?"). One row in a list is no clearer than a
  question.
- **1.3** The list MUST hold only the documents the action would close. Quitting lists every
  open document; closing one window lists that window's documents only.
- **1.4** Documents with no unsaved changes MUST NOT appear in the list.

## 2. What the list shows

![The reference implementation: three unsaved designs, each with its own Save and Discard](images/solidcraft-reference.png)

- **2.1 Title.** The title MUST say the count, e.g. "Save changes to 3 documents?". It SHOULD
  use the application's own word for a document (design, drawing, image, project).
- **2.2 One row per unsaved document.** The list MUST have a row for each, in the order the
  user sees them (tab order, window order). Each row MUST have:
  - **a tick box,** ticked when the list opens (2.4);
  - **the document's name.** Clicking it SHOULD bring that document to the front, so the user
    can look before deciding;
  - **where it lives,** in muted text under the name: its file path, or **"Not saved yet"** for
    a document that has never been saved;
  - **its own Save and Discard buttons** (section 4).
- **2.3 The window's buttons.** The window MUST have **Save Selected** (the default, primary
  button), **Don't Save** and **Cancel**. An application MAY use its platform's or its
  language's wording for them. If the window has a close button (×), it MUST act as Cancel.
- **2.4** Every row MUST start ticked. The safe default is to keep work; unticking is a choice
  the user makes.
- **2.5** A long list MUST scroll inside the window. The buttons MUST stay visible, and the
  window MUST fit on the screen with any number of rows.
- **2.6 What the ticks mean.** The window MUST say, in words next to the rows, that an
  unticked document closes without saving, e.g. "Ticked documents are saved; the rest close
  without saving." Unticking must never read as "leave this one open".

## 3. The window's buttons

- **3.1 Save Selected** MUST save the ticked documents, in row order.
  - It MUST ask for a location (a save picker) **only** for ticked documents that have never
    been saved, one at a time, and each picker MUST name the document it is asking about.
    Documents with a file MUST save to it without a question.
  - **3.1.1 No file left behind:** if any of those saves fails, or the user cancels a save
    picker, the application MUST NOT close **anything**. Not the documents already saved by
    this click, and not the unticked ones. The list MUST stay open. The failed or cancelled
    row says why (3.4), and documents that did save show as saved.
  - **3.1.2** When every ticked document has saved, the application MUST close all the
    documents in the list (the unticked ones unsaved) and carry on with the action (quit,
    update, close).
  - **3.1.3** A failed save MUST NOT stop the others: the application still tries every other
    ticked document, so one bad file costs no other document its save. A cancelled picker MUST
    stop the run: no further picker opens, and the documents not yet reached stay as they
    were. Either way, 3.1.1 holds.
- **3.2 Don't Save** MUST close every listed document without saving and carry on with the
  action. It SHOULD look less prominent than Save Selected, and MUST NOT be the default button.
- **3.3 Cancel** MUST close nothing more and stop the action. Every document still in the list
  stays open and unsaved. A document already answered on its own row (section 4) stays
  answered: one that was saved stays saved and closed, one that was discarded stays closed.
- **3.4** A failure MUST be shown on its row, in words the user can act on ("The disk is full",
  "You don't have permission to write to Documents"), not only as a code.

## 4. A row's own buttons

- **4.1** A row's **Save** MUST save that one document (asking for a location if it was never
  saved). When it succeeds, that document closes and its row leaves the list. If it fails or
  its picker is cancelled, the row MUST stay, with the reason (3.4).
- **4.2** A row's **Discard** MUST close that one document without saving, and its row leaves
  the list.
- **4.3** When the last row has left the list, the application MUST carry on with the action,
  exactly as if the user had answered for all of them at once.
- **4.4** Answering one row MUST NOT change any other row's tick or state.

## 5. Keyboard, focus and screen readers

- **5.1** **Enter** MUST activate Save Selected and **Escape** MUST be Cancel. These match the
  single-document prompt, so muscle memory carries over. When another button has keyboard
  focus, Enter MAY activate that button instead, where the platform does so.
- **5.2** Tab MUST move through the rows (tick box, name, Save, Discard) in order, then the
  window's buttons, and MUST stay inside the window while it is open. Space toggles the
  focused tick box. When the window closes without finishing the action, focus SHOULD return
  to where it was.
- **5.3** Each row MUST be one group for assistive technology, named after its document. The
  tick box's label names the document ("Save Report.docx"), and so do the row's Save and
  Discard ("Save Report.docx", "Discard Report.docx"), not just "Save" and "Discard".
- **5.4** The title's count MUST be announced when the window opens.

## 6. Edge cases

- **6.1 Read-only or moved file.** Saving where the file was is impossible, so the row's Save,
  and Save Selected, MUST ask for a new location for that document, as for one never saved.
  The row MUST say why ("The original file is read-only"), and SHOULD say so before the user
  presses anything.
- **6.2 A save that takes time** (a large file, a network drive). The row MUST show that it is
  saving, and every button except Cancel MUST wait. Cancel MUST stay available. Pressed during
  a save, it stops the saves not yet started and abandons the one in progress; documents
  already saved stay saved, and nothing closes (3.3). An abandoned or failed save MUST NOT
  leave a half-written file in place of the original: the application SHOULD write to a
  temporary file and replace the original only on success.
- **6.3 Two documents with the same name.** Their rows MUST be told apart by where they live
  (2.2). Never-saved documents with the same name MUST be told apart by order ("Untitled",
  "Untitled 2").
- **6.4 The application is told to quit by the system** (logout, shutdown). It SHOULD show the
  list and ask the system for more time where the platform allows. If it can't wait, it MUST
  keep a recoverable copy of every unsaved document (autosave, crash recovery) rather than lose
  one.
- **6.5 A document changes while the list is open** (an autosave, a background edit). Its row
  MUST stay, and saving MUST save its latest state.

## 7. Choosing the old behaviour

- **7.1** An application changing from one-prompt-per-document SHOULD offer a preference to
  keep the old prompts, such as "Ask about each unsaved document separately when closing
  several", off by default.

## 8. Scripts and automation

- **8.1** An application that can be driven by scripts or tests SHOULD let them read the list
  (rows, names, locations, ticks) and answer it: tick a set of rows and press one of the
  window's buttons, or answer one row. The answers MUST follow the same rules as clicks,
  including 3.1.1.

## Conformance

An application conforms to No File Left Behind when it meets every MUST in sections 1 to 6.
The [checklist](CHECKLIST.md) lists them for a review. Say which version you checked against:
"conforms to No File Left Behind 0.2".

## Changes

**0.2**

- Every numbered rule now carries a key word, so the row's own buttons, Cancel, the keys and
  the read-only case are required, not just described.
- New 2.6: the window says that an unticked document closes without saving.
- New 3.1.3: a failed save doesn't stop the other saves; a cancelled picker does.
- 2.3: a close button (×) acts as Cancel, and the button wording may follow the platform.
- 3.1: each save picker names its document.
- 3.3: Cancel closes nothing *more*; documents already answered on their row stay answered.
- 5.2: Tab stays inside the window.
- 6.2: what Cancel does during a slow save.
- 6.3: never-saved documents with the same name must be told apart.

**0.1** — the first draft ([tagged `v0.1`](https://github.com/raykholo/no-file-left-behind/tree/v0.1)).
