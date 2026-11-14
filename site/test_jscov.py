#!/usr/bin/env python3
"""Self-test of the e2e coverage (site/jscov.py collects, site/jscov/report.js reports).

A fixture site in a temporary directory: a page script with a function it
calls, one it never calls, a never-called function nested in a never-called
one and an if whose else never runs; a Worker that runs a function in its
top-level code, posts the result and is terminated by the page at once; a
service worker whose fetch handler runs; a second page reached by goto,
which then leaves for a third page on another site by itself (location.href:
its last function runs just before the old renderer process goes away).
The collector handles the service worker's attach a second late, as a busy
CI runner may: Playwright runs a new service worker at once, and its
coverage must still start before the worker's first line (block detail in
sw.js; jscov.HOLD_SW_JS).
The coverage of one headless Chromium run goes through report.js, and the
report must show each executed function covered and each unexecuted one
uncovered (the plan's V1 self-test), with the branch and line counts.

Usage: site/test_jscov.py   (needs playwright + chromium and npm ci in site/jscov)
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError, sync_playwright

import jscov
from test_browser import serve, wait_for

SITE = pathlib.Path(__file__).resolve().parent

FILES = {
    "index.html": """<!doctype html><meta charset="utf-8"><title>jscov fixture</title>
<script src="cov.js?v=1"></script>""",
    "other.html": """<!doctype html><meta charset="utf-8"><title>jscov fixture 2</title>
<script src="other.js"></script>""",
    "cov.js": """// page script
function ran(x) {
\treturn x + 1;
}

function never(x) {
\treturn x * 2;
}

function outerNever() {
\tfunction innerNever() {
\t\treturn 3;
\t}
\treturn innerNever();
}

function branchy(x) {
\tif (x)
\t\treturn "then";
\telse
\t\treturn "else";
}

window.result = ran(1) + branchy(true);
const w = new Worker("wk.js?v=1");
w.onmessage = (e) => {
\twindow.fromWorker = e.data;
\tw.terminate();
\twindow.terminated = true;
};
navigator.serviceWorker.register("sw.js").then(() => { window.swRegistered = true; });
""",
    "wk.js": """// worker script
function workerRan() {
\treturn 42;
}

function workerNever() {
\treturn 0;
}

postMessage(workerRan());
""",
    "sw.js": """// service worker
function swFetch(url) {
\tif (url.includes("through-sw"))
\t\treturn "through";
\treturn url.length;
}

function swNever() {
\treturn 1;
}

// other.js from the cache it fills at install, as the site's sw.js does
// (Chromium keeps a V8 code cache of such a script).
self.addEventListener("install", (e) => e.waitUntil(caches.open("fx").then((c) => c.add("other.js")).then(() => self.skipWaiting())));
self.addEventListener("activate", (e) => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", (e) => {
	swFetch(e.request.url);
	if (e.request.url.endsWith("/other.js"))
		e.respondWith(caches.match(e.request).then((r) => r || fetch(e.request)));
});
""",
    "other.js": """// second page
// Chromium keeps a code cache only of scripts of 1 KiB and more: this comment
// makes it one. """ + "-" * 1200 + """
function otherRan() {
\treturn 7;
}

function otherNever() {
\treturn 8;
}

// Runs only in the document after a reload (the same script again).
function afterReload(x) {
	if (x)
		return "again";
	return "first";
}

if (sessionStorage.getItem("loaded"))
	window.again = afterReload(true);
sessionStorage.setItem("loaded", "1");

// The page leaves for another site itself (a new renderer process).
function otherLeave(to) {
	window.left = to;
	location.href = to;
}

window.other = otherRan();
""",
    "third.html": """<!doctype html><meta charset="utf-8"><title>jscov fixture 3</title>
<script src="third.js"></script>""",
    "third.js": """// third page, on another site (localhost)
