#!/usr/bin/env python3
"""Fail when app.js looks up an element id that index.html does not have.

The page broke once in the wild with `$("header") is null`: a browser kept
an app.js from an older deploy next to a newer index.html. The workflow now
stamps asset URLs with the commit (cache busting); this check guards the
other half, that the app.js and index.html of one commit agree.
"""
import re
import sys
from pathlib import Path

site = Path(__file__).resolve().parent
# every page script that looks up elements
js = "\n".join((site / f).read_text() for f in ("app.js", "graph.js", "source.js", "coverage.js", "symimport.js", "livedis.js", "fastforward.js", "vars.js", "dbginfo.js", "jumps.js", "pwa.js", "settings.js", "snapshots.js", "replay.js", "bookmarks.js"))
html = (site / "index.html").read_text()
ids = set(re.findall(r'\bid="([^"]+)"', html))
# an id must be unique: $() finds the first one only, so a second element
# with the same id never gets its content (the Coverage tab's notes once went
# into the Page tab's "cov_info").
all_ids = re.findall(r'\bid="([^"]+)"', html)
dups = sorted({i for i in all_ids if all_ids.count(i) > 1})
if dups:
    sys.exit("index.html has duplicate ids: " + ", ".join(dups))
# ids created at runtime by app.js itself
ids |= set(re.findall(r'\.id\s*=\s*"([^"]+)"', js))
used = set(re.findall(r'\$\("([^"]+)"\)', js))
missing = sorted(used - ids)
if missing:
    sys.exit("app.js uses ids missing from index.html: " + ", ".join(missing))
print(f"check-ids: {len(used)} ids used by app.js, all present in index.html")
