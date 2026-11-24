"""Browser test suite preentry_old (test_browser.py --suites): what an older
page saved before the kernel entry, a run that ends there, and the page's
words and layout around them, driven through the page as a user does (plan
A3f).

Case po_old_page needs the page of 658155e08 built (MKB_OLD_PAGE: its site
directory with app.bundle.js, index.html, style.css and sw.js; CI builds it
from git). Its own Save, bookmark and Download run buttons make the old
records, served on the same origin, then this page opens the same storage."""

import json
import os
import re
import tempfile
import time

import jscov
from tests.browser.common import PHONE, report, show_tab, snap_click, snap_rows, snap_save, wait_for
from tests.browser.hotpatch import hp_continue
from tests.browser.hprun import DEBIAN, MINI, patch_do_exit, still, steps
from tests.browser.preentry import SAVE_AT, UNDER_BADGE_JS, open_full, pause_in_ff
from tests.browser.restored import AT_PROMPT, STATE_JS, restore, same_run

# A count as the page writes it (1,482,752), not a digit of a name (startup_32).
COUNT_RE = re.compile(r"\d{1,3}(?:,\d{3})+|\d{4,}")

OLD_SHA = "658155e08"
TYPES = {".js": "text/javascript", ".html": "text/html", ".css": "text/css", ".map": "application/json"}
# A row of the Snapshots tab: its buttons and its reason.
ROW_JS = """(id) => { const tr = document.querySelector(`#snapshots tr[data-id='${id}']`); if (!tr) return null;
  const on = (c) => { const b = tr.querySelector('button.' + c); return !!b && !b.disabled; };
  const why = tr.querySelector('.snapwhy');
  return { restore: on('restore'), replay: on('snapreplay'), why: why && why.getClientRects().length ? why.textContent : '' }; }"""
# The bookmarks list's rows: buttons on or off, and the reason shown.
BM_JS = """() => [...document.querySelectorAll('#bm_list .bmrow')].map((l) => { const b = (c) => { const x = l.querySelector('.' + c);
  return x ? (x.disabled ? 'off' : 'on') : 'none'; }; const w = l.querySelector('.bmwhy');
  return { name: (l.querySelector('.bmgo') || {}).textContent || '', restore: b('bmrestore'), replay: b('bmreplay'),
    why: w && w.getClientRects().length ? w.textContent : '' }; })"""
# The state line and the line under the counters, read in one go.
LINES_JS = """() => ({ state: document.getElementById('dbg_state').textContent, count: document.getElementById('count_info').textContent,
  ff: (document.getElementById('runbadge_toggle') || { dataset: {} }).dataset.state === 'ff' })"""
# 390 px: each card cell's value starts after its column's name, with room.
CELLS_JS = """() => { const out = [], cv = document.createElement('canvas').getContext('2d');
  for (const td of document.querySelectorAll('#snapshots tbody td[data-label]')) {
    if (!td.getClientRects().length || !td.textContent.trim()) continue;
    const pre = getComputedStyle(td, '::before');
    if (pre.content === 'none' || pre.display === 'none') continue;
    cv.font = pre.font;
    const label = td.dataset.label, need = cv.measureText(label).width + 4;
    const r = document.createRange(); r.selectNodeContents(td);
    const first = [...r.getClientRects()].find((q) => q.width > 0);
    const off = first ? first.left - td.getBoundingClientRect().left : 0;
    if (off < need) out.push(label + ': ' + td.textContent.trim().slice(0, 30)); }
  return out; }"""
# The Diff's pickers: the picked name fits in the select.
PICKS_JS = """() => ['snap_a', 'snap_b'].map((id) => { const s = document.getElementById(id), cv = document.createElement('canvas').getContext('2d');
  cv.font = getComputedStyle(s).font; const t = s.options[s.selectedIndex].text;
  return { id, text: t, cut: cv.measureText(t).width > s.clientWidth - 28 }; })"""
