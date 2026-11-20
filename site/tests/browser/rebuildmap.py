"""Browser test suite rebuildmap (test_browser.py --suites)."""

import gzip
import time
import urllib.request

from tests.browser.common import (
    ALIVE, BM_STATE_JS, bm_close_menu, bm_dialog_save, bm_open_list, bm_state, cov_open_all, cov_rows, kernel_entries, report,
    run_to_wait, show_tab, SITE, snap_click, snap_rows, snap_save, srcedit_ready, srcedit_state, wait_for,
)
from tests.browser.rebuild import rb_press, rb_shots, rb_wait
from tests.browser.rebuild_linux import rbl_open
from tests.browser.hotpatch import FRAMES_JS, HP_PA, hp_apply, hp_continue, hp_edit, SYM_JS


# ---- Places across builds (plan E8, buildmap.js) ----------------------------
# What a user does: bookmarks a line, a function and an instruction of
# hello, sets a Run to target, saves a snapshot, then edits boot/hello.c and
# rebuilds it in the page, twice, and goes back to the first build's text.
#   build 1: a function inserted above put_line (7 lines): every line below
#     moves by 7, put_line and hello_main move in memory. The line bookmark
#     (hello.c:32) is placed at line 39, its ★ in the Source panel there;
#     the function bookmark at put_line's new address; the instruction
#     bookmark (hello_main + its offset) at the new hello_main + the same
#     offset; the Run to field says boot/hello.c:39. The coverage tree
#     compares with the shipped build: every line that ran there ran here
#     (+0 -0 on hello_main). The shipped snapshot is the shipped build's.
#   build 2: a condition put before the "Still alive" call (one line more,
#     hello_main changes size): the instruction bookmark is placed by its C
#     line (moved to 40) inside the new hello_main; compared with build 1
#     the call no longer runs: ◌ on line 40 in the Source panel, - on the
#     file's row; build 1's snapshot says it is build 1's, offers no
#     Restore, and its Replay is refused with the reason.
#   build 3: build 1's text again: the same bzImage, so build 1's snapshot
#     is restorable again; a Run to file:line of a file no build edits stays.
#   build 4: no edit (CI's bytes): a Run to address in e8_extra has no place
#     in it (the status says so); build 5: build 1's text again, a Run to
#     address in put_line moves with put_line.
# Lines through a diff without the old text (found by their own text, or
# kept), past the old text, and changed at the end of a file.
# Hot patch (shipped hello, paused at hello_main): put_line gets a comment
# line and the case swap; after the patch the Source panel shows the
# patched text (its note says so), every frame of the copy and of
# hello_main names a line whose text in the panel is the line the code is
# of, the line bookmark and the Run to target moved by one, and Run to the
# store line stops in the copy.
RBM_FILE = "boot/hello.c"
RBM_AFTER = "void hello_main(void);\n"
RBM_INSERT = "int e8_extra(void);\n\nint e8_extra(void)\n{\n\treturn 8;\n}\n\n"
RBM_ALIVE = '\tput_line(row + 1, "Still alive");\n'
RBM_COND = "\tif (e8_extra() == 9)\n"
RBM_LINE = 32       # put_line(row + 1, "Still alive"); in the shipped text

BM_ROWS_JS = """() => [...document.querySelectorAll('#bm_list .bmrow')].map((e) => ({ id: e.dataset.id, group: e.dataset.group,
  placed: e.dataset.placed || '', what: (e.querySelector('.bmwhat') || {}).textContent || '' }))"""

LINE_CODE_JS = """async ([path, line]) => {
  let r = MKB.dbgLineCode(path, line);
  for (let i = 0; i < 600 && r.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); r = MKB.dbgLineCode(path, line); }
  return r.state === 'ok' ? r.bps : [r.state, r.why]; }"""

SRC_ROW_JS = """(l) => { const e = document.querySelector('#src_body [data-line="' + l + '"]');
  return e ? { cls: e.className, text: (e.querySelector('.srctext') || {}).textContent || '', star: !!e.querySelector('.bmstar'),
    mark: e.textContent.slice(6, 7) } : null; }"""


def shipped_text(base):
    entry = kernel_entries()["hello"]
    url = f"{base}/{entry['src']}{RBM_FILE}.gz"
    raw = urllib.request.urlopen(url, timeout=60).read() if base.startswith("http") else (SITE / entry["src"] / (RBM_FILE + ".gz")).read_bytes()
    return gzip.decompress(raw).decode()


