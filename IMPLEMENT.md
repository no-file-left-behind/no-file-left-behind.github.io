# Implement it

No File Left Behind is a set of rules, not a library. Build it however suits your app, in any
language and toolkit. You conform when you meet the rules, and you are the one who says so.

## With an AI coding assistant

Open your app's code in your assistant and paste this:

```
Implement No File Left Behind in this app.
Start at https://raykholo.github.io/no-file-left-behind/llms.txt
```

That link leads it to [the procedure](skills/no-file-left-behind/SKILL.md), which tells it what to read, the steps to take, the mistakes implementations usually make,
and how to report what it did. It will find every way your app closes several documents, test
the rules against the [scenarios](scenarios/), build the window in your toolkit, and go through
the [checklist](CHECKLIST.md).

- **If your assistant supports skills,** copy the folder
  [`skills/no-file-left-behind`](https://github.com/raykholo/no-file-left-behind/tree/main/skills/no-file-left-behind)
  into its skills folder. It then loads by itself whenever you ask about unsaved-changes prompts.
- **If it reads `AGENTS.md` or `llms.txt`,** this project has both:
  [AGENTS.md](https://github.com/raykholo/no-file-left-behind/blob/main/AGENTS.md) and
  [llms.txt](https://raykholo.github.io/no-file-left-behind/llms.txt).

## By hand

1. Read the [specification](SPEC.md). It is short.
2. Keep the rules in a small model with no window in it. There is a
   [reference model](https://github.com/raykholo/no-file-left-behind/blob/main/scenarios/reference/model.mjs)
   of about 150 lines to port, or write your own.
3. Run the [scenarios](scenarios/) against your model: 32 cases, each a list of inputs and what
   the window must show after them.
4. Build the window the way your toolkit does dialogs, and go through the
   [checklist](CHECKLIST.md).

## Saying you conform

Nobody certifies this. You assert it, and the checklist and the scenarios are how anyone can
check. Put a statement like this in your documentation or release notes:

```
Conforms to No File Left Behind 0.3 (https://github.com/raykholo/no-file-left-behind)
- Scenarios: 32 of 32 pass
- Checklist: all lines met
```

If a MUST is unmet, say "Partly implements" and list what is missing. Then open a pull request
adding your app to the [adopters table](https://github.com/raykholo/no-file-left-behind#who-uses-it).