# Visible above the docked Source sheet (a phone: the sheet is the lower 65vh).
ABOVE_SHEET_JS = """(ids) => { const sheet = document.querySelector('body > .srcpanel:not([hidden])');
  const top = sheet ? sheet.getBoundingClientRect().top : innerHeight;
  return ids.map((id) => { const e = document.getElementById(id); if (!e || !e.getClientRects().length || !e.textContent.trim()) return { id, shown: false };
    const r = e.getBoundingClientRect(); return { id, shown: true, top: Math.round(r.top), bottom: Math.round(r.bottom), sheet: Math.round(top),
      covered: r.top < 0 || r.bottom > top + 1 }; }); }"""


def old_page_dir():
    d = os.environ.get("MKB_OLD_PAGE", "")
    return d if d and os.path.isfile(os.path.join(d, "app.bundle.js")) else ""


def bm_open(page):
    """The run badge's menu, then its Bookmarks list, by their buttons."""
    if page.get_attribute("#runbadge_toggle", "aria-expanded") != "true":
        page.click("#runbadge_toggle")
    if page.get_attribute("#bm_open", "aria-expanded") != "true":
        page.click("#bm_open")
    page.wait_for_selector("#bm_box:not([hidden])")


def bm_close(page):
    if page.get_attribute("#runbadge_toggle", "aria-expanded") == "true":
        page.click("#runbadge_toggle")


def bm_here(page, name):
    """A position bookmark of here, by the menu's Here button."""
    bm_open(page)
    page.click("#bm_here")
    page.wait_for_selector("#bm_dialog:not([hidden])")
    page.fill("#bm_name", name)
    page.click("#bm_save")
    page.wait_for_timeout(500)
    bm_close(page)


def bm_row(page, name):
    bm_open(page)
    page.wait_for_timeout(300)
    rows = [x for x in page.evaluate(BM_JS) if x["name"] == name]
    bm_close(page)
    return rows[0] if rows else None


def replay_verdict(page, secs=1500):
    show_tab(page, "snapshots")
    wait_for(lambda: page.get_attribute("#run_status", "data-replay") in ("match", "differ", "stopped", "error"), secs, 0.3)
    still(page)
    return page.get_attribute("#run_status", "data-replay"), page.inner_text("#run_status")