def sym(page, name):
    return page.evaluate(SYM_JS, name)


def wait_end(page, timeout=120):
    """The boot ran to hello's end (stepped from the entry: coverage recorded)."""
    return wait_for(lambda: page.get_attribute("#end_banner", "data-kind") and ALIVE in page.inner_text("#screen_container"), timeout)


def cov_known(page):
    """The Coverage tree with what ran (the line tables read)."""
    show_tab(page, "source")

    def known():
        page.evaluate("MKB.covRender()")
        rows = cov_rows(page, "root")
        return rows and rows[0]["linesRun"] is not None and rows[0]["linesRun"] > 0
    return wait_for(known, 60, 0.3)


def open_file(page, line=0):
    page.evaluate("([p, l]) => MKB.openSourceLine ? (l ? MKB.openSourceLine(p, l) : MKB.openSourceFile(p)) : null", [RBM_FILE, line])
    return wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == RBM_FILE, 60)


def src_row(page, line):
    return page.evaluate(SRC_ROW_JS, line)


def bm_rows(page):
    bm_open_list(page, False)
    rows = page.evaluate(BM_ROWS_JS)
    bm_close_menu(page)
    return rows


def edit_text(page, text, problems, tag):
    """The editor (Edit on boot/hello.c) holds text, saved."""
    if not rbl_open(page, RBM_FILE, False):
        problems.append(f"{tag}: Edit did not open {RBM_FILE}: {srcedit_state(page)['state']!r}")
        return False
    page.locator("#ed_text").fill(text)
    # The shipped text again is no edit any more.
    if not wait_for(lambda: any(w in srcedit_state(page)["state"] for w in ("saved", "the text the kernel was built from")), 10):
        problems.append(f"{tag}: the edit was not saved: {srcedit_state(page)['state']!r}")
        return False
    return True


def rebuild(page, n, problems, tag, r, before=None):
    """Rebuild and boot, the n-th build of hello: booted, its data files made
    (before: done right before the press)."""
    t0 = time.monotonic()
    if before:
        before()
    rb_press(page, "#rb_go_panel", False)
    if not wait_for(lambda: page.evaluate("(n) => !!(MKB.image && MKB.image.build && MKB.image.build.n === n)", n), 300, 0.1):
        st = rb_wait(page, ("done", "failed"), 5)
        problems.append(f"{tag}: the rebuild: {st['state']} {st['status'][:300]!r} {st['log'][:300]!r}")
        return False
    st = rb_wait(page, ("done", "failed"), 300)
    r[tag + " rebuild s"] = round(time.monotonic() - t0, 1)
    if st["state"] != "done":
        problems.append(f"{tag}: the rebuild: {st['state']} {st['status'][:300]!r} {st['log'][:300]!r}")
        return False
    wait_for(lambda: page.evaluate("MKB.GEN.state === 'done' && MKB.GEN.image === MKB.image && MKB.DBG.mode === 'shards'"), 120)
    # Leave the editor: the panel shows the build's text.
    if srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", False)
    return True


def check_bookmarks(page, ids, want, problems, tag, r):
    """Each bookmark (id -> (group, placed text)) is listed so and placed there."""
    rows = {x["id"]: x for x in bm_rows(page)}
    r[tag + " bookmarks"] = {k: [rows.get(i, {}).get("group"), rows.get(i, {}).get("placed"), rows.get(i, {}).get("what", "")[:160]] for k, i in ids.items()}
    for k, (group, placed) in want.items():
        got = rows.get(ids[k])
        if not got or got["group"] != group or (placed is not None and got["placed"] != placed):
            problems.append(f"{tag}: the {k} bookmark is listed as {got and (got['group'], got['placed'], got['what'])}, want {(group, placed)}")


def cmp_with(page, label_part, problems, tag):
    """The coverage tree (Source tab) compares with the earlier build whose label has label_part."""
    show_tab(page, "source")
    if not wait_for(lambda: page.is_visible("#cov_cmp"), 30):
        problems.append(f"{tag}: no compare picker in the coverage tree")
        return False
    opts = page.eval_on_selector_all("#cov_cmp option", "os => os.map((o) => [o.value, o.textContent])")
    pick = next((v for v, t in opts if label_part in t), None)
    if pick is None:
        problems.append(f"{tag}: the compare picker has no {label_part!r}: {opts}")
        return False
    page.select_option("#cov_cmp", pick)
    cov_open_all(page)
    return wait_for(lambda: page.eval_on_selector_all("#cov_tree .covrow[data-was]", "es => es.length") > 0, 60, 0.3)


