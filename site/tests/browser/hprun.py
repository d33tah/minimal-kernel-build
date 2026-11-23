"""Browser test suite hprun (test_browser.py --suites): a hot patch inside a
run that was stepped back, driven through the page as a user does (plan A3b)."""

import os
import time

from tests.browser.common import report, show_tab, snap_click, snap_rows, snap_save, wait_for
from tests.browser.hotpatch import HP_DO_EXIT, SYM_JS, hp_continue, hp_edit, hp_mem, hp_state, hp_step_over, hp_wait, screen
from tests.browser.rebuild_linux import rbl_open

MINI = "minified-linux"
DEBIAN = "debian-6.1-686-clang"
PATCHED = "Hot patched do_exit"
STILL_JS = "() => MKB.runState === 'paused' && !(MKB.REV && MKB.REV.busy)"
# The counters table as the page shows it: {row label: [its cells]}.
STAGES_JS = """() => { const out = {};
  for (const tr of document.querySelectorAll('#counters tbody tr')) {
    const c = [...tr.children].map((x) => x.textContent.trim());
    if (c[0] !== 'now') out[c[0]] = c.slice(1);
  }
  const e = document.getElementById('end_banner');
  return { rows: out, kind: e.hidden ? '' : e.dataset.kind, text: e.hidden ? '' : e.textContent }; }"""


def stages(page):
    show_tab(page, "machine")
    return page.evaluate(STAGES_JS)


def steps(page):
    return page.evaluate("MKB.stepper.steps")


def still(page, timeout=300):
    """Paused, no reverse operation running, the count the same for 0.5 s."""
    last = [None]

    def same():
        now = page.evaluate("[MKB.runState, MKB.REV.busy, MKB.stepper.steps]")
        ok, last[0] = now == last[0] and now[0] == "paused" and not now[1], now
        return ok
    return wait_for(same, timeout, 0.5)


def boot(page, base, kernel):
    """The page as it opens; the Kernel menu; speed full (the count stays on)."""
    page.goto(f"{base}/index.html")
    page.wait_for_function("() => document.querySelectorAll('#kernel option').length > 3", timeout=120000)
    page.select_option("#kernel", kernel)
    # A user's pace: the new kernel starts at the page's default speed.
    page.wait_for_timeout(800)
    page.select_option("#dbg_speed", "full")
    page.wait_for_function("(n) => MKB.image && MKB.image.name === n", arg=kernel, timeout=300000)


def boot_end(page, base, problems, tag):
    boot(page, base, MINI)
    if not wait_for(lambda: stages(page)["kind"], 300, 0.5) or not still(page):
        problems.append(f"{tag}: no end of the boot: {stages(page)}")
        return None
    return stages(page)


def back_to(page, name, problems, tag):
    show_tab(page, "debugger")
    at = steps(page)
    page.fill("#dbg_addr", name)
    page.click("#dbg_backto")
    if not wait_for(lambda: steps(page) < at and page.evaluate(STILL_JS), 300, 0.2) or not still(page):
        problems.append(f"{tag}: Back to {name} from {at}: {page.inner_text('#dbg_status')[:200]!r}")
        return False
    return True


def run_to_end(page, problems, tag):
    hp_continue(page)
    time.sleep(0.5)
    if not wait_for(lambda: stages(page)["kind"] and page.evaluate(STILL_JS), 300, 0.3) or not still(page):
        problems.append(f"{tag}: Continue did not reach the end: {stages(page)}")
        return None
    return stages(page)


def patch_do_exit(page, problems, tag):
    """kernel/exit.c edited in the Source panel and patched in; do_exit's address."""
    if not rbl_open(page, "kernel/exit.c", False):
        problems.append(f"{tag}: Edit did not open kernel/exit.c")
        return None
    text = page.input_value("#ed_text")
    hp_edit(page, text.replace('\tpanic("Attempted to kill init!', HP_DO_EXIT + '\tpanic("Attempted to kill init!', 1))
    st = hp_wait(page, ("ready", "refused", ""), 600)
    if st["state"] != "ready":
        problems.append(f"{tag}: the edit is not ready: {st['state']} {st['status']!r}")
        return None
    page.click("#hp_go_panel")
    st = hp_wait(page, ("done", "failed"), 120)
    if st["state"] != "done":
        problems.append(f"{tag}: the patch: {st['status']!r}")
        return None
    return page.evaluate(SYM_JS, "do_exit")[1]


