"""Browser test suite sameway (test_browser.py --suites): a stop does not
change where a run goes, the stages say what happened, the menu toggle keeps
off the Machine tab's text, and the kernel entry is the same one with and
without the fast-forward, driven through the page as a user does (plan A3h)."""

import os
import time

import jscov
from tests.browser.common import PHONE, report, show_tab, snap_save, wait_for
from tests.browser.hotpatch import hp_continue
from tests.browser.hprun import DEBIAN, MINI, patch_do_exit, still, steps
from tests.browser.preentry import STOPPED_JS
from tests.browser.restored import STATE_JS, restore, same_run
from tests.browser.restoreway import ABOVE_SHEET_JS

# The counters table (label: [from the entry, from reset]), the end banner,
# the entry line and the screen's text.
VIEW_JS = """() => { const rows = {};
  for (const tr of document.querySelectorAll('#counters tbody tr')) { const c = [...tr.children].map((x) => x.textContent.trim());
    if (c[0] !== 'now') rows[c[0]] = [c[1], c[3]]; }
  return { rows, end: document.getElementById('end_banner').textContent, entry: document.getElementById('entry_info').textContent,
    entrySteps: document.getElementById('entry_info').dataset.steps || '', head: document.querySelector('#counters thead tr').children[1].textContent,
    screen: document.getElementById('screen_text').textContent }; }"""
# Text of these elements (and the end banner's box) in the menu toggle's
# column: the toggle is fixed, so at some scroll offset, or with a longer
# page above (the verifier's tabs with names took four rows), it sits over
# them: [selector, the text, its right edge, the toggle's left edge].
TEXT_UNDER_TOGGLE_JS = """(sels) => { const b = document.getElementById('runbadge_toggle').getBoundingClientRect(), hits = [];
  const rects = (e) => { const out = [], w = document.createTreeWalker(e, NodeFilter.SHOW_TEXT); let n;
    while ((n = w.nextNode())) { if (!n.textContent.trim()) continue; const r = document.createRange(); r.selectNodeContents(n);
      for (const q of r.getClientRects()) if (q.width && q.height) out.push([q, n.textContent.trim().slice(0, 40)]); }
    if (e.id === 'end_banner') out.push([e.getBoundingClientRect(), '(the banner box)']);
    return out; };
  for (const s of sels) for (const e of document.querySelectorAll(s)) { if (!e.getClientRects().length) continue;
    for (const [q, text] of rects(e)) if (q.left < b.right - 1 && b.left + 1 < q.right) hits.push([s, text, Math.round(q.right), Math.round(b.left)]); }
  const seen = new Set();
  return hits.filter((h) => { const k = h[0] + h[1]; if (seen.has(k)) return false; seen.add(k); return true; }); }"""
MACHINE_TEXT = ["#counters", "#vclock", "#guest_in", "#entry_info", "#end_banner", "#count_info", "#dbg_state"]


def boot_paused(page, base, kernel, ff, speed_param=False):
    """Paused at the reset vector, count off, speed full picked in the menu."""
    q = f"?kernel={kernel}&paused=1" + ("" if ff else "&fastforward=0") + ("&speed=full" if speed_param else "")
    page.goto(f"{base}/index.html{q}")
    page.wait_for_function("(n) => window.MKB && MKB.stepper && MKB.image && MKB.image.name === n && MKB.runState === 'paused'",
                           arg=kernel, timeout=300000)
    show_tab(page, "machine")
    if page.is_checked("#count"):
        page.uncheck("#count")
    page.select_option("#dbg_speed", "full")
    page.wait_for_timeout(300)


def to_stop(page, secs):
    """Continue, then the run's stop (its end or the prompt): the machine and what the page says."""
    hp_continue(page)
    time.sleep(0.5)
    if not wait_for(lambda: page.evaluate(STOPPED_JS), secs, 0.3) or not still(page):
        return None
    show_tab(page, "machine")
    return {**page.evaluate(STATE_JS), "view": page.evaluate(VIEW_JS)}


def run_to(page, target):
    show_tab(page, "debugger")
    page.fill("#dbg_addr", target)
    page.click("#dbg_runto")
    time.sleep(0.5)
    # Paused is also the state under the Run to's fast-forward (A3f).
    return wait_for(lambda: page.evaluate("MKB.runState === 'paused' && !MKB.REV.busy && !MKB.ffActive && MKB.stepper.steps > 1e6"), 900, 0.3) and still(page)


def same_view(got, want, problems, tag):
    """The stages as the page shows them: the same counts and end text."""
    g, w = got["view"], want["view"]
    if g["end"] != w["end"]:
        problems.append(f"{tag}: the end says {g['end']!r}, in the uninterrupted run {w['end']!r}")
    for k, v in w["rows"].items():
        if g["rows"].get(k) != v:
            problems.append(f"{tag}: row {k!r} is {g['rows'].get(k)}, in the uninterrupted run {v}")


