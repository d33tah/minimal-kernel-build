"""Browser test suite runstate (test_browser.py --suites)."""

import random
import re
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import dbg_shown_once, machine_do, PHONE, report, show_tab, wait_for


# ---- run state: random clicks at 1/s ---------------------------------------
# The operator's report: clicking around at 1 instruction per second reached
# a state where the page was not paused yet nothing ran. run_state_problem()
# is the invariant, checked after every action of a seeded random-click
# fuzzer (desktop and 390 px touch, hello and the minified Linux).

RUN_STATE_JS = """() => ({
    state: document.getElementById('dbg_state').dataset.state || '',
    text: document.getElementById('dbg_state').textContent,
    steps: MKB.stepper ? MKB.stepper.steps : -1,
    end: !!(MKB.stepper && MKB.stepper.end),
    pauseOff: document.getElementById('dbg_pause').disabled,
    contOff: document.getElementById('dbg_continue').disabled,
    overlay: !document.getElementById('vm_overlay').hidden,
    speed: document.getElementById('dbg_speed').value,
    status: document.getElementById('status').textContent,
    note: document.getElementById('dbg_status').textContent,
    line: document.getElementById('vclock').textContent.split(' \u00b7 ')[0],
})"""


def run_state_problem(page, budget=3.5):
    """None, or what is wrong with the run state now. Paused: Pause disabled,
    Continue enabled (unless the run has ended), the "VM paused" overlay
    shown. Running: the opposite, the speed line never "speed 0/s" (the
    operator's "not paused and zero instructions per second"), and at 1/s
    or 10/s the instruction count moves within budget seconds."""
    bad = run_state_zero_speed(page)
    if bad:
        return bad
    s0 = page.evaluate(RUN_STATE_JS)
    if s0["state"] == "paused":
        if not s0["pauseOff"] or (s0["contOff"] and not s0["end"]) or not s0["overlay"]:
            return {"paused, controls disagree": s0}
        return None
    if s0["state"] in ("stepped", "jit", "ff") and (s0["pauseOff"] or s0["overlay"]):
        return {"running, controls disagree": s0}
    if s0["state"] == "jit" and not s0["contOff"]:
        return {"running, Continue enabled": s0}
    if s0["state"] == "off":
        # Between Boot and the machine being ready: must not stay there.
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") != "off", 30):
            return {"off for 30 s after a Boot": page.evaluate(RUN_STATE_JS)}
        return None
    if s0["state"] == "ff":
        # A fast-forward runs the JIT: the count moves, or it ends.
        moved = wait_for(lambda: page.evaluate("MKB.stepper ? MKB.stepper.steps : -1") != s0["steps"]
                         or page.get_attribute("#dbg_state", "data-state") != "ff", 10)
        return None if moved else {"fast-forward not moving for 10 s": page.evaluate(RUN_STATE_JS)}
    if s0["state"] != "stepped" or s0["speed"] not in ("1", "10"):
        return None
    moved = wait_for(lambda: page.evaluate(RUN_STATE_JS)["steps"] != s0["steps"]
                     or page.evaluate(RUN_STATE_JS)["state"] != "stepped"
                     or page.evaluate(RUN_STATE_JS)["speed"] != s0["speed"], budget)
    if not moved:
        return {"running at %s/s, no instruction for %s s" % (s0["speed"], budget): page.evaluate(RUN_STATE_JS)}
    return run_state_zero_speed(page)


def run_state_zero_speed(page):
    """None, or the state while the speed line says "speed 0/s" with the
    run going on (stepping, the JIT or a fast-forward)."""
    s = page.evaluate(RUN_STATE_JS)
    if s["state"] in ("stepped", "jit", "ff") and re.match(r"speed 0(\.0)?/s\b", s["line"]):
        return {"running, the speed line says 0/s": s}
    return None


RUN_FUZZ_PICK = """([sel, r]) => {
    document.querySelectorAll('[data-fz]').forEach((e) => e.removeAttribute('data-fz'));
    const all = [...document.querySelectorAll(sel)].filter((e) => {
        if (e.disabled)
            return false;
        const b = e.getBoundingClientRect();
        return b.width > 0 && b.height > 0 && getComputedStyle(e).visibility !== 'hidden';
    });
    if (!all.length)
        return null;
    const e = all[Math.floor(r * all.length)];
    e.setAttribute('data-fz', '1');
    return e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + ' ' + (e.textContent || '').trim().slice(0, 24);
}"""
# Anything clickable on the page, except file pickers, downloads and links
# to other pages.
RUN_FUZZ_ANY = ("button:not(#graph_json):not(#graph_dot):not(#cov_export):not(#src_dir_btn), a[href^='#'], [role=button], "
                "#listing [data-addr], #context [data-addr], .jc, #pages, #screen_container, input[type=checkbox]")
