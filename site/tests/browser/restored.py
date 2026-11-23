"""Browser test suite restored (test_browser.py --suites): what a restored run
reports and does, driven through the page as a user does (plan A3c)."""

import os
import re
import time

import jscov
from tests.browser.common import PHONE, report, show_tab, snap_click, snap_rows, snap_save, wait_for
from tests.browser.hotpatch import hp_continue
from tests.browser.hprun import DEBIAN, back_to, boot, boot_end, patch_do_exit, run_to_end, stages, steps, still
from tests.browser.rebuild_linux import rbl_open

SAVE_AT = 1.40e9
# Everything a run depends on, at a stop.
STATE_JS = """async () => { const st = MKB.stepper, c = st.cpu;
  const d = await crypto.subtle.digest('SHA-256', c.mem8.slice());
  return { steps: st.steps, eip: (c.instruction_pointer[0] >>> 0).toString(16), hlt: c.in_hlt[0],
    ram: Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join('').slice(0, 16),
    regs: Array.from(c.reg32, (x) => (x >>> 0).toString(16)).join(','), eflags: (c.get_eflags() >>> 0).toString(16),
    clock: MKB.CLOCK.now(), irq0: st.irq0, prompt: st.prompt ? st.prompt.steps : null }; }"""
SAME = ("steps", "eip", "hlt", "ram", "regs", "eflags", "clock", "irq0", "prompt")
AT_PROMPT = "() => !!MKB.stepper.prompt && MKB.runState === 'paused' && !MKB.REV.busy"
# The rect of a selector's element, or null when it is not displayed.
RECT_JS = """(s) => { const e = document.querySelector(s); if (!e) return null;
  const r = e.getBoundingClientRect(); return r.width ? [r.left, r.top, r.right, r.bottom] : null; }"""
# With the Rebuild row at the toggle's height: the row's controls under the toggle.
REBUILD_ROW_JS = """() => { const t = document.getElementById('runbadge_toggle'), go = document.getElementById('rb_go');
  go.scrollIntoView({ block: 'center' });
  const g = go.getBoundingClientRect(), b0 = t.getBoundingClientRect();
  window.scrollBy(0, (g.top + g.bottom) / 2 - (b0.top + b0.bottom) / 2);
  const b = t.getBoundingClientRect();
  return [...document.querySelectorAll('#rb_box .scan button, #rb_box .scan label, #rb_box .scan .tip')].filter((e) => {
    const r = e.getBoundingClientRect();
    return r.width && r.left < b.right && b.left < r.right && r.top < b.bottom && b.top < r.bottom;
  }).map((e) => e.id || e.className || e.tagName); }"""
CUT_JS = """() => { const e = document.getElementById('vm_reason');
  return { text: e.textContent, cut: e.scrollHeight > e.clientHeight + 1 }; }"""


def restore(page, row):
    show_tab(page, "snapshots")
    snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row, 120)
    page.evaluate("document.getElementById('snap_status').removeAttribute('data-restored')")
    still(page)


def to_prompt(page, problems, tag):
    hp_continue(page)
    time.sleep(0.5)
    if not wait_for(lambda: page.evaluate(AT_PROMPT), 900, 0.3) or not still(page):
        problems.append(f"{tag}: Continue did not stop at the prompt: {page.evaluate('[MKB.runState, MKB.stepper.steps]')}")
        return None
    return page.evaluate(STATE_JS)


def same_run(got, want, problems, tag):
    for k in SAME:
        if got[k] != want[k]:
            problems.append(f"{tag}: {k} is {got[k]}, in the uninterrupted run {want[k]}")


