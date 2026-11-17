"""Browser test suite speedline (test_browser.py --suites)."""

import re
import time

from tests.browser.common import machine_do, PHONE, report, wait_for


SPEED_LINE_JS = """() => ({
    state: MKB.runState, entry: MKB.entryPhase, runTo: MKB.runTo !== null, ff: MKB.ffActive, end: !!(MKB.stepper && MKB.stepper.end),
    halted: !!MKB.stepper && MKB.stepper.halted(), set: document.getElementById('dbg_speed').value,
    text: document.getElementById('vclock').textContent.split(' \u00b7 ')[0],
    steps: MKB.stepper ? MKB.stepper.steps : -1,
})"""


def speed_line_bad(s):
    """None, or why the speed line s (SPEED_LINE_JS) is wrong for a slow-motion run."""
    if s["state"] != "stepped" or s["entry"] or s["runTo"] or s["ff"] or s["end"] or s["set"] == "full":
        return None
    if s["text"] == "speed: measuring":
        return None
    # Below half the set rate the line adds " (set 1/s: too fast for this device)".
    m = re.fullmatch(r"speed ([0-9.]+)(k|M)?/s( \(set [^)]*\))?", s["text"])
    if not m:
        return "unreadable"
    v = float(m.group(1)) * {"k": 1e3, "M": 1e6}.get(m.group(2), 1)
    want = float(s["set"])
    return None if want / 2 <= v <= want * 2 else f"shows {v:g}/s, set {want:g}/s"


def speed_line_case(browser, base, shots, touch, rate):
    """The speed line while running in slow motion (the operator: "not paused
    and zero instructions per second", on a phone at 1/s). Sampled every
    100 ms for about 60 s, through Pause and Continue, a speed change, the
    tab hidden for 6 s (the run must go on), then frozen for 8 s (as a
    phone freezes a background tab) and shown again; phone: 390 px touch, CPU 5x slower. While running it must
    show the set rate within a factor of two, or "measuring" right after a
    change; never 0/s. And the run itself must not stall: the count moves
    at the set rate."""
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    if touch:
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 5})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed={rate}")
    wait_for(lambda: page.evaluate("typeof window.MKB?.entryPhase !== 'undefined' && MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 120)
    act = page.tap if touch else page.click
    problems, samples = [], 0

    def sample(secs, label):
        nonlocal samples
        t0 = time.monotonic()
        s0 = page.evaluate(SPEED_LINE_JS)
        while time.monotonic() - t0 < secs:
            s = page.evaluate(SPEED_LINE_JS)
            samples += 1
            why = speed_line_bad(s)
            if why and len(problems) < 10:
                problems.append({"after": label, "at_s": round(time.monotonic() - t0, 1), "why": why, "line": s["text"]})
            time.sleep(0.1)
        s = page.evaluate(SPEED_LINE_JS)
        ran = s["steps"] - s0["steps"]
        if s["state"] == "stepped" and not s["end"] and ran < float(s["set"]) * secs / 3:
            problems.append({"after": label, "stalled": f"{ran} instructions in {secs} s at {s['set']}/s"})

    def hide(hidden_s, frozen_s):
        # As a browser does for a hidden tab: no animation frames until shown
        # (headless Chromium would go on running them).
        page.evaluate("""() => {
            Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
            Object.defineProperty(document, 'hidden', { value: true, configurable: true });
            window.mkbRaf = [window.requestAnimationFrame, window.cancelAnimationFrame];
            window.mkbHeld = new Map();
            let id = 1e9;
            window.requestAnimationFrame = (cb) => { window.mkbHeld.set(++id, cb); return id; };
            window.cancelAnimationFrame = (i) => { if (!window.mkbHeld.delete(i)) window.mkbRaf[1].call(window, i); };
            document.dispatchEvent(new Event('visibilitychange'));
        }""")
        # Hidden (a background tab: no animation frames): the run goes on.
        before = page.evaluate("MKB.stepper.steps")
        time.sleep(hidden_s)
        ran = page.evaluate("MKB.stepper.steps") - before
        if ran < float(rate) * hidden_s / 3:
            problems.append({"hidden": f"{ran} instructions in {hidden_s} s at {rate}/s"})
        cdp.send("Page.setWebLifecycleState", {"state": "frozen"})
        time.sleep(frozen_s)
        cdp.send("Page.setWebLifecycleState", {"state": "active"})
        page.evaluate("""() => {
            delete document.visibilityState;
            delete document.hidden;
            // The event first (frames it cancels stay cancelled), then the held frames run.
            document.dispatchEvent(new Event('visibilitychange'));
            [window.requestAnimationFrame, window.cancelAnimationFrame] = window.mkbRaf;
            const held = [...window.mkbHeld.values()];
            window.mkbHeld.clear();
            for (const cb of held)
                window.requestAnimationFrame(cb);
        }""")

    other = "10" if rate == "1" else "1"
    sample(15, "start")
    act("#dbg_pause")
    time.sleep(2)
    act("#dbg_continue")
    sample(12, "Pause, Continue")
    machine_do(page, "select_option", "#dbg_speed", other)
    time.sleep(3)
    machine_do(page, "select_option", "#dbg_speed", rate)
    sample(12, f"speed {other} then {rate}")
    hide(6, 8)
    sample(15, "hidden 6 s, frozen 8 s, shown again")
    page.screenshot(path=str(shots / f"speed-line-{rate}-{'phone' if touch else 'desktop'}.png"))
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"samples": samples}


def suite_speedline(ctx):
    failed = False
    for touch, rate in ((False, "1"), (True, "1"), (True, "10")):
        t0 = time.monotonic()
        p, r = speed_line_case(ctx.browser, ctx.base, ctx.shots, touch, rate)
        failed |= report("speed_line", p, r, touch=touch, rate=rate, seconds=round(time.monotonic() - t0))
    return failed