RUN_FUZZ_CONTROLS = ["#dbg_pause", "#dbg_continue", "#vm_continue", "#runbadge_toggle", "#runbadge_pause", "#dbg_step", "#dbg_stepn", "#dbg_runto", "#boot",
                     "#tab_machine", "#tab_memory", "#tab_page", "#tab_debugger", "#tab_snapshots", "#tab_calls", "#src_close",
                     "#snap_save", "#snapshots .restore", "#count"]
RUN_FUZZ_TARGETS = ["0x100000", "start_kernel", "hello_main", "rest_init", "printk", "kernel_init", "do_exit",
                    "init/main.c:1000", "hello.c:30", "0x1000", "no_such_symbol"]
RUN_FUZZ_KERNELS = ["hello", "hello-os", "minified-linux", "minified-linux-os"]
RUN_FUZZ_THROTTLE = 4


def run_fuzz_action(page, rng, touch):
    """One random action, as a short description."""
    r = rng.random()
    if r < 0.1:
        # An asm listing watched for a while (the audit's repro of bug 1:
        # the Page tab at 1/s on a throttled phone ran 6 instructions in 30 s).
        t = rng.choice(["#tab_page", "#tab_page", "#tab_debugger"])
        hold = rng.choice([3, 5, 8])
        try:
            (page.tap if touch else page.click)(t, timeout=5000)
        except PlaywrightError as e:
            return "could not open " + t + ": " + str(e).splitlines()[0][:60]
        time.sleep(hold)
        return f"watch {t} for {hold} s"
    r = rng.random()
    if r < 0.06:
        k = rng.choice(["Escape", "Enter", "ArrowDown", "a", " ", "Tab"])
        page.keyboard.press(k)
        return "key " + k
    if r < 0.22:
        v = rng.choice(["1", "1", "1", "10", "10", "100", "1000", "full"])
        machine_do(page, "select_option", "#dbg_speed", v)
        return "speed " + v
    if r < 0.25:
        v = rng.choice(["10", "100", "1000", "real"])
        machine_do(page, "select_option", "#clock_mips", v)
        return "clock " + v
    if r < 0.28:
        v = rng.choice(RUN_FUZZ_KERNELS)
        show_tab(page, "machine")
        page.select_option("#kernel", v)
        return "kernel " + v
    if r < 0.33:
        v = rng.choice(RUN_FUZZ_TARGETS)
        page.evaluate("v => { document.getElementById('dbg_addr').value = v; }", v)
        return "Run to field " + v
    sel = rng.choice(RUN_FUZZ_CONTROLS) if r < 0.75 else RUN_FUZZ_ANY
    what = page.evaluate(RUN_FUZZ_PICK, [sel, rng.random()])
    if what is None:
        return "nothing to click for " + sel
    try:
        el = page.locator("[data-fz]").first
        if touch:
            el.tap(timeout=1500)
        else:
            el.click(timeout=1500)
        return ("tap " if touch else "click ") + what
    except PlaywrightError as e:
        return "could not click " + what + ": " + str(e).splitlines()[0][:60]


def run_fuzz_case(browser, base, shots, kernel, touch, seed, actions, verbose=False):
    """Seeded random clicks from kernel at 1/s; stops at the first broken
    invariant. Returns (problems, record)."""
    rng = random.Random(seed)
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    if touch:
        # A mid-tier phone (Chrome's "4x slowdown").
        ctx.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": RUN_FUZZ_THROTTLE})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") not in (None, "", "off"), 120)
    # RUN_STATE_JS reads #dbg_status and Run to's field is set below: there once the Debugger tab has been shown.
    dbg_shown_once(page)
    log, problems = [], []
    for _ in range(actions):
        for extra in ctx.pages[1:]:
            extra.close()
        if "/index.html" not in page.url:  # a link left the page: back to it
            page.go_back()
            dbg_shown_once(page)  # a new load of the page: the Debugger tab not shown yet
        pause = rng.choice([0, 0, 0.05, 0.3, 1, 2])
        log.append(run_fuzz_action(page, rng, touch))
        time.sleep(pause)
        bad = run_state_problem(page)
        if bad or errors:
            problems.append({"after": log[-8:], "problem": bad, "page_errors": errors})
            page.screenshot(path=str(shots / f"runfuzz-{kernel}-{'phone' if touch else 'desktop'}-{seed}.png"))
            break
    ctx.close()
    return problems, {"actions": len(log), **({"log": log} if verbose else {})}


