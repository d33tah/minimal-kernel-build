#!/usr/bin/env python3
"""Boot kernels on the page in headless Chromium and check it end to end.

Serves site/ with python's http.server on a free port and opens
index.html?kernel=NAME&mem=BYTES for each case. Per case: the VGA screen
shows "Hello, World!" then "Still alive"; the boot counters count both
lines; the page scan; the Page tab listing (instructions and hex rows,
one symbol index, survives scans, tab kept in the URL hash across a
reload, all-zero page collapsed); no horizontal scroll at 390-1280 px;
a snapshot round trip. Then per kernel, started paused at the reset
vector: Step 10, Run to 0x100000, 1k/s slow motion. Then a 390 px touch
case for the "?" popovers, a coverage case (never-executed
instructions tinted in the Page listing) and a stale-cache case (two deploys with
max-age=600). See README.md, Testing.

Usage:
  site/test_browser.py                      all of the above
  site/test_browser.py --suites debian,dwarf  some suites (SUITES below; CI runs them in parallel jobs)
  site/test_browser.py hello:1048576 minified-linux:18874368
  site/test_browser.py --probe hello:655360 report only, never fail

Needs: pip install playwright && playwright install chromium
"""

import argparse
import pathlib
import re
import sys
import time

from playwright.sync_api import sync_playwright

import jscov
from tests.browser.common import kernel_entries, serve, SITE
from tests.browser.boot import suite_boot
from tests.browser.symbols import suite_symbols
from tests.browser.debugger import suite_debugger
from tests.browser.misc import suite_misc
from tests.browser.debian import suite_debian
from tests.browser.dwarf import suite_dwarf
from tests.browser.dwarf_import import suite_dwarf_import
from tests.browser.types import suite_types
from tests.browser.runto import suite_runto
from tests.browser.runto_phone import suite_runto_phone
from tests.browser.initramfs import suite_initramfs
from tests.browser.tasks import suite_tasks, suite_tasks_minified
from tests.browser.coverage import suite_coverage
from tests.browser.covtree import suite_covtree
from tests.browser.unwind import suite_unwind
from tests.browser.operands import suite_operands
from tests.browser.lineaddr import suite_lineaddr
from tests.browser.fnfacts import suite_fnfacts
from tests.browser.branches import suite_branches
from tests.browser.touch import suite_touch
from tests.browser.hello import suite_hello
from tests.browser.firstlook import suite_firstlook
from tests.browser.srclinks import suite_srclinks
from tests.browser.unoptimized import suite_unoptimized
from tests.browser.jumps import suite_jumps
from tests.browser.runstate import suite_runstate
from tests.browser.speedline import suite_speedline
from tests.browser.speedasm import suite_speedasm
from tests.browser.navhist import suite_navhist
from tests.browser.runbadge import suite_runbadge
from tests.browser.overlap import suite_overlap
from tests.browser.memeip import suite_memeip
from tests.browser.graphopen import suite_graphopen
from tests.browser.srcfiles import suite_srcfiles
from tests.browser.snapdiff import suite_snapdiff
from tests.browser.deltasnap import suite_deltasnap
from tests.browser.bookmarks import suite_bookmarks
from tests.browser.tabs import suite_tabs
from tests.browser.pwa import suite_pwa
from tests.browser.followeip import suite_followeip
from tests.browser.followfuzz import suite_followfuzz
from tests.browser.nopath import suite_nopath
from tests.browser.cfi import suite_cfi
from tests.browser.dwarf_lto import suite_dwarf_lto
from tests.browser.dwarf_forms import suite_dwarf_forms
from tests.browser.tipmarks import suite_tipmarks
from tests.browser.legend import suite_legend
from tests.browser.jitdet import suite_jitdet
from tests.browser.guestin import suite_guestin
from tests.browser.exactstop import suite_exactstop
from tests.browser.replay import suite_replay
from tests.browser.reverse import suite_reverse
from tests.browser.framegold import suite_framegold
from tests.browser.jitsnap import suite_jitsnap
from tests.browser.jitrev import suite_jitrev
from tests.browser.genimport import suite_genimport
from tests.browser.memevents import suite_memevents
from tests.browser.memmap import suite_memmap
from tests.browser.scanpages import suite_scanpages
from tests.browser.quests import suite_quests
from tests.browser.memtabs import suite_memtabs
from tests.browser.srcedit import suite_srcedit
from tests.browser.rebuild import suite_rebuild
from tests.browser.rebuild_linux import suite_rebuild_linux
from tests.browser.rebuild_debian import suite_rebuild_debian


def default_cases():
    names = kernel_entries()
    # hello boots with the page's default (1k/s from the kernel entry, about
    # 0.2 s); Linux would take ~15 minutes that way, so it runs with
    # ?speed=full (whole boot counted in the background, then the JIT).
    cases = [("hello", None, None)]
    if "hello-os" in names:
        cases.append(("hello-os", None, None))
    if "minified-linux" in names:
        cases.append(("minified-linux", None, "full"))
        cases.append(("minified-linux", 18 * 1024 * 1024, "full"))
        # The smallest RAM it boots with in v86 (measured: 2176 KiB fails).
        cases.append(("minified-linux", 2180 * 1024, "full"))
    if "minified-linux-os" in names:
        # The -Os product build: the same boot at its default and smallest RAM.
        cases.append(("minified-linux-os", None, "full"))
        cases.append(("minified-linux-os", 2180 * 1024, "full"))
    return cases