def po_old_page(browser, base, tmp, problems, r):
    """658155e08's own Save, bookmark and Download run before the kernel entry
    (on the JIT's fast-forward) and Save after it; then this page: the early
    snapshot is refused on its row and on the bookmark's, with the reason; the
    late one stays offered; the downloaded run's Replay is refused and the
    machine goes on as the uninterrupted boot (item 1, item 2 for older files)."""
    tag = "older page's records"
    old = old_page_dir()
    if not old:
        problems.append(f"{tag}: MKB_OLD_PAGE does not hold the page of {OLD_SHA} (app.bundle.js)")
        return
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, service_workers="block", accept_downloads=True)

    def old_route(route):
        path = route.request.url.split("?")[0][len(base) + 1:] or "index.html"
        local = os.path.join(old, path)
        if os.path.isfile(local) and os.path.splitext(path)[1] in TYPES:
            route.fulfill(status=200, content_type=TYPES[os.path.splitext(path)[1]], body=open(local, "rb").read())
        else:
            route.continue_()

    try:
        # The old page's own tab, without coverage (its bundle has the same
        # URL as this page's), closed before this page opens.
        jscov.hold()
        try:
            page = ctx.new_page()
            page.route(f"{base}/**", old_route)
            open_full(page, base, DEBIAN)
            # The bundle served is the old one (A3e's snapRefused is not in it).
            if page.evaluate("fetch('app.bundle.js').then((x) => x.text()).then((t) => t.includes('snapRefused'))"):
                problems.append(f"{tag}: the page served is not {OLD_SHA}'s (test setup)")
                return
            if not pause_in_ff(page, problems, tag):
                return
            pre_at = steps(page)
            pre = snap_save(page, 1, False)["rows"][0]["id"]
            bm_here(page, "old pre-entry")
            show_tab(page, "snapshots")
            with page.expect_download(timeout=30000) as dl:
                page.click("#run_dl")
            run_file = os.path.join(tmp, "old-pre-entry.mkbrun.json")
            dl.value.save_as(run_file)
            run = json.load(open(run_file))
            r["old run"] = {"end": run["end"]["at"], "log": len(run["log"]), "fastforward": run.get("fastforward"), "preEntry": run.get("preEntry")}
            hp_continue(page)
            if not wait_for(lambda: page.evaluate(f"MKB.stepper.steps > {SAVE_AT} && MKB.runState === 'stepped'"), 1800, 0.1):
                problems.append(f"{tag}: the old page's run did not pass {SAVE_AT}")
                return
            show_tab(page, "machine")
            page.click("#dbg_pause")
            still(page)
            post = snap_save(page, 2, False)["rows"]
            post = [x["id"] for x in post if x["id"] != pre][0]
            r["old snapshots"] = {"before the entry": pre_at, "after": steps(page)}
            page.close()
        finally:
            jscov.release()
        jscov.hold()
        try:
            page = ctx.new_page()
            open_full(page, base, DEBIAN)
            if not wait_for(lambda: page.evaluate(f"!!MKB.stepper && ({AT_PROMPT})()"), 1800, 0.3) or not still(page):
                problems.append(f"{tag}: this page's boot did not stop at the prompt")
                return
        finally:
            jscov.release()
        first = page.evaluate(STATE_JS)
        r["prompt"] = first["steps"]
        show_tab(page, "snapshots")
        wait_for(lambda: {pre, post} <= {x["id"] for x in snap_rows(page)["rows"] if x["kernel"] == "match"}, 120)
        row = page.evaluate(ROW_JS, pre)
        r["row before the entry"] = row
        if not row or row["restore"] or row["replay"] or "kernel entry" not in row["why"]:
            problems.append(f"{tag}: {OLD_SHA}'s snapshot at {pre_at:,}, before the kernel entry, offers {row}")
        kept = page.evaluate(ROW_JS, post)
        r["row after the entry"] = kept
        if not kept or not kept["restore"] or kept["why"]:
            problems.append(f"{tag}: {OLD_SHA}'s snapshot after the entry offers {kept}")
        bm = bm_row(page, "old pre-entry")
        r["bookmark"] = bm
        if not bm or bm["restore"] == "on" or bm["replay"] == "on" or "kernel entry" not in bm["why"]:
            problems.append(f"{tag}: {OLD_SHA}'s bookmark before the kernel entry offers {bm}")
        show_tab(page, "snapshots")
        page.set_input_files("#run_file", run_file)
        state, said = replay_verdict(page)
        r["file replay"] = [state, said]
        if state != "error" or "kernel entry" not in said:
            problems.append(f"{tag}: Replay of {OLD_SHA}'s run that ends before the kernel entry: {state} {said!r}")
        jscov.hold()
        try:
            show_tab(page, "machine")
            hp_continue(page)
            time.sleep(0.5)
            if not wait_for(lambda: page.evaluate(AT_PROMPT), 1500, 0.3) or not still(page):
                problems.append(f"{tag}: Continue after the refused replay did not stop at the prompt")
                return
        finally:
            jscov.release()
        again = page.evaluate(STATE_JS)
        r["after the refused replay"] = again["steps"]
        same_run(again, first, problems, tag + ": Continue after the refused replay")
        # A3g item 2: that page's count of IRQ0 cannot be trusted (its
        # fast-forward counted some twice): the clock line says not known.
        restore(page, post)
        show_tab(page, "machine")
        line = page.inner_text("#vclock")
        r["IRQ0 after restoring the old snapshot after the entry"] = line
        if "IRQ0 count not known" not in line or re.search(r"IRQ0 ×", line):
            problems.append(f"{tag}: restored {OLD_SHA}'s snapshot after the entry, the clock line says {line!r}")
    finally:
        ctx.close()


