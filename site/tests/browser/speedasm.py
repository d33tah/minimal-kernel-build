"""Browser test suite speedasm (test_browser.py --suites)."""

import time

from tests.browser.common import dbg_shown_once, PHONE, report, wait_for
from tests.browser.speedline import speed_line_bad, SPEED_LINE_JS


# ---- memeip: where EIP is in the Memory tab (M1) --------------------------
# The operator: "na memory view pokazuj eip". The page square holding the
# next instruction's physical address (paging-aware) carries a mark of its
# own (#pages_eip, .memeip; ESP's page a weaker .memesp), labelled in the
# grid's left gutter (.memtag, with a popover); the region bar holding it is
# marked (.eiphere, a tick .eiptick); a line above the grid (#mem_eip) says
# "EIP 0x... (phys 0x...) symbol+0x.., page 0x..." and opens the Page tab at
# EIP. The checks read the CPU independently: CS base + EIP from v86's
# registers, then its page tables from guest RAM (2-level, PSE, PAE).

MEMEIP_JS = """() => {
  if (window.mkbEip) return;
  const u32 = (a) => { const b = MKB.emulator.read_memory(a, 4); return (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0; };
  const walk = (lin) => {
    const c = MKB.emulator.v86.cpu;
    if (!(c.cr[0] & 0x80000000)) return lin >>> 0;
    const cr3 = c.cr[3] >>> 0, pae = !!(c.cr[4] & 0x20), pse = !!(c.cr[4] & 0x10);
    if (pae) {
      const pdpte = u32((cr3 & ~0x1f) + ((lin >>> 30) & 3) * 8); if (!(pdpte & 1)) return -1;
      const pde = u32((pdpte & ~0xfff) + ((lin >>> 21) & 0x1ff) * 8); if (!(pde & 1)) return -1;
      if (pde & 0x80) return ((pde & 0xffe00000) | (lin & 0x1fffff)) >>> 0;
      const pte = u32((pde & ~0xfff) + ((lin >>> 12) & 0x1ff) * 8); if (!(pte & 1)) return -1;
      return ((pte & ~0xfff) | (lin & 0xfff)) >>> 0;
    }
    const pde = u32((cr3 & ~0xfff) + (lin >>> 22) * 4); if (!(pde & 1)) return -1;
    if (pse && (pde & 0x80)) return ((pde & 0xffc00000) | (lin & 0x3fffff)) >>> 0;
    const pte = u32((pde & ~0xfff) + ((lin >>> 12) & 0x3ff) * 4); if (!(pte & 1)) return -1;
    return ((pte & ~0xfff) | (lin & 0xfff)) >>> 0;
  };
  const cpu = () => { const c = MKB.emulator.v86.cpu, eip = c.get_real_eip() >>> 0, lin = (c.segment_offsets[1] + eip) >>> 0;
    const pm = !!(c.cr[0] & 1), esp = c.reg32[4] >>> 0, slin = (c.segment_offsets[2] + (pm ? esp : esp & 0xffff)) >>> 0;
    return { eip, lin, phys: walk(lin), esp, sphys: pm ? walk(slin) : -1, steps: MKB.stepper.steps, paging: !!(c.cr[0] & 0x80000000) }; };
  const M = window.mkbEip = { checks: 0, viol: [], walk, cpu };
  // Each drawing of the mark: in the same task as the drawing, the CPU is
  // where the mark says.
  const seen = new WeakSet();
  const watch = () => { const e = document.getElementById('pages_eip'); if (!e || seen.has(e)) return; seen.add(e);
    new MutationObserver(() => { if (e.hidden || e.dataset.page === undefined || e.dataset.page === '') return;
      const w = cpu(); M.checks++;
      if (Number(e.dataset.steps) !== w.steps || Number(e.dataset.page) !== Math.floor(w.phys / 4096))
        if (M.viol.length < 20) M.viol.push({ kind: 'drawn', mark_page: e.dataset.page, mark_steps: e.dataset.steps, ...w });
    }).observe(e, { attributes: true, attributeFilter: ['data-steps', 'data-page'] }); };
  watch(); setInterval(watch, 500);
  // Slow motion: the mark follows within 1.5 s of an instruction.
  let pend = null;
  setInterval(() => { const e = document.getElementById('pages_eip');
    if (!e || typeof window.MKB?.stepper === 'undefined' || !MKB.stepper || MKB.runState !== 'stepped' || MKB.entryPhase || MKB.ffActive || MKB.fastStepping()
        || document.getElementById('panel_memory').hidden) { pend = null; return; }
    if (Number(e.dataset.steps) === MKB.stepper.steps) { pend = null; return; }
    const t = Date.now(); if (pend === null) pend = t;
    else if (t - pend > 1500) { if (M.viol.length < 20) M.viol.push({ kind: 'stale', for_ms: t - pend, mark_steps: e.dataset.steps, steps: MKB.stepper.steps }); pend = t; }
  }, 100);
}"""


LONG_TASKS_JS = """() => { window.mkbLong = []; new PerformanceObserver((l) => {
    for (const e of l.getEntries()) window.mkbLong.push(e.duration); }).observe({ type: 'longtask' }); }"""
