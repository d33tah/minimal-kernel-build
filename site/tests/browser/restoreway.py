"""Browser test suite restoreway (test_browser.py --suites): a restored run
goes the way its run went, and the Machine tab and the Diff around it at
390 px and after a resize, driven through the page as a user does (plan A3g)."""

import os
import time

from tests.browser.common import PHONE, report, show_tab, snap_click, snap_rows, snap_save, wait_for
from tests.browser.hotpatch import hp_continue
from tests.browser.hprun import DEBIAN, MINI, patch_do_exit, still, steps
from tests.browser.preentry import STOPPED_JS
from tests.browser.restored import STATE_JS, restore, same_run

# Visible above the docked Source sheet (a phone: the sheet is the lower 65vh).
ABOVE_SHEET_JS = """(ids) => { const sheet = document.querySelector('body > .srcpanel:not([hidden])');
  const top = sheet ? sheet.getBoundingClientRect().top : innerHeight;
  return ids.map((id) => { const e = document.getElementById(id); if (!e || !e.getClientRects().length || !e.textContent.trim()) return { id, shown: false };
    const r = e.getBoundingClientRect(); return { id, shown: true, top: Math.round(r.top), bottom: Math.round(r.bottom), sheet: Math.round(top),
      covered: r.top < 0 || r.bottom > top + 1 }; }); }"""
# The menu toggle over one of these elements, at any scroll offset: [selector, offset].
UNDER_TOGGLE_JS = """async (sels) => { const t = document.getElementById('runbadge_toggle'), sc = document.scrollingElement, keep = sc.scrollTop, hits = [];
  for (let y = 0; y <= sc.scrollHeight - sc.clientHeight + 19; y += 20) {
    sc.scrollTop = y;
    await new Promise((f) => requestAnimationFrame(() => f()));
    const b = t.getBoundingClientRect();
    for (const s of sels) { const e = document.querySelector(s); if (!e || !e.getClientRects().length) continue;
      const q = e.getBoundingClientRect();
      if (q.width && q.left < b.right && b.left < q.right && q.top < b.bottom && b.top < q.bottom) hits.push([s, Math.round(sc.scrollTop)]); } }
  sc.scrollTop = keep;
  return hits; }"""
RUN_CONTROLS = ["#dbg_pause", "#dbg_continue", "#dbg_step", "#dbg_back", "#dbg_speed", "#clock_mips", "#count"]
# The Diff's pickers: the picked name, whether the select cuts it, and the
# whole name written out under it (shown, not cut, inside the window).
PICKS_JS = """() => ['snap_a', 'snap_b'].map((id) => { const s = document.getElementById(id), cv = document.createElement('canvas').getContext('2d');
  cv.font = getComputedStyle(s).font; const t = s.options[s.selectedIndex].text, f = document.getElementById(id + '_full');
  const fr = f && f.getClientRects().length ? f.getBoundingClientRect() : null;
  return { id, text: t, cut: cv.measureText(t).width > s.clientWidth - 28,
    full: fr ? f.textContent : '', fullWhole: !!fr && f.scrollWidth <= f.clientWidth + 1 && fr.left >= 0 && fr.right <= innerWidth + 1 }; })"""
LONG_NAME = "before __list_del_entry_valid+0x21, zswap loaded, second try"
# The oracle for the IRQ0 count (item 2): every device_raise_irq(0), with the
# instruction count it came at (the JIT slice's own count added). A raise at
# or below a count already logged means the machine went back (a restored
# state) and the raises past it are not in this run any more.
RAISES_JS = """(() => { const log = []; window.__raises = log; let hooked = null;
  const tick = () => { const st = window.MKB && MKB.stepper;
    if (st && st !== hooked) { hooked = st; log.length = 0; log.before = st.irq0; const cpu = st.cpu, f = cpu.device_raise_irq;
      cpu.device_raise_irq = (irq) => { if (irq === 0) { const x = MKB.CLOCK.extraSteps; log.push(st.steps + (x ? x() : 0)); } f(irq); }; }
    setTimeout(tick, 20); };
  tick(); })();"""
TRUE_IRQ0_JS = """() => { const log = window.__raises, kept = [];
  for (const c of log) { while (kept.length && kept[kept.length - 1] >= c) kept.pop(); kept.push(c); }
  return { page: MKB.stepper.irq0, raised: log.before + kept.length, logged: log.length, line: document.getElementById('vclock').textContent }; }"""


def stopped(page, secs):
    """Continue, then the run's stop (its end or the prompt)."""
    hp_continue(page)
    time.sleep(0.5)
    if not wait_for(lambda: page.evaluate(STOPPED_JS), secs, 0.3) or not still(page):
        return None
    return page.evaluate(STATE_JS)


