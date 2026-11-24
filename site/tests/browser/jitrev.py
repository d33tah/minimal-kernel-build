"""Browser test suite jitrev (test_browser.py --suites)."""

import os
import time

from tests.browser.common import (
    machine_do, PHONE, replay_state, replay_wait, report, rev_click, show_tab, snapdiff_guard, wait_for,
)
from tests.browser.reverse import REV_SAME, REV_STATE_JS


# ---- jitrev: reverse execution at JIT speed (R5) ----------------------------------
# While the JIT runs on the virtual clock the page keeps checkpoints too (a
# v86 snapshot and the reset of v86's hidden state, logged with the run, at
# the start of every JIT stretch and every second or so of JIT running).
# Step back in a JIT stretch restores the last checkpoint before the target
# and runs the JIT again to the target itself: v86-det.patch's instruction
# budget stops compiled code before a basic block that would cross it and
# interprets the rest with the counter compiled code reads the clock with.
# Checked against the run itself: Step forward from there again to the pause
# (the live state there, registers, RAM and clock), and a Replay from boot to
# the count stepped back to (J4, no restore of any checkpoint on the way)
# ends with the same EIP, RAM, screen, clock and registers. Debian (kernel
# code), Debian+initrd in initramfs userspace (R3: 0 of 150 JIT slices
# reproduced by the stepper there) and at the (initramfs) prompt (the last
# stretch stepped by the exact stop).
JITREV_RUNS = [  # kernel, scheme, phone, pause past this count (None: the prompt), ?revjitk=
    ("debian-6.1-686", "light", False, 2.5e9, 40000000),
    ("debian-6.1-686-initrd", "dark", True, 3.2e9, None),  # the initramfs scripts
    ("debian-6.1-686-initrd", "light", False, None, None),
]
JITREV_REGS_JS = """() => { const c = MKB.stepper.cpu; return { regs: Array.from(c.reg32, (x) => x >>> 0), sregs: Array.from(c.sreg),
  eflags: c.get_eflags() >>> 0, cr: [c.cr[0] >>> 0, c.cr[2] >>> 0, c.cr[3] >>> 0, c.cr[4] >>> 0] }; }"""
JITREV_LATENCY_MS = 10000
JITREV_STI_NOTE = "the instruction after an sti runs with it"
# Worker calls that unpack a stored checkpoint, counted from here (A3h item
# 2): Step back from the pause restores the newest checkpoint, whose state
# the page keeps; unpacking its chain of deltas took 7.9 s of the 11 s that
# Debian+initrd's first Step back 1 at the prompt took (d-claude).
JITREV_UNPACKS_JS = """() => { const m = MKB.api.machine; if (!('__unpacks' in window)) { const f = m.snapWorker;
  m.snapWorker = function (msg, tr) { if (msg && msg.op === 'unpack') window.__unpacks++; return f.call(this, msg, tr); }; }
  window.__unpacks = 0; return true; }"""


def jitrev_back(page, phone, sel, n, tag, problems, r, key):
    if n is not None:
        page.fill("#dbg_n", str(n))
    wall, ms, text, st = rev_click(page, sel, phone, 300)
    r[key] = {"wall_ms": wall, "page_ms": ms, "status": text[:300]}
    if st != "done":
        problems.append(f"{tag}: {key}: {st}: {text[:300]!r}")
    elif wall > JITREV_LATENCY_MS:
        problems.append(f"{tag}: {key} took {wall} ms")
    return page.evaluate(REV_STATE_JS)


