"""Browser test suite reverse (test_browser.py --suites)."""

import json
import os
import time

from tests.browser.common import machine_do, PHONE, report, rev_click, run_to_wait, show_tab, snapdiff_guard, wait_for


# ---- reverse: reverse execution in stepped mode (R2) ---------------------------
# While the page steps the CPU on the virtual clock it keeps checkpoints (R1
# deltas in memory); Step back and Back to (reverse continue) restore the last
# one before the target and step forward to it. Checked against the machine
# as the forward run had it at the same counts: registers, EFLAGS, CR, v86's
# counter, clock, IRQ0 count, RAM SHA-256 and the VGA text; stepping forward
# again gives the same state and does not count the recordings (coverage,
# paths, calls) twice; keys typed in the stepped-over stretch go in again at
# their counts; typing or the JIT after stepping back drops what was ahead.
REV_STATE_JS = """async () => { const st = MKB.stepper, c = st.cpu, m = c.mem8.slice(0, MKB.emuRam);
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  return { steps: st.steps, phys: st.nextPhys(), eip: c.instruction_pointer[0] >>> 0, regs: Array.from(c.reg32, (x) => x >>> 0),
    sregs: Array.from(c.sreg), eflags: c.get_eflags() >>> 0, cr: [c.cr[0] >>> 0, c.cr[2] >>> 0, c.cr[3] >>> 0, c.cr[4] >>> 0],
    icount: c.instruction_counter[0] >>> 0, clock: MKB.CLOCK.now(), irq0: st.irq0, hlt: c.in_hlt[0], ram: sha, vga: MKB.runScreenHash(st) }; }"""
REV_SAME = ("steps", "phys", "eip", "regs", "sregs", "eflags", "cr", "icount", "clock", "irq0", "hlt", "ram", "vga")
REV_REC_JS = """() => { const st = MKB.stepper; let ns = 0; for (const x of st.nodeSteps) ns += x;
  return { unique: st.unique, uniqueKernel: st.uniqueKernel, jumps: st.jumpCount, jumpKeys: st.jumps.size, frames: st.fr.n,
    nodeSteps: ns, edges: st.edges.size, history: st.history.length, switches: st.swStep.length,
    end: st.end ? st.end.steps : null, milestones: st.milestones.map((m) => m.found ? m.found.steps : null) }; }"""
# One pass over [steps, to) stopping at every function's first instruction:
# (count, address, registers) of each call, the way Back to must find them.
REV_ORACLE_JS = """({ to, hits }) => { const st = MKB.stepper, c = st.cpu;
  if (!window.mkbRevSet) window.mkbRevSet = new Set(MKB.codeSymbols().map((s) => s.paddr));
  let leave = -1;
  while (st.steps < to) {
    const r = st.run(to - st.steps, window.mkbRevSet, 5000, leave);
    leave = -1;
    if (r === 'addr') { hits.push([st.steps, st.nextPhys(), Array.from(c.reg32, (x) => x >>> 0), c.get_eflags() >>> 0]); leave = st.nextPhys(); }
    else if (r === 'hlt' && st.dead()) break;
  }
  return hits; }"""
REV_CASES = {
    # kernel: (URL params, Run to first, how far past it, how far the deep step back goes).
    # hello and the minified Linux are stepped from the reset vector with a
    # checkpoint every 20,000 instructions (so the 32 kept do not reach
    # back to the start); Debian runs the JIT to startup_32 and is stepped
    # from there with the page's own interval (half a second of stepping).
    "hello": ("fastforward=0&revk=20000", "0x100000", 400, 300000),
    "minified-linux": ("fastforward=0&revk=20000", "0x100000", 300000, 250000),
    "debian-6.1-686": ("", "startup_32", 8000000, 5000000),
}


def rev_state(page):
    return page.evaluate(REV_STATE_JS)


def rev_diff(got, want):
    return {k: (str(got.get(k))[:70], str(want.get(k))[:70]) for k in REV_SAME if got.get(k) != want.get(k)}


