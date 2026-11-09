#!/usr/bin/env python3
"""Stamp a site directory with the deployed commit (the Pages workflow runs it).

Cache busting: Pages serves with max-age=600, and a browser that keeps an
older app.js next to a newer index.html breaks. Every script and stylesheet
index.html loads gets ?v=<first 12 hex digits of the commit> (the page's
own fetches add the same stamp, app.js withV), and sw.js gets the commit
as its V, so a deploy installs a new service worker with new caches.

Usage: site/stamp.py SITE_DIR COMMIT_SHA
"""
import re
import sys
from pathlib import Path

ASSET = re.compile(r'\b(href|src)="((?![a-z]+:|/|#)[^"?]+\.(?:js|css))(\?v=[^"]*)?"')
SW_V = re.compile(r'^const V = "[^"]*";$', re.M)


def stamp_html(html, v):
    return ASSET.sub(lambda m: f'{m[1]}="{m[2]}?v={v}"', html)


def stamp_sw(js, v):
    out, n = SW_V.subn(f'const V = "{v}";', js)
    if n != 1:
        sys.exit(f"sw.js: {n} lines 'const V = ...;', want 1")
    return out


def main():
    if len(sys.argv) != 3 or not re.fullmatch(r"[0-9a-f]{12,40}", sys.argv[2]):
        sys.exit(__doc__)
    site, v = Path(sys.argv[1]), sys.argv[2][:12]
    html = stamp_html((site / "index.html").read_text(), v)
    (site / "index.html").write_text(html)
    (site / "sw.js").write_text(stamp_sw((site / "sw.js").read_text(), v))
    for m in ASSET.finditer(html):
        print(m[0])
    print(f'sw.js: const V = "{v}";')


if __name__ == "__main__":
    main()
