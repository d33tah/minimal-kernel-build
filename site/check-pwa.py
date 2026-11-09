#!/usr/bin/env python3
"""Fail when the web app's files do not fit together (the Pages workflow runs it).

- every file sw.js precaches exists in the site (after site/stamp.py,
  site/fetch-v86.sh and site/gen-kernels.sh), and every script and stylesheet
  of index.html is precached with the stamp the page will ask for;
- sw.js's V is the stamp of index.html and the commit in version.json;
- manifest.webmanifest is valid JSON with the fields an installable app
  needs; start_url and scope resolve to the site's directory under the
  Pages subpath; every icon exists and is a PNG of the size it claims;
  192 and 512 px icons and a maskable one are there.

Usage: site/check-pwa.py [SITE_DIR] [--subpath /minimal-kernel-build/]
"""
import json
import re
import struct
import sys
from pathlib import Path
from urllib.parse import urljoin

args = sys.argv[1:]
subpath = "/minimal-kernel-build/"
if "--subpath" in args:
    i = args.index("--subpath")
    subpath = args[i + 1]
    del args[i:i + 2]
site = Path(args[0]) if args else Path(__file__).resolve().parent
bad = []

sw = (site / "sw.js").read_text()
v = re.search(r'^const V = "([^"]*)";$', sw, re.M)[1]


def array(name):
    m = re.search(r"const %s = \[(.*?)\];" % name, sw, re.S)
    return re.findall(r'"([^"]+)"', m[1]) if m else []


plain, stamped, kernels = array("PRECACHE_PLAIN"), array("PRECACHE_STAMPED"), array("PRECACHE_KERNELS")
if not plain or not stamped:
    bad.append("sw.js: no PRECACHE_PLAIN / PRECACHE_STAMPED")
size = 0
for f in plain + stamped + kernels:
    p = site / f
    if not p.is_file():
        bad.append(f"sw.js precaches {f}: not in the site")
    else:
        size += p.stat().st_size

html = (site / "index.html").read_text()
for kind, ref in re.findall(r'\b(href|src)="((?![a-z]+:|/|#)[^"]+)"', html):
    path, _, query = ref.partition("?")
    if path.endswith((".js", ".css")):
        if path not in stamped:
            bad.append(f"index.html loads {path}, sw.js does not precache it (PRECACHE_STAMPED)")
        want = "" if v == "dev" else "v=" + v
        if query != want:
            bad.append(f"index.html loads {ref}: stamp {query!r}, sw.js's V wants {want!r}")
    elif path not in plain:
        bad.append(f"index.html links {path}, sw.js does not precache it (PRECACHE_PLAIN)")
# Files the page fetches itself (app.js withV): v86's wasm and BIOSes.
for f in ("v86/v86.wasm", "v86/seabios.bin", "v86/vgabios.bin", "kernels.json"):
    if f not in stamped:
        bad.append(f"sw.js does not precache {f}")
if (site / "version.json").exists():
    sha = json.loads((site / "version.json").read_text()).get("sha", "")
    if v != sha[:12]:
        bad.append(f"sw.js V {v!r} is not version.json's commit {sha[:12]!r}")
if (site / "kernels.json").exists():
    paths = {k[f] for k in json.loads((site / "kernels.json").read_text())["kernels"]
             for f in ("path", "symbols", "disasm") if f in k}
    for f in kernels:
        if f not in paths:
            bad.append(f"sw.js precaches {f}: no kernel in kernels.json has it")


def png_size(p):
    b = p.read_bytes()[:24]
    if b[:8] != b"\x89PNG\r\n\x1a\n" or b[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", b[16:24])


try:
    man = json.loads((site / "manifest.webmanifest").read_text())
except (OSError, ValueError) as e:
    sys.exit(f"manifest.webmanifest: {e}")
for k in ("name", "short_name", "description", "start_url", "scope", "display", "theme_color", "background_color", "icons"):
    if not man.get(k):
        bad.append(f"manifest: no {k}")
origin = "https://example.org"
man_url = origin + subpath + "manifest.webmanifest"
for k in ("start_url", "scope"):
    if urljoin(man_url, man.get(k, "")) != origin + subpath:
        bad.append(f"manifest {k} {man.get(k)!r} resolves to {urljoin(man_url, man.get(k, ''))}, not {origin + subpath}")
if man.get("display") not in ("standalone", "fullscreen", "minimal-ui"):
    bad.append(f"manifest display {man.get('display')!r} is not installable")
have = set()
for i in man.get("icons", []):
    p = site / i["src"]
    wh = png_size(p) if p.is_file() else None
    if not wh:
        bad.append(f"manifest icon {i['src']}: missing or not a PNG")
        continue
    if i.get("sizes") != f"{wh[0]}x{wh[1]}":
        bad.append(f"manifest icon {i['src']}: sizes {i.get('sizes')!r}, the PNG is {wh[0]}x{wh[1]}")
    if i.get("type") != "image/png":
        bad.append(f"manifest icon {i['src']}: type {i.get('type')!r}")
    if not urljoin(man_url, i["src"]).startswith(origin + subpath):
        bad.append(f"manifest icon {i['src']} is outside the scope")
    for purpose in i.get("purpose", "any").split():
        have.add((i.get("sizes"), purpose))
for want in (("192x192", "any"), ("512x512", "any")):
    if want not in have:
        bad.append(f"manifest: no {want[0]} icon with purpose {want[1]}")
if not any(p == "maskable" for _, p in have):
    bad.append("manifest: no maskable icon")
if bad:
    sys.exit("check-pwa:\n" + "\n".join(bad))
print(f"check-pwa: V {v}, {len(plain) + len(stamped) + len(kernels)} files precached ({size / 1e6:.1f} MB), manifest and {len(man['icons'])} icons ok")