def patched_end(page, pa, want, problems, tag):
    """The patch is in RAM and in the record, ran, and the run ended as want."""
    st = hp_state(page)
    first = hp_mem(page, pa, 1)[0]
    now = stages(page)
    if first != 0xE9 or len(st["patches"]) != 1 or PATCHED not in screen(page):
        problems.append(f"{tag}: do_exit starts {first:#x}, patches {len(st['patches'])}, screen {screen(page)[-160:]!r}")
    if now["kind"] != want["kind"] or "not reached" in now["rows"].get("Still alive", ["not reached"])[0]:
        problems.append(f"{tag}: the end of the patched run: {now}, the first run: {want}")


def hr_lost(browser, base, problems, r):
    """Back to, Continue, Back to, patch, Continue (items 1 and 4)."""
    tag = "patch after Back and Continue"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        first = boot_end(page, base, problems, tag)
        if not first or not back_to(page, "con_write", problems, tag):
            return
        again = run_to_end(page, problems, tag)
        r["first end"], r["after Back and Continue"] = first, again
        if again != first:
            problems.append(f"{tag}: Back and Continue with no patch reports other stages: {again}, the first run: {first}")
        if not back_to(page, "con_write", problems, tag):
            return
        pa = patch_do_exit(page, problems, tag)
        if pa is None or not run_to_end(page, problems, tag):
            return
        patched_end(page, pa, first, problems, tag)
    finally:
        ctx.close()