def double_boot_case(browser, base, shots, touch):
    """Boot pressed twice in a row while the minified Linux fast-forwards
    to its entry (the first minimal sequence the fuzzer found): the second
    machine must come up and run at 1/s, with no page error. Desktop: both
    clicks in one task (always hit the race before the fix: "fast-forwarding
    to startup_32 with the JIT: 0 instructions", Pause enabled, nothing
    running); phone: two real taps (about every other run before the fix)."""
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=1")
    wait_for(lambda: page.evaluate("typeof window.MKB?.ffActive !== 'undefined' && MKB.ffActive"), 60, 0.01)
    show_tab(page, "machine")
    if touch:
        page.tap("#boot", no_wait_after=True)
        page.tap("#boot", no_wait_after=True)
    else:
        page.evaluate("() => { const b = document.getElementById('boot'); b.click(); b.click(); }")
    problems = []
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "stepped"
                  and page.evaluate("!MKB.entryPhase"), 60)
    dbg_shown_once(page)  # RUN_STATE_JS reads #dbg_status: there once the Debugger tab has been shown
    if not ok:
        problems.append({"not running at 1/s 60 s after two Boots": page.evaluate(RUN_STATE_JS)})
    else:
        bad = run_state_problem(page)
        if bad:
            problems.append(bad)
    if errors:
        problems.append({"page errors": errors})
    tag = "phone" if touch else "desktop"
    page.screenshot(path=str(shots / f"double-boot-{tag}.png"))
    ctx.close()
    return problems


# (kernel, touch, seed): fixed, so CI runs the same sequences every time.
RUN_FUZZ_SEEDS = [("minified-linux", False, 1), ("minified-linux", True, 2), ("hello", False, 3), ("hello", True, 4),
                  ("minified-linux", False, 5), ("minified-linux", True, 6)]


def runto_during_ff_case(browser, base, shots):
    """Run to start_kernel pressed twice at once (one task) while the boot
    fast-forwards to the entry at 1/s: the fast-forwards must run one at a
    time on the machine and it must stop at start_kernel."""
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=1#debugger")
    wait_for(lambda: page.evaluate("typeof window.MKB?.ffActive !== 'undefined' && MKB.ffActive"), 60, 0.01)
    page.evaluate("""() => {
        document.getElementById('dbg_addr').value = 'start_kernel';
        const b = document.getElementById('dbg_runto');
        b.click();
        b.click();
    }""")
    problems = []
    done = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.evaluate("!MKB.ffActive && MKB.runTo === null")
                    and not page.inner_text("#dbg_status").startswith(("Running to", "Finding")), 120)
    s = page.evaluate(RUN_STATE_JS)
    sym = page.evaluate("MKB.stepper.symName[MKB.stepper.symbolOf(MKB.stepper.nextPhys())] || ''")
    if not done or sym != "start_kernel":
        problems.append({"not stopped at start_kernel": s, "at": sym})
    bad = run_state_problem(page)
    if bad:
        problems.append(bad)
    if errors:
        problems.append({"page errors": errors})
    page.screenshot(path=str(shots / "runto-during-ff.png"))
    page.close()
    return problems, {"at": sym, "note": s["note"][:100]}


def suite_runstate(ctx):
    failed = False
    if "minified-linux" in ctx.entries:
        for touch in (False, True):
            failed |= report("double_boot", double_boot_case(ctx.browser, ctx.base, ctx.shots, touch), touch=touch)
        failed |= report("runto_during_ff", *runto_during_ff_case(ctx.browser, ctx.base, ctx.shots))
    seeds = [s for s in RUN_FUZZ_SEEDS if s[0] in ctx.entries]
    if ctx.args.fuzz_seeds:
        n = ctx.args.fuzz_seeds
        seeds = [(k, t, s) for s in range(ctx.args.fuzz_from, ctx.args.fuzz_from + n)
                 for k, t in [("minified-linux", False), ("minified-linux", True), ("hello", False), ("hello", True)]]
    for kernel, touch, seed in seeds:
        t0 = time.monotonic()
        p, r = run_fuzz_case(ctx.browser, ctx.base, ctx.shots, kernel, touch, seed, ctx.args.fuzz_actions,
                             verbose=bool(ctx.args.fuzz_seeds))
        failed |= report("run_fuzz", p, r, kernel=kernel, touch=touch, seed=seed, seconds=round(time.monotonic() - t0))
    return failed
