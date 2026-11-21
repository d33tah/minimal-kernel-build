"""Browser test suite jitdet (test_browser.py --suites)."""

import time

from tests.browser.common import dbg_shown_once, debian_names, JITDET_CAP, JITDET_WATCH_JS, machine_do, PHONE, report, wait_for


JITDET_MIN_RATIO = 0.9
JITDET_RUNNING_AT = 1_500_000_000  # count at which the running page is checked
JITDET_STATE_JS = """async () => { const st = MKB.stepper, w = window.__jd || {}, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const c = MKB.ffCounter(st);
  return { steps: st.steps, counter: c, eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    chain: st.jitChain === undefined ? null : (st.jitChain >>> 0).toString(16), irq0: st.irq0, end: st.end ? st.end.kind : null,
    exact: st.end ? st.end.exact === true : null, compiles: st.jitCompiles || 0, waits: st.jitWaits || 0,
    checkpoints: MKB.REV.cps.filter((c) => c.jit).length, save_ms: Math.round(MKB.REV.cps.reduce((a, c) => a + (c.jit ? c.saveMs : 0), 0)),
    mips: w.at ? +(((w.c - w.c0) >>> 0) / (w.at - w.t0) / 1000).toFixed(1) : null }; }"""
JITDET_RUNNING_JS = """() => ({ clock: document.getElementById('vclock').textContent, virtual: document.getElementById('vclock').dataset.virtual,
  badge: document.getElementById('runbadge').dataset.state, face: document.getElementById('runbadge_speed').textContent,
  status: document.getElementById('dbg_status').textContent, runState: MKB.runState })"""


def jitdet_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def jitdet_count_run(browser, base, shots, name, mips, scheme, phone, tag, problems):
    """From the reset vector (paused) at full speed: on the virtual clock to
    the page's stop at the end, on real time to v86's count JITDET_CAP."""
    ctx = jitdet_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips={mips}", timeout=120000)
        if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused'"), 180, 0.5):
            problems.append(f"{tag}: not paused at the reset vector")
            return None
        dbg_shown_once(page)  # JITDET_RUNNING_JS reads #dbg_status: there once the Debugger tab has been shown
        page.evaluate(JITDET_WATCH_JS, [JITDET_CAP, mips == "real"])
        machine_do(page, "click", "#dbg_continue")
        # Look at the running page well before the cap: a fast runner (CI,
        # 650 MIPS) reaches it about 4 s after Continue.
        wait_for(lambda: page.evaluate(f"MKB.ffCounter(MKB.stepper) >= {JITDET_RUNNING_AT} || !!window.__jd.at"), 120, 0.05)
        running = page.evaluate(JITDET_RUNNING_JS)
        page.screenshot(path=str(shots / f"jitdet-{tag}-running.png"))
        if not wait_for(lambda: page.evaluate("window.__jd.done && MKB.runState === 'paused'"), 400, 0.5):
            problems.append(f"{tag}: did not stop (counter {page.evaluate('MKB.ffCounter(MKB.stepper)'):,})")
            return None
        out = page.evaluate(JITDET_STATE_JS)
        out["running"] = running
        page.screenshot(path=str(shots / f"jitdet-{tag}-stopped.png"))
        want_virtual = "false" if mips == "real" else "true"
        if running["virtual"] != want_virtual or ("virtual time" if want_virtual == "true" else "real time") not in running["clock"]:
            problems.append(f"{tag}: while running at full speed the clock line says {running['clock']!r} (virtual={running['virtual']})")
        if running["runState"] != "jit" or running["face"] != "full":
            problems.append(f"{tag}: while running: runState {running['runState']}, run badge face {running['face']!r}")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def jitdet_end_run(browser, base, shots, name, scheme, phone, tag, problems):
    """The default load at full speed, count off: fast-forward to the
    entry, then the JIT to the kernel's end."""
    ctx = jitdet_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 240, 0.5):
            problems.append(f"{tag}: no end in 240 s")
            return None
        time.sleep(0.5)
        out = page.evaluate(JITDET_STATE_JS)
        out["virtual"] = page.get_attribute("#vclock", "data-virtual")
        page.screenshot(path=str(shots / f"jitdet-{tag}-end.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def jitdet_case(browser, base, shots, entries, debian):
    problems, r = [], {}
    keys = ("counter", "steps", "eip", "chain", "ram")
    # hello and the minified Linux: two full-speed boots end in the same state.
    for name, scheme, phone in (("hello", "light", False), ("minified-linux", "dark", True)):
        if name not in entries:
            continue
        runs = [jitdet_end_run(browser, base, shots, name, scheme, phone, f"{name}-{i}", problems) for i in range(2)]
        r[name] = runs
        if None in runs:
            continue
        a, b = runs
        for k in keys + ("irq0", "end"):
            if a[k] != b[k]:
                problems.append(f"{name}: end state differs in {k}: {a[k]} vs {b[k]}")
        if a["chain"] is None:
            problems.append(f"{name}: no chunk-chain hash (the JIT did not run from the page)")
    if not debian:
        return problems, r
    # Debian to its end twice on the virtual clock, to count JITDET_CAP once on real time.
    name = debian[0]
    det = [jitdet_count_run(browser, base, shots, name, "10", scheme, phone, f"debian-det{i}-{scheme}-{390 if phone else 1280}", problems)
           for i, (scheme, phone) in enumerate((("light", False), ("dark", True)))]
    rt = jitdet_count_run(browser, base, shots, name, "real", "light", False, "debian-realtime", problems)
    r["debian"] = det
    r["debian_realtime"] = rt
    if None not in det:
        a, b = det
        for k in keys + ("irq0", "end"):
            if a[k] != b[k]:
                problems.append(f"debian to its end: {k} differs: {a[k]} vs {b[k]}")
        if a["chain"] is None:
            problems.append("debian: no chunk-chain hash (the JIT did not run from the page)")
        for x in det:
            if x["end"] != "panic-text" or not x["exact"]:
                problems.append(f"debian: stopped at count {x['counter']:,} with end {x['end']} (exact {x['exact']}), "
                                "not at the instruction that wrote the panic's last line")
        # R6: the det JIT compiles a block where v86 queues it (fastforward.js
        # jitDetInstall), so the next slice never waits for it. Waiting for
        # WebAssembly.instantiate() there took about 1.4 s of an 8 s run on
        # d-claude, and came back after every JIT checkpoint's reset.
        if browser.browser_type.name == "chromium":
            for x in det:
                if x["waits"]:
                    problems.append(f"debian: the det JIT waited for {x['waits']} of {x['compiles']} compiled blocks")
        if rt and rt.get("mips") and a.get("mips"):
            best = max(x["mips"] for x in det)
            r["speed_ratio"] = round(best / rt["mips"], 3)
            if best < JITDET_MIN_RATIO * rt["mips"]:
                problems.append(f"debian: {best} MIPS on the virtual clock, below {JITDET_MIN_RATIO} x {rt['mips']} MIPS of v86's own loop")
    return problems, r


def suite_jitdet(ctx):
    return report("jitdet", *jitdet_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx)))
