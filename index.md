---
layout: default
title: No File Left Behind
---
{%- comment -%}
The site's front page: the README, with the live demo as its own section after the introduction.
GitHub's README view can't run a page inside a page, so the README links the demo instead.
{%- endcomment -%}
{%- capture readme -%}{% include_relative README.md %}{%- endcapture -%}
{%- assign readme = readme
  | replace: "(SPEC.md)", "(SPEC.html)"
  | replace: "(CHECKLIST.md)", "(CHECKLIST.html)"
  | replace: "(IMPLEMENT.md)", "(IMPLEMENT.html)"
  | replace: "(https://no-file-left-behind.github.io/demo/)", "(#try-it)"
  | replace: "<!-- adopters -->", '<div id="adopters"></div>' -%}
{%- assign parts = readme | split: "## The pattern in five rules" -%}
{{ parts[0] | markdownify }}
<section id="try-it" class="try-it">
  <h2>Try it here</h2>
  <p>A pretend app with four documents. Make one fail to save or cancel a "Save as", then quit: nothing closes.
    <a href="{{ '/demo/' | relative_url }}">Open it on its own page</a></p>
  <iframe id="demo-frame" src="{{ '/demo/?embed' | relative_url }}" title="No File Left Behind demo" loading="lazy"></iframe>
</section>
<script>
  // The demo says how tall it is, so the frame fits it without a scrollbar of its own.
  window.addEventListener("message", (e) => {
    const frame = document.getElementById("demo-frame");
    const h = e.source === frame.contentWindow && e.data && e.data.nflbHeight;
    if (typeof h === "number" && h > 0) frame.style.height = Math.max(560, Math.ceil(h)) + "px";
    // A prompt opened in the demo: scroll just enough to show all of it below the top bar.
    const at = e.source === frame.contentWindow && e.data && e.data.nflbPrompt;
    if (at && typeof at.top === "number" && typeof at.bottom === "number") {
      const y = frame.getBoundingClientRect().top, bar = document.querySelector(".topbar").getBoundingClientRect().bottom + 12;
      const over = bar - (y + at.top), under = y + at.bottom + 12 - window.innerHeight;
      if (over > 0) window.scrollBy({ top: -over, behavior: "smooth" });
      else if (under > 0) window.scrollBy({ top: Math.min(under, -over), behavior: "smooth" });
    }
  });
</script>
{{ "## The pattern in five rules" | append: parts[1] | markdownify }}
<script>
  // The list of apps lives in its own repository; show it here as a table. Without it, the link above still works.
  fetch("https://raw.githubusercontent.com/no-file-left-behind/adopters/main/adopters.json").then((r) => r.json()).then(({ apps }) => {
    const box = document.getElementById("adopters");
    if (!box || !Array.isArray(apps) || !apps.length) return;
    const el = (tag, text) => { const e = document.createElement(tag); if (text !== undefined) e.textContent = text; return e; };
    const table = el("table"), head = el("tr");
    for (const h of ["App", "In the app since", "Against the specification", "Notes"]) head.append(el("th", h));
    table.append(head);
    for (const a of apps) {
      const row = el("tr"), name = el("td");
      if (/^https:\/\//.test(a.url)) { const link = el("a", a.name); link.href = a.url; name.append(link); } else name.textContent = a.name;
      const meets = a.status === "conforms" ? `Conforms to ${a.spec_version}` : `Partly meets ${a.spec_version}; not yet: ${(a.missing || []).join(", ")}`;
      row.append(name, el("td", a.app_version ? `${a.name} ${a.app_version}` : ""), el("td", meets), el("td", a.notes || ""));
      table.append(row);
    }
    box.append(table);
  }).catch(() => {});
</script>
