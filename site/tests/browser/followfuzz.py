"""Browser test suite followfuzz (test_browser.py --suites)."""

import random
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import dbg_shown_once, machine_do, report, show_tab, wait_for
from tests.browser.runstate import RUN_FUZZ_PICK
from tests.browser.followeip import follow_install


FOLLOW_FUZZ_HIDE = """() => {
    Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
    Object.defineProperty(document, 'hidden', { value: true, configurable: true });
    window.mkbRaf = [window.requestAnimationFrame, window.cancelAnimationFrame];
    window.mkbHeld = new Map(); let id = 1e9;
    window.requestAnimationFrame = (cb) => { window.mkbHeld.set(++id, cb); return id; };
    window.cancelAnimationFrame = (i) => { if (!window.mkbHeld.delete(i)) window.mkbRaf[1].call(window, i); };
    document.dispatchEvent(new Event('visibilitychange')); }"""
FOLLOW_FUZZ_SHOW = """() => { delete document.visibilityState; delete document.hidden;
    document.dispatchEvent(new Event('visibilitychange'));
    [window.requestAnimationFrame, window.cancelAnimationFrame] = window.mkbRaf;
    const held = [...window.mkbHeld.values()]; window.mkbHeld.clear();
    for (const cb of held) window.requestAnimationFrame(cb); }"""
FOLLOW_FUZZ_TARGETS = ["start_kernel", "printk", "rest_init", "kernel_init", "0x100000", "0x1000", "hello_main"]


def follow_fuzz_action(page, cdp, rng):
    """One random action for follow_fuzz_case, as a short description."""
    r = rng.random()

    def click(sel):
        what = page.evaluate(RUN_FUZZ_PICK, [sel, rng.random()])
        if what is None:
            return "nothing for " + sel
        try:
            page.locator("[data-fz]").first.click(timeout=1500)
            return "click " + what
        except PlaywrightError:
            return "could not click " + what
    if r < 0.18:
        return click(rng.choice(["#tab_debugger", "#tab_debugger", "#tab_page", "#tab_page", "#tab_memory", "#tab_calls", "#tab_source"]))
    if r < 0.32:
        v = rng.choice(["1", "10", "10", "1000", "100000", "1000000", "full"])
        machine_do(page, "select_option", "#dbg_speed", v)
        return "speed " + v
    if r < 0.45:
        return click(rng.choice(["#dbg_pause", "#dbg_continue", "#dbg_step", "#dbg_stepn", "#vm_continue", "#runbadge_toggle", "#runbadge_pause"]))
    if r < 0.50:
        v = rng.choice(FOLLOW_FUZZ_TARGETS)
        page.evaluate("v => { document.getElementById('dbg_addr').value = v; }", v)
        return "Run to " + v + ": " + click("#dbg_runto")
    if r < 0.60:
        return click(rng.choice([".srclink", ".srclink", "#src_close"]))
    if r < 0.70:
        return click(rng.choice(["#nav_back", "#nav_fwd", "#runbadge_toggle"]))
    if r < 0.73:
        # The browser's back, only inside the page's jump history (past it
        # the browser leaves the page, as for any page).
        if page.evaluate("!!(history.state && history.state.nav > 0)"):
            page.go_back()
            return "browser back"
        return "no browser back (would leave the page)"
    if r < 0.78:
        return click(rng.choice(["#listing [data-addr]", "#context [data-addr]", ".symlink"]))
    if r < 0.81:
        v = rng.choice(["minified-linux", "minified-linux-os", "hello"])
        show_tab(page, "machine")
        page.select_option("#kernel", v)
        return "kernel " + v
    if r < 0.84:
        show_tab(page, "machine")
        return click("#boot")
    if r < 0.89:
        page.evaluate(FOLLOW_FUZZ_HIDE)
        h = rng.choice([1, 3, 6])
        time.sleep(h)
        page.evaluate(FOLLOW_FUZZ_SHOW)
        return f"hidden {h} s"
    if r < 0.93:
        t = rng.choice([1, 4])
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": t})
        return f"CPU {t}x slower"
    time.sleep(3)
    return "wait 3 s"


def follow_fuzz_case(browser, base, shots, kernel, seed, actions, verbose=False):
    """Seeded random actions (tabs, speeds, Pause, Step, Run to, the Source
    panel, Back and Forward, links into the listings, kernel switch, Boot
    during the fast-forward, a hidden tab, CPU throttling 1x/4x) with the
    follow-EIP invariant watched all along; stops at the first broken one."""
    rng = random.Random(seed)
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    if rng.choice([1, 4]) > 1:
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 4})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed={rng.choice(['1', '10'])}#{rng.choice(['debugger', 'page'])}")
    wait_for(lambda: page.evaluate("typeof window.MKB?.entryPhase !== 'undefined' && MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 180)
    # The Debugger's controls (Run to's address) exist once its tab has been shown.
    dbg_shown_once(page)
    follow_install(page)
    log, problems = [], []
    for _ in range(actions):
        for extra in ctx.pages[1:]:
            extra.close()
        log.append(follow_fuzz_action(page, cdp, rng))
        time.sleep(rng.choice([0, 0.3, 1, 2, 3]))
        viol = page.evaluate("mkbFollow.viol")
        if viol or errors:
            problems.append({"after": log[-8:], "follow EIP": viol[:3], "page errors": errors})
            page.screenshot(path=str(shots / f"follow-fuzz-{kernel}-{seed}.png"))
            break
    ctx.close()
    return problems, {"actions": len(log), **({"log": log} if verbose else {})}
# (kernel, seed): fixed, so CI runs the same sequences every time.
FOLLOW_FUZZ_SEEDS = [("minified-linux", 1), ("minified-linux-os", 2), ("minified-linux", 3), ("hello", 4)]


def suite_followfuzz(ctx):
    failed = False
    seeds = [s for s in FOLLOW_FUZZ_SEEDS if s[0] in ctx.entries]
    if ctx.args.fuzz_seeds:
        seeds = [(["minified-linux", "minified-linux-os", "hello"][s % 3], s)
                 for s in range(ctx.args.fuzz_from, ctx.args.fuzz_from + ctx.args.fuzz_seeds)]
    for kernel, seed in seeds:
        t0 = time.monotonic()
        p, r = follow_fuzz_case(ctx.browser, ctx.base, ctx.shots, kernel, seed, ctx.args.fuzz_actions,
                                verbose=bool(ctx.args.fuzz_seeds))
        failed |= report("follow_fuzz", p, r, kernel=kernel, seed=seed, seconds=round(time.monotonic() - t0))
    return failed
