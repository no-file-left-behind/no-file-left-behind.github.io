# Conformance checklist

Copy this into an issue or a review. Each line points to its rule in the [specification](SPEC.md).

**When it shows**
- [ ] Two or more unsaved documents closing at once get one list, not one prompt each (1.1)
- [ ] It lists only the documents this action closes, and only unsaved ones (1.3, 1.4)

**What it shows**
- [ ] The title gives the count (2.1)
- [ ] Every row has a tick box, the name, where it lives or "Not saved yet", and its own Save and Discard (2.2)
- [ ] Every row starts ticked (2.4)
- [ ] A long list scrolls; the buttons stay visible; the window fits on screen (2.5)

**What the buttons do**
- [ ] Save Selected asks for a location only for never-saved documents (3.1)
- [ ] **If any save fails or a picker is cancelled, nothing closes** (3.1.1)
- [ ] Don't Save closes all without saving; it is not the default button (3.2)
- [ ] Cancel closes nothing and stops the quit or close (3.3)
- [ ] A failure is shown on its row in words the user can act on (3.4)
- [ ] A row's Save or Discard answers that row only; answering the last row carries on (4.1–4.4)

**Keyboard and accessibility**
- [ ] Enter is Save Selected and Escape is Cancel (5.1)
- [ ] Each row is one named group; its controls name its document (5.3)
- [ ] The count is announced when the window opens (5.4)

**Edge cases**
- [ ] A read-only or moved file asks for a new location (6.1)
- [ ] A slow save never leaves a half-written file in place of the original (6.2)
- [ ] Rows with the same name can be told apart (6.3)
- [ ] A forced quit keeps a recoverable copy of every unsaved document (6.4)
