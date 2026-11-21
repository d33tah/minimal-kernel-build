"""Browser test suite followeip (test_browser.py --suites)."""

import time

from tests.browser.common import machine_do, report, show_tab, wait_for
from tests.browser.runstate import RUN_STATE_JS


# ---- follow EIP: the asm views show the stepper's EIP --------------------
# The operator's report: on the minified Linux, in the Debugger tab, the
# instructions went on but Code at EIP no longer moved. FOLLOW_EIP_JS is the
# invariant, watched in the page: each drawing of Code at EIP (Debugger) and
# of the Page listing (data-draws) must box the line holding EIP, and once an
# instruction has executed a view on screen must show the new EIP within
# max(1 s, twice the last drawing plus 400 ms) of its last drawing. It
# holds while stepping at any speed (slow motion, Run to, counting) and
# paused; not while the JIT runs (the views say so) or the tab is hidden.
# The Page listing need only show EIP while stepping, or paused on EIP's
# page.

FOLLOW_EIP_JS = """() => {
    if (window.mkbFollow)
        return;
    const M = window.mkbFollow = { viol: [], draws: { context: 0, listing: 0 }, good: {}, pend: {}, samples: 0 };
    const sel = { context: '#context .cur[data-addr]', listing: '#listing .cur[data-addr]' };
    const tabOf = { context: 'debugger', listing: 'page' };
    const now = () => MKB.realNow(); // performance.now is the guest's clock (clock.js)
    const boxed = (v) => { const e = document.querySelector(sel[v]); return e ? Number(e.dataset.eip ?? e.dataset.addr) : -1; };
    // The boxed line holds EIP: an instruction at EIP (or with EIP inside
    // it, data-eip), or the hex row around it.
    const shows = (v, eip) => { const e = document.querySelector(sel[v]);
        return !!e && (Number(e.dataset.eip ?? e.dataset.addr) === eip ||
            (e.dataset.end !== undefined && eip >= Number(e.dataset.addr) && eip < Number(e.dataset.end))); };
    const due = (v) => MKB.stepper && MKB.emuReady && !document.hidden && !MKB.ffActive && MKB.tab === tabOf[v] && MKB.stepper.nextPhys() >= 0 &&
        MKB.stepper.nextPhys() < MKB.emuRam && (MKB.runState === 'paused' || MKB.runState === 'stepped') &&
        (v === 'context' || MKB.runState === 'stepped' || MKB.selectedPage === Math.floor(MKB.stepper.nextPhys() / MKB.PAGE));
    const state = (v) => ({ view: v, boxed: boxed(v), eip: MKB.stepper.nextPhys(), steps: MKB.stepper.steps, state: MKB.runState,
        speed: MKB.speed(), fast: MKB.fastStepping(), runTo: MKB.runTo !== null, kernel: MKB.image && MKB.image.name,
        source: !document.getElementById('src_panel').hidden, page: Number(document.getElementById('listing')?.dataset.page) });
    const seen = (v) => {
        if (!MKB.stepper)
            return;
        M.draws[v]++;
        if (shows(v, MKB.stepper.nextPhys())) {
            M.good[v] = { t: now(), steps: MKB.stepper.steps, st: MKB.stepper };
            M.pend[v] = null;
        } else if (due(v) && M.viol.length < 20 && (v === 'context' ||
                Number(document.getElementById('listing').dataset.page) === Math.floor(MKB.stepper.nextPhys() / MKB.PAGE)))
            // The listing drawn at another page (the page EIP has just
            // left, before it is followed) is caught by the time limit.
            M.viol.push({ kind: 'drawn without EIP boxed', ...state(v) });
    };
    // Each listing is watched from when it exists (the Page tab's from its
    // first showing: A1 mounts it then).
    const watched = {};
    const watch = () => {
        for (const v of ['context', 'listing']) {
            const e = document.getElementById(v);
            if (e && watched[v] !== e) {
                watched[v] = e;
                new MutationObserver(() => seen(v)).observe(e, { attributes: true, attributeFilter: ['data-draws'] });
            }
        }
    };
    watch();
    const limit = () => Math.max(1000, 2 * (MKB.viewCost || 0) + 400);
    M.timer = setInterval(() => {
        watch();
        M.samples++;
        for (const v of ['context', 'listing']) {
            if (!due(v)) {
                M.pend[v] = null;
                continue;
            }
            // Fresh: drawn at this instruction, or showing EIP anyway (an
            // instruction repeated in place: rep movs, a jump to itself).
            const g = M.good[v];
            if ((g && g.st === MKB.stepper && g.steps === MKB.stepper.steps) || shows(v, MKB.stepper.nextPhys())) {
                M.pend[v] = null;
                continue;
            }
            const t = now();
            if (M.pend[v] == null)
                M.pend[v] = t;
            else if (t - M.pend[v] > limit()) {
                if (M.viol.length < 20)
                    M.viol.push({ kind: 'stale', for_ms: Math.round(t - M.pend[v]), limit_ms: Math.round(limit()),
                        drawn_steps: g && g.st === MKB.stepper ? g.steps : null, ...state(v) });
                M.pend[v] = t;
            }
        }
    }, 50);
}"""