def rw_runto(browser, base, kernel, problems, r, tries=2):
    """Count off, full speed: Run to start_kernel from the reset vector (the
    page steps there), Save, Continue (the JIT) to the stop. Restore + Continue
    must reach the same stop with the same machine, every time (item 1)."""
    tag = f"{kernel}: Run to with the count off, Save, Restore"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={kernel}&paused=1&speed=full")
        page.wait_for_function("(n) => window.MKB && MKB.stepper && MKB.image && MKB.image.name === n && MKB.runState === 'paused'",
                               arg=kernel, timeout=300000)
        show_tab(page, "machine")
        page.uncheck("#count")
        show_tab(page, "debugger")
        page.fill("#dbg_addr", "start_kernel")
        page.click("#dbg_runto")
        if not wait_for(lambda: page.evaluate("MKB.runState === 'paused' && MKB.stepper.steps > 1e6"), 900, 0.3) or not still(page):
            problems.append(f"{tag}: Run to start_kernel did not stop: {page.evaluate('[MKB.runState, MKB.stepper.steps]')}")
            return
        at = steps(page)
        r["saved at"] = at
        row = snap_save(page, 1, False)["rows"][0]["id"]
        first = stopped(page, 1500)
        if not first:
            problems.append(f"{tag}: Continue did not stop")
            return
        r["uninterrupted"] = {k: first[k] for k in ("steps", "ram", "irq0")}
        for i in range(tries):
            restore(page, row)
            if steps(page) != at:
                problems.append(f"{tag}: restored at {steps(page)}, saved at {at}")
                return
            again = stopped(page, 1500)
            if not again:
                problems.append(f"{tag}: try {i + 1}: Continue after the Restore did not stop")
                return
            r[f"restored {i + 1}"] = {k: again[k] for k in ("steps", "ram", "irq0")}
            same_run(again, first, problems, f"{tag}: try {i + 1}")
    finally:
        ctx.close()


def rw_irq0_ff(browser, base, problems, r):
    """Debian with the fast-forward to the entry, to its prompt: the IRQ0
    count the page shows is the raises of this run, none counted twice where
    the fast-forward goes back to its last slice's start (item 2)."""
    tag = "IRQ0 count of a fast-forward boot"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    ctx.add_init_script(RAISES_JS)
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={DEBIAN}&speed=full")
        if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.prompt) && MKB.runState === 'paused'"), 3600, 1) \
                or not still(page):
            problems.append(f"{tag}: no prompt")
            return
        got = page.evaluate(TRUE_IRQ0_JS)
        r["at the prompt"] = got
        if got["page"] != got["raised"] or f"IRQ0 ×{got['raised']:,}" not in got["line"]:
            problems.append(f"{tag}: the page counts {got['page']:,} ({got['line']!r}), the PIT raised IRQ0 {got['raised']:,} times in this run")
    finally:
        ctx.close()


def rw_old_records(browser, base, problems, r):
    """Records as older pages kept them (item 2 and item 1's old rule): one
    from before A3b/A3c (no endAhead, no IRQ0 count) says the count is not
    known after Restore and still runs to the end recorded ahead; one of a
    boot without the fast-forward (jitBeforeEntry false, no irq0Exact) shows
    its own count."""
    tag = "older records"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={MINI}&speed=10000")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 20000",
                               timeout=120000)
        show_tab(page, "machine")
        page.click("#dbg_pause")
        still(page)
        a = snap_save(page, 1, False)["rows"][0]["id"]
        b = [x["id"] for x in snap_save(page, 2, False)["rows"] if x["id"] != a][0]
        # Made older in this page's list, as those pages wrote them.
        kept = page.evaluate("""([a, b]) => { const sa = MKB.snapById(a).meta.stop, sb = MKB.snapById(b).meta.stop;
          delete sa.endAhead; delete sa.irq0; delete sa.irq0Exact; delete sb.irq0Exact;
          return { irq0: sb.irq0, jitBeforeEntry: sb.jitBeforeEntry }; }""", [a, b])
        r["second record"] = kept
        if kept["jitBeforeEntry"] is not False:
            problems.append(f"{tag}: the minified Linux's boot ran the JIT before its entry: {kept}")
        show_tab(page, "machine")
        page.select_option("#dbg_speed", "full")
        first = stopped(page, 600)
        if not first:
            problems.append(f"{tag}: no end")
            return
        restore(page, a)
        show_tab(page, "machine")
        line = page.inner_text("#vclock")
        r["before A3b, restored"] = line
        if "IRQ0 count not known" not in line:
            problems.append(f"{tag}: a record without an IRQ0 count, restored: {line!r}")
        again = stopped(page, 600)
        r["before A3b, end"] = again and again["steps"]
        if not again or again["steps"] != first["steps"] or again["ram"] != first["ram"]:
            problems.append(f"{tag}: a record without endAhead, restored, ended {again and again['steps']}, the run {first['steps']}")
        restore(page, b)
        show_tab(page, "machine")
        line = page.inner_text("#vclock")
        r["no fast-forward, restored"] = line
        if f"IRQ0 ×{kept['irq0']:,}" not in line:
            problems.append(f"{tag}: a record of a boot without the fast-forward, restored: {line!r}, saved IRQ0 {kept['irq0']}")
    finally:
        ctx.close()