def jitrev_replay(page, run, want, regs, tag, problems, r, key, ui):
    """Replay run (ending at the count stepped back to) from boot: the page's
    verdict, and the registers there."""
    # The run box's status is the Snapshots tab's, drawn once the tab was shown (A1: mounted on first show).
    show_tab(page, "snapshots")
    page.evaluate("() => { MKB.INPUT.replay = null; delete document.getElementById('run_status').dataset.replay; }")
    if ui:
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        page.click("#run_replay")
    else:
        page.evaluate("(run) => { MKB.replayRun(run); }", run)
    wait_for(lambda: page.evaluate("!!MKB.INPUT.replay || document.getElementById('run_status').dataset.replay === 'error'"), 60)
    if page.evaluate("document.getElementById('run_status').dataset.replay") == "error":
        problems.append(f"{tag}: {key}: {page.inner_text('#run_status')[:300]!r}")
        return
    if not replay_wait(page, 900):
        problems.append(f"{tag}: {key}: the replay did not finish ({page.evaluate('[MKB.runState, MKB.stepper && MKB.stepper.steps]')})")
        return
    b = replay_state(page)
    got = page.evaluate(JITREV_REGS_JS)
    r[key] = {"steps": b["steps"], "verdict": b["verdict"], "status": b["status"][:300], "late": (b["replay"] or {}).get("late")}
    if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
        problems.append(f"{tag}: {key}: the replay from boot: {b['verdict']} {b['replay']} {b['status'][:300]!r}")
    if b["steps"] != want["steps"]:
        problems.append(f"{tag}: {key}: the replay ended at {b['steps']:,}, stepped back to {want['steps']:,}")
    for k in ("regs", "sregs", "eflags", "cr"):
        if got[k] != regs[k]:
            problems.append(f"{tag}: {key}: {k} after the replay {got[k]}, stepped back {regs[k]}")