def po_pre_entry_run(browser, base, problems, r):
    """On the fast-forward: the state line and the count line say the same
    count and that it runs (item 3); at a pause there Download run, Replay
    and a bookmark's Replay refuse, saying why (item 2)."""
    tag = "a run before the entry"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        open_full(page, base, DEBIAN)
        seen = []
        end = time.monotonic() + 120
        while time.monotonic() < end and len(seen) < 12:
            x = page.evaluate(LINES_JS)
            if x["ff"] and "fast-forwarding" in x["state"]:
                seen.append(x)
            time.sleep(0.15)
        r["lines while it fast-forwards"] = seen[:2]
        if not seen:
            problems.append(f"{tag}: no fast-forward seen")
        for x in seen:
            nums = [COUNT_RE.findall(x[k]) for k in ("state", "count")]
            if "(paused)" in x["count"] or not nums[0] or nums[0][0] not in nums[1]:
                problems.append(f"{tag}: while the JIT runs: state {x['state']!r}, count line {x['count']!r}")
                break
        if not pause_in_ff(page, problems, tag):
            return
        here = steps(page)
        r["paused at"] = here
        show_tab(page, "snapshots")
        downloads = []
        page.on("download", lambda d: downloads.append(d.suggested_filename))
        said0 = page.inner_text("#run_status")
        page.click("#run_dl")
        wait_for(lambda: page.inner_text("#run_status") != said0, 10, 0.1)
        page.wait_for_timeout(1000)
        said = page.inner_text("#run_status")
        r["Download run"] = [downloads, said]
        if downloads or "kernel entry" not in said:
            problems.append(f"{tag}: Download run at {here:,}, before the entry: {downloads} {said!r}")
        page.click("#run_replay")
        wait_for(lambda: page.inner_text("#run_status") != said, 10, 0.1)
        page.wait_for_timeout(1500)
        said = page.inner_text("#run_status")
        r["Replay"] = [said, steps(page)]
        if "kernel entry" not in said or not said.startswith("Cannot replay") or steps(page) < here:
            problems.append(f"{tag}: Replay at {here:,}, before the entry: {said!r}, now at {steps(page):,}")
            return
        bm_here(page, "pre-entry here")
        bm = bm_row(page, "pre-entry here")
        r["bookmark"] = bm
        if not bm or bm["replay"] == "on" or "kernel entry" not in bm["why"]:
            problems.append(f"{tag}: a bookmark at {here:,} offers {bm}")
    finally:
        ctx.close()


def po_sw_blocked(browser, base, problems, r):
    """A browser that blocks service workers: no page error (item 4)."""
    tag = "service worker blocked"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, service_workers="block")
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={MINI}")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.image && MKB.image.name === 'minified-linux'", timeout=120000)
        page.wait_for_timeout(3000)
        r["errors"] = errors
        if errors:
            problems.append(f"{tag}: page errors {errors}")
    finally:
        ctx.close()


def po_phone_snapshots(browser, base, shots, problems, r):
    """390 px, light and dark: each snapshot card's names stand apart from
    their values, the menu toggle is over no row, the Diff's pickers show
    the whole names (item 5)."""
    tag = "snapshots at 390 px"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
            if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'"), 180, 0.3):
                problems.append(f"{tag} {scheme}: no end")
                continue
            snap_save(page, 1, True)
            snap_save(page, 2, True)
            show_tab(page, "snapshots")
            cut = page.evaluate(CELLS_JS)
            hits = page.evaluate(UNDER_BADGE_JS, "#snapshots tbody")
            picks = page.evaluate(PICKS_JS)
            r[scheme] = {"cells run together": cut[:4], "toggle over rows at": (hits or [])[:4], "pickers": picks}
            page.locator("#snapshots").scroll_into_view_if_needed()
            page.screenshot(path=str(shots / f"preentry-old-rows-390-{scheme}.png"))
            page.locator("#snap_diffbox").scroll_into_view_if_needed()
            page.screenshot(path=str(shots / f"preentry-old-diff-390-{scheme}.png"))
            if cut:
                problems.append(f"{tag} {scheme}: names run into their values: {cut[:4]}")
            if hits is None or hits:
                problems.append(f"{tag} {scheme}: the toggle covers a snapshot row at scroll offsets {(hits or [])[:4]}")
            if any(p["cut"] for p in picks):
                problems.append(f"{tag} {scheme}: the Diff's pickers cut {[p['text'] for p in picks if p['cut']]}")
        finally:
            ctx.close()


