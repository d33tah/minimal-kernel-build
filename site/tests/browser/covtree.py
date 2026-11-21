"""Browser test suite covtree (test_browser.py --suites)."""

import gzip
import json
import pathlib
import re
import time

from tests.browser.common import cov_open_all, cov_rows, kernel_entries, no_hscroll, report, show_tab, SITE, wait_for


def covtree_case(browser, base, shots):
    """The Source tab after a counted minified boot: init/main.c has lines
    that ran, die() (never called in a good boot) has none, the root and every
    directory are the sums of their files, the tree's numbers agree with
    files.json and with the Source panel's whole-file view, a function opens
    the Page tab, the lcov export has the same totals; screenshots light/dark
    at 1280 and 390 px with no horizontal scroll."""
    problems, r = [], {}
    entry = kernel_entries()["minified-linux"]
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
        problems.append("the counted boot did not end")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 30)
    r["coverage"] = page.get_attribute("#cov_tree", "data-coverage")
    cov_open_all(page)
    rows = cov_rows(page)
    files = {x["path"]: x for x in rows if x["kind"] == "file"}
    root = next((x for x in rows if x["kind"] == "root"), None)
    r["root"] = root
    r["rows"] = len(rows)
    want_lines = sum(f[1] for f in F["files"])
    want_ins = sum(f[3] for f in F["funcs"] if f[1] >= 0)
    if not root or root["lines"] != want_lines or root["ins"] != want_ins or len(files) != len(F["files"]):
        problems.append(f"root {root} != files.json: {want_lines} lines, {want_ins} instructions, {len(F['files'])} files ({len(files)} rows)")
    for key in ("lines", "linesRun", "ins", "insRun"):
        total = sum(x[key] or 0 for x in files.values())
        if root and root[key] != total:
            problems.append(f"root {key} {root[key]} != sum of the files {total}")
    # Every directory is the sum of the files below it.
    for d in (x for x in rows if x["kind"] == "dir"):
        below = [f for p, f in files.items() if p.startswith(d["path"] + "/")]
        for key in ("lines", "linesRun"):
            if d[key] != sum(f[key] or 0 for f in below):
                problems.append(f"directory {d['path']}: {key} {d[key]} != sum of its files")
                break
    main = files.get("init/main.c")
    r["init/main.c"] = main
    if not main or not main["linesRun"] or not main["insRun"] or main["linesRun"] >= main["lines"]:
        problems.append(f"init/main.c: {main}")
    die = next((x for x in rows if x["kind"] == "func" and x["path"].endswith(":die")), None)
    r["die"] = die
    if not die or die["linesRun"] != 0 or die["insRun"] != 0 or not die["lines"] or not die["ins"]:
        problems.append(f"die() should be in the tree with nothing run: {die}")
    sk = next((x for x in rows if x["kind"] == "func" and x["path"].endswith(":start_kernel")), None)
    r["start_kernel"] = sk
    if not sk or not sk["insRun"] or sk["path"].split(":")[0] != "init/main.c":
        problems.append(f"start_kernel: {sk}")

    # Sorting by lines not run: the top directories, most first.
    page.select_option("#cov_sort", "notrun")
    top = page.eval_on_selector_all('#cov_tree .covrow[data-kind="dir"][data-depth="1"]',
                                    "es => es.map(e => [e.dataset.path, e.dataset.lines - e.dataset.linesRun])")
    r["most_not_run"] = top[:3]
    if len(top) < 2 or any(a[1] < b[1] for a, b in zip(top, top[1:])):
        problems.append(f"sort by lines not run: top directories {top}")
    # The other orders: lines run (share), lines with code, instructions run (share), most first.
    share = lambda run, total: -1 if run in (None, "") or not int(total) else int(run) / int(total)
    for sort, key in (("run", lambda d: share(d["linesRun"], d["lines"])), ("lines", lambda d: int(d["lines"])),
                      ("ins", lambda d: share(d["insRun"], d["ins"]))):
        page.select_option("#cov_sort", sort)
        top = page.eval_on_selector_all('#cov_tree .covrow[data-kind="dir"][data-depth="1"]', "es => es.map(e => ({...e.dataset}))")
        r["sort_" + sort] = [x["path"] for x in top[:3]]
        if len(top) < 2 or any(key(a) < key(b) for a, b in zip(top, top[1:])):
            problems.append(f"sort by {sort}: top directories {[(x['path'], key(x)) for x in top]}")
    page.select_option("#cov_sort", "path")
    # The filter keeps matching functions and their file.
    page.fill("#cov_filter", "start_kernel")
    fr = cov_rows(page)
    r["filter_rows"] = len(fr)
    if not any(x["path"].endswith(":start_kernel") for x in fr) or any(x["kind"] == "func" and "start_kernel" not in x["path"] for x in fr):
        problems.append(f"filter start_kernel: {[x['path'] for x in fr][:8]}")
    page.fill("#cov_filter", "")

    # The whole file in the Source panel: same lines with code and run.
    page.locator('#cov_tree .covrow[data-path="init/main.c"] .covname').click()
    wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 20)
    r["panel"] = [page.inner_text("#src_title"), page.get_attribute("#src_body", "data-code"), page.get_attribute("#src_body", "data-executed")]
    if main and (r["panel"][0] != "init/main.c (whole file)" or int(r["panel"][1] or -1) != main["lines"] or int(r["panel"][2] or -1) != main["linesRun"]):
        problems.append(f"Source panel of init/main.c {r['panel']} != tree {main}")
    page.screenshot(path=str(shots / "covtree-panel-1280-light.png"))
    page.click("#src_close")

    with page.expect_download() as dl:
        page.click("#cov_export")
    lcov = pathlib.Path(dl.value.path()).read_text()
    lf = sum(int(l[3:]) for l in lcov.splitlines() if l.startswith("LF:"))
    lh = sum(int(l[3:]) for l in lcov.splitlines() if l.startswith("LH:"))
    r["lcov"] = [lf, lh, lcov.count("SF:")]
    if root and (lf != root["lines"] or lh != root["linesRun"] or lcov.count("SF:") != len(F["files"])):
        problems.append(f"lcov LF/LH/files {r['lcov']} != tree {root}")

    for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": width, "height": 1000})
        page.fill("#cov_filter", "init")
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1") or not no_hscroll(page, "#cov_tree"):
            problems.append(f"{scheme}/{width}: horizontal scroll")
        page.evaluate("document.getElementById('panel_source').scrollIntoView()")
        page.screenshot(path=str(shots / f"covtree-{scheme}-{width}.png"))
    page.fill("#cov_filter", "")
    page.set_viewport_size({"width": 1280, "height": 1000})
    # The root's arrow closes the tree to its one row and opens it again as it was.
    n_open = len(cov_rows(page))
    page.click('#cov_tree .covrow[data-kind="root"] .ttog')
    closed = len(cov_rows(page))
    page.click('#cov_tree .covrow[data-kind="root"] .ttog')
    r["root arrow"] = [n_open, closed, len(cov_rows(page))]
    if closed != 1 or r["root arrow"][2] != n_open:
        problems.append(f"the root's arrow: rows open, closed, open again {r['root arrow']}")
    page.locator('#cov_tree .covrow[data-kind="func"][data-path$=":start_kernel"] .covname').click()
    syms = {x["name"]: x for x in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    if not wait_for(lambda: page.get_attribute("#listing", "data-page") == str(syms["start_kernel"]["paddr"] // 4096), 5):
        problems.append("tapping start_kernel in the tree did not open its page")
    # Its instructions run = the listing's instructions in it that are not tinted.
    fn = next(f for f in F["funcs"] if f[0] == "start_kernel")
    lo, hi = fn[4], fn[4] + fn[5]
    marks = page.eval_on_selector_all("#listing .ins[data-addr]", "es => es.map(e => [Number(e.dataset.addr), e.classList.contains('nx')])")
    inside = [nx for a, nx in marks if lo <= a < hi]
    r["start_kernel_listing"] = [len(inside), inside.count(False)]
    if sk and (len(inside) != sk["ins"] or inside.count(False) != sk["insRun"]):
        problems.append(f"start_kernel: listing {len(inside)} instructions, {inside.count(False)} not tinted; tree {sk}")
    page.close()
    covtree_edges(browser, base, shots, entry, F, lf, problems, r, errors)
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def covtree_edges(browser, base, shots, entry, F, lf_full, problems, r, errors):
    """The Source tab paused at the reset vector: no execution coverage,
    totals only. After a counted minified boot with the line shards held
    back and the one with start_kernel answering HTTP 404: while they are
    read the tree says so and the lcov export waits ("reading N of M
    shards"); once they are in, the export downloads, without the lines
    of the failed shard (LF below the full export's, every file still
    listed), and the tree says the lines run are unknown and why."""
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 30)
    r["paused_status"] = page.inner_text("#cov_status")
    root = next((x for x in cov_rows(page) if x["kind"] == "root"), None)
    if r["paused_status"] != "No execution coverage: nothing has been stepped yet. Totals only." or \
            page.get_attribute("#cov_tree", "data-coverage") != "none" or not root or root["linesRun"] is not None:
        problems.append(f"paused at the reset vector: {r['paused_status']!r}, coverage {page.get_attribute('#cov_tree', 'data-coverage')}, root {root}")
    page.close()

    syms = {x["name"]: x for x in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    bits = json.loads((SITE / entry["dwarf"] / "index.json").read_text())["shardBits"]
    bad = f"{syms['start_kernel']['paddr'] >> bits:x}.bin"
    held = []
    def shard(route):
        if route.request.url.split("?")[0].endswith("/dwarf/" + bad):
            route.fulfill(status=404, body="")
        else:
            held.append(route)
    # A browser without service workers (their fetches would not pass the page's routes).
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.add_init_script("delete Navigator.prototype.serviceWorker")
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.route(re.compile(r"/dwarf/[0-9a-f]+\.bin(\?|$)"), shard)
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
        problems.append("held shards: the counted boot did not end")
    show_tab(page, "source")
    wait_for(lambda: page.inner_text("#cov_status").startswith("Reading the line tables"), 30)
    r["held_status"] = page.inner_text("#cov_status")
    page.click("#cov_export")
    r["held_export"] = page.inner_text("#cov_export_status")
    m = re.fullmatch(r"lcov: reading (\d+) of (\d+) shards…", r["held_export"])
    if not m or int(m.group(2)) != len(json.loads((SITE / entry["dwarf"] / "index.json").read_text())["shards"]) or \
            int(m.group(1)) != len(held):
        problems.append(f"lcov export with {len(held)} shards held back: {r['held_export']!r}")
    with page.expect_download(timeout=60000) as dl:
        while held:
            held.pop().continue_()
    lcov = pathlib.Path(dl.value.path()).read_text()
    got = [sum(int(l[3:]) for l in lcov.splitlines() if l.startswith("LF:")), lcov.count("SF:")]
    wait_for(lambda: page.inner_text("#cov_status").startswith("Lines run unknown"), 10)
    r["failed_status"] = page.inner_text("#cov_status")
    r["failed_lcov"] = [got, page.inner_text("#cov_export_status")]
    if not 0 < got[0] < lf_full or got[1] != len(F["files"]) or page.inner_text("#cov_export_status") != f"lcov: {len(F['files'])} files":
        problems.append(f"lcov export without shard {bad}: LF {got[0]} (full {lf_full}), {got[1]} files, {page.inner_text('#cov_export_status')!r}")
    root = next((x for x in cov_rows(page) if x["kind"] == "root"), None)
    if r["failed_status"] != "Lines run unknown: debug info did not load: HTTP 404" or not root or root["linesRun"] is not None:
        problems.append(f"shard {bad} 404: status {r['failed_status']!r}, root {root}")
    page.evaluate("document.getElementById('panel_source').scrollIntoView()")
    page.screenshot(path=str(shots / "covtree-shard-404-1280.png"))
    page.close()


# Multi-line C statements: the line rows name only the first line of
# `asm volatile("..."\n "..." : ...);`, the rest of it must show as part of
# the statement (V3).
MULTILINE_POINTS = (
    # (file, function whose statement it is, or None: inlined everywhere)
    ("arch/x86/lib/string_32.c", "strlen"),
    ("arch/x86/include/asm/irqflags.h", "native_save_fl"),
)


def multiline_statement(text, func):
    """1-based (first, last) line of the first asm statement in func's body."""
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if re.search(r"\b" + func + r"\(void\)|\b" + func + r"\(const", l))
    first = next(i for i in range(start, len(lines)) if "asm volatile(" in lines[i])
    last = next(i for i in range(first, len(lines)) if lines[i].rstrip().endswith(");"))
    return first + 1, last + 1


def panel_line_marks(page):
    """{line: (classes, mark, title)} of the Source panel's lines."""
    title = page.inner_text("#src_title")
    m = re.search(r":(\d+)-\d+$", title)
    first = int(m.group(1)) if m else 1
    spans = page.eval_on_selector_all("#src_body > span", "es => es.map(e => [e.className, e.textContent, e.title])")
    return {first + i: (c.split(), t[6:7], ti) for i, (c, t, ti) in enumerate(spans)}


def multiline_case(browser, base, shots):
    """After a counted minified boot, every line of a multi-line asm statement
    that ran shows as run in the Source panel (whole file and the function
    view), the lines after the first marked as its continuation; the panel's
    counts (lines with their own code only) equal the Coverage tree's and
    the lcov export's for that file."""
    problems, r = [], {}
    entry = kernel_entries()["minified-linux"]
    src = SITE / entry["src"]
    syms = {x["name"]: x for x in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
        problems.append("the counted boot did not end")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 30)
    cov_open_all(page)
    files = {x["path"]: x for x in cov_rows(page, "file")}
    with page.expect_download() as dl:
        page.click("#cov_export")
    lcov, cur = {}, None
    for l in pathlib.Path(dl.value.path()).read_text().splitlines():
        if l.startswith("SF:"):
            cur = lcov.setdefault(l[3:], {})
        elif l.startswith(("LF:", "LH:")):
            cur[l[:2]] = int(l[3:])

    def check(label, path, lo, hi, ran=True):
        """Lines lo..hi of the panel: the first ran (or has code that did not
        run), every other one shows the same as its continuation, except a
        line with line rows of its own (-O0: line 43 of strlen's asm, the
        "1"(s) input, loads s into EDI), which shows as code of its own."""
        marks = panel_line_marks(page)
        got = {l: marks.get(l, ([], "", ""))[:2] for l in range(lo, hi + 1)}
        r[label] = {l: " ".join(c) + " " + m for l, (c, m) in got.items()}
        want = "exec" if ran else "code"
        own_rows = set(page.evaluate("([p, lo, hi]) => { const c = MKB.fileLines(p).code; const o = [];"
                                     " for (let l = lo; l <= hi; l++) if (c.has(l)) o.push(l); return o; }", [path, lo + 1, hi]))
        if own_rows:
            r[label + " lines with rows of their own"] = sorted(own_rows)
        if want not in got[lo][0]:
            problems.append(f"{label}: line {lo}, the statement's first line, is not {want}: {got[lo]}")
        for l in range(lo + 1, hi + 1):
            c, mark = got[l]
            if l in own_rows:
                if "cont" in c or not ("exec" in c or "code" in c):
                    problems.append(f"{label}: line {l} has code of its own but shows {c} {mark!r}")
                continue
            if want not in c or "cont" not in c:
                problems.append(f"{label}: line {l} of the statement {lo}-{hi} is not shown as its continuation ({want}): {c} {mark!r}")
            elif "statement" not in marks[l][2] or "not counted" not in marks[l][2].lower():
                problems.append(f"{label}: line {l}: tooltip {marks[l][2]!r}")
        own = [c for c, _, _ in marks.values() if ("exec" in c or "code" in c) and "cont" not in c]
        code, ran = int(page.get_attribute("#src_body", "data-code")), int(page.get_attribute("#src_body", "data-executed"))
        if code != len(own) or ran != sum("exec" in c for c in own):
            problems.append(f"{label}: panel counts {code}/{ran} != its lines with their own code {len(own)}/{sum('exec' in c for c in own)}")
        return code, ran

    # The statement finder on C that trips naive scanners: `;` and parens
    # in strings, characters and comments, a #define with continuations
    # inside a call, if headers, a label, else, an initializer, a statement
    # expression, macro calls without `;` outside functions. Expected: {first line: last line} of each multi-line one.
    snippet = [
        'int f(int a,',                        # 1  header, ends at the { of 2
        '      int b)',                         # 2
        '{',                                    # 3
        '\tstruct s x = {',                    # 4  initializer 4-7
        '\t\t.a = ";(",',                     # 5
        '\t\t.b = \')\',',                  # 6
        '\t};',                                # 7
        '\tif (a &&',                          # 8  header 8-9
        '\t    b)',                            # 9
        '\t\tfoo(a, /* ); */',                # 10 call 10-14, a directive inside
        '#define X(y) \\',                    # 11
        '\t(y);',                              # 12
        '\t\t    b,',                         # 13
        '\t\t    c);',                        # 14
        'out:',                                 # 15 label alone
        '\telse',                              # 16 else alone
        '\t\tx = ({ int t = a;',              # 17 statement expression 17-18
        '\t\t\tt; });',                      # 18
        '\treturn a; // ;(',                   # 19 one line
        '}',                                    # 20
        'DEFINE_A(x)',                          # 21 file scope macro calls
        '\tDEFINE_B(y)',                       # 22 without `;`: one line each
        'static int g(void)',                   # 23 a definition after them
        '{ return 0; }',                        # 24
    ]
    got = page.evaluate("""lines => { const s = MKB.srcStatements({ lines });
        return [Object.fromEntries(s.end), Array.from(s.of).slice(1, lines.length + 1)]; }""", snippet)
    r["statements"] = got[0]
    want = {"1": 3, "4": 7, "8": 9, "10": 14, "17": 18, "23": 24}
    if got[0] != want or got[1][10] or got[1][11]:
        problems.append(f"srcStatements: {got} != {want}, lines 11-12 (a #define) in no statement")

    def panel_open(js, arg):
        page.evaluate(js, arg)
        wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 20)

    for path, func in MULTILINE_POINTS:
        text = gzip.decompress((src / (path + ".gz")).read_bytes()).decode()
        lo, hi = multiline_statement(text, func)
        r[path + " statement"] = [lo, hi]
        if hi <= lo:
            problems.append(f"{path}: {func}'s asm statement {lo}-{hi} is one line")
            continue
        # The whole file (the Source tab's view).
        panel_open("p => MKB.openSourceFile(p)", path)
        code, ran = check(path + " whole file", path, lo, hi)
        tree, lc = files.get(path), lcov.get(path, {})
        r[path + " counts"] = {"panel": [code, ran], "tree": tree and [tree["lines"], tree["linesRun"]], "lcov": [lc.get("LF"), lc.get("LH")]}
        if not tree or [code, ran] != [tree["lines"], tree["linesRun"]] or [code, ran] != [lc.get("LF"), lc.get("LH")]:
            problems.append(f"{path}: panel, tree and lcov disagree: {r[path + ' counts']}")
        if path.endswith("string_32.c"):
            page.screenshot(path=str(shots / "multiline-whole-1280-light.png"))
        page.click("#src_close")
        # The function view at a copy of the statement that ran (inlined
        # into a caller: the view must reach the statement's last line).
        at = page.evaluate("""([p, l]) => { const s = MKB.fileLines(p).code.get(l) || [];
            const x = s.find(([a, b]) => MKB.stepper.countIn(a, b) > 0); return x ? x[0] : -1; }""", [path, lo])
        r[path + " ran at"] = hex(at) if at >= 0 else at
        if at < 0:
            problems.append(f"{path}:{lo} has no address that ran")
        else:
            panel_open("pa => MKB.openSource(pa)", at)
            check(path + " at " + hex(at), path, lo, hi)
            for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
                page.emulate_media(color_scheme=scheme)
                page.set_viewport_size({"width": width, "height": 1000})
                time.sleep(0.3)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{path} {scheme}/{width}: horizontal scroll")
                page.screenshot(path=str(shots / f"multiline-{path.split('/')[-1]}-{scheme}-{width}.png"))
            page.emulate_media(color_scheme="light")
            page.set_viewport_size({"width": 1280, "height": 1000})
            page.click("#src_close")
        # The out-of-line function: at -Os its own copy does not run in this
        # boot (strlen is inlined where it is called), at -O0 it does.
        if func in syms and path.endswith("string_32.c"):
            f = syms[func]
            own_ran = page.evaluate("([a, n]) => MKB.stepper.countIn(a, a + n) > 0", [f["paddr"], f["size"]])
            r[func + "() ran"] = own_ran
            panel_open("pa => MKB.openSource(pa)", f["paddr"])
            check(path + " " + func + "()", path, lo, hi, ran=own_ran)
            page.click("#src_close")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_covtree(ctx):
    failed = report("covtree", *covtree_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("multiline", *multiline_case(ctx.browser, ctx.base, ctx.shots))