def true_stages(got, problems, tag):
    """A line on the screen is not "not reached"; nor exit() or panic() when the end is a panic."""
    v = got["view"]
    for line in ("Hello, World!", "Still alive"):
        cells = v["rows"].get(line)
        if line in v["screen"] and (not cells or cells[0] in ("not reached", "counting...")):
            problems.append(f"{tag}: the screen shows {line!r}, its row says {cells}")
    for k, cells in v["rows"].items():
        if k.startswith(("exit(", "panic()")) and "panic" in v["end"].lower() and cells[0] == "not reached":
            problems.append(f"{tag}: the run ended in a panic ({v['end']!r}), row {k!r} says not reached")


def sw_runto(browser, base, kernel, problems, r, ffs=(False,), reload=True, secs=1800):
    """Count off, speed full picked while paused at the reset vector:
    Continue to the end; then on another page Run to start_kernel, Save,
    Continue; and after a reload Restore + Continue. All three are one run
    (item 1), the stages true in each."""
    tag = f"{kernel}: Run to with the count off at full speed"
    # Debian's stepped boot under coverage (site/jscov.py) is about 20 times
    # slower: its pages run without it (the minified Linux's cases cover the
    # same lines); hold() applies to pages opened after it.
    held = kernel == DEBIAN
    if held:
        jscov.hold()
    try:
        sw_runto_pages(browser, base, kernel, problems, r, ffs, reload, secs, tag)
    finally:
        if held:
            jscov.release()


def sw_runto_pages(browser, base, kernel, problems, r, ffs, reload, secs, tag):
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    try:
        page = ctx.new_page()
        boot_paused(page, base, kernel, ffs[0])
        first = to_stop(page, secs)
        if not first:
            problems.append(f"{tag}: the uninterrupted run did not stop")
            return
        r["uninterrupted"] = {k: first[k] for k in ("steps", "ram", "irq0")}
        r["uninterrupted view"] = {"end": first["view"]["end"], "rows": first["view"]["rows"]}
        true_stages(first, problems, f"{tag}: uninterrupted")
    finally:
        ctx.close()
    for ff in ffs:
        how = "with the fast-forward" if ff else "?fastforward=0"
        ctx = browser.new_context(viewport={"width": 1280, "height": 900})
        try:
            page = ctx.new_page()
            boot_paused(page, base, kernel, ff)
            if not run_to(page, "start_kernel"):
                problems.append(f"{tag} {how}: Run to start_kernel did not stop: {page.evaluate('[MKB.runState, MKB.stepper.steps]')}")
                continue
            at = steps(page)
            r[f"{how}: stopped at"] = at
            row = snap_save(page, 1, False)["rows"][0]["id"]
            got = to_stop(page, secs)
            if not got:
                problems.append(f"{tag} {how}: Continue after the Run to did not stop")
                continue
            r[f"{how}: Run to + Continue"] = {k: got[k] for k in ("steps", "ram", "irq0")}
            same_run(got, first, problems, f"{tag} {how}: Run to + Continue")
            same_view(got, first, problems, f"{tag} {how}: Run to + Continue")
            true_stages(got, problems, f"{tag} {how}: Run to + Continue")
            if not reload:
                continue
            # The record keeps the way: after a reload (speed full is now
            # the remembered one) Restore + Continue goes it too.
            page.reload()
            page.wait_for_function("(n) => window.MKB && MKB.stepper && MKB.image && MKB.image.name === n && MKB.runState === 'paused'",
                                   arg=kernel, timeout=300000)
            restore(page, row)
            if steps(page) != at:
                problems.append(f"{tag} {how}: restored at {steps(page)}, saved at {at}")
                continue
            again = to_stop(page, secs)
            if not again:
                problems.append(f"{tag} {how}: Continue after a reload and Restore did not stop")
                continue
            r[f"{how}: reload, Restore + Continue"] = {k: again[k] for k in ("steps", "ram", "irq0")}
            same_run(again, first, problems, f"{tag} {how}: reload, Restore + Continue")
            if again["view"]["end"] != first["view"]["end"]:
                problems.append(f"{tag} {how}: reload, Restore + Continue: the end says {again['view']['end']!r}, "
                                f"in the uninterrupted run {first['view']['end']!r}")
        finally:
            ctx.close()


