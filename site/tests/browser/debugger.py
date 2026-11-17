"""Browser test suite debugger (test_browser.py --suites)."""

import time

from tests.browser.common import counters, kernel_entries, machine_do, report, show_tab, wait_for


def regs(page):
    return {e.get_attribute("data-reg"): e.inner_text() for e in page.locator("#regs div").all()}


def debugger_case(browser, base, name, mem, shots):
    """Start paused at reset: Step 10, Run to 0x100000, slow motion."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1" + (f"&mem={mem}" if mem else "") + "#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        return ["did not start paused: " + page.inner_text("#dbg_status")], r
    before = regs(page)
    r["reset_eip"] = before.get("EIP")
    page.fill("#dbg_n", "10")
    page.click("#dbg_stepn")
    wait_for(lambda: "Stepped" in page.inner_text("#dbg_status"), 5)
    after = regs(page)
    r["step10_eip"] = after.get("EIP")
    if after.get("EIP") == before.get("EIP"):
        problems.append("Step 10 did not change EIP")
    if page.locator("#regs div.chg").count() == 0:
        problems.append("register panel marks no changed register after Step 10")
    now = counters(page).get("now", {})
    if now.get("steps") != 10:
        problems.append(f"Step 10 counted {now.get('steps')} instructions")

    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(0x100000), 60):
        problems.append("Run to 0x100000 did not stop there: " + page.inner_text("#dbg_status"))
    r["runto_status"] = page.inner_text("#dbg_status")
    r["runto_steps"] = counters(page).get("now", {}).get("steps")
    if page.locator("#context .cur").count() != 1:
        problems.append("Debugger tab shows no current instruction")
    page.screenshot(path=str(shots / f"tab-debugger-{name}-1100.png"))

    # hello is done 159 instructions after the entry: watch it at 10/s.
    rate, least = ("10", 10) if name == "hello" else ("1000", 200)
    machine_do(page, "select_option", "#dbg_speed", rate)
    show_tab(page, "memory")
    machine_do(page, "click", "#dbg_continue")
    start = counters(page).get("now", {}).get("steps") or 0
    time.sleep(2.0)
    end = counters(page).get("now", {}).get("steps") or 0
    r["slow_steps_2s"] = end - start
    r["flashes"] = int(page.get_attribute("#pages", "data-flashes") or 0)
    if not end - start >= least:
        problems.append(f"{rate}/s slow motion advanced only {end - start} instructions in 2 s")
    if r["flashes"] < 1:
        problems.append("the grid flashed no written page during slow motion")
    page.screenshot(path=str(shots / f"slow-{name}-1100.png"))
    page.set_viewport_size({"width": 390, "height": 900})
    show_tab(page, "debugger")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"tab-debugger-{name}-390.png"))
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("debugger tab scrolls horizontally at 390 px")
    if page.is_enabled("#dbg_pause"):
        machine_do(page, "click", "#dbg_pause")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def screen_flush_case(browser, base):
    """v86 draws the screen's changed rows in a requestAnimationFrame loop
    and stops drawing when the VM stops, so rows written in the last frame
    before a pause stayed blank (CI: the Debian panic stop without its
    "end Kernel panic" line). With the frames starved (no
    requestAnimationFrame at all) the screen must still show what the
    kernel wrote: after single steps while paused (hello) and after the
    run's own end stop at full speed (minified Linux)."""
    problems, r = [], {}
    page = browser.new_page()
    page.add_init_script("window.requestAnimationFrame = () => 0;")
    page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.inner_text("#dbg_status").startswith("Stopped at"), 120)
    page.fill("#dbg_n", "1000")
    page.click("#dbg_stepn")
    wait_for(lambda: page.inner_text("#dbg_status").startswith("Stepped"), 60)
    wait_for(lambda: page.evaluate("MKB.stepper.screenText().includes('Still alive')") or page.inner_text("#dbg_status").startswith("Stepped 1,000"), 5)
    if not page.evaluate("MKB.stepper.screenText().includes('Hello, World!')"):
        page.fill("#dbg_n", "1000")
        page.click("#dbg_stepn")
        time.sleep(2)
    r["hello status"] = page.inner_text("#dbg_status")
    screen = page.inner_text("#screen_text")
    if "Hello, World!" not in screen:
        problems.append(f"hello: stepped past Hello, World! while paused, the screen does not show it ({r['hello status']!r})")
    page.close()
    if "minified-linux" in kernel_entries():
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame = () => 0;")
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                 and page.evaluate("!!(MKB.stepper && MKB.stepper.end)"), 120)
        r["minified end"] = page.inner_text("#end_banner")
        screen = page.inner_text("#screen_text")
        if "Still alive" not in screen:
            problems.append(f"minified-linux: the run ended ({r['minified end']!r}), the screen does not show Still alive")
        page.close()
    return problems, r


def suite_debugger(ctx):
    failed = False
    for name in [n for n in ("hello", "minified-linux") if n in ctx.entries]:
        dp, dr = debugger_case(ctx.browser, ctx.base, name, None, ctx.shots)
        failed |= report("debugger", dp, dr, kernel=name)
    failed |= report("screen after a pause", *screen_flush_case(ctx.browser, ctx.base))
    return failed
