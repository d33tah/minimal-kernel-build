"""Browser test suite jitsnap (test_browser.py --suites)."""

import os
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import machine_do, PHONE, replay_state, replay_wait, report, show_tab, snap_click, wait_for


# ---- jitsnap: a snapshot repeats the JIT run (R3, R4) ------------------------------
# v86's JIT keeps state that save_state() does not (compiled code, hotness,
# hash maps whose iteration order decided what one compiled function holds,
# the TLB, the TSC's extrapolation), and that state decides where main_loop's
# slices end and so where timer interrupts land. The page's v86.wasm is built
# with site/v86-det.patch (site/gen-v86.sh): fixed-key hash maps, set_tsc from
# the clock, and jit_reset_hidden(), which the page calls after every restore
# and after every snapshot it saves, logging it with the run (J4). From a
# snapshot the JIT then runs as it did from that point the first time: X1 the
# original run on, X2 restored in the same page, X3 in a reloaded page (from
# IndexedDB), X4 a fresh page that boots to the point itself, saves and
# restores. Every slice end (count, EIP, registers, IRQ0) equal (R3's
# r3replay). And the run is no longer tainted by "the JIT after a snapshot
# restore": Restore at a pause under the JIT, the (initramfs) prompt comes at
# the same count, and Replay from boot matches.
JITSNAP_SHA256 = "cdf710949be5d004f0a8da9b559a49ddebdd5fde94d172e2a2cdf365b1a7a6a1"
# kernel name filter -> (A, B): save at the first slice end at or past A, run to B.
JITSNAP_WINDOWS = [("debian", False, 1.0e9, 2.55e9), ("debian", True, 2.5e9, 3.3e9)]
JITSNAP_DRV_JS = """() => { window.JS4 = {
  async run(cap, rec) {
    const st = MKB.stepper, cpu = st.cpu;
    const ch = new MessageChannel();
    const y = () => new Promise((r) => { ch.port1.onmessage = () => r(); ch.port2.postMessage(0); });
    const t0 = performance.timeOrigin, w0 = Date.now();
    let n = 0;
    const irq = st.irq0 || 0; // the page's count, not part of the machine: from the run's start
    if (rec) this.trace = [];
    while (st.steps < cap && !st.dead()) {
      await MKB.jitStep(st);
      n++;
      if (rec) {
        let h = 0x811c9dc5;
        for (const x of cpu.reg32) h = Math.imul(h ^ x, 0x01000193);
        h = Math.imul(h ^ cpu.get_eflags(), 0x01000193);
        this.trace.push([st.steps, cpu.instruction_pointer[0] >>> 0, h >>> 0, (st.irq0 || 0) - irq]);
      }
      if (n % 8 === 0) await y();
    }
    await MKB.jitFlush(st);
    return { steps: st.steps, slices: n, ms: Date.now() - w0 };
  },
  async state() {
    const cpu = MKB.stepper.cpu, m = cpu.mem8;
    const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('').slice(0, 16);
    return { steps: MKB.stepper.steps, icount: cpu.instruction_counter[0] >>> 0, eip: (cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha,
      regs: Array.from(cpu.reg32).map((x) => (x >>> 0).toString(16)).join(','), clock: MKB.CLOCK.now(), taint: MKB.INPUT.taint };
  },
  // The guest's TSC against the virtual clock (offset 0 from power-on): read twice at the same count.
  tsc() {
    const c = MKB.stepper.cpu, rd = () => { c.store_current_tsc(); return c.current_tsc[0] + c.current_tsc[1] * 2 ** 32 - Math.round(MKB.CLOCK.now() * 1e6); };
    return [rd(), rd()];
  },
  async save() {
    const n = MKB.snapshots.length;
    await MKB.saveSnapshot();
    if (MKB.snapshots.length !== n + 1) throw new Error('no snapshot: ' + document.getElementById('snap_status').textContent);
    const s = MKB.snapshots[MKB.snapshots.length - 1];
    return { id: s.id, steps: s.meta.steps, status: document.getElementById('snap_status').textContent };
  },
  async restore(id) {
    const s = MKB.snapById(id);
    if (!s) throw new Error('no snapshot ' + id);
    await MKB.restoreSnapshot(s);
    if (document.getElementById('snap_status').dataset.restored !== id) throw new Error(document.getElementById('snap_status').textContent);
    return MKB.stepper.steps;
  },
}; }"""


def jitsnap_first_diff(t1, t2):
    for i, (x, y) in enumerate(zip(t1, t2)):
        if x != y:
            return i, x, y
    if len(t1) != len(t2):
        return min(len(t1), len(t2)), None, None
    return None


