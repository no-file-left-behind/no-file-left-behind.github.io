---
name: no-file-left-behind
description: Implement or review the No File Left Behind pattern in an application. When several documents with unsaved changes are closed at once (quit, close a window, Close All, restart to update), the app asks once, in one list, and never closes anything if a save fails. Use when asked to add, fix or check a "save changes" prompt that covers more than one document, or to say whether an app conforms to No File Left Behind.
---

# Implementing No File Left Behind

You are adding the pattern to an application, in whatever language and toolkit it uses. Nothing
here needs code from this project: an implementation conforms when it meets the rules, however it
is written.

## Sources

Read these before writing code. They are short.

| What | Where |
|---|---|
| The rules (read all of it) | https://raw.githubusercontent.com/raykholo/no-file-left-behind/main/SPEC.md |
| The review checklist | https://raw.githubusercontent.com/raykholo/no-file-left-behind/main/CHECKLIST.md |
| The rules as test data, and their format | https://raw.githubusercontent.com/raykholo/no-file-left-behind/main/scenarios/README.md and https://raw.githubusercontent.com/raykholo/no-file-left-behind/main/scenarios/scenarios.json |
| A reference model to port (about 150 lines) | https://raw.githubusercontent.com/raykholo/no-file-left-behind/main/scenarios/reference/model.mjs |
| A working window to look at | https://raykholo.github.io/no-file-left-behind/demo/ (source: `demo/index.html`) |

The spec's first line gives its version. Quote that version in what you report.

## Steps

1. **Find every way the app closes more than one document.** Quit, closing a window that holds
   several tabs, Close All and its variants, restarting to install an update, and the system
   telling the app to quit (logout, shutdown). List them for the user. Each one must end up at
   the same code path. Also find how the app saves a document, how it asks for a location, and
   how it knows a document has unsaved changes.
2. **Ask the user what only they can decide,** in one message, then carry on with defaults if
   they don't answer: the app's word for a document ("design", "drawing"); whether to keep a
   preference for the old one-prompt-per-document behaviour (spec 7.1, off by default); and
   whether the single-document prompt stays as it is (1.2: it should).
3. **Put the rules in a model with no window in it.** Port the reference model or write your own
   with the same inputs, requests and snapshot (see the scenarios README). Keeping the rules
   apart from the window is what makes them testable.
4. **Run the scenarios against your model** in the app's own test framework: load
   `scenarios.json`, apply each step, compare each `expect` as a partial match. Vendor the file
   into the app's tests with a note of the spec version. All of them must pass before you build
   the window.
5. **Build the window in the app's toolkit, the way that toolkit does dialogs.** Use its modal
   dialog, button order, focus handling and accessibility API. Draw the model's snapshot; send
   clicks to the model; when the model raises a request, call the app's real save or its real
   save picker and report the result back.
6. **Route every path from step 1 through it.** Closing must wait for the outcome: go ahead on
   "proceeded", stop on "stopped".
7. **Check the window against the checklist,** line by line, in the running app if you can drive
   it, and say which lines you verified by running and which only by reading the code.
8. **Report** with the conformance statement below.

## Where implementations go wrong

These are the mistakes found most often, including in the app the pattern came from. Check each.

- **Closing the list when a save fails.** The list must stay open, with the reason on that row
  (3.1.1, 3.4). Stopping the quit is not enough.
- **Closing documents as each one saves.** Under Save Selected nothing closes until every ticked
  document has saved (3.1.1, 3.1.2). Only a row's own Save closes its document straight away (4.1).
- **Stopping at the first failure.** A failed save doesn't stop the other saves; a cancelled
  picker does (3.1.3).
- **Leaving out the line that says what unticking means** (2.6). Unticked means "close without
  saving", and the window has to say so.
- **Treating Cancel as undo.** Cancel closes nothing *more*; rows already answered stay answered (3.3).
- **Asking where to save documents that already have a file,** or asking for every location up
  front (3.1).
- **Rows that can't be told apart:** several "Untitled" (6.3), or names with no path (2.2).
- **Controls a screen reader hears as "Save, Discard, Save, Discard".** Each control's accessible
  name includes its document's name (5.3).
- **Focus escaping the window,** or Enter and Escape not doing what the single prompt does (5.1, 5.2).
- **A cancelled or failed save leaving half a file** where the original was (6.2): write to a
  temporary file and replace on success.
- **A forced quit losing work.** If the system won't wait, keep a recoverable copy (6.4).

## Don't

- Don't weaken a MUST because the toolkit makes it awkward. Say what is unmet instead.
- Don't change how single documents are saved, or the single-document prompt, beyond what a
  failing save needs.
- Don't add a dependency on this project unless the user asks for one.

## Saying it conforms

Nobody certifies this. The app's makers assert it, and the checklist and the scenarios are how
anyone can check. Put this in the pull request and offer it for the app's documentation:

```
Conforms to No File Left Behind 0.3 (https://github.com/raykholo/no-file-left-behind)
- Scenarios: 32 of 32 pass (<path to the test>)
- Checklist: all lines met / all except: <line and why>
- Verified by running: <which lines>. By reading the code only: <which lines>.
- Paths covered: <quit, close window, Close All, update, system quit>
```

If any MUST is unmet, write "Partly implements" instead of "Conforms to" and list what is missing.
Then tell the user they can add the app to the adopters table with a pull request to the repo's
README.
