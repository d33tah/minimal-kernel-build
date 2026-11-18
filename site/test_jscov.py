#!/usr/bin/env python3
"""Self-test of the e2e coverage (site/jscov.py collects, site/jscov/report.js reports).

A fixture site in a temporary directory: a page script with a function it
calls, one it never calls, a never-called function nested in a never-called
one and an if whose else never runs; a Worker that runs a function in its
top-level code, posts the result and is terminated by the page at once; a
service worker whose fetch handler runs; a second page reached by goto,
which then leaves for a third page on another site by itself (location.href:
its last function runs just before the old renderer process goes away); a
fourth page opened while jscov.hold() (its coverage starts at
jscov.release(): what ran before is left out, what runs after has blocks).
The collector handles the service worker's attach a second late, as a busy
CI runner may: Playwright runs a new service worker at once, and its
coverage must still start before the worker's first line (block detail in
sw.js; jscov.HOLD_SW_JS).
The coverage of one headless Chromium run goes through report.js, and the
report must show each executed function covered and each unexecuted one
uncovered (the plan's V1 self-test), with the branch and line counts.

Usage: site/test_jscov.py   (needs playwright + chromium and npm ci in site/jscov)
"""

import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError, sync_playwright

import jscov
from tests.browser.common import serve, wait_for

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
    "held.html": """<!doctype html><meta charset="utf-8"><title>jscov fixture 4</title>
<script src="held.js"></script>""",
    "held.js": """// a page opened while jscov.hold(): its coverage starts at jscov.release()
function heldBefore() {
\treturn 1;
}

function heldAfter(x) {
\tif (x)
\t\treturn "after";
\treturn "no";
}

function heldNever() {
\treturn 2;
}

window.before = heldBefore();
""",
}

WANT_COVERED = {"cov.js": ["ran", "branchy"], "wk.js": ["workerRan"], "sw.js": ["swFetch"],
                "other.js": ["otherRan", "otherLeave", "afterReload"], "held.js": ["heldAfter"]}
# heldBefore ran before release(): compiled without block counters, it is
# left out, so it reads as not run.
WANT_UNCOVERED = {"cov.js": ["never", "outerNever", "innerNever"], "wk.js": ["workerNever"], "sw.js": ["swNever"],
                  "other.js": ["otherNever"], "held.js": ["heldBefore", "heldNever"]}


def where(page):
    """[location.href, window.third], or None while the page is between documents."""
    try:
        return page.evaluate("[location.href, window.third]")
    except PlaywrightError:
        return None


def code_of(lines, nums):
    """listed.js's codeOf: SHA-256 of the line before the block and the block's lines, trimmed."""
    nums = sorted(nums)
    text = [lines[nums[0] - 2].strip() if nums[0] > 1 else ""] + [lines[n - 1].strip() for n in nums]
    return hashlib.sha256("\n".join(text).encode()).hexdigest()[:8]


def line_set(text):
    out = set()
    for a, _, b in (x.partition("-") for x in text.split(",") if x):
        out.update(range(int(a), int(b or a) + 1))
    return out