def sw_jit_runto(browser, base, problems, r):
    """The minified Linux with speed full from the start (the JIT): Continue
    from the reset vector, and Run to start_kernel + Continue. The stages are
    true in both (item 1); the second ends with the same machine, or the page
    says it is another run (stepping and the JIT deliver the timer's
    interrupts at other instructions: not repeatable by design)."""
    tag = "JIT run: Run to + Continue"
    ends, said = [], ""
    for way in ("none", "runto"):
        ctx = browser.new_context(viewport={"width": 1280, "height": 900})
        try:
            page = ctx.new_page()
            boot_paused(page, base, MINI, False, speed_param=True)
            if way == "runto" and not run_to(page, "start_kernel"):
                problems.append(f"{tag}: Run to start_kernel did not stop")
                return
            got = to_stop(page, 600)
            if not got:
                problems.append(f"{tag} {way}: no stop")
                return
            r[way] = {k: got[k] for k in ("steps", "ram", "irq0")}
            r[way + " view"] = {"end": got["view"]["end"], "rows": got["view"]["rows"]}
            true_stages(got, problems, f"{tag} {way}")
            ends.append(got)
            if way == "runto":
                said = page.inner_text("#count_info")
                r["count line"] = said
        finally:
            ctx.close()
    differs = []
    same_run(ends[1], ends[0], differs, tag)
    if differs and "another run" not in said:
        problems.extend(differs + [f"{tag}: and the page does not say so: {said!r}"])


def sw_entry(browser, base, problems, r):
    """Debian's kernel entry with and without the fast-forward: the same
    address and count, and the counters' column says from where (item 4)."""
    tag = "the kernel entry"
    seen = {}
    for ff in (True, False):
        ctx = browser.new_context(viewport={"width": 1280, "height": 900})
        jscov.hold()  # the stepped way to the entry: this page without coverage
        try:
            page = ctx.new_page()
            page.goto(f"{base}/index.html?kernel={DEBIAN}&speed=100000" + ("" if ff else "&fastforward=0"))
            ok = wait_for(lambda: page.evaluate("!!window.MKB && !!MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 1000"),
                          1200, 0.5)
            show_tab(page, "machine")
            v = page.evaluate(VIEW_JS)
            seen[ff] = {"entry": v["entry"], "steps": v["entrySteps"], "head": v["head"]}
            r["fast-forward" if ff else "?fastforward=0"] = seen[ff]
            if not ok:
                problems.append(f"{tag}: {'with' if ff else 'without'} the fast-forward the entry was not reached: {v['entry']!r}")
                return
        finally:
            ctx.close()
            jscov.release()
    a, b = seen[True], seen[False]
    where = lambda e: e["entry"].split(" after ")[0]  # noqa: E731
    if where(a) != where(b) or a["steps"] != b["steps"]:
        problems.append(f"{tag}: with the fast-forward {a['entry']!r}, without it {b['entry']!r}")
    for x in (a, b):
        if "1 MiB" in x["head"]:
            problems.append(f"{tag}: the column says {x['head']!r}, the count is from {where(x)!r}")


def sw_entry_restored(browser, base, problems, r):
    """After a reload (paused at the reset vector), Restore of a snapshot
    taken after the entry shows the entry line at once (item 4)."""
    tag = "the entry line after a reload and Restore"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    try:
        page = ctx.new_page()
        page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
        page.wait_for_function("() => !!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'", timeout=180000)
        still(page)
        show_tab(page, "machine")
        want = page.evaluate(VIEW_JS)["entry"]
        row = snap_save(page, 1, False)["rows"][0]["id"]
        page.goto(f"{base}/index.html?kernel={MINI}&paused=1")
        page.wait_for_function("(n) => window.MKB && MKB.stepper && MKB.image && MKB.image.name === n && MKB.runState === 'paused'",
                               arg=MINI, timeout=300000)
        restore(page, row)
        show_tab(page, "machine")
        got = page.evaluate(VIEW_JS)["entry"]
        r["before"] = want
        r["restored"] = got
        if "Entered the kernel" not in got or got.split(" (")[0].split(".")[0] != want.split(" (")[0].split(".")[0]:
            problems.append(f"{tag}: {got!r}, the run said {want!r}")
    finally:
        ctx.close()


