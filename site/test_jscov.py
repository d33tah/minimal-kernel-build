#!/usr/bin/env python3
"""Self-test of the e2e coverage (site/jscov.py collects, site/jscov/report.js reports).

A fixture site in a temporary directory: a page script with a function it
calls, one it never calls, a never-called function nested in a never-called
one and an if whose else never runs; a Worker that runs a function in its
top-level code, posts the result and is terminated by the page at once; a
service worker whose fetch handler runs; a second page reached by goto.
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

from playwright.sync_api import sync_playwright

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
\treturn url.length;
}

function swNever() {
\treturn 1;
}

self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (e) => e.waitUntil(self.clients.claim()));
self.addEventListener("fetch", (e) => { swFetch(e.request.url); });
""",
    "other.js": """// second page
function otherRan() {
\treturn 7;
}

function otherNever() {
\treturn 8;
}

window.other = otherRan();
""",
}

WANT_COVERED = {"cov.js": ["ran", "branchy"], "wk.js": ["workerRan"], "sw.js": ["swFetch"], "other.js": ["otherRan"]}
WANT_UNCOVERED = {"cov.js": ["never", "outerNever", "innerNever"], "wk.js": ["workerNever"], "sw.js": ["swNever"],
                  "other.js": ["otherNever"]}


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
            lines = cov.get("lines", {})
            if not lines.get("total") or lines.get("covered", 0) >= lines.get("total", 0):
                problems.append(f"cov.js: lines {lines}, want some uncovered")
            print(json.dumps({n: {m: s["files"][n][m] for m in ("lines", "functions", "branches")} for n in s["files"]}, indent=1))
    if problems:
        print("FAIL\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