def hr_snapshot(browser, base, problems, r):
    """A snapshot at exactly the patch's count keeps the patch (item 2)."""
    tag = "snapshot at the patch's count"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        first = boot_end(page, base, problems, tag)
        if not first or not back_to(page, "con_write", problems, tag):
            return
        at = steps(page)
        pa = patch_do_exit(page, problems, tag)
        if pa is None or not run_to_end(page, problems, tag):
            return
        end = steps(page)
        patched_end(page, pa, first, problems, tag)
        show_tab(page, "debugger")
        page.fill("#dbg_n", str(end - at + 1))
        page.click("#dbg_backn")
        if not wait_for(lambda: steps(page) < at, 300, 0.2) or not still(page):
            problems.append(f"{tag}: Step back to before {at}: {steps(page)}")
            return
        page.fill("#dbg_n", str(at - steps(page)))
        page.click("#dbg_stepn")
        if not wait_for(lambda: steps(page) == at, 300, 0.2) or not still(page):
            problems.append(f"{tag}: Step to {at}: {steps(page)}")
            return
        here = [hp_mem(page, pa, 1)[0], len(hp_state(page)["patches"])]
        r["at the patch's count"] = here
        if here != [0xE9, 1]:
            problems.append(f"{tag}: stepped to {at}, the patch's count: do_exit starts {here[0]:#x}, patches {here[1]}")
        # One Step across the patch's count makes it on the way, then back to the count.
        page.fill("#dbg_n", "1")
        page.click("#dbg_backn")
        wait_for(lambda: steps(page) < at, 120, 0.1)
        still(page)
        if hp_mem(page, pa, 1)[0] == 0xE9:
            problems.append(f"{tag}: the patch is still in RAM at {steps(page)}, before its count {at}")
        page.fill("#dbg_n", str(at + 1 - steps(page)))
        page.click("#dbg_stepn")
        wait_for(lambda: steps(page) == at + 1, 120, 0.1)
        still(page)
        crossed = [hp_mem(page, pa, 1)[0], len(hp_state(page)["patches"])]
        r["stepped across the count"] = crossed
        if crossed != [0xE9, 1]:
            problems.append(f"{tag}: stepped across {at}: do_exit starts {crossed[0]:#x}, patches {crossed[1]}")
        page.fill("#dbg_n", "1")
        page.click("#dbg_backn")
        wait_for(lambda: steps(page) == at, 120, 0.1)
        still(page)
        snap_save(page, 1, False)
        if not run_to_end(page, problems, tag):
            return
        rows = snap_rows(page)["rows"]
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{rows[0]['id']}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rows[0]["id"], 60)
        still(page)
        kept = [hp_mem(page, pa, 1)[0], len(hp_state(page)["patches"])]
        r["restored"] = kept
        if kept != [0xE9, 1]:
            problems.append(f"{tag}: the restored snapshot: do_exit starts {kept[0]:#x}, patches {kept[1]}")
        if not run_to_end(page, problems, tag):
            return
        patched_end(page, pa, first, problems, tag)
        if steps(page) != end:
            problems.append(f"{tag}: the restored run ended at {steps(page)}, the uninterrupted one at {end}")
        # The snapshot is kept in the browser: restored after a reload, on a
        # machine that has recorded no end, it reports the same stages.
        page.goto(f"{base}/index.html?kernel={MINI}&speed=full&paused=1")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.runState === 'paused'", timeout=120000)
        show_tab(page, "snapshots")
        wait_for(lambda: len(snap_rows(page)["rows"]) == 1, 30)
        snap_click(page, f"#snapshots tr[data-id='{rows[0]['id']}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rows[0]["id"], 60)
        still(page)
        if run_to_end(page, problems, tag + " after a reload"):
            patched_end(page, pa, first, problems, tag + " after a reload")
            r["end after a reload"] = steps(page)
            if steps(page) != end:
                problems.append(f"{tag}: restored after a reload, the run ended at {steps(page)}, the uninterrupted one at {end}")
    finally:
        ctx.close()


def hr_snapshot_end(browser, base, problems, r):
    """A snapshot taken after exit() and panic(), restored after a reload,
    shows those stages and ends as the first run did."""
    tag = "snapshot at the end, after a reload"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        first = boot_end(page, base, problems, tag)
        if not first:
            return
        snap_save(page, 1, False)
        row = snap_rows(page)["rows"][0]["id"]
        page.goto(f"{base}/index.html?kernel={MINI}&speed=full&paused=1")
        page.wait_for_function("() => window.MKB && MKB.stepper && MKB.runState === 'paused'", timeout=120000)
        show_tab(page, "snapshots")
        wait_for(lambda: len(snap_rows(page)["rows"]) == 1, 30)
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row, 60)
        still(page)
        now = run_to_end(page, problems, tag)
        r["restored end"] = now
        if not now:
            return
        for label in ("exit(0)", "panic() entered"):
            if now["rows"].get(label) != first["rows"].get(label):
                problems.append(f"{tag}: {label}: {now['rows'].get(label)}, the first run: {first['rows'].get(label)}")
        if now["kind"] != first["kind"]:
            problems.append(f"{tag}: ends as {now['kind']!r} {now['text']!r}, the first run as {first['kind']!r}")
    finally:
        ctx.close()