def jitrev_run(browser, base, shots, name, scheme, phone, past, k):
    problems, r = [], {}
    w = 390 if phone else 1280
    tag = f"jitrev {name} {w} {scheme} {'prompt' if past is None else f'{past:.2e}'}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10" + (f"&revjitk={k}" if k else ""), timeout=180000)
        if past is None:
            if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.stepper.prompt && MKB.runState === 'paused'"), 900, 1):
                return [f"{tag}: no pause at the (initramfs) prompt"], r
        else:
            if not wait_for(lambda: page.evaluate(f"typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.stepper.steps > {past} && MKB.runState === 'jit'"), 900, 0.2):
                return [f"{tag}: the JIT did not pass {past:.2e}"], r
            machine_do(page, "tap" if phone else "click", "#dbg_pause")
            wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        show_tab(page, "debugger")
        time.sleep(0.5)
        sn = page.evaluate(REV_STATE_JS)
        N = sn["steps"]
        r["paused_at"] = N
        r["history"] = page.evaluate("""() => ({ text: document.getElementById('rev_info').textContent,
          cps: typeof window.MKB?.REV === 'object' ? MKB.REV.cps.map((c) => [c.steps, !!c.jit]) : null,
          segs: typeof window.MKB?.REV === 'object' && MKB.REV.segs ? MKB.REV.segs.map((s) => [s.at, s.kind]) : null,
          back: !document.getElementById('dbg_back').disabled })""")
        if not r["history"]["back"]:
            problems.append(f"{tag}: Step back is disabled after the JIT ran: {r['history']['text'][:200]!r}")
            return problems, r
        show_tab(page, "machine")
        page.evaluate(JITREV_UNPACKS_JS)
        s1 = jitrev_back(page, phone, "#dbg_back", None, tag, problems, r, "back 1")
        r["back 1 unpacks"] = page.evaluate("window.__unpacks")
        if r["back 1 unpacks"]:
            problems.append(f"{tag}: Step back 1 from the pause unpacked {r['back 1 unpacks']} stored checkpoint(s) in the Worker")
        # R6: v86 runs the instruction after an sti in the sti's step, so
        # from the hlt of "sti; hlt" (the initramfs prompt's halt) Step back
        # 1 lands on the sti, 2 back, and says why (it said "Stepped back 0
        # instructions" and stayed at the hlt).
        e1 = N - 2 if JITREV_STI_NOTE in r["back 1"]["status"] else N - 1
        r["back 1 expected"] = e1
        if s1["steps"] != e1:
            problems.append(f"{tag}: Step back 1 from {N:,} landed at {s1['steps']:,}, not {e1:,}")
        if e1 == N - 2:
            op = page.evaluate("MKB.emulator.read_memory(MKB.stepper.nextPhys(), 1)[0]")
            r["back 1 opcode"] = op
            if op != 0xFB:
                problems.append(f"{tag}: Step back 1 says the target is inside an sti's step, but the instruction there is {op:#x}")
        run1 = page.evaluate("MKB.runNow()")
        regs1 = page.evaluate(JITREV_REGS_JS)
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-back1.png"))
        show_tab(page, "debugger")
        s1000 = jitrev_back(page, phone, "#dbg_backn", 999, tag, problems, r, "back 1000")
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-back1000.png"))
        e1000 = e1 - 999 - (1 if JITREV_STI_NOTE in r["back 1000"]["status"] else 0)
        if s1000["steps"] != e1000:
            problems.append(f"{tag}: Step back 999 more from {e1:,} landed at {s1000['steps']:,}, not {e1000:,}")
        run1000 = page.evaluate("MKB.runNow()")
        regs1000 = page.evaluate(JITREV_REGS_JS)
        # Forward again on the run: at N - 1 and at the pause, as they were.
        page.fill("#dbg_n", str(e1 - e1000))
        t0 = time.monotonic()
        page.locator("#dbg_stepn").tap() if phone else page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") == e1 and page.evaluate("!MKB.REV.busy && MKB.runState === 'paused'"), 300, 0.1)
        r["forward 999 ms"] = round((time.monotonic() - t0) * 1000)
        f1 = page.evaluate(REV_STATE_JS)
        for k in REV_SAME:
            if f1[k] != s1[k]:
                problems.append(f"{tag}: forward to {e1:,} again: {k} {str(f1[k])[:120]} vs stepped back {str(s1[k])[:120]}")
        page.evaluate("MKB.doStep(1)")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") == N and page.evaluate("!MKB.REV.busy && MKB.runState === 'paused'"), 300, 0.1)
        fn = page.evaluate(REV_STATE_JS)
        for k in REV_SAME:
            if fn[k] != sn[k]:
                problems.append(f"{tag}: forward to the pause {N:,} again: {k} {str(fn[k])[:120]} vs the run {str(sn[k])[:120]}")
        # Where the windows are: user mode (CS & 3) in the last 1,000 of them.
        r["user mode"] = [s1000["sregs"][1] & 3, s1["sregs"][1] & 3]
        r["states"] = {"N": {k: sn[k] for k in ("steps", "eip", "ram", "clock")}, "N-1": {k: s1[k] for k in ("steps", "eip", "ram", "clock")},
                       "N-1000": {k: s1000[k] for k in ("steps", "eip", "ram", "clock")}}
        # Back to looks in the stretches it can watch per instruction: one
        # the JIT ran is crossed, and the status says so.
        page.fill("#dbg_addr", "schedule")
        wall, ms, text, st = rev_click(page, "#dbg_backto", phone, 300)
        r["back to"] = {"wall_ms": wall, "status": text[:300], "state": st}
        if st != "done" or "JIT" not in text:
            problems.append(f"{tag}: Back to schedule over a JIT stretch: {st} {text[:300]!r}")
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
        # Forward on the run again to N - 1 (from the history's start, where
        # Back to stopped), and Replay that run from boot there.
        page.evaluate(f"MKB.doStep({e1} - MKB.stepper.steps)")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") == e1 and page.evaluate("!MKB.REV.busy && MKB.runState === 'paused'"), 300, 0.1)
        g1 = page.evaluate(REV_STATE_JS)
        r["forward to N-1 again"] = page.inner_text("#dbg_status")[:300]
        for k_ in REV_SAME:
            if g1[k_] != s1[k_]:
                problems.append(f"{tag}: forward from the history's start to {e1:,}: {k_} {str(g1[k_])[:120]} vs stepped back {str(s1[k_])[:120]}")
        jitrev_replay(page, run1, s1, regs1, tag, problems, r, "replay to N-1", True)
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-replayed.png"))
        jitrev_replay(page, run1000, s1000, regs1000, tag, problems, r, "replay to N-1000", False)
    finally:
        ctx.close()
    return problems, r


def jitrev_case(browser, base, shots, entries):
    out = []
    only = os.environ.get("MKB_JITREV_ONLY")
    for i, (name, scheme, phone, past, k) in enumerate(JITREV_RUNS):
        if name not in entries or (only and str(i) not in only.split(",")):
            continue
        t0 = time.monotonic()
        problems, r = snapdiff_guard(jitrev_run, browser, base, shots, name, scheme, phone, past, k)
        out.append(report("jitrev", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_jitrev(ctx):
    return jitrev_case(ctx.browser, ctx.base, ctx.shots, ctx.entries)
