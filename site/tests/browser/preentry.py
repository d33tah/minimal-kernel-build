"""Browser test suite preentry (test_browser.py --suites): a pause before the
kernel entry, what a restored run shows, driven through the page as a user
does (plan A3d)."""

import os
import re
import time

import jscov
from tests.browser.common import PHONE, report, show_tab, snap_click, snap_rows, snap_save, wait_for
from tests.browser.hotpatch import hp_continue
from tests.browser.hprun import DEBIAN, MINI, patch_do_exit, run_to_end, stages, steps, still
from tests.browser.restored import AT_PROMPT, STATE_JS, restore, same_run

SAVE_AT = 7e8     # after the kernel entry
PAUSE_AT = 1e8    # inside the fast-forward to the entry
# The "now" row of the counters table: from 1 MiB, unique EIPs, from reset.
NOW_JS = """() => { const tr = [...document.querySelectorAll('#counters tbody tr')].find((x) => x.children[0].textContent.trim() === 'now');
  return [...tr.children].slice(1).map((x) => x.textContent.trim()); }"""
# The parts of el's text under the run badge's toggle, at every scroll offset.
UNDER_BADGE_JS = """async (sel) => { const el = document.querySelector(sel), t = document.getElementById('runbadge_toggle');
  if (!el || !el.textContent) return null;
  const sc = document.scrollingElement, keep = sc.scrollTop, r = document.createRange(), hits = [];
  r.selectNodeContents(el);
  for (let y = 0; y <= sc.scrollHeight - sc.clientHeight + 19; y += 20) {
    sc.scrollTop = y;
    await new Promise((f) => requestAnimationFrame(() => f()));
    const b = t.getBoundingClientRect();
    for (const q of r.getClientRects())
      if (q.width && q.left < b.right && b.left < q.right && q.top < b.bottom && b.top < q.bottom) { hits.push(Math.round(sc.scrollTop)); break; }
  }
  sc.scrollTop = keep;
  return hits; }"""


def now_row(page):
    show_tab(page, "machine")
    return page.evaluate(NOW_JS)


def number(cell):
    return int(cell.replace(",", "")) if re.fullmatch(r"[\d,]+", cell) else None


def open_full(page, base, kernel):
    """The page, the Kernel menu, speed full, Boot: the clicks of a by-hand run."""
    page.goto(f"{base}/index.html")
    page.wait_for_function("() => document.querySelectorAll('#kernel option').length > 3", timeout=120000)
    page.select_option("#kernel", kernel)
    page.wait_for_function("(n) => window.MKB && MKB.image && MKB.image.name === n && MKB.stepper", arg=kernel, timeout=300000)
    page.select_option("#dbg_speed", "full")
    page.wait_for_timeout(500)
    page.click("#boot")


def to_prompt(page, problems, tag):
    hp_continue(page)
    time.sleep(0.5)
    if not wait_for(lambda: page.evaluate(AT_PROMPT), 1500, 0.3) or not still(page):
        problems.append(f"{tag}: Continue did not stop at the prompt: {page.evaluate('[MKB.runState, MKB.stepper.steps]')}")
        return None
    return page.evaluate(STATE_JS)


def pause_in_ff(page, problems, tag):
    """Pause pressed while the JIT runs the way to the kernel entry."""
    if not wait_for(lambda: page.evaluate(f"!!MKB.stepper && MKB.stepper.steps > {PAUSE_AT}"), 300, 0.02):
        problems.append(f"{tag}: the boot did not pass {PAUSE_AT}")
        return False
    show_tab(page, "machine")
    page.click("#dbg_pause")
    if not still(page):
        problems.append(f"{tag}: Pause did not stop the machine")
        return False
    return True