window.third = 9;
""",
}

WANT_COVERED = {"cov.js": ["ran", "branchy"], "wk.js": ["workerRan"], "sw.js": ["swFetch"],
                "other.js": ["otherRan", "otherLeave", "afterReload"]}
WANT_UNCOVERED = {"cov.js": ["never", "outerNever", "innerNever"], "wk.js": ["workerNever"], "sw.js": ["swNever"],
                  "other.js": ["otherNever"]}


def where(page):
    """[location.href, window.third], or None while the page is between documents."""
    try:
        return page.evaluate("[location.href, window.third]")
    except PlaywrightError:
        return None


def main():
    problems = []
    with tempfile.TemporaryDirectory(prefix="jscov-selftest-", dir=os.environ.get("MKB_TMP")) as tmp:
        tmp = pathlib.Path(tmp)
        fix, raw, out = tmp / "site", tmp / "raw", tmp / "report"
        fix.mkdir()
        for n, t in FILES.items():
            (fix / n).write_text(t)
        os.environ["MKB_JSCOV"] = str(raw)
        jscov.configure(fix)
        attached = jscov.Collector._attached

        def late(self, sid, info, waiting):
            if info.get("type") == "service_worker":
                time.sleep(1.0)
            return attached(self, sid, info, waiting)
        jscov.Collector._attached = late
        jscov.install("selftest")
        httpd = serve(fix)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        try:
            with sync_playwright() as p:
                browser = p.chromium.launch()
                ctx = browser.new_context()
                page = ctx.new_page()
                page.goto(base + "/index.html")
                if not wait_for(lambda: page.evaluate("window.terminated === true && window.swRegistered === true"), 30):
                    problems.append(f"fixture: {page.evaluate('[window.result, window.fromWorker, window.terminated, window.swRegistered]')}")
                if not wait_for(lambda: page.evaluate("!!navigator.serviceWorker.controller"), 30):
                    problems.append("fixture: the service worker never controlled the page")
                page.evaluate("fetch('index.html?through-sw')")
                time.sleep(0.2)
                page.goto(base + "/other.html")
                wait_for(lambda: page.evaluate("window.other === 7"), 10)
                # The same script in a new document: its blocks must still be seen.
                page.reload()
                if not wait_for(lambda: page.evaluate("window.again === 'again'"), 10):
                    problems.append("fixture: afterReload did not run after the reload")
                # Not through Playwright: the page navigates itself, cross-site.
                third = f"http://localhost:{httpd.server_address[1]}/third.html"
                page.evaluate(f"setTimeout(() => otherLeave({json.dumps(third)}), 0)")
                if not wait_for(lambda: where(page) == [third, 9], 10):
                    problems.append(f"fixture: the page did not reach {third} itself ({where(page)})")
                ctx.close()
                browser.close()
        finally:
            httpd.shutdown()
        raws = sorted(raw.glob("*.jsonl"))
        if not raws:
            problems.append("no raw coverage written")
        r = subprocess.run(["node", str(SITE / "jscov" / "report.js"), "--src", str(fix), "--out", str(out), str(raw)],
                           capture_output=True, text=True)
        print(r.stdout, r.stderr, sep="")
        if r.returncode:
            problems.append(f"report.js exited {r.returncode}")
        else:
            s = json.loads((out / "summary.json").read_text())
            if not (out / "index.html").exists():
                problems.append("no HTML report")
            for n in FILES:
                if n.endswith(".js") and n not in s["files"]:
                    problems.append(f"{n}: not in the report")
            for n, fns in WANT_UNCOVERED.items():
                got = {f["name"] for f in s["files"].get(n, {}).get("uncoveredFunctions", [])}
                for fn in fns:
                    if fn not in got:
                        problems.append(f"{n}: {fn} never ran, the report does not list it as uncovered ({sorted(got)})")
                for fn in WANT_COVERED[n]:
                    if fn in got:
                        problems.append(f"{n}: {fn} ran, the report lists it as uncovered")
            cov = s["files"].get("cov.js", {})
            # ran, never, outerNever, innerNever, branchy, the onmessage arrow, the then-callback.
            if cov.get("functions", {}).get("total") != 7:
                problems.append(f"cov.js: {cov.get('functions')} functions, want 7 (the nested one included)")
            b = cov.get("branches", {})
            if b.get("total") != 2 or b.get("covered") != 1:
                problems.append(f"cov.js: branches {b}, want the if's then covered and its else not (1 of 2)")
            # Block detail of the reloaded document: the taken return ran, the other did not.
            src = FILES["other.js"].split("\n")
            taken, not_taken = src.index('\t\treturn "again";') + 1, src.index('\treturn "first";') + 1
            unc = set()
            for a, _, b in (x.partition("-") for x in s["files"].get("other.js", {}).get("uncoveredLines", "").split(",") if x):
                unc.update(range(int(a), int(b or a) + 1))
            if taken in unc or not_taken not in unc:
                problems.append(f"other.js after a reload: uncovered lines {sorted(unc)}, want {not_taken} (the return "
                                f"not taken) among them and not {taken}")
            # sw.js with block detail: in swFetch the return the page's fetch
            # takes is covered, the other return (requests without
            # "through-sw") too, and swNever's body not.
            src = FILES["sw.js"].split("\n")
            unc = set()
            for a, _, b in (x.partition("-") for x in s["files"].get("sw.js", {}).get("uncoveredLines", "").split(",") if x):
                unc.update(range(int(a), int(b or a) + 1))
            through, never_ret = src.index('\t\treturn "through";') + 1, src.index("\treturn 1;") + 1
            if through in unc or never_ret not in unc:
                problems.append(f"sw.js: uncovered lines {sorted(unc)}, want {never_ret} (swNever) among them and not "
                                f"{through} (the service worker ran before its coverage started?)")
            lines = cov.get("lines", {})
            if not lines.get("total") or lines.get("covered", 0) >= lines.get("total", 0):
                problems.append(f"cov.js: lines {lines}, want some uncovered")
            # The list of uncovered lines with reasons (--uncovered): never's
            # lines listed, the rest of cov.js's uncovered lines unlisted, a
            # listed line that ran stale; --require-listed fails on unlisted.
            src = FILES["cov.js"].split("\n")
            never = [src.index("function never(x) {") + 2]
            ran_line = src.index("function ran(x) {") + 2
            listed = tmp / "uncovered.tsv"
            listed.write_text(f"# self-test\ncov.js\t{never[0]},{ran_line}\tnever is never called\n")
            cov_unc = set()
            for a, _, b in (x.partition("-") for x in cov.get("uncoveredLines", "").split(",") if x):
                cov_unc.update(range(int(a), int(b or a) + 1))
            for req in (False, True):
                r2 = subprocess.run(["node", str(SITE / "jscov" / "report.js"), "--src", str(fix), "--out", str(tmp / "report2"),
                                     "--uncovered", str(listed)] + (["--require-listed"] if req else []) + [str(raw)],
                                    capture_output=True, text=True)
                s2 = json.loads((tmp / "report2" / "summary.json").read_text())
                f2 = s2["files"]["cov.js"]
                want_unlisted = sorted(cov_unc - set(never))
                got_unlisted = []
                for a, _, b in (x.partition("-") for x in f2.get("unlisted", "").split(",") if x):
                    got_unlisted.extend(range(int(a), int(b or a) + 1))
                if got_unlisted != want_unlisted or f2.get("stale") != str(ran_line):
                    problems.append(f"--uncovered: cov.js unlisted {got_unlisted} stale {f2.get('stale')!r}, want "
                                    f"{want_unlisted} and {ran_line}")
                if bool(r2.returncode) != (req and bool(want_unlisted)):
                    problems.append(f"--uncovered{' --require-listed' if req else ''}: exit {r2.returncode}")
            print(json.dumps({n: {m: s["files"][n][m] for m in ("lines", "functions", "branches")} for n in s["files"]}, indent=1))
    if problems:
        print("FAIL\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