def sw_toggle(browser, base, shots, problems, r):
    """The menu toggle over the Machine tab's text: at 844x390 beside the
    Source panel none of it, at any scroll offset; at 390 px the end banner
    and the lines under the table; with the Source sheet open at 390 px the
    speed picker is above the sheet after a Restore and a patch (item 3)."""
    tag = "the menu toggle"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
            page.wait_for_function("() => !!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'", timeout=180000)
            still(page)
            show_tab(page, "machine")
            page.wait_for_timeout(500)
            hits = page.evaluate(TEXT_UNDER_TOGGLE_JS, ["#vclock", "#guest_in", "#entry_info", "#end_banner", "#count_info", "#dbg_state"])
            r[f"390 {scheme}"] = hits[:8]
            page.locator("#end_banner").scroll_into_view_if_needed()
            page.screenshot(path=str(shots / f"sameway-390-end-{scheme}.png"))
            if hits:
                problems.append(f"{tag} 390 {scheme}: over {hits[:8]}")
            if patch_do_exit(page, problems, f"{tag} 390 {scheme}", True) is None:
                continue
            show_tab(page, "machine")
            page.wait_for_timeout(800)
            page.screenshot(path=str(shots / f"sameway-390-sheet-{scheme}.png"))
            got = page.evaluate(ABOVE_SHEET_JS, ["vm_reason", "dbg_continue", "dbg_speed"])
            r[f"390 sheet {scheme}"] = got
            bad = [g for g in got if not g["shown"] or g["covered"]]
            if bad:
                problems.append(f"{tag} 390 {scheme} with the Source sheet: under it or not shown: {bad}")
            page.set_viewport_size({"width": 844, "height": 390})
            page.wait_for_timeout(1000)
            show_tab(page, "machine")
            hits = page.evaluate(TEXT_UNDER_TOGGLE_JS, MACHINE_TEXT)
            r[f"844x390 {scheme}"] = hits[:8]
            page.screenshot(path=str(shots / f"sameway-844x390-{scheme}.png"))
            if hits:
                problems.append(f"{tag} 844x390 {scheme} beside the Source panel: over {hits[:8]}")
        finally:
            ctx.close()
    # A desktop window beside the panel too, Debian's longer numbers.
    ctx = browser.new_context(viewport={"width": 844, "height": 390})
    page = ctx.new_page()
    try:
        # The Source panel open on a file (the page's own link form, ~src=).
        page.goto(f"{base}/index.html?kernel={DEBIAN}&speed=full&count=0#machine~src=init/main.c:1000")
        if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.prompt) && MKB.runState === 'paused'"), 900, 1):
            problems.append(f"{tag}: Debian did not stop at its prompt")
            return
        still(page)
        page.wait_for_function("() => !!document.querySelector('body > .srcpanel:not([hidden])')", timeout=60000)
        show_tab(page, "machine")
        page.wait_for_timeout(800)
        hits = page.evaluate(TEXT_UNDER_TOGGLE_JS, MACHINE_TEXT)
        r["Debian 844x390"] = hits[:8]
        page.screenshot(path=str(shots / "sameway-844x390-debian.png"))
        if hits:
            problems.append(f"{tag} Debian 844x390 beside the Source panel: over {hits[:8]}")
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag} Debian 844x390: the page scrolls sideways")
    finally:
        ctx.close()


def sw_sheet_restored(browser, base, shots, problems, r):
    """390 px, a Restore and a patch (Step back off with its reason, as the
    verifier's phone): the overlay's reason, Continue, Step back's reason and
    the speed picker above the Source sheet (item 3)."""
    tag = "390 px after a Restore and a patch"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            page.goto(f"{base}/index.html?kernel={MINI}&speed=10000")
            page.wait_for_function("() => window.MKB && MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 20000",
                                   timeout=120000)
            show_tab(page, "machine")
            page.locator("#dbg_pause").tap()
            still(page)
            row = snap_save(page, 1, True)["rows"][0]["id"]
            restore(page, row)
            if patch_do_exit(page, problems, f"{tag} {scheme}", True) is None:
                continue
            show_tab(page, "machine")
            page.wait_for_timeout(800)
            page.screenshot(path=str(shots / f"sameway-390-restored-sheet-{scheme}.png"))
            got = page.evaluate(ABOVE_SHEET_JS, ["vm_reason", "dbg_continue", "dbg_back_why", "dbg_speed"])
            r[scheme] = got
            bad = [g for g in got if not g["shown"] or g["covered"]]
            if bad:
                problems.append(f"{tag} {scheme}: under the Source sheet or not shown: {bad}")
        finally:
            ctx.close()


def suite_sameway(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_SAMEWAY_ONLY", "").split(",") if x]
    cases = (("sw_mini_runto", lambda p, r: sw_runto(ctx.browser, ctx.base, MINI, p, r, ffs=(False, True), secs=600)),
             ("sw_jit_runto", lambda p, r: sw_jit_runto(ctx.browser, ctx.base, p, r)),
             ("sw_entry_restored", lambda p, r: sw_entry_restored(ctx.browser, ctx.base, p, r)),
             ("sw_toggle", lambda p, r: sw_toggle(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("sw_sheet_restored", lambda p, r: sw_sheet_restored(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("sw_entry", lambda p, r: sw_entry(ctx.browser, ctx.base, p, r)),
             ("sw_debian_runto", lambda p, r: sw_runto(ctx.browser, ctx.base, DEBIAN, p, r, ffs=(True, False), reload=False)))
    for name, fn in cases:
        if only and name not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("sameway", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