def hr_debian(browser, base, problems, r):
    """Debian with the default count: Step back works and undoes a patch (item 3)."""
    tag = "Debian Step back, default settings"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        t0 = time.monotonic()
        boot(page, base, DEBIAN)
        if not wait_for(lambda: "(initramfs)" in screen(page), 1800, 1):
            problems.append(f"{tag}: no prompt: {screen(page)[-200:]!r}")
            return
        r["boot to the prompt s"] = round(time.monotonic() - t0)
        show_tab(page, "machine")
        if page.evaluate("MKB.runState") != "paused" and page.is_enabled("#dbg_pause"):
            page.click("#dbg_pause")
        still(page)
        show_tab(page, "debugger")
        r["reverse line"] = page.inner_text("#rev_info")
        show_tab(page, "machine")
        if page.is_disabled("#dbg_back"):
            problems.append(f"{tag}: Step back is disabled at the prompt: {r['reverse line']!r}")
            return
        if not rbl_open(page, "fs/proc/version.c", False):
            problems.append(f"{tag}: Edit did not open fs/proc/version.c")
            return
        text = page.input_value("#ed_text")
        s = page.evaluate(SYM_JS, "version_proc_show")
        code = ("version_proc_show", s[1], hp_mem(page, s[1], s[2]))
        hp_edit(page, text.replace("\tseq_printf(m, linux_proc_banner,", '\tseq_puts(m, "Hot patched /proc/version\\n");\n\tseq_printf(m, linux_proc_banner,'))
        st = hp_wait(page, ("ready", "refused", ""), 900)
        if st["state"] != "ready":
            problems.append(f"{tag}: the edit is not ready: {st['state']} {st['status']!r}")
            return
        page.click("#hp_go_panel")
        st = hp_wait(page, ("done", "failed"), 120)
        if st["state"] != "done":
            problems.append(f"{tag}: the patch: {st['status']!r}")
            return
        hp_step_over(page, st, code, problems, tag, r)
    finally:
        ctx.close()


def hr_refusal(browser, base, problems, r):
    """con_write of the -O0 kernel: the refusal says the true reason (item 5)."""
    tag = "refusal of con_write"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    try:
        if not boot_end(page, base, problems, tag):
            return
        if not rbl_open(page, "drivers/tty/vt/vt.c", False):
            problems.append(f"{tag}: Edit did not open drivers/tty/vt/vt.c")
            return
        text = page.input_value("#ed_text")
        hp_edit(page, text.replace("unsigned char c = buf[n];", "unsigned char c = buf[n] == 'S' ? 'Z' : buf[n];", 1))
        st = hp_wait(page, ("ready", "refused"), 600)
        r["refusal"] = st["status"]
        for word in ("con_write", "link-time optimization", "Rebuild and boot"):
            if word not in st["status"]:
                problems.append(f"{tag}: no {word!r} in {st['status']!r}")
    finally:
        ctx.close()


def hr_pick_race(browser, base, problems, r):
    """Another kernel picked while the first still loads: the Source tree is
    the picked kernel's (item 6)."""
    tag = "kernel picked during a load"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    held = []
    page.route("**/kernels/hello/disasm.json*", lambda route: held.append(route))
    try:
        page.goto(f"{base}/index.html?kernel=hello")
        page.wait_for_function("() => document.querySelectorAll('#kernel option').length > 3", timeout=120000)
        page.wait_for_timeout(1000)  # Playwright delivers the held request while it waits
        if not held:
            problems.append(f"{tag}: hello's load never asked for its disassembly")
            return
        page.select_option("#kernel", MINI)
        page.wait_for_function("(n) => MKB.image && MKB.image.name === n", arg=MINI, timeout=120000)
        for route in held:
            route.continue_()
        page.wait_for_timeout(4000)
        show_tab(page, "source")
        page.wait_for_selector("#cov_tree[data-state=ok]", timeout=60000)
        page.fill("#cov_filter", "exit.c")
        page.wait_for_timeout(600)
        name = page.evaluate("MKB.image.name")
        row = page.locator('#cov_tree .covrow[data-path="kernel/exit.c"]').count()
        r["image, kernel/exit.c rows"] = [name, row]
        if name != MINI or not row:
            problems.append(f"{tag}: the machine shows {name}, kernel/exit.c rows {row}: {page.inner_text('#cov_tree')[:160]!r}")
    finally:
        ctx.close()


def suite_hprun(ctx):
    failed = False
    only = [x for x in os.environ.get("MKB_HPRUN_ONLY", "").split(",") if x]
    for fn in (hr_pick_race, hr_lost, hr_snapshot, hr_snapshot_end, hr_refusal, hr_debian):
        if only and fn.__name__ not in only:
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(ctx.browser, ctx.base, problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("hprun", problems, r, case=fn.__name__, seconds=round(time.monotonic() - t0))
    return failed

