// Drives example.html in headless Chromium and checks the window itself: the parts the scenarios
// can't reach (rows, keys, focus, accessible names, a slow save).
//
//   node implementations/web/test/browser.mjs
//
// Needs playwright-core and a Chromium. Point at them with
//   NFLB_PLAYWRIGHT  the playwright-core package folder (or leave it out to import "playwright-core")
//   NFLB_CHROMIUM    the browser to launch (or leave it out for playwright's own)
//   NFLB_SHOTS       where screenshots go (default: a folder in the system's temporary directory)
import { createServer } from "node:http";
import { readFile, mkdir } from "node:fs/promises";
import { existsSync, readdirSync } from "node:fs";
import { createRequire } from "node:module";
import { join, extname, dirname } from "node:path";
import { tmpdir, homedir } from "node:os";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, "..");

function findPlaywright() {
  const require = createRequire(import.meta.url);
  const dirs = [process.env.NFLB_PLAYWRIGHT, join(homedir(), "Documents/plans/nflb-test/node_modules/playwright-core")].filter(Boolean);
  for (const d of dirs) if (existsSync(d)) return require(d);
  return require("playwright-core");
}
function findChromium() {
  if (process.env.NFLB_CHROMIUM) return process.env.NFLB_CHROMIUM;
  const base = join(homedir(), ".cache/ms-playwright/chromium_headless_shell-1223");
  if (!existsSync(base)) return undefined;
  const d = readdirSync(base).find((n) => n.startsWith("chrome-headless-shell"));
  return d ? join(base, d, "chrome-headless-shell") : undefined;
}