def rs_debian(browser, base, problems, r):
    """Debian with the default count: a snapshot before the prompt and one a
    few instructions before it, each restored, reach the prompt instruction
    for instruction as the first run did (item 1); the IRQ0 count is the
    snapshot's (item 2); keys typed at the stop reach the shell (item 3)."""
    tag = "Debian restored"
    # Under coverage (site/jscov.py) stepping Debian's boot is about 20 times
    # slower: the page's coverage starts at the pause, before what is tested.
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    try:
        jscov.hold()
        try:
            page = ctx.new_page()
            boot(page, base, DEBIAN)
            passed = wait_for(lambda: page.evaluate(f"!!MKB.stepper && MKB.stepper.steps > {SAVE_AT}"), 1800, 0.05)
            show_tab(page, "machine")
            page.click("#dbg_pause")
            still(page)
        finally:
            jscov.release()
        if not passed:
            problems.append(f"{tag}: the run did not pass {SAVE_AT}")
            return
        saved = page.evaluate(STATE_JS)
        far = snap_save(page, 1, False)["rows"][0]["id"]
        first = to_prompt(page, problems, tag)
        if not first:
            return
        r["prompt"], r["saved at"] = first["steps"], saved["steps"]
        show_tab(page, "debugger")
        page.fill("#dbg_n", "50")
        page.click("#dbg_backn")
        if not wait_for(lambda: steps(page) == first["steps"] - 50, 300, 0.2) or not still(page):
            problems.append(f"{tag}: Step back 50 from the prompt: {steps(page)}")
            return
        near = next(x["id"] for x in snap_save(page, 2, False)["rows"] if x["id"] != far)
        restore(page, far)
        back = page.evaluate(STATE_JS)
        r["IRQ0 restored"] = back["irq0"]
        if back["irq0"] != saved["irq0"]:
            problems.append(f"{tag}: IRQ0 after Restore is {back['irq0']}, at the snapshot {saved['irq0']}")
        again = to_prompt(page, problems, tag + " from the far snapshot")
        if not again:
            return
        r["prompt from the far snapshot"] = again["steps"]
        same_run(again, first, problems, tag + " from the far snapshot")
        # The machine is stopped at the prompt: what is typed now reaches the
        # shell on Continue, and the page says it is waiting.
        show_tab(page, "machine")
        # A corner: the middle of the screen is the overlay's Continue.
        page.click("#screen_container", position={"x": 4, "y": 4})
        # No shifted characters: type() presses no Shift, and v86 reads key codes.
        page.keyboard.type("echo a3c 42\n", delay=40)
        time.sleep(0.5)
        said = page.inner_text("#vm_reason")
        r["said with keys waiting"] = said
        r["state with keys waiting"] = page.evaluate("[MKB.runState, MKB.INPUT.pending.length]")
        if "key" not in said.lower():
            problems.append(f"{tag}: keys typed at the stop, the overlay says {said!r}")
        hp_continue(page)
        # Its output's own row (the command line echoed holds the text too).
        if not wait_for(lambda: "a3c 42" in [x.strip() for x in page.inner_text("#screen_container").splitlines()], 60, 0.5):
            problems.append(f"{tag}: the keys typed at the stop did not reach the shell: {page.inner_text('#screen_container')[-160:]!r}")
        page.click("#dbg_pause")
        still(page)
        restore(page, near)
        again = to_prompt(page, problems, tag + " from the near snapshot")
        if again:
            r["prompt from the near snapshot"] = again["steps"]
            same_run(again, first, problems, tag + " from the near snapshot")
    finally:
        ctx.close()


def known(cells):
    """A row's unique EIP cell: as shown, 'not known' is an honest answer."""
    return cells[1]


def rs_numbers(browser, base, problems, r):
    """After a patched rerun, a restore and a reload, an earlier stage's
    unique EIPs are that stage's or say they are not known (item 2)."""
    tag = "numbers of a restored run"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        first = boot_end(page, base, problems, tag)
        # Twice: the second Back to lands before the line "Hello, World!", so
        # the run on records that stage again.
        if not first or not back_to(page, "con_write", problems, tag) or not back_to(page, "con_write", problems, tag):
            return
        hello = known(first["rows"]["Hello, World!"])
        if patch_do_exit(page, problems, tag) is None:
            return
        row = snap_save(page, 1, False)["rows"][0]["id"]
        if not run_to_end(page, problems, tag):
            return
        for when in ("after the patched rerun", "after a restore", "after a reload"):
            if when == "after a restore":
                restore(page, row)
                if not run_to_end(page, problems, tag + " " + when):
                    return
            if when == "after a reload":
                page.goto(f"{base}/index.html?kernel=minified-linux&speed=full&paused=1")
                page.wait_for_function("() => window.MKB && MKB.stepper && MKB.runState === 'paused'", timeout=120000)
                show_tab(page, "snapshots")
                wait_for(lambda: len(snap_rows(page)["rows"]) == 1, 30)
                restore(page, row)
                if not run_to_end(page, problems, tag + " " + when):
                    return
            got = stages(page)["rows"]
            r[when] = {k: known(v) for k, v in got.items() if len(v) > 1}
            cell = known(got.get("Hello, World!", ["", "missing"]))
            if cell not in (hello, "not known"):
                problems.append(f"{tag}: {when}: Hello's unique EIPs show {cell}, the stage had {hello}")
            for k, v in got.items():
                if len(v) > 1 and known(v).startswith("0/"):
                    problems.append(f"{tag}: {when}: {k} shows {known(v)} unique EIPs")
    finally:
        ctx.close()


