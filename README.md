# No File Left Behind

**When you close several documents at once, ask about them once, in one list, and never close
anything if a save fails.**

You quit an app with eight unsaved documents. Most apps ask you eight times, one prompt after
another, each in a different spot, and each offering the same three buttons. Somewhere around
the fifth one you stop reading and click Don't Save. That's how work gets lost.

No File Left Behind is an open pattern for that moment. One window lists every unsaved
document. Each row says what it is and where it lives, and has its own **Save** and
**Discard**. The window's **Save Selected** saves everything you leave ticked, and if any save
fails, **nothing closes**.

![Three unsaved designs listed in one window, each with its own Save and Discard](images/solidcraft-reference.png)

[**Try the demo**](https://raykholo.github.io/no-file-left-behind/demo/) · [**Read the specification**](SPEC.md) · [**Review checklist**](CHECKLIST.md)

## The pattern in five rules

1. **One list, not N prompts.** Closing two or more unsaved documents at once shows one window
   with a row each. One unsaved document keeps the familiar single prompt.
2. **Every row starts ticked.** The safe default is to keep work.
3. **Each row answers for itself.** A row's own Save or Discard handles just that document, so
   you can deal with the one you're unsure about and leave the rest.
4. **No file left behind.** If any save fails, or you cancel a "Save as" picker, nothing closes:
   not the ones that saved, not the ones you unticked. You fix that one and try again.
5. **Same keys as before.** Enter saves the selection and Escape cancels, as the
   single-document prompt does.

The [specification](SPEC.md) states these precisely, with the edge cases: read-only files, slow
saves, duplicate names, shutdown, screen readers, and scripting.

## Why a standard

Lists like this already exist in a few places, each with its own rules. Some ask for every
file's location up front, some close the documents that did save when another fails, and some
have no way to answer one document at a time. Writing the behaviour down gives app makers one
thing to copy and users the same safe answer everywhere.

## Adopt it

- **App makers:** check your app against the [checklist](CHECKLIST.md). Add it to your issue
  tracker as a feature request and link here.
- **Toolkit and design-system makers:** a shared "save several" dialog in a toolkit brings the
  pattern to every app built on it.
- **Users:** if an app you use asks you one file at a time, point its makers here.

## Who uses it

| App | Since | Notes |
|---|---|---|
| [SolidCraft](https://github.com/tangency-labs/solidcraft) | 0.40 | The reference: quitting, updating and closing several tabs ([pull request](https://github.com/tangency-labs/solidcraft/pull/11683)) |

Using it in your app? Open a pull request adding a row.

## Licence

The specification, checklist and images are [CC BY 4.0](LICENSE-docs): use them, adapt them,
credit "No File Left Behind". The demo's code is [MIT](LICENSE-code).
