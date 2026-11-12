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
  | replace: "(scenarios/)", "(scenarios/README.html)"
  | replace: "(implementations/)", "(implementations/README.html)"
  | replace: "(https://no-file-left-behind.github.io/demo/)", "(#try-it)" -%}
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