def po_phone_patch(browser, base, shots, problems, r):
    """390 px: after a Restore and a patch made on the phone, the Machine
    overlay and Step back's reason are above the docked Source sheet (item 6)."""
    tag = "patch at 390 px"
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={MINI}&speed=10000")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 20000",
                               timeout=120000)
        show_tab(page, "machine")
        page.tap("#dbg_pause")
        still(page)
        row = snap_save(page, 1, True)["rows"][0]["id"]
        restore(page, row)
        show_tab(page, "machine")
        if patch_do_exit(page, problems, tag, phone=True) is None:
            return
        show_tab(page, "machine")
        page.wait_for_timeout(800)
        got = page.evaluate(ABOVE_SHEET_JS, ["vm_reason", "dbg_back_why"])
        r["above the sheet"] = got
        page.screenshot(path=str(shots / "preentry-old-patch-390.png"))
        bad = [g for g in got if not g["shown"] or g["covered"]]
        if bad:
            problems.append(f"{tag}: under the Source sheet or not shown: {bad}")
        hits = page.evaluate(UNDER_BADGE_JS, "#dbg_back_why")
        r["toggle over Step back's reason at"] = (hits or [])[:4]
        if hits is None or hits:
            problems.append(f"{tag}: the menu toggle covers Step back's reason at scroll offsets {(hits or [])[:4]}")
    finally:
        ctx.close()


def po_switch(browser, base, problems, r):
    """Switch to on a snapshot's row: once the kernel runs, the status line
    no longer says "Switching to..." (item 7)."""
    tag = "Switch to"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={MINI}&paused=1")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.runState === 'paused'", timeout=300000)
        row = snap_save(page, 1, False)["rows"][0]["id"]
        show_tab(page, "machine")
        page.select_option("#kernel", "hello")
        page.wait_for_function("() => MKB.image && MKB.image.name === 'hello' && MKB.stepper", timeout=120000)
        show_tab(page, "snapshots")
        sel = f"#snapshots tr[data-id='{row}'] button.snapswitch"
        if not wait_for(lambda: page.locator(sel).count() == 1, 30):
            problems.append(f"{tag}: the row offers no Switch to")
            return
        snap_click(page, sel, False)
        ok = wait_for(lambda: page.locator(f"#snapshots tr[data-id='{row}'] button.restore").count() == 1, 180)
        page.wait_for_timeout(3000)
        said = page.inner_text("#snap_status")
        r["status"] = said
        if not ok or said.startswith("Switching to"):
            problems.append(f"{tag}: switched {ok}, the status says {said!r}")
    finally:
        ctx.close()


def suite_preentry_old(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_PREENTRY_ONLY", "").split(",") if x]
    tmp = tempfile.mkdtemp(prefix="mkb-preentry-old-")
    cases = (("po_sw_blocked", lambda p, r: po_sw_blocked(ctx.browser, ctx.base, p, r)),
             ("po_switch", lambda p, r: po_switch(ctx.browser, ctx.base, p, r)),
             ("po_phone_snapshots", lambda p, r: po_phone_snapshots(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("po_phone_patch", lambda p, r: po_phone_patch(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("po_pre_entry_run", lambda p, r: po_pre_entry_run(ctx.browser, ctx.base, p, r)),
             ("po_old_page", lambda p, r: po_old_page(ctx.browser, ctx.base, tmp, p, r)))
    for name, fn in cases:
        if only and name not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("preentry_old", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