def pe_debian(browser, base, problems, r):
    """Debian at speed full: the prompt is the same instruction, registers,
    RAM, clock and IRQ0 count after a pause before the entry; Save there
    does not make a snapshot that replays differently; "from 1 MiB" is true
    before the entry and after a reload and Restore (items 1, 2, 6)."""
    tag = "Debian"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    try:
        jscov.hold()
        try:
            page = ctx.new_page()
            open_full(page, base, DEBIAN)
            if not wait_for(lambda: page.evaluate(f"!!MKB.stepper && MKB.stepper.steps > {SAVE_AT} && MKB.runState === 'stepped'"), 1800, 0.1):
                problems.append(f"{tag}: the run did not pass {SAVE_AT}")
                return
            said = page.inner_text("#dbg_state")
            r["state while it counts"] = said
            show_tab(page, "machine")
            wait_for(lambda: "takes over" in page.inner_text("#count_info"), 5, 0.1)
            info = page.inner_text("#count_info")
            r["line while it counts"] = info
            if "stepping (counting)" in said or "the JIT takes over" not in info:
                problems.append(f"{tag}: at speed full the page says only {said!r} and {info!r}")
            page.click("#dbg_pause")
            still(page)
        finally:
            jscov.release()
        at = page.evaluate(STATE_JS)
        row = snap_save(page, 1, False)["rows"][0]["id"]
        first = to_prompt(page, problems, tag)
        if not first:
            return
        entry = first["steps"] - number(now_row(page)[0])
        r["prompt"], r["entry"], r["saved at"] = first["steps"], entry, at["steps"]
        jscov.hold()
        try:
            show_tab(page, "machine")
            page.click("#boot")
            page.wait_for_function("(n) => !!MKB.stepper && MKB.stepper.steps < n", arg=first["steps"], timeout=120000)
            paused = pause_in_ff(page, problems, tag + " paused before the entry")
        finally:
            jscov.release()
        if not paused:
            return
        here = steps(page)
        r["paused at"] = here
        if here >= entry:
            problems.append(f"{tag}: the pause landed at {here}, past the entry {entry} (test setup)")
            return
        cell = now_row(page)[0]
        r["from 1 MiB before the entry"] = cell
        if re.search(r"\d", cell):
            problems.append(f"{tag}: before the entry (at {here}) the page shows {cell} instructions from the kernel entry")
        before = len(snap_rows(page)["rows"])
        show_tab(page, "snapshots")
        said0 = page.evaluate("document.getElementById('snap_status').textContent")
        snap_click(page, "#snap_save", False)
        wait_for(lambda: page.evaluate("document.getElementById('snap_status').textContent") not in (said0, "Saving..."), 60)
        said = page.inner_text("#snap_status")
        r["Save before the entry"] = said
        if len(snap_rows(page)["rows"]) != before or "kernel entry" not in said:
            problems.append(f"{tag}: Save at {here}, before the entry, made a snapshot that cannot replay this run: {said!r}")
        jscov.hold()
        try:
            again = to_prompt(page, problems, tag + " after a pause before the entry")
        finally:
            jscov.release()
        if not again:
            return
        same_run(again, first, problems, tag + " after a pause before the entry")
        if now_row(page)[0] != f"{first['steps'] - entry:,}":
            problems.append(f"{tag}: after a pause before the entry, from 1 MiB at the prompt is {now_row(page)[0]}")
        # A reload, the kernel picked, Restore while its boot still runs.
        page.reload()
        page.wait_for_function("() => document.querySelectorAll('#kernel option').length > 3", timeout=120000)
        show_tab(page, "machine")
        if page.input_value("#kernel") != DEBIAN:
            page.select_option("#kernel", DEBIAN)
        page.wait_for_function("(n) => window.MKB && MKB.image && MKB.image.name === n && MKB.stepper", arg=DEBIAN, timeout=300000)
        show_tab(page, "snapshots")
        wait_for(lambda: any(x["id"] == row and x["kernel"] == "match" for x in snap_rows(page)["rows"]), 120)
        restore(page, row)
        cell = now_row(page)[0]
        r["from 1 MiB, restored after a reload"] = cell
        if number(cell) != at["steps"] - entry:
            problems.append(f"{tag}: restored at {at['steps']} after a reload, from 1 MiB shows {cell}, not {at['steps'] - entry:,}")
        jscov.hold()
        try:
            again = to_prompt(page, problems, tag + " restored after a reload")
        finally:
            jscov.release()
        if not again:
            return
        same_run(again, first, problems, tag + " restored after a reload")
        if number(now_row(page)[0]) != first["steps"] - entry:
            problems.append(f"{tag}: restored after a reload, from 1 MiB at the prompt is {now_row(page)[0]}")
    finally:
        ctx.close()


def pe_overlay(browser, base, problems, r):
    """Keys typed at the prompt's stop: the overlay's reason and its notice
    about the keys are apart (item 5)."""
    tag = "overlay"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    try:
        jscov.hold()
        try:
            page = ctx.new_page()
            open_full(page, base, DEBIAN)
            if not wait_for(lambda: page.evaluate(f"!!MKB.stepper && ({AT_PROMPT})()"), 1800, 0.3) or not still(page):
                problems.append(f"{tag}: no stop at the prompt")
                return
        finally:
            jscov.release()
        show_tab(page, "machine")
        page.click("#screen_container", position={"x": 4, "y": 4})
        page.keyboard.type("a", delay=40)
        wait_for(lambda: "Typed keys" in page.inner_text("#vm_reason"), 10, 0.1)
        said = page.inner_text("#vm_reason")
        r["said"] = said
        if not re.search(r"prompt(\.\s|\s*\n\s*)Typed keys", said):
            problems.append(f"{tag}: the reason and the keys' notice run together: {said!r}")
    finally:
        ctx.close()


def pe_ff_resume(browser, base, problems, r):
    """Pause inside the fast-forward and Continue: the page goes on to the
    kernel entry and says so (with coverage on: only the JIT's way there)."""
    tag = "Continue inside the fast-forward"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        open_full(page, base, DEBIAN)
        if not pause_in_ff(page, problems, tag):
            return
        r["paused at"] = steps(page)
        hp_continue(page)
        if not wait_for(lambda: "Entered the kernel" in page.inner_text("#entry_info"), 300, 0.2):
            problems.append(f"{tag}: no entry: {page.inner_text('#entry_info')!r}, {page.evaluate('MKB.stepper.steps')}")
        r["line"] = page.inner_text("#entry_info")
        page.click("#dbg_pause")
        still(page)
    finally:
        ctx.close()