def jitsnap_window(browser, base, shots, name, a, b, problems, r):
    """X1-X4 over [a, b] on the det JIT, from the page's own Save and Restore."""
    tag = f"jitsnap {name} {a:.2e} -> {b:.2e}"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    url = f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips=10"
    out = {}
    try:
        def fresh():
            page.goto(url, timeout=180000)
            if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused' && MKB.emuReady"), 300, 0.5):
                raise PlaywrightError("did not start paused")
            page.evaluate(JITSNAP_DRV_JS)
            # JS4.save/restore read the Snapshots tab's status: shown once, it stays mounted (A1).
            show_tab(page, "snapshots")
            show_tab(page, "machine")

        def run(cap, rec=True):
            x = page.evaluate("([c, rec]) => JS4.run(c, rec)", [cap, rec])
            x["state"] = page.evaluate("() => JS4.state()")
            if rec:
                x["trace"] = page.evaluate("() => JS4.trace")
            return x

        fresh()
        run(a, False)
        sv = page.evaluate("() => JS4.save()")
        out["save"] = sv
        out["X1"] = run(b)
        tscs = {"X1 end": page.evaluate("() => JS4.tsc()")}
        page.evaluate("(id) => JS4.restore(id)", sv["id"])
        tscs["X2 restored"] = page.evaluate("() => JS4.tsc()")
        out["X2"] = run(b)
        fresh()
        wait_for(lambda: page.evaluate("(id) => !!MKB.snapById(id)", sv["id"]), 30)
        page.evaluate("(id) => JS4.restore(id)", sv["id"])
        tscs["X3 restored"] = page.evaluate("() => JS4.tsc()")
        out["X3"] = run(b)
        fresh()
        run(a, False)
        s4 = page.evaluate("() => JS4.save()")
        if s4["steps"] != sv["steps"]:
            problems.append(f"{tag}: X4 saved at {s4['steps']:,}, X1 at {sv['steps']:,}: the boot to A differs")
        page.evaluate("(id) => JS4.restore(id)", s4["id"])
        tscs["X4 restored"] = page.evaluate("() => JS4.tsc()")
        out["X4"] = run(b)
        tscs["X4 end"] = page.evaluate("() => JS4.tsc()")
        out["tsc"] = tscs
        # A restore puts the TSC back as saved: it stays on the virtual
        # clock, twice in a row at the same count too (v86's extrapolation
        # of repeated readings once doubled it after a reset: CI run
        # 43013630565, "TSC off the virtual clock by 232084897999 ticks").
        for k, v in tscs.items():
            if any(abs(x) > 1_000_000 for x in v):
                problems.append(f"{tag}: {k}: the TSC is off the virtual clock by {v} ticks (1 ms = 10^6)")
        page.screenshot(path=str(shots / f"jitsnap-{name}-1280-light.png"))
    finally:
        ctx.close()
    t1 = out.get("X1", {}).get("trace")
    res = {"saved_at": out.get("save", {}).get("steps"), "tsc": out.get("tsc")}
    for k in ("X1", "X2", "X3", "X4"):
        if k not in out:
            problems.append(f"{tag}: {k} did not run")
            continue
        x = out[k]
        d = jitsnap_first_diff(t1, x["trace"]) if k != "X1" else None
        res[k] = {"steps": x["steps"], "slices": x["slices"], "ms": x["ms"], "eip": x["state"]["eip"], "ram": x["state"]["ram"],
                  "first_diff": d}
        if d:
            problems.append(f"{tag}: {k} differs from the original run at slice {d[0]} of {len(t1)}: {d[1]} vs {d[2]}")
        for f in ("steps", "eip", "ram", "regs", "clock"):
            if x["state"][f] != out["X1"]["state"][f]:
                problems.append(f"{tag}: {k} ends with {f} {x['state'][f]}, the original run {out['X1']['state'][f]}")
    if t1 is not None and len(t1) < 1000:
        problems.append(f"{tag}: only {len(t1)} slices from A to B (test setup)")
    if errs:
        problems.append(f"{tag}: page errors {errs[:3]}")
    r[name] = res


JITSNAP_RUN_JS = """() => ({ steps: MKB.stepper.steps, prompt: MKB.stepper.prompt ? MKB.stepper.prompt.steps : null, runState: MKB.runState, taint: MKB.INPUT.taint,
  resets: MKB.INPUT.log.filter((e) => e.reset).map((e) => e.at) })"""


