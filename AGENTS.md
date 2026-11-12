# For AI coding assistants

This repository is the home of **No File Left Behind**, an open pattern for closing several
documents at once: ask once, in one list, and never close anything if a save fails.

## If you were sent here to add the pattern to an application

Follow [`skills/no-file-left-behind/SKILL.md`](skills/no-file-left-behind/SKILL.md). It is the
whole procedure: what to read, the steps, the usual mistakes, and how to report conformance.
In short:

1. Read [`SPEC.md`](SPEC.md). It is the only thing that defines the pattern.
2. Keep the rules in a small model with no window in it, and test that model against
   [`scenarios/scenarios.json`](scenarios/scenarios.json) (format in
   [`scenarios/README.md`](scenarios/README.md); a model to port in
   [`scenarios/reference/model.mjs`](scenarios/reference/model.mjs)).
3. Build the window in the application's own toolkit and check it against
   [`CHECKLIST.md`](CHECKLIST.md).
4. Report with the conformance statement at the end of the skill.

The application does not have to use any code from this repository. It conforms when it meets
every MUST in sections 1 to 6 of the spec.

If your tool supports skills, copy the folder `skills/no-file-left-behind` into its skills
folder and the procedure loads by itself when it is relevant.

## If you are changing this repository

- `SPEC.md` is the source of truth. A change to a rule bumps its version, adds a line under
  "Changes", and updates `CHECKLIST.md`, `scenarios/` and the skill to match.
- After touching `scenarios/`: `node scenarios/reference/run.mjs` must print "32 of 32" (or the
  new total). A new rule gets at least one scenario.
- `demo/index.html` is a single page with no build step. Check a change to it in a real browser,
  in light and dark, at desktop and phone widths.
- The site is GitHub Pages from `main`: `index.md` wraps `README.md`, and `_layouts`, `_includes`
  and `assets` hold the top bar and styles.
- Write plainly. Describe other software's behaviour without naming products.