def cmp_row(page, kind, path):
    return page.evaluate("""([k, p]) => { const e = document.querySelector('#cov_tree .covrow[data-kind="' + k + '"][data-path="' + p + '"]');
      return e ? { was: e.dataset.was, plus: e.dataset.plus, minus: e.dataset.minus, lines: e.dataset.linesRun,
        text: (e.querySelector('.covwas') || {}).textContent || '' } : null; }""", [kind, path])


def rebuildmap_run(browser, base, shots):
    problems, r, errors = [], {}, []
    orig = shipped_text(base)
    lines0 = orig.split("\n")
    if lines0[RBM_LINE - 1] != RBM_ALIVE.rstrip("\n"):
        return [f"hello.c:{RBM_LINE} is {lines0[RBM_LINE - 1]!r}, the test wants {RBM_ALIVE!r}"], r
    t1 = orig.replace(RBM_AFTER, RBM_AFTER + RBM_INSERT)
    t2 = t1.replace(RBM_ALIVE, RBM_COND + RBM_ALIVE)
    line1 = RBM_LINE + RBM_INSERT.count("\n")
    line2 = line1 + 1
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme="light")
    try:
        page.goto(f"{base}/index.html?kernel=hello&speed=full#source")
        srcedit_ready(page, "hello")
        if not wait_end(page) or not cov_known(page):
            return problems + [f"the shipped hello did not run to its end with coverage: {page.inner_text('#cov_status')!r}"], r
        # No earlier build yet: no compare picker.
        if page.is_visible("#cov_cmp"):
            problems.append("the coverage tree shows a compare picker before any build was made")
        hm0, pl0 = sym(page, "hello_main"), sym(page, "put_line")
        bps = page.evaluate(LINE_CODE_JS, [RBM_FILE, RBM_LINE])
        if not bps or not isinstance(bps[0], int):
            return problems + [f"no code of {RBM_FILE}:{RBM_LINE}: {bps}"], r
        code_pa = bps[0]
        r["shipped"] = {"hello_main": hm0, "put_line": pl0, "line code": bps}

        # Bookmarks as a user makes them: a right click on the line, on
        # put_line's row of the Coverage tree, on the instruction in the Page tab.
        ids = {}
        open_file(page, RBM_LINE)
        page.click(f'#src_body [data-line="{RBM_LINE}"]', button="right")
        b = bm_dialog_save(page, problems, "shipped", "line", name="e8 line")
        ids["line"] = b and b["id"]
        cov_open_all(page)
        page.click(f'#cov_tree .covrow[data-kind="func"][data-path="{RBM_FILE}:put_line"] .covname', button="right")
        b = bm_dialog_save(page, problems, "shipped", "fn", name="e8 fn")
        ids["fn"] = b and b["id"]
        page.click(f'#cov_tree .covrow[data-kind="file"][data-path="{RBM_FILE}"] .covname', button="right")
        b = bm_dialog_save(page, problems, "shipped", "file", name="e8 file")
        ids["file"] = b and b["id"]
        show_tab(page, "page")
        page.evaluate("(p) => MKB.selectPage(Math.floor(p / 4096))", code_pa)
        wait_for(lambda: page.locator(f'#listing .ins[data-addr="{code_pa}"]').count() > 0, 30)
        page.click(f'#listing .ins[data-addr="{code_pa}"]', button="right")
        b = bm_dialog_save(page, problems, "shipped", "code", name="e8 code")
        ids["code"] = b and b["id"]
        if not all(ids.values()):
            return problems + [f"bookmarks not made: {ids}"], r
        made = {x["id"]: x for x in bm_state(page)["list"]}
        r["anchors"] = {k: {x: made[i]["t"].get(x) for x in ("fn", "off", "fh", "txt", "src")} for k, i in ids.items()}
        if made[ids["code"]]["t"].get("fn") != "hello_main" or made[ids["line"]]["t"].get("txt") != RBM_ALIVE.rstrip("\n"):
            problems.append(f"the bookmarks keep no function or line text to place them by: {r['anchors']}")
        snap_save(page, 1, False)
        show_tab(page, "debugger")
        page.fill("#dbg_addr", f"{RBM_FILE}:{RBM_LINE}")

        # Build 1: lines below the insert move by 7, the functions move.
        if not edit_text(page, t1, problems, "build 1") or not rebuild(page, 1, problems, "build 1", r):
            return problems, r
        if not wait_end(page):
            problems.append(f"build 1 did not run to its end: {page.inner_text('#screen_container')[-200:]!r}")
        hm1, pl1 = sym(page, "hello_main"), sym(page, "put_line")
        r["build 1"] = {"hello_main": hm1, "put_line": pl1}
        if not hm1 or hm1[1] == hm0[1] or hm1[2] != hm0[2]:
            problems.append(f"build 1: hello_main {hm1} should move and keep its size {hm0}")
        # The Run to target (the Debugger's breakpoint) moved with the edit.
        r["run to after build 1"] = [page.input_value("#dbg_addr"), page.inner_text("#rb_status")[-200:]]
        if page.input_value("#dbg_addr") != f"{RBM_FILE}:{line1}" or f"is {RBM_FILE}:{line1} now: the edit moved the line" not in page.inner_text("#rb_status"):
            problems.append(f"build 1: the Run to field {r['run to after build 1']}, want {RBM_FILE}:{line1} and why")
        tg = page.evaluate("async (t) => { const w = await MKB.api.step.runToTargets(t); return w.targets || w.why; }", f"{RBM_FILE}:{line1}")
        want_bps = page.evaluate(LINE_CODE_JS, [RBM_FILE, line1])
        if tg != want_bps or not all(hm1[1] <= a < hm1[1] + hm1[2] for a in tg):
            problems.append(f"build 1: Run to {RBM_FILE}:{line1} targets {tg}, want the call's code in hello_main {want_bps}")
        off = code_pa - hm0[1]
        check_bookmarks(page, ids, {"line": ("build", f"{RBM_FILE}:{line1}"), "fn": ("build", f"put_line() at {hex(pl1[1])}"),
                                    "code": ("build", None), "file": ("build", f"{RBM_FILE} (whole file)")}, problems, "build 1", r)
        # The marks: the line's ★ at its new line, the instruction's at hello_main + off.
        open_file(page, line1)
        wait_for(lambda: (src_row(page, line1) or {}).get("star"), 15)
        rows = {line: src_row(page, line) for line in (RBM_LINE, line1)}
        r["build 1 stars"] = rows
        if not rows[line1] or not rows[line1]["star"] or rows[line1]["text"] != RBM_ALIVE.rstrip("\n") or (rows[RBM_LINE] or {}).get("star"):
            problems.append(f"build 1: the line bookmark's ★ is not at {line1} only: {rows}")
        marks = page.evaluate("() => [...MKB.bmMarks().code.keys()]")
        if hm1[1] + off not in marks:
            problems.append(f"build 1: no code mark at hello_main+{off:#x} = {hm1[1] + off:#x}: {[hex(x) for x in marks]}")
        # Going to them: the Source panel at line 39, the Page tab at the new address.
        bm_open_list(page, False)
        page.click(f'#bm_list .bmrow[data-id="{ids["line"]}"] .bmgo')
        ok = wait_for(lambda: page.evaluate("MKB.srcPanel && MKB.srcPanel.line") == line1, 15)
        r["go line"] = page.text_content("#bm_status")
        if not ok or f"placed at line {line1}" not in r["go line"]:
            problems.append(f"build 1: going to the line bookmark: panel at {page.evaluate('MKB.srcPanel && MKB.srcPanel.line')}, status {r['go line']!r}")
        bm_open_list(page, False)
        page.click(f'#bm_list .bmrow[data-id="{ids["code"]}"] .bmgo')
        ok = wait_for(lambda: page.evaluate("MKB.jumpHit") == hm1[1] + off, 15)
        r["go code"] = page.text_content("#bm_status")
        if not ok or f"hello_main+{off:#x}" not in r["go code"]:
            problems.append(f"build 1: going to the code bookmark: jumpHit {page.evaluate('MKB.jumpHit')}, want {hm1[1] + off}, status {r['go code']!r}")
        bm_close_menu(page)
        # Coverage: compared with the shipped build, the same lines ran.
        if cmp_with(page, "hello (shipped)", problems, "build 1"):
            fr, hr = cmp_row(page, "file", RBM_FILE), cmp_row(page, "func", f"{RBM_FILE}:hello_main")
            r["build 1 vs shipped"] = [fr, hr, page.inner_text("#cov_cmp_status")]
            if not fr or not hr or hr["plus"] != "0" or hr["minus"] != "0" or fr["minus"] != "0" or int(fr["was"]) < 5:
                problems.append(f"build 1 compared with the shipped build: file {fr}, hello_main {hr}")
        rb_shots(page, shots, "rebuildmap-1280-build1-source", problems, "build 1")
        page.emulate_media(color_scheme="light")
        # The shipped build's snapshot; one of build 1.
        snap_save(page, 2, False)
        rows = snap_rows(page)["rows"]
        r["snapshots in build 1"] = [[x["kernel"], x["restore"], x["sw"], x["text"][:200]] for x in rows]
        if rows[0]["kernel"] != "other" or not rows[0]["sw"] or "for the shipped build" not in rows[0]["text"] or rows[1]["kernel"] != "match":
            problems.append(f"build 1: the snapshots {r['snapshots in build 1']}")

        # Build 2: hello_main changes size; the "Still alive" call no longer runs.
        if not edit_text(page, t2, problems, "build 2") or not rebuild(page, 2, problems, "build 2", r):
            return problems, r
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
            problems.append("build 2 did not run to its end")
        hm2 = sym(page, "hello_main")
        r["build 2"] = {"hello_main": hm2, "run to": [page.input_value("#dbg_addr"), page.inner_text("#dbg_status")]}
        if page.input_value("#dbg_addr") != f"{RBM_FILE}:{line2}":
            problems.append(f"build 2: the Run to field {r['build 2']['run to']}, want {RBM_FILE}:{line2}")
        bps2 = page.evaluate(LINE_CODE_JS, [RBM_FILE, line2])
        want_code = next((a for a in bps2 if isinstance(a, int) and hm2[1] <= a < hm2[1] + hm2[2]), None)
        check_bookmarks(page, ids, {"line": ("build", f"{RBM_FILE}:{line2}"),
                                    "code": ("build", f"hello_main+{hex(want_code - hm2[1])}, {hex(want_code)}" if want_code else "?")},
                        problems, "build 2", r)
        rows = {x["id"]: x for x in bm_rows(page)}
        if "by its line" not in rows.get(ids["code"], {}).get("what", ""):
            problems.append(f"build 2: the code bookmark of a changed hello_main is not placed by its line: {rows.get(ids['code'])}")
        if cmp_with(page, "build 1 of hello", problems, "build 2"):
            fr = cmp_row(page, "file", RBM_FILE)
            open_file(page, line2)
            wait_for(lambda: "was" in (src_row(page, line2) or {}).get("cls", ""), 15)
            row = src_row(page, line2)
            r["build 2 vs build 1"] = [fr, row, page.inner_text("#cov_cmp_status")]
            if not fr or fr["minus"] == "0" or fr["plus"] == "0" or not row or "was" not in row["cls"] or row["mark"] != "◌":
                problems.append(f"build 2 compared with build 1: file {fr}, line {line2} {row}")
        rb_shots(page, shots, "rebuildmap-1280-build2-source", problems, "build 2")
        # The same at 390 px: the comparison in the tree, the menu's list.
        page.set_viewport_size({"width": 390, "height": 844})
        page.evaluate("MKB.closeSource && MKB.closeSource()")
        show_tab(page, "source")
        page.locator("#cov_cmp_box").scroll_into_view_if_needed()
        rb_shots(page, shots, "rebuildmap-390-build2-source", problems, "build 2 at 390 px")
        bm_open_list(page, False)
        rb_shots(page, shots, "rebuildmap-390-bookmarks", problems, "build 2 at 390 px")
        bm_close_menu(page)
        page.set_viewport_size({"width": 1280, "height": 1000})
        page.emulate_media(color_scheme="light")
        show_tab(page, "snapshots")
        wait_for(lambda: snap_rows(page)["rows"][1]["kernel"] == "build", 15)
        rows = snap_rows(page)["rows"]
        r["snapshots in build 2"] = [[x["kernel"], x["restore"], x["sw"], x["text"][:240]] for x in rows]
        b1 = rows[1]
        if b1["kernel"] != "build" or b1["restore"] or b1["sw"] or "build 1 of hello" not in b1["text"] or "not the build running" not in b1["text"]:
            problems.append(f"build 2: build 1's snapshot row {r['snapshots in build 2'][1]}")
        snap_click(page, f"#snapshots tr[data-id='{b1['id']}'] button.snapreplay", False)
        wait_for(lambda: "Cannot replay" in page.inner_text("#run_status"), 15)
        r["replay of build 1 in build 2"] = page.inner_text("#run_status")
        if "Cannot replay: this run is of build 1 of hello" not in r["replay of build 1 in build 2"]:
            problems.append(f"build 2: Replay of build 1's snapshot: {r['replay of build 1 in build 2']!r}")
        rb_shots(page, shots, "rebuildmap-1280-snapshots", problems, "build 2")
        page.emulate_media(color_scheme="light")
        bm_open_list(page, False)
        rb_shots(page, shots, "rebuildmap-1280-bookmarks", problems, "build 2")
        page.emulate_media(color_scheme="light")
        bm_close_menu(page)

        # A line whose old text this page never saw (a bookmark from before
        # a reload): found by its own text near its number, else kept.
        got = page.evaluate("([t, l]) => [MKB.mapLine(32, 'feedf00d:1', l, t), MKB.mapLine(5, 'feedf00d:1', '', t)]", [t1, RBM_ALIVE.rstrip("\n")])
        r["lines without the old text"] = got
        if got != [{"line": line1, "how": "text"}, {"line": 5, "how": "kept"}]:
            problems.append(f"lines placed without their old text: {got}")
        # Edge lines through a diff: past the old text (kept), a changed
        # last line (the line after the last one kept), nothing kept (line 1).
        got = page.evaluate("""([a]) => [MKB.mapLine(500, MKB.textKeep(a), '', a + 'x'), MKB.mapLine(3, MKB.textKeep('x\\ny\\nz'), 'z', 'x'),
          MKB.mapLine(1, MKB.textKeep('y'), 'y', 'q')]""", [orig])
        r["edge lines"] = got
        if got != [{"line": 500, "how": "kept"}, {"line": 2, "how": "changed"}, {"line": 1, "how": "changed"}]:
            problems.append(f"edge lines through a diff: {got}")

        # Build 3: build 1's text again, the same bytes: its snapshot
        # restores. A Run to file:line of a file no build edits stays.
        show_tab(page, "debugger")
        page.fill("#dbg_addr", "nosuch.c:5")
        # As for a big kernel whose coverage tree was not shown: build 2's
        # line tables not read when it is left (the record says what it lacks).
        if not edit_text(page, t1, problems, "build 3") or \
                not rebuild(page, 3, problems, "build 3", r, before=lambda: page.evaluate("MKB.DBG.tables.clear(); MKB.COV.run = null")):
            return problems, r
        if page.input_value("#dbg_addr") != "nosuch.c:5":
            problems.append(f"build 3: the Run to field of another file became {page.input_value('#dbg_addr')!r}")
        if cmp_with(page, "build 2 of hello", problems, "build 3"):
            r["build 3 vs build 2"] = page.inner_text("#cov_cmp_status")
            if "Incomplete: the line tables of 1 of its 64 KiB pieces of code that ran were not read" not in r["build 3 vs build 2"]:
                problems.append(f"build 3 compared with build 2 (its tables unread): {r['build 3 vs build 2']!r}")
        show_tab(page, "snapshots")
        wait_for(lambda: snap_rows(page)["rows"][1]["restore"], 30)
        rows = snap_rows(page)["rows"]
        r["snapshots in build 3"] = [[x["kernel"], x["restore"], x["text"][:200]] for x in rows]
        if rows[1]["kernel"] != "match" or not rows[1]["restore"] or "build 1 of hello" not in rows[1]["text"]:
            problems.append(f"build 3 (build 1's text): build 1's snapshot {r['snapshots in build 3'][1]}")
        else:
            snap_click(page, f"#snapshots tr[data-id='{rows[1]['id']}'] button.restore", False)
            if not wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rows[1]["id"], 30):
                problems.append(f"build 3: build 1's snapshot did not restore: {page.inner_text('#snap_status')!r}")

        # Build 4: no edit (CI's bytes). A Run to address in e8_extra
        # (build 3's) has no place in it: the field stays, the status says why.
        ex3 = sym(page, "e8_extra")
        show_tab(page, "debugger")
        page.fill("#dbg_addr", hex(ex3[0] + 2))
        if not edit_text(page, orig, problems, "build 4") or not rebuild(page, 4, problems, "build 4", r):
            return problems, r
        pl4 = sym(page, "put_line")
        r["run to e8_extra in build 4"] = [hex(ex3[0] + 2), page.input_value("#dbg_addr"), page.inner_text("#rb_status")[-240:]]
        if page.input_value("#dbg_addr") != hex(ex3[0] + 2) or "(e8_extra+0x2) has no place in this build: no e8_extra here" not in page.inner_text("#rb_status"):
            problems.append(f"build 4 (no edit): the Run to address in e8_extra {r['run to e8_extra in build 4']}")
        # Build 5: build 1's text again: put_line moves; a Run to address of
        # build 4 (put_line + 4) moves with it.
        show_tab(page, "debugger")
        page.fill("#dbg_addr", hex(pl4[0] + 4))
        if not edit_text(page, t1, problems, "build 5") or not rebuild(page, 5, problems, "build 5", r):
            return problems, r
        pl5 = sym(page, "put_line")
        r["run to address in build 5"] = [hex(pl4[0] + 4), page.input_value("#dbg_addr"), pl4, pl5, page.inner_text("#rb_status")[-240:]]
        if pl4 != pl0 or pl5 != pl1 or page.input_value("#dbg_addr") != hex(pl5[0] + 4) or "put_line+0x4 moved with the build" not in page.inner_text("#rb_status"):
            problems.append(f"build 5: the Run to address {r['run to address in build 5']}, put_line shipped {pl0}, build 1 {pl1}")
    finally:
        if errors:
            problems.append(f"page errors {errors[:3]}")
        ctx.close()
    return problems, r