SUITES = {
    "boot": (suite_boot, False),
    "symbols": (suite_symbols, True),
    "debugger": (suite_debugger, False),
    "misc": (suite_misc, True),
    "debian": (suite_debian, False),
    "dwarf": (suite_dwarf, False),
    "dwarf_import": (suite_dwarf_import, True),
    "types": (suite_types, True),
    "runto": (suite_runto, True),
    "runto_phone": (suite_runto_phone, False),
    "initramfs": (suite_initramfs, False),
    "tasks": (suite_tasks, False),
    "tasks_minified": (suite_tasks_minified, True),
    "coverage": (suite_coverage, True),
    "covtree": (suite_covtree, True),
    "unwind": (suite_unwind, True),
    "operands": (suite_operands, True),
    "lineaddr": (suite_lineaddr, True),
    "fnfacts": (suite_fnfacts, True),
    "branches": (suite_branches, True),
    "touch": (suite_touch, False),
    "hello": (suite_hello, False),
    "firstlook": (suite_firstlook, False),
    "srclinks": (suite_srclinks, True),
    "unoptimized": (suite_unoptimized, True),
    "jumps": (suite_jumps, False),
    "runstate": (suite_runstate, False),
    "speedline": (suite_speedline, True),
    "speedasm": (suite_speedasm, True),
    "navhist": (suite_navhist, False),
    "runbadge": (suite_runbadge, False),
    "overlap": (suite_overlap, False),
    "memeip": (suite_memeip, False),
    "graphopen": (suite_graphopen, False),
    "srcfiles": (suite_srcfiles, False),
    "snapdiff": (suite_snapdiff, False),
    "deltasnap": (suite_deltasnap, False),
    "bookmarks": (suite_bookmarks, False),
    "tabs": (suite_tabs, False),
    "pwa": (suite_pwa, False),
    "followeip": (suite_followeip, False),
    "followfuzz": (suite_followfuzz, False),
    "nopath": (suite_nopath, False),
    "cfi": (suite_cfi, False),
    "dwarf_lto": (suite_dwarf_lto, False),
    "dwarf_forms": (suite_dwarf_forms, False),
    "tipmarks": (suite_tipmarks, False),
    "legend": (suite_legend, True),
    "jitdet": (suite_jitdet, False),
    "guestin": (suite_guestin, False),
    "exactstop": (suite_exactstop, False),
    "replay": (suite_replay, False),
    "reverse": (suite_reverse, False),
    "framegold": (suite_framegold, True),
    "jitsnap": (suite_jitsnap, False),
    "jitrev": (suite_jitrev, False),
    "genimport": (suite_genimport, True),
    "memevents": (suite_memevents, False),
    "memmap": (suite_memmap, False),
    "scanpages": (suite_scanpages, False),
    "quests": (suite_quests, False),
    "memtabs": (suite_memtabs, False),
    "srcedit": (suite_srcedit, False),
    "rebuild": (suite_rebuild, False),
    "rebuild_linux": (suite_rebuild_linux, True),
    "rebuild_debian": (suite_rebuild_debian, False),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*", help="NAME or NAME:MEMBYTES (only the boot suite then runs)")
    ap.add_argument("--suites", help="comma-separated, of: " + ", ".join(SUITES))
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--probe", action="store_true", help="report, do not assert")
    ap.add_argument("--shots", default=str(SITE / "test-screenshots"))
    ap.add_argument("--fuzz-seeds", type=int, default=0, help="runstate, followfuzz: this many seeds instead of the fixed ones")
    ap.add_argument("--fuzz-from", type=int, default=100, help="runstate, followfuzz: first seed with --fuzz-seeds")
    ap.add_argument("--fuzz-actions", type=int, default=30, help="runstate, followfuzz: actions per seed")
    ap.add_argument("--base", help="test this deployed site (e.g. https://d33.pl/minimal-kernel-build) instead of serving site/")
    args = ap.parse_args()

    cases = []
    for c in args.cases:
        name, mem, speed = (c.split(":") + ["", ""])[:3]
        cases.append((name, int(mem) if mem else None, speed or None))
    names = args.suites.split(",") if args.suites else ["boot"] if args.cases else list(SUITES)
    unknown = [n for n in names if n not in SUITES]
    if unknown:
        ap.error("unknown suites: " + ", ".join(unknown))
    shots = pathlib.Path(args.shots)
    shots.mkdir(parents=True, exist_ok=True)

    httpd = None if args.base else serve()
    entries = kernel_entries()
    ctx = argparse.Namespace(args=args, cases=cases or default_cases(), shots=shots, entries=entries,
                             base=args.base.rstrip("/") if args.base else f"http://127.0.0.1:{httpd.server_address[1]}")
    failed = False
    # With MKB_JSCOV set: V8 coverage of the site's scripts (site/jscov.py).
    jscov.install("browser-" + re.sub(r"[^a-z0-9]+", "-", ",".join(names))[:60])
    with sync_playwright() as p:
        ctx.browser = p.chromium.launch()
        for name in names:
            run, needs_linux = SUITES[name]
            if needs_linux and "minified-linux" not in entries:
                continue
            t0 = time.monotonic()
            failed |= bool(run(ctx))
            print(f"suite {name}: {time.monotonic() - t0:.0f} s", flush=True)
        ctx.browser.close()
    if httpd:
        httpd.shutdown()
    if failed and not args.probe:
        print("FAIL", file=sys.stderr)
        return 1
    print("done" if args.probe else "PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