def rev_step_to(page, target):
    """Step forward with the Debugger's Step (doStep) until stepper.steps == target."""
    for _ in range(4000):
        left = target - page.evaluate("MKB.stepper.steps")
        if left <= 0:
            break
        page.evaluate(f"MKB.doStep({min(left, 2000000)})")
    return page.evaluate("MKB.stepper.steps")


def rev_nav(page):
    return page.evaluate("[MKB.navList.length, MKB.navAt]")


def reverse_kernel(browser, base, shots, name, scheme, phone):
    """Forward from the kernel entry, noting the machine at E-deep, E-1000,
    E-1 and E (and every call in the last 20,000 instructions); then Step
    back 1 (the run bar), Step back 999 and Step back to E-deep (the
    Debugger), each the machine as it was there, 1 and 1,000 within a
    second; forward to E again: the same machine, the recordings not
    counted twice; back 100 and on to E + 30 records past E, and back 30
    is E; Back to the most called function twice: its last two calls; the
    history keeps the last 32 checkpoints."""
    params, first, past, deep = REV_CASES[name]
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"reverse {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def shot(s):
        page.screenshot(path=str(shots / f"reverse-{name}-{w}-{scheme}-{s}.png"), full_page=False)

    def start():
        page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10&{params}#debugger", timeout=120000)
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator && !!MKB.symbols"), 60)
        show_tab(page, "debugger")
        return ok

    try:
        if not start():
            return [f"{tag}: did not start paused"], r
        page.fill("#dbg_addr", first)
        page.click("#dbg_runto")
        want_at = int(first, 16) if first.startswith("0x") else page.evaluate(f"MKB.symbols.symbols.find((s) => s.name === '{first}' && s.paddr >= 0x100000).paddr")
        ok, at, status = run_to_wait(page, {want_at}, 600)
        if not ok:
            return [f"{tag}: Run to {first} stopped at {at:#x}: {status[:200]}"], r
        s0 = page.evaluate("MKB.stepper.steps")
        e = s0 + past
        if e - max(deep, 20000) < s0:
            # Stepped from the reset vector (fastforward=0): the same run
            # again, from the start, to note the machine before the entry.
            page.goto("about:blank")
            if not start():
                return [f"{tag}: did not start paused again"], r
        t0 = time.monotonic()
        if rev_step_to(page, e - deep) != e - deep:
            problems.append(f"{tag}: could not step to {e - deep} (test setup)")
        p_deep = rev_state(page)
        rev_step_to(page, e - 20000)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e - 1000, "hits": []})
        p1000 = rev_state(page)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e - 1, "hits": hits})
        p1 = rev_state(page)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e, "hits": hits})
        pe = rev_state(page)
        rec_e = page.evaluate(REV_REC_JS)
        page.evaluate("MKB.afterStop()")  # the views at E (the passes above ran the stepper directly)
        r["forward"] = {"entry": s0, "E": e, "seconds": round(time.monotonic() - t0, 1), "calls in the last 20,000": len(hits)}
        if pe["steps"] != e:
            problems.append(f"{tag}: the forward run ended at {pe['steps']}, want {e} (halted early?)")
        cps = page.evaluate("[MKB.REV.cps.length, MKB.REV.cps.length ? MKB.REV.cps[0].steps : -1, MKB.REV.kFixed || MKB.REV.k]")
        r["history"] = {"checkpoints": cps[0], "from": cps[1], "k": cps[2], "info": page.inner_text("#rev_info")[:200]}
        if cps[0] < 2 or cps[0] > 32:
            problems.append(f"{tag}: {cps[0]} checkpoints kept (want 2 to 32)")
        if cps[1] > e - deep:
            problems.append(f"{tag}: the history starts at {cps[1]}, after E-deep {e - deep}")
        nav0 = rev_nav(page)
        shot("forward")

        # Step back 1 from the run bar (Machine tab).
        show_tab(page, "machine")
        wall, ms, text, st = rev_click(page, "#dbg_back", phone)
        got = rev_state(page)
        r["back 1"] = {"wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p1):
            problems.append(f"{tag}: Step back 1: {st} {text[:160]!r}; differs from E-1: {rev_diff(got, p1)}")
        if wall > 1000:
            problems.append(f"{tag}: Step back 1 took {wall} ms (want <= 1000)")
        if page.evaluate(REV_REC_JS) != rec_e:
            problems.append(f"{tag}: the recordings changed by stepping back: {page.evaluate(REV_REC_JS)} vs {rec_e}")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        shot("back1-machine")

        # Step back 999 more (the Debugger's Step back with N).
        show_tab(page, "debugger")
        page.fill("#dbg_n", "999")
        wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
        got = rev_state(page)
        r["back 1000"] = {"wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p1000):
            problems.append(f"{tag}: Step back 999 more: {st} {text[:160]!r}; differs from E-1000: {rev_diff(got, p1000)}")
        if wall > 1000:
            problems.append(f"{tag}: Step back 1,000 took {wall} ms (want <= 1000)")
        info = page.inner_text("#rev_info")
        if "stepped back" not in info:
            problems.append(f"{tag}: the reverse line does not say it is stepped back: {info[:200]!r}")
        prev = page.inner_text("#prev_sym") if page.locator("#prev_sym").count() else ""
        if "stepped back" not in prev:
            problems.append(f"{tag}: 'previous' while stepped back: {prev[:120]!r}")
        page.evaluate("window.scrollTo(0, 0)")
        shot("back1000-debugger")

        # Deep: across checkpoints.
        page.fill("#dbg_n", str(deep - 1000))
        wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
        got = rev_state(page)
        r["back deep"] = {"n": deep, "wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p_deep):
            problems.append(f"{tag}: Step back to E-{deep}: {st} {text[:160]!r}; differs: {rev_diff(got, p_deep)}")
        # The Calls timeline while stepped back: drawn to the furthest count, now marked.
        show_tab(page, "calls")
        time.sleep(1)
        cv = page.evaluate("(() => { const c = document.querySelector('#panel_calls canvas'); return c ? { now: c.dataset.nowMarker, drawn: c.dataset.drawn } : null; })()")
        r["calls while back"] = cv
        shot("back-deep-calls")

        # Forward to E again: the same machine, the recordings not counted twice.
        show_tab(page, "debugger")
        page.fill("#dbg_n", str(deep))
        page.click("#dbg_stepn")
        rev_step_to(page, e)
        got = rev_state(page)
        if rev_diff(got, pe):
            problems.append(f"{tag}: forward to E again differs: {rev_diff(got, pe)}")
        rec = page.evaluate(REV_REC_JS)
        if rec != rec_e:
            problems.append(f"{tag}: the recordings at E after stepping back and forward: {rec} vs {rec_e}")
        # Back 100, then on to E + 30: the re-run catches up at E and records 30
        # new instructions; back 30 from there is the machine at E again.
        page.fill("#dbg_n", "100")
        _, _, text, st = rev_click(page, "#dbg_backn", phone)
        rev_step_to(page, e + 30)  # Step stops at a milestone (hello's line) on the way
        page.evaluate("MKB.afterStop()")
        past = page.evaluate("[MKB.stepper.steps, MKB.stepper.behind(), MKB.stepper.future.length]")
        rec = page.evaluate(REV_REC_JS)
        page.fill("#dbg_n", "30")
        _, _, text2, st2 = rev_click(page, "#dbg_backn", phone)
        got = rev_state(page)
        r["past E"] = [st, past, st2]
        if st != "done" or past != [e + 30, False, 0] or rec["unique"] < rec_e["unique"] or rec["jumps"] <= rec_e["jumps"] or \
                st2 != "done" or rev_diff(got, pe):
            problems.append(f"{tag}: back 100 ({st}), on to E + 30 gave [steps, behind, future] {past} (want [{e + 30}, False, 0]), "
                            f"jumps {rec_e['jumps']} -> {rec['jumps']}; back 30 ({st2}) differs from E: {rev_diff(got, pe)}")

        # Back to (reverse continue) the function called most in the last 20,000.
        calls = {}
        for h in hits:
            calls.setdefault(h[1], []).append(h)
        if calls:
            target, hl = max(calls.items(), key=lambda kv: (len(kv[1]), kv[1][-1][0]))
            r["back to"] = {"target": hex(target), "calls": len(hl)}
            page.fill("#dbg_addr", hex(target))
            for k in range(min(2, len(hl))):
                want = hl[-1 - k]
                wall, ms, text, st = rev_click(page, "#dbg_backto", phone)
                got = rev_state(page)
                r[f"back to {k + 1}"] = {"wall ms": wall, "page ms": ms, "steps": got["steps"], "want": want[0], "status": text[:200]}
                if st != "done" or got["steps"] != want[0] or got["phys"] != target or got["regs"] != want[2] or got["eflags"] != want[3]:
                    problems.append(f"{tag}: Back to {hex(target)} #{k + 1}: {st} at {got['steps']} {got['phys']:#x} "
                                    f"(want {want[0]}); {text[:200]!r}")
        else:
            problems.append(f"{tag}: no call in the last 20,000 instructions (test setup)")
        page.eval_on_selector("#dbg_status", "e => e.scrollIntoView()")
        shot("backto")
        # For comparison: a Step forward of 1 with its redrawing, as Step back has it.
        page.fill("#dbg_n", "1")
        t1 = time.monotonic()
        page.click("#dbg_stepn")
        wait_for(lambda: page.inner_text("#dbg_status").startswith("Stepped 1 "), 30, 0.05)
        r["step 1 wall ms"] = round((time.monotonic() - t1) * 1000)
        if rev_nav(page) != nav0:
            problems.append(f"{tag}: stepping back added jump history entries: {nav0} -> {rev_nav(page)}")
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")

        # Keys typed after stepping back: another way from here.
        if name != "debian-6.1-686":
            page.fill("#dbg_n", "50")
            rev_click(page, "#dbg_backn", phone)
            now = page.evaluate("MKB.stepper.steps")
            show_tab(page, "machine")
            page.click("#screen_container")
            page.keyboard.press("a")
            show_tab(page, "debugger")
            page.fill("#dbg_n", "1")
            page.click("#dbg_stepn")
            time.sleep(0.5)
            b = page.evaluate("[MKB.stepper.behind(), MKB.stepper.future.length, MKB.INPUT.redo.length, MKB.REV.cps.every((c) => c.steps <= %d),"
                              " Array.from({ length: MKB.stepper.fr.n }, (_, k) => MKB.stepper.fr.start(k)).every((s) => s <= MKB.stepper.steps), MKB.INPUT.log.filter((e) => e.keys).length]" % now)
            r["branch"] = b
            if b[0] or b[1] or b[2] or not b[3] or not b[4] or not b[5]:
                problems.append(f"{tag}: after a key typed while stepped back: behind {b[0]}, future {b[1]}, redo {b[2]}, "
                                f"checkpoints ahead dropped {b[3]}, calls ahead dropped {b[4]}, keys logged {b[5]}")
            s1 = page.evaluate("MKB.stepper.steps")
            page.fill("#dbg_n", "5")
            wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
            if st != "done" or page.evaluate("MKB.stepper.steps") != s1 - 5:
                problems.append(f"{tag}: Step back after the new way: {st} {text[:160]!r}")
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


def reverse_jit(browser, base, shots):
    """The JIT after stepping back (hello at full speed past its entry):
    what was ahead is dropped (another way from there), and the history goes
    on with the JIT's checkpoints (R5: before it, the JIT dropped it all)."""
    problems, r = [], {}
    tag = "reverse jit hello 1280 dark"
    ctx = browser.new_context(color_scheme="dark", viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10&speed=full&count=0&revk=50#debugger", timeout=120000)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 60)
        page.fill("#dbg_addr", "0x100000")
        page.click("#dbg_runto")
        run_to_wait(page, {0x100000}, 300)
        rev_step_to(page, page.evaluate("MKB.stepper.steps") + 200)
        page.fill("#dbg_n", "100")
        rev_click(page, "#dbg_backn", False)
        r["back"] = page.evaluate("[MKB.stepper.steps, MKB.stepper.ahead, MKB.REV.cps.length]")
        machine_do(page, "select_option", "#dbg_speed", "full")
        machine_do(page, "click", "#dbg_continue")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused" or page.evaluate("!!MKB.stepper.end"), 60)
        time.sleep(1)
        b = page.evaluate("""[MKB.stepper.behind(), MKB.REV.cps.filter((c) => c.jit).length, document.getElementById('dbg_back').disabled,
          document.getElementById('rev_info').textContent, MKB.REV.cps.filter((c) => !c.jit).every((c) => c.steps <= MKB.REV.cps.find((x) => x.jit).steps)]""")
        r["after jit"] = b
        if b[0] or not b[1] or b[2] or "at JIT speed" not in b[3] or not b[4]:
            problems.append(f"{tag}: after the JIT: behind {b[0]}, {b[1]} JIT checkpoints, Step back disabled {b[2]}, "
                            f"stepped ones before the branch {b[4]}, {b[3][:160]!r}")
        page.screenshot(path=str(shots / "reverse-jit-hello-1280-dark.png"))
    finally:
        ctx.close()
    return problems, r


def reverse_keys(browser, base, shots, name):
    """Debian+initrd at its (initramfs) prompt (J1): stepped, "echo r2" typed
    and run; Step back to before the keys: the keys wait to go in again,
    the screen is as before them; forward again: the same machine and log
    at the count where "r2" appeared."""
    problems, r = [], {}
    tag = f"reverse keys {name} 390 light"
    ctx = browser.new_context(color_scheme="light", **PHONE)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    rows = "(() => { const t = MKB.stepper.screenText(MKB.STEP_CONST.VGA_WINDOW_BYTES); const out = []; for (let i = 0; i < t.length; i += 80) out.push(t.slice(i, i + 80).trim()); return out; })()"
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.evaluate("!!MKB.stepper && !!MKB.stepper.prompt && MKB.runState === 'paused'"), 900, 1):
            return [f"{tag}: no pause at the (initramfs) prompt"], r
        show_tab(page, "debugger")
        page.fill("#dbg_n", "100000")
        page.tap("#dbg_stepn")
        time.sleep(0.5)
        s0 = page.evaluate("[MKB.stepper.steps, MKB.REV.cps.length ? MKB.REV.cps[0].steps : -1]")
        show_tab(page, "machine")
        page.tap("#screen_container", position={"x": 4, "y": 4})  # not the overlay's Continue
        page.keyboard.type("echo r2")
        page.keyboard.press("Enter")
        held = page.evaluate("[MKB.runState, MKB.INPUT.pending.length]")
        if held != ["paused", 16]:
            problems.append(f"{tag}: typing at the prompt: {held} (want paused, 16 scancodes held)")
        show_tab(page, "debugger")
        e = None
        for _ in range(600):
            page.evaluate("MKB.doStep(200000)")
            if "r2" in page.evaluate(rows):
                e = page.evaluate("MKB.stepper.steps")
                break
        if e is None:
            return [f"{tag}: the shell did not print r2 while stepping ({page.evaluate('MKB.stepper.steps')} steps)"], r
        pe = rev_state(page)
        log_e = page.evaluate("JSON.stringify(MKB.INPUT.log)")
        key_at = page.evaluate("MKB.INPUT.log.filter((x) => x.keys)[0].at")
        start = page.evaluate("MKB.REV.cps[0].steps")
        r["run"] = {"history from": start, "keys at": key_at, "r2 at": e, "checkpoints": page.evaluate("MKB.REV.cps.length")}
        if start > key_at:
            problems.append(f"{tag}: the history starts at {start}, after the keys at {key_at}")
        page.fill("#dbg_n", str(e - start))
        wall, ms, text, st = rev_click(page, "#dbg_backn", True, 300)
        r["back"] = {"wall ms": wall, "page ms": ms, "status": text[:200]}
        b = page.evaluate(f"[MKB.stepper.steps, MKB.INPUT.redo.filter((x) => x.keys).length, {rows}.includes('r2')]")
        if st != "done" or b[0] != start or not b[1] or b[2]:
            problems.append(f"{tag}: back to {start}: {st} at {b[0]}, {b[1]} key entries waiting, r2 on the screen {b[2]}; {text[:160]!r}")
        show_tab(page, "machine")
        page.evaluate("MKB.flushScreen()")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"reverse-keys-{name}-390-back.png"))
        show_tab(page, "debugger")
        # R5: the history starts where the JIT ran (its checkpoints): one
        # Step goes forward on the recorded run from the checkpoint before e.
        page.evaluate(f"MKB.doStep({e} - MKB.stepper.steps)")
        rev_step_to(page, e)
        got = rev_state(page)
        if rev_diff(got, pe):
            problems.append(f"{tag}: forward to {e} again differs: {rev_diff(got, pe)}")
        # R4: the log also holds the resets of v86's hidden JIT state the
        # restores did, at checkpoints' counts (a replay resets there too;
        # R5: a JIT checkpoint's own reset is there already); the rest is the
        # recorded log.
        log_g = json.loads(page.evaluate("JSON.stringify(MKB.INPUT.log)"))
        new_resets = [x["at"] for x in log_g if x.get("reset")][len([x for x in json.loads(log_e) if x.get("reset")]):]
        if [x for x in log_g if not x.get("reset")] != [x for x in json.loads(log_e) if not x.get("reset")] or \
                page.evaluate("MKB.INPUT.redo.length"):
            problems.append(f"{tag}: the input log after going forward again differs from the recorded one")
        cps = page.evaluate("MKB.REV.cps.map((c) => c.steps)")
        if any(x not in cps for x in new_resets):
            problems.append(f"{tag}: the step back logged the JIT resets {new_resets}, not all at checkpoints {cps}")
        show_tab(page, "machine")
        page.evaluate("MKB.flushScreen()")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"reverse-keys-{name}-390-again.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


# B1: the minified Linux stepped from the reset vector to near its end
# (1.8 M), then Step back at once. A checkpoint is packed in the Worker
# after it is taken (a delta of the one before it); stepping goes on
# meanwhile, so the newest few can still be packing when Step back
# restores one. revBlob read the checkpoint's base before its packing was
# done (none yet), took the delta's few pages for the whole RAM and v86
# ran from zeroed memory: "Maximum call stack size exceeded" (its page
# fault handler recursing) or a wasm "unreachable". Part 1 holds each new
# checkpoint's packing back REV_LATE_HOLD_MS (a slow Worker) so the step
# back meets one still packing on any machine; part 2 is the report's case
# as it is (step to 1.8 M, Step back 5,000 right away).
REV_LATE_HOLD_MS = 3000
REV_LATE_HOLD_JS = """(ms) => { if (window.mkbSnapWorker) return;
  window.mkbSnapWorker = MKB.snapWorker;
  MKB.snapWorker = (msg, transfer) => msg.op !== 'pack' ? window.mkbSnapWorker(msg, transfer) :
    new Promise((res) => setTimeout(res, ms)).then(() => window.mkbSnapWorker(msg, transfer)); }"""
REV_LATE_UNHOLD_JS = "() => { if (window.mkbSnapWorker) { MKB.snapWorker = window.mkbSnapWorker; delete window.mkbSnapWorker; } }"
REV_LATE_READY_JS = "async () => { await Promise.all(MKB.REV.cps.map((c) => c.ready.catch(() => {}))); return MKB.REV.cps.filter((c) => c.failed).length; }"


def reverse_late(browser, base, shots):
    """Step back right after stepping, from near the minified Linux's end:
    the machine is the one recorded on the way, while the newest checkpoint
    is still being packed (B1)."""
    name = "minified-linux"
    params = REV_CASES[name][0]
    problems, r, errors = [], {}, []
    tag = f"reverse late {name} 1280 light"
    ctx = browser.new_context(color_scheme="light", viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def back(n, want, what):
        page.evaluate(f"MKB.revStepBack({n})")
        wait_for(lambda: page.evaluate("!MKB.REV.busy"), 120, 0.05)
        got = rev_state(page)
        status = page.inner_text("#dbg_status")
        r[what] = {"steps": got["steps"], "status": status[:200]}
        if page.get_attribute("#dbg_status", "data-rev") != "done" or rev_diff(got, want):
            problems.append(f"{tag}: {what}: {status[:200]!r}; differs from the machine recorded there: {rev_diff(got, want)}")

    def start():
        page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10&{params}#debugger", timeout=120000)
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator && !!MKB.symbols"), 60)
        show_tab(page, "debugger")
        return ok

    try:
        # Part 1: a checkpoint at cp, its packing held back; 1,000 past it
        # the machine is noted, 1,000 more stepped, Step back 1,000.
        if not start():
            return [f"{tag}: did not start paused"], r
        k = page.evaluate("MKB.REV.kFixed")
        cp = 1780000
        if rev_step_to(page, cp - 1000) != cp - 1000:
            return [f"{tag}: could not step to {cp - 1000} (the run ended at {page.evaluate('MKB.stepper.steps')})"], r
        if page.evaluate(REV_LATE_READY_JS):
            problems.append(f"{tag}: a checkpoint failed to pack")
        page.evaluate(REV_LATE_HOLD_JS, REV_LATE_HOLD_MS)
        rev_step_to(page, cp + 1000)
        want = rev_state(page)
        rev_step_to(page, cp + 2000)
        # The checkpoint the step back restores (the last at or before
        # cp + 1,000): taken on the way past cp, still packing.
        held = page.evaluate(f"(() => {{ const c = MKB.revFind({cp + 1000}); return c ? [c.steps, c.size === 0, !c.full] : null; }})()")
        r["held"] = {"k": k, "checkpoint [count, still packing, a delta]": held}
        if not held or held[0] <= cp - 1000 or not held[1] or not held[2]:
            problems.append(f"{tag}: the checkpoint the step back restores is {held} [count, still packing, a delta]; "
                            f"want one taken after {cp - 1000}, a delta still packing (test setup: k {k})")
        back(1000, want, f"back 1,000 to {cp + 1000} (checkpoint still packing)")
        # Part 2: the report's case, nothing held back, on a new machine
        # (part 1's may be broken).
        page.goto("about:blank")
        if not start():
            return problems + [f"{tag}: did not start paused again"], r
        rev_step_to(page, 1795000)
        want = rev_state(page)
        if rev_step_to(page, 1800000) != 1800000:
            problems.append(f"{tag}: could not step to 1,800,000 (at {page.evaluate('MKB.stepper.steps')})")
        else:
            back(5000, want, "back 5,000 from 1,800,000")
        page.screenshot(path=str(shots / "reverse-late-minified-linux-1280-light.png"))
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


def reverse_case(browser, base, shots, entries):
    out = []
    runs = [("hello", "dark", True), ("hello", "light", False)]
    if "minified-linux" in entries:
        runs += [("minified-linux", "light", False), ("minified-linux", "dark", True)]
    if "debian-6.1-686" in entries:
        runs += [("debian-6.1-686", "dark", False)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(reverse_kernel, browser, base, shots, name, scheme, phone)
        out.append(report("reverse", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    if "minified-linux" in entries:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(reverse_late, browser, base, shots)
        out.append(report("reverse late", problems, r, seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(reverse_jit, browser, base, shots)
    out.append(report("reverse jit", problems, r))
    if "debian-6.1-686-initrd" in entries:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(reverse_keys, browser, base, shots, "debian-6.1-686-initrd")
        out.append(report("reverse keys", problems, r, seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_reverse(ctx):
    only = os.environ.get("MKB_REVERSE_ONLY")
    entries = {k: v for k, v in ctx.entries.items() if not only or k in only.split(",")}
    return reverse_case(ctx.browser, ctx.base, ctx.shots, entries)