def rebuildmap_hotpatch(browser, base, shots, width, scheme):
    """Shipped hello paused at hello_main; put_line patched with one line
    more: the Source panel shows the patched text and every place counts
    on it."""
    problems, r, errors = [], {}, []
    tag = f"hot patch {width}"
    phone = width < 600
    orig = shipped_text(base)
    patched = orig.replace("\twhile (*s)", "\t/* E8: hot patched */\n\twhile (*s)").replace("(unsigned char)*s++;", "(unsigned char)(*s++ ^ 0x20);")
    pl = patched.split("\n")
    store = next(i + 1 for i, x in enumerate(pl) if "^ 0x20" in x)
    ctx = browser.new_context(viewport={"width": width, "height": 844 if phone else 1000}, is_mobile=phone, has_touch=phone)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&fastforward=0&speed=full#debugger")
        srcedit_ready(page, "hello")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        hm = sym(page, "hello_main")
        page.fill("#dbg_addr", "hello_main")
        page.click("#dbg_runto")
        ok, at, why = run_to_wait(page, {hm[1]}, 120)
        if not ok:
            return [f"{tag}: Run to hello_main stopped at {at:#x}: {why!r}"], r
        if not phone:
            open_file(page, RBM_LINE - 1)
            page.click(f'#src_body [data-line="{RBM_LINE - 1}"]', button="right")
            b = bm_dialog_save(page, problems, tag, "line", name="e8 hello line")
            bm_id = b and b["id"]
        show_tab(page, "debugger")
        # 1280: the store line, which the patch changes and moves; 390: a
        # line above the edit, which stays.
        want_field = f"{RBM_FILE}:{5 if phone else store}"
        page.fill("#dbg_addr", f"{RBM_FILE}:{5 if phone else store - 1}")
        if not rbl_open(page, RBM_FILE, phone):
            return problems + [f"{tag}: Edit did not open {RBM_FILE}"], r
        hp_edit(page, patched)
        st = hp_apply(page, r, "patch", problems, tag, phone=phone)
        if not st:
            return problems, r
        if srcedit_state(page)["editing"]:
            rb_press(page, "#src_edit", phone)
        # The panel at EIP (hello_main): the patched text, its lines.
        page.evaluate("MKB.openSource(MKB.stepper.nextPhys())")
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-over") == "1", 30)
        r["panel note"] = page.inner_text("#src_info")[-300:]
        if "Patched: this is the text the machine runs since patch 1" not in r["panel note"]:
            problems.append(f"{tag}: the Source panel does not say it shows the patched text: {r['panel note']!r}")
        # Frames: in the copy and in hello_main, each line's text shown is
        # the patched text's line (the copy: put_line's body; hello_main:
        # the shipped line moved by one).
        frames = page.evaluate(FRAMES_JS, [HP_PA, 96]) + page.evaluate(FRAMES_JS, [hm[1], hm[2]])
        bad = []
        for fn, path, line in {tuple(f) for f in frames}:
            shown = page.evaluate("([p, l]) => MKB.srcText(p, l)", [path, line])
            if path == RBM_FILE and line and shown != pl[line - 1]:
                bad.append((fn, line, shown))
            if fn == "hello_main" and path == RBM_FILE and line and pl[line - 1] != orig.split("\n")[line - 2]:
                bad.append((fn, line, "not the shipped line moved by one: " + pl[line - 1]))
            if fn == "put_line" and path == RBM_FILE and line and not 12 <= line <= 19:
                bad.append((fn, line, "outside put_line"))
        r["frames"] = sorted({tuple(f) for f in frames})[:12]
        if bad or not frames:
            problems.append(f"{tag}: frames whose shown line is not their code's: {bad[:6]}")
        open_file(page, store)
        row = src_row(page, store)
        if not row or "^ 0x20" not in row["text"]:
            problems.append(f"{tag}: line {store} in the panel is {row}, want the patched store")
        # The Run to target moved by the patch's extra line; it stops in the copy.
        r["run to"] = [page.input_value("#dbg_addr"), page.inner_text("#dbg_status")]
        if page.input_value("#dbg_addr") != want_field:
            problems.append(f"{tag}: the Run to field {r['run to']}, want {want_field}")
        if phone:
            show_tab(page, "debugger")
            page.fill("#dbg_addr", f"{RBM_FILE}:{store}")
            open_file(page, store)
        tg = page.evaluate("async (t) => { const w = await MKB.api.step.runToTargets(t); return w.targets || w.why; }", f"{RBM_FILE}:{store}")
        r["run to targets"] = tg
        if not isinstance(tg, list) or not any(HP_PA <= a < HP_PA + 0x20000 for a in tg):
            problems.append(f"{tag}: Run to {RBM_FILE}:{store} has no target in the copy: {tg}")
        if not phone:
            wait_for(lambda: (src_row(page, RBM_LINE) or {}).get("star"), 15)
            r["line bookmark"] = [src_row(page, RBM_LINE - 1), src_row(page, RBM_LINE)]
            if not (src_row(page, RBM_LINE) or {}).get("star") or (src_row(page, RBM_LINE - 1) or {}).get("star"):
                problems.append(f"{tag}: the line bookmark of {RBM_LINE - 1} is not marked at {RBM_LINE}: {r['line bookmark']}")
            row = page.evaluate(BM_STATE_JS)
            r["bookmark rows"] = [x["group"] for x in row["rows"] if x["id"] == bm_id]
        rb_shots(page, shots, f"rebuildmap-hotpatch-{width}-source", problems, tag)
        page.emulate_media(color_scheme=scheme)
        show_tab(page, "debugger")
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and HP_PA <= int(page.get_attribute("#regs", "data-phys") or -1) < HP_PA + 0x20000, 120)
        r["run to stopped"] = [page.get_attribute("#regs", "data-phys"), page.inner_text("#dbg_status")[:200]]
        if not ok:
            problems.append(f"{tag}: Run to {RBM_FILE}:{store} did not stop in the copy: {r['run to stopped']}")
        hp_continue(page)
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


def suite_rebuildmap(ctx):
    if not ctx.base.startswith("http") and not (SITE / "kits" / "index.json").exists():
        return report("rebuildmap", ["no site/kits: nothing to rebuild"], {})
    failed = False
    for name, fn in (("builds", lambda: rebuildmap_run(ctx.browser, ctx.base, ctx.shots)),
                     ("hot patch 1280", lambda: rebuildmap_hotpatch(ctx.browser, ctx.base, ctx.shots, 1280, "dark")),
                     ("hot patch 390", lambda: rebuildmap_hotpatch(ctx.browser, ctx.base, ctx.shots, 390, "light"))):
        t0 = time.monotonic()
        try:
            problems, r = fn()
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems, r = [f"{type(e).__name__}: {str(e)[:600]}"], {}
        failed |= report("rebuildmap", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