def check_list(tmp, fix, raw, summary):
    """The list of uncovered lines with reasons on the fixture's coverage:
    report.js --uncovered --require-listed fails on an unlisted line, a
    listed line that ran, a row whose code moved or changed and a TODO
    row, passes on its own uncovered-candidates.tsv once the TODOs have
    reasons, and lets a "sometimes:" row run; relist.js renumbers rows
    after lines are added above them and refuses a row whose code changed."""
    problems = []
    report_js, relist_js = str(SITE / "jscov" / "report.js"), str(SITE / "jscov" / "relist.js")
    src = FILES["cov.js"].split("\n")
    unc = {n: line_set(f.get("uncoveredLines", "")) for n, f in summary["files"].items()}
    unc = {n: u for n, u in unc.items() if u}
    ran_line = src.index("function ran(x) {") + 2
    listed = tmp / "uncovered.tsv"

    def rows_of(text):
        return [line.split("\t") for line in text.split("\n") if line and not line.startswith("#")]

    def run(name, rows, want_exit, **want):
        listed.write_text("# self-test\n" + "".join("\t".join(map(str, r)) + "\n" for r in rows))
        out = tmp / ("list-" + name)
        r = subprocess.run(["node", report_js, "--src", str(fix), "--out", str(out), "--uncovered", str(listed),
                            "--require-listed", str(raw)], capture_output=True, text=True)
        s = json.loads((out / "summary.json").read_text())
        got = {k: s.get(k) for k in want}
        if r.returncode != want_exit or got != want:
            problems.append(f"list case {name}: exit {r.returncode}, {got}; want exit {want_exit}, {want}\n{r.stdout}{r.stderr}")
        return r, s, rows_of((out / "uncovered-candidates.tsv").read_text())

    # An empty list: every uncovered line unlisted, the job output names
    # them and the way out; the candidates are one TODO row per block.
    r, s, cand = run("empty", [], 1, unlisted=sum(map(len, unc.values())), stale=0)
    if f"cov.js:{s['files']['cov.js'].get('unlisted')}" not in r.stdout or "uncovered-candidates.tsv" not in r.stdout:
        problems.append(f"list case empty: the message does not name cov.js's lines and the candidates file:\n{r.stdout}")
    got = {}
    for c in cand:
        got.setdefault(c[0], set()).update(line_set(c[1]))
    if any(not c[3].startswith("TODO") for c in cand) or got != unc:
        problems.append(f"list case empty: candidates {cand}, want TODO rows covering {unc}")
    # The candidates with reasons written: true for this run.
    good = [c[:3] + ["never called in the fixture"] for c in cand]
    if any(c[2] != code_of(FILES[c[0]].split("\n"), line_set(c[1])) for c in good):
        problems.append(f"list case empty: candidate codes {[c[2] for c in good]} are not listed.js's codeOf")
    run("good", good, 0, unlisted=0, stale=0, moved=0, changed=0, todo=0)
    # A listed line that ran: stale, named in the output.
    r, _, _ = run("stale", good + [["cov.js", ran_line, code_of(src, [ran_line]), "wrong"]], 1, unlisted=0, stale=1)
    if f"cov.js:{ran_line}" not in r.stdout:
        problems.append(f"list case stale: the output does not name cov.js:{ran_line}:\n{r.stdout}")
    # ... unless it is one that runs only sometimes.
    run("sometimes", good + [["cov.js", ran_line, code_of(src, [ran_line]), "sometimes: a timing"]], 0, unlisted=0, stale=0)
    # A row three lines off with its code: moved; the candidates put it back.
    first = line_set(good[0][1])
    off = [good[0][:1] + [",".join(str(n + 3) for n in sorted(first))] + good[0][2:]] + good[1:]
    _, _, cand = run("moved", off, 1, unlisted=0, stale=0, moved=1, changed=0)
    if [c[:3] for c in cand] != [c[:3] for c in good]:
        problems.append(f"list case moved: candidates {cand}, want {good}")
    # A row whose code is not in the file; a TODO row.
    run("changed", [good[0][:2] + ["00000000"] + good[0][3:]] + good[1:], 1, changed=1)
    run("todo", [good[0][:3] + ["TODO: later"]] + good[1:], 1, todo=1)

    # relist.js on a copy of the fixture: two lines added at the top move
    # every row by two; a listed line edited makes its row "changed".
    rl = tmp / "relist"
    rl.mkdir()
    lst = rl / "jscov-uncovered.tsv"
    lst.write_text("# self-test\n" + "".join("\t".join(r) + "\n" for r in good))
    for n in {c[0] for c in good}:
        (rl / n).write_text(FILES[n])

    def relist(*a):
        return subprocess.run(["node", relist_js, *a, str(lst)], capture_output=True, text=True)
    if relist("--check").returncode:
        problems.append("relist --check: fails on a list that matches")
    (rl / "cov.js").write_text("// one\n// two\n" + FILES["cov.js"])
    r = relist("--check")
    if r.returncode != 1 or "moved" not in r.stdout or "relist.js" not in r.stdout:
        problems.append(f"relist --check after two lines added: exit {r.returncode}\n{r.stdout}")
    r = relist()
    want = [[c[0], ",".join(str(n + 2 * (c[0] == "cov.js")) for n in sorted(line_set(c[1]))), c[2], c[3]] for c in good]
    got = rows_of(lst.read_text())
    if r.returncode or [[g[0], ",".join(str(n) for n in sorted(line_set(g[1])))] + g[2:] for g in got] != want:
        problems.append(f"relist: exit {r.returncode}, rows {got}, want {want}\n{r.stdout}")
    if relist("--check").returncode:
        problems.append("relist --check: fails after relist rewrote the list")
    moved_src = ("// one\n// two\n" + FILES["cov.js"]).split("\n")
    n = min(line_set(got[0][1]))
    moved_src[n - 1] += " // edited"
    (rl / "cov.js").write_text("\n".join(moved_src))
    r = relist()
    if r.returncode != 1 or "CHANGED" not in r.stdout:
        problems.append(f"relist after a listed line was edited: exit {r.returncode}\n{r.stdout}")
    r = subprocess.run(["node", relist_js, "--rehash", f"cov.js:{got[0][1]}", str(lst)], capture_output=True, text=True)
    if r.stdout.split("\t")[:3] != ["cov.js", got[0][1], code_of(moved_src, line_set(got[0][1]))]:
        problems.append(f"relist --rehash: {r.stdout!r}")

    # Two rows of the same code (`return;` twice, as source.js has): lines
    # added between them move only the second, which must not land on the
    # first's lines (it once did, and the second's lines went unlisted).
    dup = rl / "dup.js"
    body = ["function f(a) {", "\tif (a)", "\t\treturn;", "\tg();", "\tif (a)", "\t\treturn;", "\th();", "}"]
    dup.write_text("\n".join(body) + "\n")
    h = code_of(body, {3})
    lst.write_text("# self-test\n" + f"dup.js\t3\t{h}\tfirst\n" + f"dup.js\t6\t{h}\tsecond\n")
    dup.write_text("\n".join(body[:4] + ["\tk();"] * 6 + body[4:]) + "\n")
    r = relist()
    got = [(g[0], g[1], g[3]) for g in rows_of(lst.read_text())]
    if r.returncode or got != [("dup.js", "3", "first"), ("dup.js", "12", "second")]:
        problems.append(f"relist with two rows of the same code: exit {r.returncode}, rows {got}, want lines 3 and 12\n{r.stdout}")
    return problems


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

        def late(self, sid, info, waiting, parent=None):
            if info.get("type") == "service_worker":
                time.sleep(1.0)
            return attached(self, sid, info, waiting, parent)
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
                # A page opened while held: what it runs before release() is
                # not recorded, what it runs after is, with its blocks.
                jscov.hold()
                try:
                    held = ctx.new_page()
                    held.goto(base + "/held.html")
                    wait_for(lambda: held.evaluate("window.before === 1"), 10)
                finally:
                    jscov.release()
                if held.evaluate("heldAfter(true)") != "after":
                    problems.append("fixture: heldAfter did not run")
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
            # held.js: heldAfter's taken return covered, its other return not.
            src = FILES["held.js"].split("\n")
            unc = set()
            for a, _, b in (x.partition("-") for x in s["files"].get("held.js", {}).get("uncoveredLines", "").split(",") if x):
                unc.update(range(int(a), int(b or a) + 1))
            taken, not_taken = src.index('\t\treturn "after";') + 1, src.index('\treturn "no";') + 1
            if taken in unc or not_taken not in unc:
                problems.append(f"held.js: uncovered lines {sorted(unc)}, want {not_taken} among them and not {taken} (block detail after release)")
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
            problems += check_list(tmp, fix, raw, s)
            print(json.dumps({n: {m: s["files"][n][m] for m in ("lines", "functions", "branches")} for n in s["files"]}, indent=1))
    if problems:
        print("FAIL\n  " + "\n  ".join(problems), file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
