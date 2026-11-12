# Implement it

No File Left Behind is a set of rules, not a library. Build it however suits your app, in any
language and toolkit. You conform when you meet the rules, and you are the one who says so.

## With an AI coding assistant

Open your app's code in your assistant and paste this:

```
Implement No File Left Behind in this app.
Start at https://no-file-left-behind.github.io/llms.txt
```

That link leads it to [the procedure](skills/no-file-left-behind/SKILL.md), which tells it what to read, the steps to take, the mistakes implementations usually make,
and how to report what it did. It will find every way your app closes several documents, test
the rules against the [scenarios](scenarios/), build the window in your toolkit, and go through
the [checklist](CHECKLIST.md).

- **If your assistant supports skills,** copy the folder
  [`skills/no-file-left-behind`](https://github.com/no-file-left-behind/no-file-left-behind.github.io/tree/main/skills/no-file-left-behind)
  into its skills folder. It then loads by itself whenever you ask about unsaved-changes prompts.
- **If it reads `AGENTS.md` or `llms.txt`,** this project has both:
  [AGENTS.md](https://github.com/no-file-left-behind/no-file-left-behind.github.io/blob/main/AGENTS.md) and
  [llms.txt](https://no-file-left-behind.github.io/llms.txt).

## By hand

1. Read the [specification](SPEC.md). It is short.
2. Keep the rules in a small model with no window in it. There is a
   [reference model](https://github.com/no-file-left-behind/no-file-left-behind.github.io/blob/main/scenarios/reference/model.mjs)
   of about 150 lines to port, or write your own.
3. Run the [scenarios](scenarios/) against your model: 37 cases, each a list of inputs and what
   the window must show after them.
4. Build the window the way your toolkit does dialogs, and go through the
   [checklist](CHECKLIST.md).

## Saying you conform

Nobody certifies this. You assert it, and the checklist and the scenarios are how anyone can
check. Put a statement like this in your documentation or release notes:

```
Conforms to No File Left Behind 0.4.0 (https://github.com/no-file-left-behind/no-file-left-behind.github.io)
- Scenarios: 37 of 37 pass
- Checklist: all lines met
```

If a MUST is unmet, say "Partly implements" and list what is missing.

### Make the pull request itself the statement

If your app is on GitHub, put the statement in a small file, `.nflb.json`, at the top of your
repository, and add or change it **in the same pull request as the work**:

```json
{ "name": "My App", "url": "https://example.org/my-app", "app_version": "3.2", "spec_version": "0.4.0", "status": "conforms" }
```

Merging that pull request is then the declaration: the code and the statement land together.
With [one small workflow](https://github.com/no-file-left-behind/adopters#2-declare-it-in-your-own-repository) the pull request says so
itself ("Merging this pull request declares that My App conforms to No File Left Behind 0.4.0"),
and the [list of apps](https://github.com/no-file-left-behind/adopters#readme) follows the file from then on, with nothing more for you to do.

## Tell us, if you like

You don't have to credit the project, ask permission, or tell anyone. Everything here is free
for any use. But we'd love to know who uses it. [The list](https://github.com/no-file-left-behind/adopters#readme) has its own repository:
[fill in a short form](https://github.com/no-file-left-behind/adopters/issues/new?template=we-use-it.yml) and we add your app, or add it yourself with a
[one-file pull request](https://github.com/no-file-left-behind/adopters#add-your-app).