def rs_behind(browser, base, problems, r):
    """A snapshot restored while stepped back before a recorded stage: the
    stage is not reached yet there (A3d item 4), and the run ends at the
    first run's count with that stage's numbers or "not known"."""
    tag = "restored while stepped back"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        first = boot_end(page, base, problems, tag)
        if not first:
            return
        end = steps(page)
        if not back_to(page, "con_write", problems, tag) or not back_to(page, "con_write", problems, tag):
            return
        at = steps(page)
        if not page.evaluate("MKB.stepper.future.some((f) => f.key === 'milestone')"):
            problems.append(f"{tag}: no stage is ahead of {at} (test setup)")
            return
        restore(page, snap_save(page, 1, False)["rows"][0]["id"])
        r["at, end"] = [at, end]
        if steps(page) != at:
            problems.append(f"{tag}: restored at {steps(page)}, saved at {at}")
        cells = stages(page)["rows"]["Hello, World!"]
        if cells[0] not in ("not reached", "counting..."):
            problems.append(f"{tag}: restored at {at}, before Hello's line, its row shows {cells}")
        if run_to_end(page, problems, tag):
            if steps(page) != end:
                problems.append(f"{tag}: the run ended at {steps(page)}, the first run at {end}")
            hello = known(stages(page)["rows"]["Hello, World!"])
            if hello not in (known(first["rows"]["Hello, World!"]), "not known"):
                problems.append(f"{tag}: Hello's unique EIPs show {hello}, the stage had {known(first['rows']['Hello, World!'])}")
    finally:
        ctx.close()


def rs_count_off(browser, base, problems, r):
    """The count box switched off at a pause: the line under the table says so."""
    tag = "count switched off"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux")
        page.wait_for_function("() => window.MKB && MKB.stepper && document.getElementById('entry_info').dataset.steps", timeout=120000)
        show_tab(page, "machine")
        page.click("#dbg_pause")
        still(page)
        page.uncheck("#count")
        wait_for(lambda: "Counting switched off" in page.inner_text("#count_info"), 10, 0.1)
        r["line"] = page.inner_text("#count_info")
        if "Counting switched off" not in r["line"]:
            problems.append(f"{tag}: the line says {r['line']!r}")
    finally:
        ctx.close()


def rs_speed_line(browser, base, problems, r):
    """The entry line does not name a speed the machine no longer runs at (item 4)."""
    tag = "entry line"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux")
        page.wait_for_function("() => window.MKB && MKB.stepper && document.getElementById('entry_info').dataset.steps", timeout=120000)
        page.select_option("#dbg_speed", "full")
        page.wait_for_timeout(500)
        line = page.inner_text("#entry_info")
        r["line"] = line
        said = re.search(r"\bnow ([^.]+)\.", line)
        if said and said.group(1) != page.evaluate("document.getElementById('dbg_speed').selectedOptions[0].textContent"):
            problems.append(f"{tag}: at full speed it says {said.group(0)!r}")
    finally:
        ctx.close()