# Page: data-draws counts full drawings and moves of the EIP mark (data-reads,
# the full ones, on apps from before B3).
# (No #listing before the Page tab is first shown: no drawings.)
VIEW_DRAWS_JS = """() => { const l = document.getElementById('listing'); return { page: l ? Number(l.dataset.draws || l.dataset.reads || 0) : 0,
    debugger: Number(document.getElementById('context').dataset.draws || 0),
    memory: Number(document.getElementById('pages').dataset.draws || 0),
    long: (window.mkbLong || []).reduce((a, b) => a + b, 0), longMax: Math.max(0, ...(window.mkbLong || [])) }; }"""


def speed_line_asm_case(browser, base, shots, kernel, tab, throttle, rate, secs=25):
    """The operator's bug 1 as the audit reproduced it: slow motion with an
    asm listing open on a phone-class CPU (390 px touch, CDP CPU throttling).
    Before the fix the Page tab's listing (about 1,400 instructions) was
    drawn again on every animation frame and the step credit grew by at most
    250 ms per frame, so at 4x the run made 6 instructions in 30 s while the
    page said "slow motion 1/s" and "speed 0/s". Checks over secs seconds
    after the tab is opened: the speed line as speed_line_bad (never 0/s;
    the set rate within a factor of two once the speed window has passed
    since the tab was opened), the count moves at a
    third of the set rate or more, the listing (Page) or Code at EIP
    (Debugger) is drawn at most once per executed instruction (plus a
    full refresh every few seconds), and the main
    thread is not busy most of the time (long tasks under 70 % of the wall
    time)."""
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": throttle})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed={rate}")
    wait_for(lambda: page.evaluate("typeof window.MKB?.entryPhase !== 'undefined' && MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 180)
    # VIEW_DRAWS_JS reads Code at EIP (#context): there once the Debugger tab has been shown.
    dbg_shown_once(page)
    page.tap("#tab_" + tab)
    page.evaluate(LONG_TASKS_JS)
    if tab == "memory":
        page.evaluate(MEMEIP_JS)
    problems, samples, zero = [], 0, 0
    d0 = page.evaluate(VIEW_DRAWS_JS)
    s0 = page.evaluate(SPEED_LINE_JS)
    t0 = time.monotonic()
    while time.monotonic() - t0 < secs:
        s = page.evaluate(SPEED_LINE_JS)
        samples += 1
        why = speed_line_bad(s)
        # Opening the tab draws the listing once (up to 2 s at 6x): the
        # 4 s speed window shows that dip until it has passed; 0/s never.
        if why and (time.monotonic() - t0 >= SPEED_WINDOW_SLOW_S or why.startswith("shows 0/s")):
            zero += why.startswith("shows 0/s")
            if len(problems) < 6:
                problems.append({"at_s": round(time.monotonic() - t0, 1), "why": why, "line": s["text"]})
        time.sleep(0.1)
    wall = (time.monotonic() - t0) * 1000
    s = page.evaluate(SPEED_LINE_JS)
    d = page.evaluate(VIEW_DRAWS_JS)
    ran = s["steps"] - s0["steps"]
    draws = d[tab] - d0[tab]
    if s["state"] == "stepped" and not s["end"] and ran < float(rate) * secs / 3:
        problems.append({"stalled": f"{ran} instructions in {secs} s at {rate}/s"})
    if draws > ran + secs / 2 + 2:
        problems.append({"listing drawn": f"{draws} times for {ran} instructions in {secs} s"})
    if d["long"] > 0.7 * wall:
        problems.append({"main thread busy": f"long tasks {d['long'] / 1000:.1f} s of {wall / 1000:.1f} s"})
    eip = page.evaluate("window.mkbEip ? [mkbEip.checks, mkbEip.viol] : null") if tab == "memory" else None
    if eip and (eip[1] or eip[0] < min(ran, secs) / 2):
        problems.append({"EIP mark (M1)": f"drawn {eip[0]} times for {ran} instructions; {eip[1][:3]}"})
    shot = f"speed-line-{tab}-{kernel}-{throttle}x-{rate}.png"
    page.screenshot(path=str(shots / shot))
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"samples": samples, "zero_samples": zero, "instructions": ran, "listing_draws": draws,
                      "long_tasks_s": round(d["long"] / 1000, 1), "longest_ms": round(d["longMax"]), "line": s["text"]}


# app.js speedWindowMs() in slow motion.
SPEED_WINDOW_SLOW_S = 4
# (kernel, tab, CPU throttling, rate): the asm listings open in slow motion.
SPEED_LINE_ASM = [("minified-linux-os", "page", 4, "1"), ("minified-linux-os", "page", 4, "10"),
                  ("minified-linux-os", "page", 6, "1"), ("minified-linux-os", "page", 6, "10"),
                  ("minified-linux-os", "debugger", 6, "1"), ("minified-linux-os", "debugger", 6, "10"),
                  ("hello", "page", 4, "1"), ("minified-linux-os", "memory", 6, "10")]


def suite_speedasm(ctx):
    failed = False
    for kernel, tab, throttle, rate in [c for c in SPEED_LINE_ASM if c[0] in ctx.entries]:
        t0 = time.monotonic()
        p, r = speed_line_asm_case(ctx.browser, ctx.base, ctx.shots, kernel, tab, throttle, rate)
        failed |= report("speed_line_asm", p, r, kernel=kernel, tab=tab, throttle=throttle, rate=rate,
                         seconds=round(time.monotonic() - t0))
    return failed