// Module scripts don't load from file:// pages, so serve the folder.
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css" };
const server = createServer(async (req, res) => {
  const path = join(root, decodeURIComponent(new URL(req.url, "http://x").pathname));
  if (!path.startsWith(root)) { res.writeHead(403).end(); return; }
  try { const body = await readFile(path); res.writeHead(200, { "content-type": TYPES[extname(path)] || "application/octet-stream" }).end(body); }
  catch { res.writeHead(404).end(); }
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const base = `http://127.0.0.1:${server.address().port}/example.html`;
const shots = process.env.NFLB_SHOTS || join(tmpdir(), "nflb-web-shots");
await mkdir(shots, { recursive: true });

let passed = 0, failed = 0;
const check = (ok, what) => { if (ok) { passed++; console.log("ok   " + what); } else { failed++; console.log("FAIL " + what); } };

const { chromium } = findPlaywright();
const browser = await chromium.launch({ executablePath: findChromium() });

// Helpers that look inside the element's shadow root.
const open = (page) => page.evaluate(() => document.querySelector("unsaved-list").shadowRoot.querySelector("dialog").open);
const focused = (page) => page.evaluate(() => {
  const el = document.querySelector("unsaved-list");
  if (document.activeElement !== el) return document.activeElement ? document.activeElement.id || document.activeElement.tagName : null;
  const a = el.shadowRoot.activeElement;
  return a ? "list:" + (a.getAttribute("aria-label") || a.textContent.trim() || a.tagName) : "list:?";
});
const title = (page) => page.evaluate(() => document.querySelector("unsaved-list").shadowRoot.querySelector("h2").textContent);
const rowText = (page, name) => page.getByRole("group", { name, exact: true }).textContent();
const set = (page, id, changes) => page.evaluate(([id, changes]) => { Object.assign(app.docs.find((d) => d.id === id), changes); app.render(); }, [id, changes]);
const result = (page) => page.evaluate(() => window.lastResult);
const logText = (page) => page.locator("#log").textContent();
const waitClosed = (page) => page.waitForFunction(() => !document.querySelector("unsaved-list").shadowRoot.querySelector("dialog").open);

async function fresh(options = {}) {
  const page = await browser.newPage({ viewport: { width: 1000, height: 720 }, ...options });
  const errors = [];
  page.on("pageerror", (e) => errors.push(String(e)));
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  await page.goto(base + "?slow=1500");
  page.errors = errors;
  return page;
}

// ---- 1. The list: rows, ticks, title, help line, names -----------------------------------------
{
  const page = await fresh({ colorScheme: "light" });
  await set(page, "budget", { slow: false });
  await page.click("#quit");
  check(await open(page), "Quit with four unsaved documents shows the list (1.1)");
  check((await title(page)) === "Save changes to 4 documents?", "the title gives the count and the app's word (2.1)");
  check((await page.getByRole("group").count()) === 4, "one row each, the clean one left out (1.4, 2.2)");
  const ticks = page.locator("unsaved-list [part=tick]");
  check((await ticks.count()) === 4 && (await ticks.evaluateAll((els) => els.every((e) => e.checked))), "every row starts ticked (2.4)");
  check((await page.locator("unsaved-list [part=help]").isVisible()) && (await page.locator("unsaved-list [part=help]").textContent()).includes("the rest close without saving"), "the help line says what unticking means (2.6)");
  check((await rowText(page, "Untitled")).includes("Not saved yet"), "a never-saved document says so (2.2)");
  const g = page.getByRole("group", { name: "Report", exact: true });
  check((await g.getByRole("checkbox", { name: "Save Report", exact: true }).count()) === 1
    && (await g.getByRole("button", { name: "Save Report", exact: true }).count()) === 1
    && (await g.getByRole("button", { name: "Discard Report", exact: true }).count()) === 1, "the row is a group named after its document, and its controls name it (5.3)");
  check((await page.getByRole("alertdialog", { name: "Save changes to 4 documents?" }).count()) === 1, "the window is named by its title, so the count is read when it opens (5.4)");
  check((await focused(page)) === "list:Save Selected", "Save Selected has focus when the list opens (2.3)");
  await page.screenshot({ path: join(shots, "1-list-light.png") });

  // Tab goes round inside the window (5.2).
  const order = [];
  let stayed = true;
  for (let i = 0; i < 24; i++) { await page.keyboard.press("Tab"); const f = await focused(page); order.push(f); if (!f || !f.startsWith("list:")) stayed = false; }
  for (let i = 0; i < 6; i++) { await page.keyboard.press("Shift+Tab"); const f = await focused(page); if (!f || !f.startsWith("list:")) stayed = false; }
  check(stayed, "Tab and Shift+Tab never leave the window (5.2)");
  check(order.slice(0, 5).join("|") === "list:Save Report|list:Report|list:Save Report|list:Discard Report|list:Save Budget", "Tab goes tick box, name, Save, Discard, then the next row (5.2)");

  // Escape is Cancel; focus goes back (3.3, 5.1, 5.2).
  await page.keyboard.press("Escape");
  await waitClosed(page);
  const r = await result(page);
  check(r.outcome === "stopped" && r.closed.length === 0 && (await page.locator(".doc").count()) === 5, "Escape is Cancel: nothing closes (3.3, 5.1)");
  check((await focused(page)) === "quit", "focus returns to the button that opened it (5.2)");

  // Space toggles a tick; Enter on a tick box is Save Selected (5.1, 5.2).
  await set(page, "report", { fails: true });
  await page.click("#quit");
  await page.getByRole("checkbox", { name: "Save Notes", exact: true }).focus();
  await page.keyboard.press("Space");
  check(!(await page.getByRole("checkbox", { name: "Save Notes", exact: true }).isChecked()), "Space unticks the focused row (5.2)");
  check((await page.getByRole("checkbox", { name: "Save Report", exact: true }).isChecked()), "unticking one row changes no other (4.4)");
  await page.keyboard.press("Space");
  await page.keyboard.press("Enter");
  await page.locator("#picker").waitFor({ state: "visible" });
  check(true, "Enter on a tick box runs Save Selected (5.1)");
  check((await page.textContent("#picker-title")).includes("Untitled"), "a failed save doesn't stop the others; the picker names its document (3.1, 3.1.3)");
  await page.click("#picker-cancel");
  await page.getByRole("group", { name: "Untitled", exact: true }).getByText("cancelled").waitFor();
  check(await open(page), "after a failure and a cancelled picker the list stays (3.1.1)");
  check((await page.locator(".doc").count()) === 5 && (await page.evaluate(() => document.querySelector("unsaved-list").snapshot().closed.length)) === 0, "nothing closed (3.1.1)");
  check((await rowText(page, "Report")).includes("The disk is full"), "the failed row says why (3.4)");
  check((await rowText(page, "Budget")).includes("Saved") && (await rowText(page, "Notes")).includes("Saved"), "the documents that saved show as saved (3.1.1)");
  await page.waitForFunction(() => document.querySelector("unsaved-list").shadowRoot.querySelector("[role=alert]").textContent.includes("The disk is full"));
  check(true, "the failure is announced in an alert (3.4)");
  check((await page.getByRole("button", { name: "Save Budget", exact: true }).isDisabled()), "a saved row has nothing left to answer");
  check((await title(page)) === "Save changes to 2 documents?", "the title counts what is left to answer (2.1)");
  await page.screenshot({ path: join(shots, "2-failure-light.png") });

  // Row Discard, then row Save on the last row carries on (4.1, 4.2, 4.3).
  await page.getByRole("button", { name: "Discard Report", exact: true }).click();
  check((await page.getByRole("group", { name: "Report", exact: true }).count()) === 0 && (await title(page)) === "Save changes to 1 document?", "a row's Discard answers that row only (4.2, 4.4)");
  await page.getByRole("button", { name: "Save Untitled", exact: true }).click();
  await page.locator("#picker").waitFor({ state: "visible" });
  await page.fill("#picker-name", "Plan.txt");
  await page.keyboard.press("Enter");
  await waitClosed(page);
  const r2 = await result(page);
  check(r2.outcome === "proceeded", "saving the last row carries on with the quit (4.1, 4.3)");
  check(r2.lost.join() === "report" && r2.saved.untitled === "~/Documents/Plan.txt" && r2.saved.budget && r2.saved.notes, "the result says what was saved and what was lost");
  check((await page.locator(".doc").count()) === 0, "everything closed");
  check(page.errors.length === 0, "no page errors " + page.errors.join("; "));
  await page.close();
}

// ---- 2. A cancelled picker stops the run (3.1.3); Don't Save; the single prompt; read-only --------
{
  const page = await fresh();
  const picks = await page.evaluate(async () => {
    const asked = [];
    const list = document.querySelector("unsaved-list");
    const done = list.ask([
      { id: "a", name: "Untitled", path: null, dirty: true },
      { id: "b", name: "Untitled 2", path: null, dirty: true },
    ], { save: async () => {}, pick: async (doc) => { asked.push(doc.name); return null; } });
    list.press("saveSelected");
    await new Promise((r) => setTimeout(r, 50));
    list.press("cancel");
    return { asked, result: await done };
  });
  check(picks.asked.join() === "Untitled" && picks.result.closed.length === 0, "a cancelled picker stops the run: no second picker, nothing closed (3.1.3)");

  await page.click("#quit");
  check(!(await page.getByRole("button", { name: "Don't Save" }).evaluate((b) => b.classList.contains("primary"))), "Don't Save is not the default button (3.2)");
  await page.getByRole("button", { name: "Don't Save" }).click();
  await waitClosed(page);
  const r = await result(page);
  check(r.outcome === "proceeded" && r.lost.length === 4 && Object.keys(r.saved).length === 0, "Don't Save closes every listed document unsaved (3.2)");

  await page.click("#reset");
  await page.click("#close-first");
  check((await title(page)) === "Save changes to “Report”?" && (await page.getByRole("group").count()) === 0, "one unsaved document gets the single prompt (1.2)");
  check(!(await page.locator("unsaved-list [part=help]").isVisible()), "the single prompt has no tick-box help line");
  await page.screenshot({ path: join(shots, "3-single-light.png") });
  await page.keyboard.press("Enter");
  await waitClosed(page);
  check((await result(page)).outcome === "proceeded" && (await result(page)).saved.report, "Enter is Save in the single prompt (5.1)");

  const own = await page.evaluate(() => document.querySelector("unsaved-list").ask([{ id: "x", name: "Lid", path: "~/Lid.sc", dirty: true }], { singlePrompt: false }));
  check(own.outcome === "single" && own.doc.id === "x" && !(await open(page)), "with singlePrompt off, ask hands the one document back to the app (1.2)");

  await set(page, "notes", { readOnly: true });
  await page.click("#quit");
  check((await rowText(page, "Notes")).includes("read-only"), "a read-only row says why before anything is pressed (6.1)");
  await page.getByRole("button", { name: "Save Notes", exact: true }).click();
  await page.locator("#picker").waitFor({ state: "visible" });
  check((await page.textContent("#picker-title")).includes("read-only"), "its Save asks for a new place (6.1)");
  await page.click("#picker-cancel");
  await page.getByRole("button", { name: "Cancel", exact: true }).focus();
  await page.keyboard.press("Enter");
  await waitClosed(page);
  check((await result(page)).outcome === "stopped", "Enter on a focused Cancel is Cancel (5.1)");
  check(page.errors.length === 0, "no page errors " + page.errors.join("; "));
  await page.close();
}

// ---- 3. A slow save: the row says so, buttons wait, Cancel abandons it (6.2) ----------------------
{
  const page = await fresh({ colorScheme: "dark" });
  await page.evaluate(() => { window.aborted = null; });
  await page.click("#quit");
  await page.getByRole("button", { name: "Save Selected" }).click();
  await page.getByRole("group", { name: "Budget", exact: true }).getByText("Saving…").waitFor();
  check(true, "the row shows that it is saving (6.2)");
  const states = await page.evaluate(() => {
    const root = document.querySelector("unsaved-list").shadowRoot;
    return [...root.querySelectorAll("button, input")].filter((b) => b.getClientRects().length).map((b) => [b.getAttribute("aria-label") || b.textContent, b.disabled]);
  });
  const enabled = states.filter(([, d]) => !d).map(([n]) => n);
  check(enabled.join() === "Cancel", "every button except Cancel waits while saving (6.2); enabled: " + enabled.join());
  check((await focused(page)).startsWith("list:"), "focus stays in the window while its buttons wait");
  await page.screenshot({ path: join(shots, "4-saving-dark.png") });
  await page.getByRole("button", { name: "Cancel", exact: true }).click();
  await waitClosed(page);
  const r = await result(page);
  check(r.outcome === "stopped" && r.abandoned.join() === "budget" && r.saved.report && r.closed.length === 0, "Cancel abandons the slow save, keeps what saved, closes nothing (6.2, 3.3)");
  check((await logText(page)).includes("Budget: save abandoned"), "the save's AbortSignal fired (6.2)");
  check(!(await logText(page)).includes("Saved Budget"), "the abandoned save did not finish later");
  await page.waitForTimeout(1700);
  check(!(await logText(page)).includes("Saved Budget") && !(await open(page)), "nothing happens when the abandoned save's time is up");

  // Dark screenshot of the failure state.
  await page.click("#reset");
  await set(page, "budget", { slow: false });
  await set(page, "report", { fails: true });
  await set(page, "notes", { readOnly: true });
  await page.click("#quit");
  await page.getByRole("button", { name: "Save Selected" }).click();
  await page.locator("#picker").waitFor({ state: "visible" });
  await page.screenshot({ path: join(shots, "5-picker-dark.png") });
  check((await page.textContent("#picker-title")).includes("Notes"), "the read-only row asks where first, in row order (3.1, 6.1)");
  await page.click("#picker-cancel");
  await page.getByRole("group", { name: "Notes", exact: true }).getByText("cancelled").waitFor();
  check(!(await page.locator("#picker").isVisible()), "the cancelled picker stopped the run before Untitled (3.1.3)");
  await page.screenshot({ path: join(shots, "6-failure-dark.png") });
  check(page.errors.length === 0, "no page errors " + page.errors.join("; "));
  await page.close();
}

// ---- 4. 360 px wide, and a long list (2.5) --------------------------------------------------------
{
  const page = await fresh({ viewport: { width: 360, height: 640 } });
  await set(page, "report", { fails: true });
  await set(page, "budget", { slow: false });
  await page.click("#quit");
  await page.getByRole("button", { name: "Save Selected" }).click();
  await page.locator("#picker").waitFor({ state: "visible" });
  await page.click("#picker-cancel");
  await page.getByRole("group", { name: "Untitled", exact: true }).getByText("cancelled").waitFor();
  const fits = await page.evaluate(() => {
    const root = document.querySelector("unsaved-list").shadowRoot;
    const d = root.querySelector("dialog").getBoundingClientRect();
    const all = [...root.querySelectorAll("button, input, h2, p, .where, .note")].filter((e) => e.getClientRects().length);
    return d.left >= 0 && d.right <= innerWidth && all.every((e) => { const r = e.getBoundingClientRect(); return r.left >= d.left - 1 && r.right <= d.right + 1; })
      && document.documentElement.scrollWidth <= innerWidth;
  });
  check(fits, "at 360 px everything fits inside the window, with no sideways scrolling");
  await page.screenshot({ path: join(shots, "7-narrow-360.png") });
  await page.keyboard.press("Escape");
  await waitClosed(page);

  await page.evaluate(() => {
    const docs = Array.from({ length: 40 }, (_, i) => ({ id: i, name: `Drawing ${i + 1}`, path: `~/Drawings/Drawing ${i + 1}.svg`, dirty: true }));
    window.longList = document.querySelector("unsaved-list").ask(docs, { save: async () => {} });
  });
  const long = await page.evaluate(() => {
    const root = document.querySelector("unsaved-list").shadowRoot;
    const d = root.querySelector("dialog").getBoundingClientRect(), rows = root.querySelector(".rows");
    const inView = (el) => { const r = el.getBoundingClientRect(); return r.top >= 0 && r.bottom <= innerHeight; };
    return { fits: d.top >= 0 && d.bottom <= innerHeight, scrolls: rows.scrollHeight > rows.clientHeight,
      buttons: [...root.querySelectorAll("footer button")].every(inView) };
  });
  check(long.fits && long.scrolls && long.buttons, "forty rows scroll inside the window; it fits the screen and its buttons stay visible (2.5)");
  await page.screenshot({ path: join(shots, "8-long-360.png") });
  await page.keyboard.press("Escape");
  check((await page.evaluate(() => window.longList)).outcome === "stopped", "the long list cancels like any other");
  check(page.errors.length === 0, "no page errors " + page.errors.join("; "));
  await page.close();
}

// ---- 5. A document that changes while the list is open (6.5) --------------------------------------
{
  const page = await fresh();
  await page.click("#quit");
  await page.evaluate(() => { const d = app.docs.find((x) => x.id === "letter"); d.dirty = true; document.querySelector("unsaved-list").changed("letter"); });
  check((await page.getByRole("checkbox", { name: "Save Letter", exact: true }).isChecked()) && (await title(page)) === "Save changes to 5 documents?", "a document that gains changes gets a row, ticked (6.5)");
  await page.keyboard.press("Escape");
  await page.close();
}

await browser.close();
server.close();
console.log(`${passed} of ${passed + failed} checks pass. Screenshots: ${shots}`);
process.exitCode = failed ? 1 : 0;