def rs_phone(browser, base, shots, problems, r):
    """390 px: the overlay's text is whole and the toggle covers no control (item 5)."""
    tag = "390 px"
    for scheme in ("light", "dark"):
        ctx = browser.new_context(color_scheme=scheme, **PHONE)
        page = ctx.new_page()
        try:
            for kernel in ("hello", "minified-linux"):
                page.goto(f"{base}/index.html?kernel={kernel}&speed=full")
                if not wait_for(lambda: page.evaluate("!!(window.MKB && MKB.stepper && MKB.stepper.end) && MKB.runState === 'paused'"), 180, 0.3):
                    problems.append(f"{tag} {kernel}: no end")
                    continue
                time.sleep(0.5)
                o = page.evaluate(CUT_JS)
                r[f"{kernel} {scheme}"] = o["text"]
                if o["cut"]:
                    problems.append(f"{tag} {kernel} {scheme}: the overlay's text is cut: {o['text']!r}")
                # The menu's row at the toggle's height, where a user can scroll it.
                page.evaluate("""() => { const k = document.getElementById('kernel').getBoundingClientRect(),
                  b = document.getElementById('runbadge_toggle').getBoundingClientRect();
                  window.scrollBy(0, (k.top + k.bottom) / 2 - (b.top + b.bottom) / 2); }""")
                time.sleep(0.2)
                page.screenshot(path=str(shots / f"restored-{kernel}-390-{scheme}.png"))
                badge, ctl = page.evaluate(RECT_JS, "#runbadge_toggle"), page.evaluate(RECT_JS, "#kernel")
                if not badge or not ctl:
                    problems.append(f"{tag} {kernel} {scheme}: toggle {badge}, Kernel menu {ctl}")
                elif badge[0] < ctl[2] and ctl[0] < badge[2] and badge[1] < ctl[3] and ctl[1] < badge[3]:
                    problems.append(f"{tag} {kernel} {scheme}: the toggle {badge} covers the Kernel menu {ctl}")
                if kernel != "hello":
                    continue
                # The Source sheet open: the toggle sits right above it, at the Rebuild row.
                if not rbl_open(page, "boot/hello.c", True):
                    problems.append(f"{tag} {scheme}: Edit did not open boot/hello.c")
                    continue
                hit = page.evaluate(REBUILD_ROW_JS)
                r[f"Rebuild row {scheme}"] = hit
                page.screenshot(path=str(shots / f"restored-rebuild-390-{scheme}.png"))
                if hit:
                    problems.append(f"{tag} {scheme}: the toggle covers the Rebuild row: {hit}")
        finally:
            ctx.close()


def rs_rebuild_race(browser, base, problems, r):
    """A kernel's load still running when a Rebuild boots: its late files do
    not replace the rebuilt kernel (item 7)."""
    tag = "load overtaken by Rebuild"
    # No service worker: its fetches do not pass through page.route.
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, service_workers="block")
    page = ctx.new_page()
    held = []
    try:
        page.goto(f"{base}/index.html?kernel=hello&speed=full")
        page.wait_for_function("() => window.MKB && MKB.image && MKB.image.name === 'hello' && MKB.stepper && MKB.stepper.end", timeout=120000)
        show_tab(page, "source")
        page.wait_for_function("() => { const b = document.getElementById('rb_go'); return b && !b.disabled; }", timeout=120000)
        page.route("**/kernels/minified-linux/disasm.json*", lambda route: held.append(route))
        show_tab(page, "machine")
        page.select_option("#kernel", "minified-linux")
        # Playwright delivers the held request while it waits.
        if not wait_for(lambda: page.wait_for_timeout(100) or bool(held), 30, 0.1):
            problems.append(f"{tag}: the load never asked for its disassembly: menu {page.input_value('#kernel')!r}, "
                            f"image {page.evaluate('MKB.image.name')!r}, status {page.inner_text('#status')[:160]!r}")
            return
        show_tab(page, "source")
        page.click("#rb_go")
        if not wait_for(lambda: page.evaluate("!!MKB.image && !!MKB.image.from && !!MKB.stepper && !!MKB.stepper.end"), 300, 0.3):
            problems.append(f"{tag}: the rebuild did not boot: {page.inner_text('#rb_status')[:200]!r}")
            return
        built = page.evaluate("MKB.image.name")
        for route in held:
            route.continue_()
        page.wait_for_timeout(5000)
        now = page.evaluate("MKB.image.name")
        r["built, after the late load"] = [built, now]
        if now != built:
            problems.append(f"{tag}: the late load replaced {built} with {now}")
    finally:
        ctx.close()


def suite_restored(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_RESTORED_ONLY", "").split(",") if x]
    cases = (("rs_speed_line", lambda p, r: rs_speed_line(ctx.browser, ctx.base, p, r)),
             ("rs_phone", lambda p, r: rs_phone(ctx.browser, ctx.base, ctx.shots, p, r)),
             ("rs_rebuild_race", lambda p, r: rs_rebuild_race(ctx.browser, ctx.base, p, r)),
             ("rs_numbers", lambda p, r: rs_numbers(ctx.browser, ctx.base, p, r)),
             ("rs_behind", lambda p, r: rs_behind(ctx.browser, ctx.base, p, r)),
             ("rs_count_off", lambda p, r: rs_count_off(ctx.browser, ctx.base, p, r)),
             ("rs_debian", lambda p, r: rs_debian(ctx.browser, ctx.base, p, r)))
    for name, fn in cases:
        if only and name not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("restored", problems, r, case=name, seconds=round(time.monotonic() - t0))
    return failed
