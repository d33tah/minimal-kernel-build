#!/usr/bin/env python3
"""Fail when a page source has an em-dash (U+2014) or a stale claim.

The operator's rule for every text of the project (UI strings, help,
README): no em-dashes; plain separators (a comma, a colon, parentheses)
instead. The Source panel's title once read "fn — path:first-last" on every
file:line click.

Stale claims: sentences that were true once and that the page must not
say again. Since J1-J3 every count on the virtual clock (the default) is
the same in every run, yet until X1 the count tooltip and the About tab
still said SeaBIOS waits on real time, so the part before the kernel
differs between runs, and that the minified Linux calibrates its delay
loop against the timer (it has not since it dropped calibrate_delay).
"""
import re
import sys
from pathlib import Path

STALE = [
    (re.compile(r"SeaBIOS waits on real time", re.I), "SeaBIOS waits on the virtual clock unless the clock is real time"),
    (re.compile(r"calibrates its delay loop against the timer", re.I), "the minified Linux has no calibrate_delay"),
    (re.compile(r"\b(var(y|ies)|differs?)( a little)? between runs", re.I), "counts on the virtual clock are the same every run"),
]

site = Path(__file__).resolve().parent
files = sorted(site.glob("*.js")) + sorted(site.glob("*.html")) + sorted(site.glob("*.css")) + [site / "README.md"]
bad = []
for f in files:
    for n, line in enumerate(f.read_text().splitlines(), 1):
        if "—" in line:
            bad.append(f"{f.name}:{n}: em-dash: {line.strip()[:120]}")
        for rx, why in STALE:
            m = rx.search(line)
            if m:
                bad.append(f"{f.name}:{n}: stale claim {m.group(0)!r} ({why})")
if bad:
    sys.exit("text check failed:\n" + "\n".join(bad))
print(f"check-text: {len(files)} page sources, no em-dash, no stale claim")
