#!/usr/bin/env python3
"""Fail when a page source has an em-dash (U+2014).

The operator's rule for every text of the project (UI strings, help,
README): no em-dashes; plain separators (a comma, a colon, parentheses)
instead. The Source panel's title once read "fn — path:first-last" on every
file:line click.
"""
import sys
from pathlib import Path

site = Path(__file__).resolve().parent
files = sorted(site.glob("*.js")) + sorted(site.glob("*.html")) + sorted(site.glob("*.css")) + [site / "README.md"]
bad = []
for f in files:
    for n, line in enumerate(f.read_text().splitlines(), 1):
        if "—" in line:
            bad.append(f"{f.name}:{n}: {line.strip()[:120]}")
if bad:
    sys.exit("em-dashes in page sources:\n" + "\n".join(bad))
print(f"check-text: {len(files)} page sources, no em-dash")
