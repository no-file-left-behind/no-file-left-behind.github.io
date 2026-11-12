# Conformance checklist

For version 0.4.0 of the [specification](SPEC.md). Copy this into an issue or a review; each line
points to its rule.

**When it shows**
- [ ] Two or more unsaved documents closing at once get one list, not one prompt each (1.1)
- [ ] It lists only the documents this action closes, and only unsaved ones (1.3, 1.4)

**What it shows**
- [ ] The title gives the count (2.1)
- [ ] Every row has a tick box, the name, where it lives or "Not saved yet", and its own Save and Discard (2.2)
- [ ] The window has Save Selected (the default), Don't Save and Cancel; a close button (×) acts as Cancel (2.3)
- [ ] Every row starts ticked (2.4)
- [ ] A long list scrolls; the buttons stay visible; the window fits on screen (2.5)
- [ ] The window says that an unticked document closes without saving (2.6)

**What the buttons do**
- [ ] Save Selected asks for a location only for never-saved documents, and each picker names its document (3.1)
- [ ] **If any save fails or a picker is cancelled, nothing closes** (3.1.1)
- [ ] When every ticked document has saved, all the listed documents close and the action carries on (3.1.2)
- [ ] A failed save doesn't stop the other saves; a cancelled picker stops the run (3.1.3)
- [ ] Don't Save closes all without saving; it is not the default button (3.2)
- [ ] Cancel closes nothing more and stops the quit or close (3.3)
- [ ] A failure is shown on its row in words the user can act on (3.4)
- [ ] A row's Save or Discard answers that row only; answering the last row carries on; a list down to one row stays a list (4.1–4.5)

**Keyboard and accessibility**
- [ ] Enter is Save Selected and Escape is Cancel (5.1)
- [ ] Tab goes through the rows, then the buttons, and stays inside the window (5.2)
- [ ] Each row is one named group; its controls name its document (5.3)
- [ ] The count is announced when the window opens (5.4)

**Edge cases**
- [ ] A read-only or moved file asks for a new location, and its row says why (6.1)
- [ ] A slow save shows on its row; Cancel still works and never leaves a half-written file in place of the original (6.2)
- [ ] Rows with the same name can be told apart (6.3)
- [ ] A forced quit keeps a recoverable copy of every unsaved document (6.4)
- [ ] A document that changes while the list is open keeps its row, or gets one, saves its latest state, and never closes unasked (6.5)

**The rules, tested**
- [ ] The [scenarios](scenarios/) pass against the implementation