def rw_phone_diff(browser, base, shots, problems, r):
    """390 px, light and dark: a long snapshot name in the Diff's pickers is
    shown whole (item 3)."""
    tag = "Diff pickers at 390 px"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
            if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'"), 180, 0.3):
                problems.append(f"{tag} {scheme}: no end")
                continue
            rows = snap_save(page, 1, True)["rows"]
            snap_save(page, 2, True)
            # Tap the first one's name and type a long one, as a user renames it.
            snap_click(page, f"#snapshots tr[data-id='{rows[0]['id']}'] .snapname", True)
            page.wait_for_selector("#snapshots .snapedit", timeout=10000)
            page.fill("#snapshots .snapedit", LONG_NAME)
            page.keyboard.press("Enter")
            wait_for(lambda: LONG_NAME in [x["name"] for x in snap_rows(page)["rows"]], 10, 0.2)
            page.select_option("#snap_a", rows[0]["id"])
            page.select_option("#snap_b", rows[0]["id"])
            page.wait_for_timeout(300)
            picks = page.evaluate(PICKS_JS)
            r[scheme] = picks
            page.locator("#snap_diffbox").scroll_into_view_if_needed()
            page.screenshot(path=str(shots / f"restoreway-diff-390-{scheme}.png"))
            for p in picks:
                if p["cut"] and (p["full"] != p["text"] or not p["fullWhole"]):
                    problems.append(f"{tag} {scheme}: {p['id']} cuts {p['text']!r} and does not show it whole elsewhere: {p}")
        finally:
            ctx.close()


def rw_resize(browser, base, shots, problems, r):
    """A desktop window with the Source panel open, after a Restore and a
    patch (Step back off, with its reason): resized to 390 px, the overlay's
    reason, the run buttons and Step back's reason are above the Source sheet
    (items 4, 5); at 844x390 the toggle covers none of the run controls. Also
    a phone, patched upright, turned sideways and back."""
    tag = "resized with the Source panel open"
    for scheme, phone in (("light", False), ("dark", False), ("light", True), ("dark", True)):
        how = "phone" if phone else "desktop"
        ctx = browser.new_context(color_scheme=scheme, **PHONE) if phone else \
            browser.new_context(viewport={"width": 1280, "height": 900}, color_scheme=scheme)
        page = ctx.new_page()
        try:
            if phone:
                # As the verifier's phone: the boot to its end, then a patch
                # (Step back on, no reason under it: the speed picker comes
                # up beside the run buttons).
                page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
                page.wait_for_function("() => !!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'", timeout=180000)
                still(page)
            else:
                page.goto(f"{base}/index.html?kernel={MINI}&speed=10000")
                page.wait_for_function("() => window.MKB && MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 20000",
                                       timeout=120000)
                show_tab(page, "machine")
                page.click("#dbg_pause")
                still(page)
                row = snap_save(page, 1, False)["rows"][0]["id"]
                restore(page, row)
            show_tab(page, "machine")
            if patch_do_exit(page, problems, f"{tag} {how} {scheme}", phone) is None:
                continue
            show_tab(page, "machine")
            page.wait_for_timeout(800)
            watch = ["vm_reason", "dbg_continue", "dbg_back"]
            if not phone:
                back_off = page.is_disabled("#dbg_back")
                r[f"{how} {scheme} Step back off"] = back_off
                if not back_off:
                    problems.append(f"{tag} {how} {scheme}: Step back is on right after a Restore and a patch")
                watch.append("dbg_back_why")
            sizes = ((844, 390, "844x390"), (390, 844, "390 again")) if phone else \
                ((390, 844, "390"), (844, 390, "844x390"), (390, 844, "390 again"))
            for w, h, name in sizes:
                page.set_viewport_size({"width": w, "height": h})
                page.wait_for_timeout(1000)
                key = f"{how} {scheme} {name}"
                page.screenshot(path=str(shots / f"restoreway-{how}-{name.replace(' ', '-')}-{scheme}.png"))
                if w == 390:
                    got = page.evaluate(ABOVE_SHEET_JS, watch)
                    r[key] = got
                    bad = [g for g in got if not g["shown"] or g["covered"]]
                    if bad:
                        problems.append(f"{tag} {key}: under the Source sheet or not shown: {bad}")
                else:
                    hits = page.evaluate(UNDER_TOGGLE_JS, RUN_CONTROLS)
                    r[key] = hits[:6]
                    if hits:
                        problems.append(f"{tag} {key}: the menu toggle covers run controls at scroll offsets {hits[:6]}")
        finally:
            ctx.close()


def suite_restoreway(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_RESTOREWAY_ONLY", "").split(",") if x]
    cases = (("rw_mini_runto", lambda p, r: rw_runto(ctx.browser, ctx.base, MINI, p, r)),
             ("rw_old_records", lambda p, r: rw_old_records(ctx.browser, ctx.base, p, r)),
             ("rw_phone_diff", lambda p, r: rw_phone_diff(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("rw_resize", lambda p, r: rw_resize(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("rw_irq0_ff", lambda p, r: rw_irq0_ff(ctx.browser, ctx.base, p, r)),
             ("rw_debian_runto", lambda p, r: rw_runto(ctx.browser, ctx.base, DEBIAN, p, r, tries=1)))
    for name, fn in cases:
        if only and name not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("restoreway", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