def follow_install(page):
    # The Page tab's listing exists once that tab has been shown (A1: mounted then).
    wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && (MKB.tab !== 'page' || !!document.getElementById('listing'))"), 60)
    page.evaluate(FOLLOW_EIP_JS)


def follow_eip_case(browser, base, shots, kernel, tab, throttle):
    """The invariant through a fixed sequence on one kernel and tab: from the
    kernel entry at 1/s, then 10/s, 1k/s, Pause, Step and Step 100,
    Continue at 1/s, a Run to that steps (as fast as it can) to the end of
    the run, then Boot at full speed (stepped and counted to the gate
    lines, then the JIT) to the end. Regression for two holes: Code at EIP and the Page
    listing kept the box where a Run to (or counting) had started while the
    count went on, and the listing marked nothing while EIP ran the
    relocation stub inside vmlinux's startup_32 (the instructions at
    0x100000 right after the entry)."""
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    if throttle > 1:
        ctx.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": throttle})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed=1#{tab}")
    if not wait_for(lambda: page.evaluate("typeof window.MKB?.entryPhase !== 'undefined' && MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 180):
        ctx.close()
        return [{"no slow motion after the entry": page.evaluate(RUN_STATE_JS)}], {}
    follow_install(page)
    phases = []

    def phase(label, secs):
        time.sleep(secs)
        phases.append([label, page.evaluate("MKB.stepper.steps"), page.evaluate("mkbFollow.draws." + ("context" if tab == "debugger" else "listing"))])

    phase("1/s", 6)
    machine_do(page, "select_option", "#dbg_speed", "10")
    phase("10/s", 4)
    # 1k/s for 4 s or a few instructions: hello's run ends about 450 after
    # its entry (and a click takes a while at 4x).
    s0 = page.evaluate("MKB.stepper.steps")
    machine_do(page, "select_option", "#dbg_speed", "1000")
    wait_for(lambda: page.evaluate("MKB.stepper.steps") - s0 > (20 if kernel.startswith("hello") else 100), 4, 0.02)
    phase("1000/s", 0)

    def act(sel):
        # From either tab (Step N and Run to are in the Debugger tab); a
        # control the end of the run has disabled is left alone.
        page.evaluate("s => { const b = document.querySelector(s); if (!b.disabled) b.click(); }", sel)

    act("#dbg_pause")
    phase("Pause", 2)
    machine_do(page, "select_option", "#dbg_speed", "1")
    act("#dbg_step")
    phase("Step", 1.5)
    act("#dbg_stepn")
    phase("Step 100", 2)
    act("#dbg_continue")
    phase("Continue at 1/s", 4)
    # Not in the kernel: no breakpoint, the Run to steps to the end of the run.
    page.evaluate("() => { document.getElementById('dbg_addr').value = '0x1000'; }")
    act("#dbg_runto")
    wait_for(lambda: page.evaluate("MKB.runState === 'paused' && MKB.runTo === null && !MKB.ffActive && !!MKB.stepper.end"), 300)
    phase("Run to 0x1000 (stepped to the end)", 2)
    # Boot at full speed: the boot is stepped and counted from the reset
    # vector to the gate lines, then the JIT runs it to its end.
    machine_do(page, "select_option", "#dbg_speed", "full")
    show_tab(page, "machine")
    page.click("#boot")
    wait_for(lambda: page.evaluate("MKB.runState === 'paused' && !MKB.ffActive && !!MKB.stepper && !!MKB.stepper.end"), 300)
    phase("Boot at full speed (counting), to the end", 2)
    viol = page.evaluate("mkbFollow.viol")
    page.screenshot(path=str(shots / f"follow-eip-{kernel}-{tab}-{throttle}x.png"))
    problems = [{"follow EIP": viol[:4]}] if viol else []
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"phases": phases}


# (kernel, tab, CPU throttling) for follow_eip_case.
FOLLOW_EIP_CASES = [(k, t, c) for k in ("minified-linux", "minified-linux-os", "hello") for t in ("debugger", "page") for c in (1, 4)]


def suite_followeip(ctx):
    failed = False
    for kernel, tab, throttle in [c for c in FOLLOW_EIP_CASES if c[0] in ctx.entries]:
        t0 = time.monotonic()
        p, r = follow_eip_case(ctx.browser, ctx.base, ctx.shots, kernel, tab, throttle)
        failed |= report("follow_eip", p, r, kernel=kernel, tab=tab, throttle=throttle, seconds=round(time.monotonic() - t0))
    return failed
