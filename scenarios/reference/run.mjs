// Runs scenarios.json against the reference model: node scenarios/reference/run.mjs
import { readFileSync } from "node:fs";
import { Closing } from "./model.mjs";

const file = JSON.parse(readFileSync(new URL("../scenarios.json", import.meta.url), "utf8"));

// `want` is a partial picture: every field it names must match; arrays must match in length and order.
function differs(got, want, at) {
  if (Array.isArray(want)) {
    if (!Array.isArray(got) || got.length !== want.length) return `${at}: wanted ${JSON.stringify(want)}, got ${JSON.stringify(got)}`;
    for (let i = 0; i < want.length; i++) { const d = differs(got[i], want[i], `${at}[${i}]`); if (d) return d; }
    return null;
  }
  if (want && typeof want === "object") {
    if (!got || typeof got !== "object") return `${at}: wanted ${JSON.stringify(want)}, got ${JSON.stringify(got)}`;
    for (const k of Object.keys(want)) { const d = differs(got[k], want[k], `${at}.${k}`); if (d) return d; }
    return null;
  }
  return got === want ? null : `${at}: wanted ${JSON.stringify(want)}, got ${JSON.stringify(got)}`;
}

let failed = 0;
for (const sc of file.scenarios) {
  const m = new Closing(sc.docs);
  let problem = null;
  sc.steps.forEach((step, i) => {
    if (problem) return;
    if (step.expect) { const d = differs(m.snapshot(), step.expect, "snapshot"); if (d) problem = `step ${i + 1}: ${d}`; return; }
    const args = step.do === "tick" ? [step.id, step.on] : "id" in step ? [step.id] : "path" in step ? [step.path] : "reason" in step ? [step.reason] : [];
    const accepted = m[step.do](...args);
    if (accepted !== !step.ignored) problem = `step ${i + 1}: ${step.do} was ${accepted ? "accepted" : "ignored"}`;
  });
  if (problem) { failed++; console.log(`FAIL ${sc.id}: ${problem}`); } else console.log(`ok   ${sc.id}`);
}
console.log(`${file.scenarios.length - failed} of ${file.scenarios.length} scenarios pass`);
process.exitCode = failed ? 1 : 0;