def pe_switch(browser, base, problems, r):
    """Switch to on a snapshot's row loads a kernel booted with its initrd (item 3)."""
    tag = "Switch to"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={DEBIAN}&paused=1")
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
        said = page.inner_text("#snap_status")
        r["status"], r["kernel"] = said, page.evaluate("MKB.image.name")
        if not ok or r["kernel"] != DEBIAN or "not one of" in said:
            problems.append(f"{tag}: kernel {r['kernel']}, status {said!r}")
    finally:
        ctx.close()


def pe_stages(browser, base, problems, r):
    """A stage after the restored point is not reached yet (item 4), also
    after a patched rerun; Step back right after the patch works or the page
    says why not (item 7)."""
    tag = "stages after a restore"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel={MINI}&speed=10000")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.stepper.sinceEntry >= 0 && MKB.stepper.steps > MKB.stepper.sinceEntry + 20000", timeout=120000)
        show_tab(page, "machine")
        page.click("#dbg_pause")
        still(page)
        at = steps(page)
        row = snap_save(page, 1, False)["rows"][0]["id"]
        show_tab(page, "machine")
        page.select_option("#dbg_speed", "full")
        if not run_to_end(page, problems, tag):
            return
        for when in ("restored", "restored again"):
            restore(page, row)
            got = stages(page)["rows"]
            r[when] = {k: got[k] for k in ("Hello, World!", "Still alive")}
            for k in ("Hello, World!", "Still alive"):
                # "counting..." with the count on: not reached yet, as a boot shows it.
                if got[k][0] not in ("not reached", "counting..."):
                    problems.append(f"{tag}: {when} at {at}: {k} shows {got[k]}")
            if when == "restored":
                if not run_to_end(page, problems, tag):
                    return
        if patch_do_exit(page, problems, tag) is None:
            return
        show_tab(page, "machine")
        off = page.is_disabled("#dbg_back")
        why = page.inner_text("#dbg_back_why") if page.locator("#dbg_back_why").count() else ""
        r["Step back after the patch"] = {"disabled": off, "says": why}
        if off and "snapshot" not in why:
            problems.append(f"{tag}: Step back is disabled after the patch and the page says {why!r}")
        # What the page says is true: one Step, and Step back works.
        here = steps(page)
        page.click("#dbg_step")
        if not wait_for(lambda: steps(page) > here and page.is_enabled("#dbg_back"), 30, 0.1):
            problems.append(f"{tag}: after a Step from {here}, Step back is still disabled")
        else:
            page.click("#dbg_back")
            if not wait_for(lambda: steps(page) == here, 60, 0.1) or not still(page):
                problems.append(f"{tag}: Step back after the Step: at {steps(page)}, not {here}")
        end = run_to_end(page, problems, tag + " patched")
        if end:
            r["patched rerun"] = {k: end["rows"][k] for k in ("Hello, World!", "Still alive")}
            for k in ("Hello, World!", "Still alive"):
                if end["rows"][k][1] != "not known":
                    problems.append(f"{tag}: after the patched rerun {k} shows {end['rows'][k]}")
    finally:
        ctx.close()


def pe_badge(browser, base, shots, problems, r):
    """390 px: the toggle covers no line of the Source tab's help (item 8)."""
    tag = "badge at 390 px"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            page.goto(f"{base}/index.html?kernel={MINI}&speed=full")
            if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'"), 180, 0.3):
                problems.append(f"{tag} {scheme}: no end")
                continue
            show_tab(page, "source")
            page.wait_for_selector("#covtree_info:not(:empty)", timeout=120000)
            hits = page.evaluate(UNDER_BADGE_JS, "#covtree_info")
            r[scheme] = hits[:5] if hits else hits
            page.screenshot(path=str(shots / f"preentry-source-390-{scheme}.png"))
            if hits is None or hits:
                problems.append(f"{tag} {scheme}: the toggle covers the help text at scroll offsets {hits}")
        finally:
            ctx.close()


def suite_preentry(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_PREENTRY_ONLY", "").split(",") if x]
    cases = (("pe_switch", lambda p, r: pe_switch(ctx.browser, ctx.base, p, r)),
             ("pe_stages", lambda p, r: pe_stages(ctx.browser, ctx.base, p, r)),
             ("pe_badge", lambda p, r: pe_badge(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("pe_ff_resume", lambda p, r: pe_ff_resume(ctx.browser, ctx.base, p, r)),
             ("pe_overlay", lambda p, r: pe_overlay(ctx.browser, ctx.base, p, r)),
             ("pe_debian", lambda p, r: pe_debian(ctx.browser, ctx.base, p, r)))
    for name, fn in cases:
        if only and name not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("preentry", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