def jitsnap_ui(browser, base, shots, name, scheme, phone, problems, r):
    """Debian+initrd at full speed: Pause past 2.5e9, Save, Continue to the
    (initramfs) prompt; Restore, Continue to the prompt again: the same
    count and machine, the run not tainted; Replay from boot matches."""
    w = 390 if phone else 1280
    tag = f"jitsnap ui {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    res = {}
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=180000)
        page.evaluate(JITSNAP_DRV_JS) if wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper"), 120) else None
        if not wait_for(lambda: page.evaluate("!!MKB.stepper && MKB.stepper.steps > 2.5e9 && MKB.runState === 'jit'"), 900, 0.2):
            problems.append(f"{tag}: the JIT did not pass 2.5e9 ({page.evaluate('[MKB.runState, MKB.stepper && MKB.stepper.steps]')})")
            return
        machine_do(page, "tap" if phone else "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 20)
        page.evaluate(JITSNAP_DRV_JS)
        # JS4.save reads the Snapshots tab's status: the tab is shown first (A1: mounted on first show).
        show_tab(page, "snapshots")
        sv = page.evaluate("() => JS4.save()")
        res["saved_at"] = sv["steps"]
        resets = page.evaluate(JITSNAP_RUN_JS)["resets"]
        # R5: the reverse history's JIT checkpoints reset it too (logged).
        if not resets or resets[-1] != sv["steps"]:
            problems.append(f"{tag}: after Save the log has the JIT resets {resets}, want the save's at {sv['steps']:,} last")
        machine_do(page, "tap" if phone else "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("!!MKB.stepper.prompt && MKB.runState === 'paused'"), 600, 0.5):
            problems.append(f"{tag}: no pause at the (initramfs) prompt after Save")
            return
        first = page.evaluate("() => JS4.state()")
        res["first"] = {k: first[k] for k in ("steps", "eip", "ram")}
        first_resets = page.evaluate(JITSNAP_RUN_JS)["resets"]
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{sv['id']}'] button.restore", phone)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == sv["id"] or "failed" in page.inner_text("#snap_status"), 60)
        if page.get_attribute("#snap_status", "data-restored") != sv["id"]:
            problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
            return
        machine_do(page, "tap" if phone else "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("!!MKB.stepper.prompt && MKB.runState === 'paused'"), 600, 0.5):
            problems.append(f"{tag}: no pause at the (initramfs) prompt after Restore")
            return
        again = page.evaluate("() => JS4.state()")
        run = page.evaluate(JITSNAP_RUN_JS)
        res["again"] = {k: again[k] for k in ("steps", "eip", "ram")}
        res["run"] = run
        for k in ("steps", "eip", "ram", "regs", "clock"):
            if again[k] != first[k]:
                problems.append(f"{tag}: at the prompt after Restore {k} is {again[k]}, the first time {first[k]}")
        if run["taint"]:
            problems.append(f"{tag}: the run is marked not replayable: {run['taint']}")
        if run["resets"] != first_resets:
            problems.append(f"{tag}: after Restore the log has the JIT resets {run['resets']}, the first time {first_resets}")
        show_tab(page, "snapshots")
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jitsnap-ui-{w}-{scheme}-restored.png"))
        snap_click(page, "#run_replay", phone)
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish ({page.evaluate('[MKB.runState, MKB.stepper && MKB.stepper.steps]')})")
            return
        b = replay_state(page)
        res["replayed"] = {k: b[k] for k in ("steps", "eip", "ram", "verdict", "replay", "status")}
        if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
            problems.append(f"{tag}: the page's replay verdict: {b['verdict']} {b['replay']} {b['status']!r}")
        if b["steps"] != again["steps"]:
            problems.append(f"{tag}: the replay ended at {b['steps']:,}, the run at {again['steps']:,}")
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jitsnap-ui-{w}-{scheme}-replayed.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        r[f"ui {w} {scheme}"] = res
        ctx.close()


def jitsnap_wasm(browser, base, problems, r):
    """The page loads the pinned build: its SHA-256 and the export."""
    ctx = browser.new_context()
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10", timeout=120000)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 120)
        w = page.evaluate("""async () => { const b = await (await fetch(MKB.withV('v86/v86.wasm'))).arrayBuffer();
          const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', b))).map((x) => x.toString(16).padStart(2, '0')).join('');
          return { sha, reset: typeof MKB.emulator.v86.cpu.wm.exports.jit_reset_hidden, patchError: MKB.wasmPatchError }; }""")
        r["wasm"] = w
        if w["sha"] != JITSNAP_SHA256:
            problems.append(f"jitsnap: the page's v86.wasm has SHA-256 {w['sha']}, want the gen-v86.sh build {JITSNAP_SHA256}")
        if w["reset"] != "function":
            problems.append(f"jitsnap: v86.wasm exports no jit_reset_hidden ({w['reset']})")
        if w["patchError"]:
            problems.append(f"jitsnap: stepper.js could not add its step exports: {w['patchError']}")
    finally:
        ctx.close()


def jitsnap_case(browser, base, shots, entries):
    problems, r = [], {}

    def guard(fn, *args):
        try:
            return fn(*args)
        except (PlaywrightError, IndexError, KeyError, TypeError) as e:
            problems.append(f"{fn.__name__}: {type(e).__name__}: {(str(e).splitlines() or [''])[0][:300]}")
            return None
    guard(jitsnap_wasm, browser, base, problems, r)
    only = os.environ.get("MKB_JITSNAP_ONLY")
    for prefix, with_initrd, a, b in JITSNAP_WINDOWS:
        names = [n for n in entries if n.startswith(prefix) and ("initrd" in entries[n]) == with_initrd and not entries[n].get("editable")]
        if names and (not only or names[0] in only.split(",")):
            guard(jitsnap_window, browser, base, shots, names[0], a, b, problems, r)
    initrd = [n for n in entries if n.startswith("debian") and "initrd" in entries[n] and not entries[n].get("editable")]
    if initrd and (not only or "ui" in only.split(",")):
        guard(jitsnap_ui, browser, base, shots, initrd[0], "dark", True, problems, r)
    return problems, r


def suite_jitsnap(ctx):
    return report("jitsnap", *jitsnap_case(ctx.browser, ctx.base, ctx.shots, ctx.entries))
