#!/usr/bin/env python3
"""Fail when a page script looks up an element id that the page does not have.

The page broke once in the wild with `$("header") is null`: a browser kept
an app.js from an older deploy next to a newer index.html. The workflow now
stamps asset URLs with the commit (cache busting); this check guards the
other half, that the app.js and index.html of one commit agree.
"""
import re
import sys
from pathlib import Path

site = Path(__file__).resolve().parent
# every page script (site/*.js but the bundle build.mjs makes of them), and
# the React components (R3), whose markup (id="...") is part of the page
# like index.html's; the page is index.src.html with the static markup of
# markup/*.jsx (build.mjs writes index.html from them)
jsx = sorted(site.glob("*.jsx")) + sorted(site.glob("markup/*.jsx"))
js = "\n".join([f.read_text() for f in sorted(site.glob("*.js")) if f.name != "app.bundle.js"]
               + [f.read_text() for f in jsx])
html = (site / "index.src.html").read_text() + "\n".join(f.read_text() for f in jsx)
ids = set(re.findall(r'\bid="([^"]+)"', html))
# an id must be unique: $() finds the first one only, so a second element
# with the same id never gets its content (the Coverage tab's notes once went
# into the Page tab's "cov_info").
all_ids = re.findall(r'\bid="([^"]+)"', html)
dups = sorted({i for i in all_ids if all_ids.count(i) > 1})
if dups:
    sys.exit("index.html has duplicate ids: " + ", ".join(dups))
# ids the page scripts create at runtime (el.id = "x", or id="x" in markup they write)
ids |= set(re.findall(r'\.id\s*=\s*"([^"]+)"', js)) | set(re.findall(r'\bid="([^"]+)"', js))
used = set(re.findall(r'\$\("([^"]+)"\)', js))
missing = sorted(used - ids)
if missing:
    sys.exit("page scripts use ids missing from the page: " + ", ".join(missing))
print(f"check-ids: {len(used)} ids used by the page scripts, all present in the page")
