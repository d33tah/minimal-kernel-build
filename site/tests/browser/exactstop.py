"""Browser test suite exactstop (test_browser.py --suites)."""

import os
import re
import time

from tests.browser.common import dbg_shown_once, debian_names, machine_do, PHONE, report, show_tab, wait_for


# ---- exactstop: Debian's end stops at an exact instruction (J3) ------------
# D0 (2026-11-10): the virtual-clock machine is deterministic, yet Debian's
# "---[ end Kernel panic - not syncing: ... ]---" stop and the initramfs
# shell's "(initramfs) " prompt were found by polling the screen every
# 250 ms, so the count at the stop and the RAM there varied by up to ~1 M
# instructions. Now the stepper's VGA hook sees the line complete: stepping
# stops at the instruction that writes the panic line's last character, and
# at the prompt where the CPU first halts after it (the shell waits for
# input). Under J1's JIT the page steps exactly from the slice in which
# "Kernel panic - not syncing" appears, and stops at the prompt at the
# first slice that ends halted after it. Each kernel and mode three times
# (1280 light, 390 dark, 1280 dark): the same counts, EIP and RAM.
EXACTSTOP_RUNS = (("light", False), ("dark", True), ("dark", False))
EXACTSTOP_STATE_JS = """async () => { const st = MKB.stepper, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const pick = (s) => s && { steps: s.steps, sinceEntry: s.sinceEntry, unique: s.unique, phys: s.phys, kind: s.kind || null, exact: s.exact === undefined ? null : s.exact };
  return { steps: st.steps, counter: MKB.ffCounter(st), eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    chain: st.jitChain === undefined ? null : (st.jitChain >>> 0).toString(16), irq0: st.irq0, clock: MKB.CLOCK.now(),
    end: pick(st.end), prompt: pick(st.prompt), runState: MKB.runState, hlt: st.halted(),
    banner: document.getElementById('end_banner').textContent, status: document.getElementById('dbg_status').textContent }; }"""


def exactstop_run(browser, base, shots, name, mode, scheme, phone, tag, problems, prompt):
    """One boot of name in mode ("stepped": counted at full stepping speed;
    "jit": J1's full speed) to its stop; the state there."""
    q = "speed=full&count=1&mips=10" if mode == "stepped" else "speed=full&count=0&mips=10"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        t0 = time.monotonic()
        page.goto(f"{base}/index.html?kernel={name}&{q}", timeout=120000)
        want = "!!MKB.stepper && !!MKB.stepper.prompt" if prompt else "!!MKB.stepper && !!MKB.stepper.end"
        if not wait_for(lambda: page.evaluate(f"{want} && MKB.runState === 'paused'"), 900, 1):
            problems.append(f"{tag}: no {'prompt' if prompt else 'end'} pause in 900 s "
                            f"({page.evaluate('MKB.stepper ? MKB.stepper.steps : null')} steps, {page.evaluate('MKB.runState')})")
            page.screenshot(path=str(shots / f"exactstop-{tag}-timeout.png"))
            return None
        time.sleep(0.5)
        dbg_shown_once(page)  # #dbg_status exists once the Debugger tab has been shown
        out = page.evaluate(EXACTSTOP_STATE_JS)
        out["seconds"] = round(time.monotonic() - t0)
        show_tab(page, "machine")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"exactstop-{tag}.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def exactstop_kernel(browser, base, shots, name, mode, prompt, problems, r):
    runs = []
    for scheme, phone in EXACTSTOP_RUNS:
        tag = f"{name}-{mode}-{scheme}-{390 if phone else 1280}"
        runs.append(exactstop_run(browser, base, shots, name, mode, scheme, phone, tag, problems, prompt))
    key = "prompt" if prompt else "end"
    r[f"{name}-{mode}"] = [x and {k: x[k] for k in ("seconds", "counter", "eip", "ram", "irq0", key)} for x in runs]
    got = [x for x in runs if x]
    if len(got) < len(runs):
        return
    for x in got:
        if prompt and x["prompt"] is None:
            problems.append(f"{name} {mode}: paused without a prompt row")
        if not prompt and (x["end"] is None or x["end"]["kind"] != "panic-text"):
            problems.append(f"{name} {mode}: the end is {x['end']}, not the panic text")
        if not prompt and x["end"] and x["end"]["exact"] is False:
            problems.append(f"{name} {mode}: the panic text was found only at the end of a JIT slice")
        if prompt and not x["hlt"]:
            problems.append(f"{name} {mode}: stopped at the prompt with the CPU not halted (EIP {x['eip']})")
    fields = ("counter", "steps", "eip", "ram", "irq0", "clock", key) + (("chain",) if mode == "jit" else ())
    for k in fields:
        vals = [x[k] for x in got]
        if any(v != vals[0] for v in vals[1:]):
            problems.append(f"{name} {mode}: {k} at the stop differs over {len(got)} runs: {vals}")


# J2 residual: the count line's rate divided every instruction since reset
# (the fast-forward's JIT part too) by the stepping time only: right after
# the fast-forward to the entry it read "0.00 s of JavaScript stepping time
# (3918091.6 M/s)". The rate is the stepped instructions over that time.
COUNTRATE_RE = re.compile(r"([\d.]+) s of JavaScript stepping time \(([\d.]+) M/s\)")


def countrate_case(browser, base, shots, name, scheme, phone, problems, r):
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    tag = f"{name}-{scheme}-{390 if phone else 1280}"
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1000&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"countrate {tag}: no kernel entry")
            return
        time.sleep(1.5)
        if page.is_enabled("#dbg_pause"):  # hello has ended by now
            machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        time.sleep(0.5)
        text = page.inner_text("#count_text")
        st = page.evaluate("({ steps: MKB.stepper.steps, ms: MKB.countMs, jit: MKB.stepper.jitBeforeEntry })")
        r[f"countrate-{tag}"] = {"text": text, **st}
        page.eval_on_selector("#count_info", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"countrate-{tag}.png"))
        m = COUNTRATE_RE.search(text)
        if not m:
            problems.append(f"countrate {tag}: no stepping time and rate in the count line: {text!r}")
            return
        # JavaScript steps a few million instructions a second at most.
        if float(m.group(2)) > 100:
            problems.append(f"countrate {tag}: {m.group(2)} M/s for {m.group(1)} s of stepping: {text!r}")
    finally:
        ctx.close()


def exactstop_case(browser, base, shots, entries, debian, initrd, modes):
    problems, r = [], {}
    countrate_case(browser, base, shots, "hello", "light", False, problems, r)
    if debian:
        countrate_case(browser, base, shots, debian[0], "dark", True, problems, r)
    for mode in modes:
        if debian:
            exactstop_kernel(browser, base, shots, debian[0], mode, False, problems, r)
        if initrd:
            exactstop_kernel(browser, base, shots, initrd[0], mode, True, problems, r)
    return problems, r


def suite_exactstop(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n] and not ctx.entries[n].get("editable")]
    modes = [m for m in os.environ.get("MKB_EXACTSTOP_MODES", "jit,stepped").split(",") if m]
    return report("exactstop", *exactstop_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx), initrd, modes))
