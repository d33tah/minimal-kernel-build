"""Browser test suite nopath (test_browser.py --suites)."""

import time

from tests.browser.common import report, run_to_wait, wait_for
from tests.browser.jumps_inline import NOPATH_MARKS_JS


# The ⊘ oracle while stepping (Z1). In the build's disassembly, "reached
# only indirectly" = the instructions without a direct path in whose
# predecessor does not fall through (jumpPaths over the bare static paths,
# no taken addresses, no stepper: the old meaning of ⊘). Steps n
# instructions with the stepper; the first time EIP stops on one of them
# that has not run yet, Code at EIP is drawn there and its marks returned.
# Then: every instruction that has run, or has an observed path in, must
# not carry ⊘ (jumpPaths with the stepper, as the listings draw it).
NOPATH_WALK_JS = """(n) => {
  const st = MKB.stepper, S0 = MKB.jumpsStatic(MKB.disasm.insns, MKB.jumpsToPhys(MKB.symbols && MKB.symbols.sections));
  const only = new Set(MKB.disasm.insns.map((x) => x[0]).filter((pa) => MKB.jumpPaths(S0, null, pa).noDirect));
  let atEip = null, steps = st.steps;
  for (let i = 0; i < n; i++) {
    st.run(1, -1, 100);
    if (st.steps === steps) continue;
    steps = st.steps;
    const pc = st.nextPhys();
    if (!atEip && only.has(pc) && !st.executed(pc)) {
      MKB.renderContext();
      const marks = [];
      for (const x of document.querySelectorAll('#context .jnone')) {
        let e = x.previousElementSibling; while (e && !e.matches('.ins')) e = e.previousElementSibling;
        if (e) marks.push(+e.dataset.addr); }
      const sym = MKB.kernelSymLookup()(pc);
      atEip = { pc, at: i, sym: sym ? sym.name + '+0x' + (pc - sym.paddr).toString(16) : '', marks };
    }
  }
  MKB.updateDebugUi();
  const S = MKB.jumpsFor(MKB.disasm.insns);
  const ran = [], seenIn = [], bad = [];
  for (const [pa] of MKB.disasm.insns) {
    const r = st.executed(pa), o = st.jumpIn.has(pa);
    if (r && only.has(pa)) ran.push(pa);
    if (o && only.has(pa)) seenIn.push(pa);
    if ((r || o) && MKB.jumpPaths(S, st, pa).noDirect) bad.push([pa, r, o]);
  }
  let none = 0;
  for (const [pa] of MKB.disasm.insns) if (MKB.jumpPaths(S, st, pa).noDirect) none++;
  return { atEip, ran, seenIn, bad: bad.slice(0, 40), nbad: bad.length, only: only.size, none,
    executed: st.countIn(0, st.seen.length * 8), steps: st.steps };
}"""

NOPATH_WALKS = {"hello": ("hello_main", 400), "hello-os": ("hello_main", 400),
                "minified-linux": ("start_kernel", 2000000), "minified-linux-os": ("start_kernel", 1000000)}


def nopath_case(browser, base, shots, name):
    """Z1, oracle 1: no instruction the stepper has run, or has seen a path
    into, carries ⊘ ("no path leads here"), in the build's disassembly
    (jumpPaths, as the listings draw it) nor in the drawn Code at EIP and
    Page listing. Paused at the start, the EIP line has no ⊘ (nothing
    is known to come before the kernel's first instruction). Runs to
    NOPATH_WALKS[name] and walks its count of single steps; the first time EIP is on an instruction reached only
    indirectly (a function called through a pointer, a switch's jump
    table, an exception fixup: no direct path in, the instruction before
    does not fall through) Code at EIP must not mark that line. On the
    minified kernels the walk must meet such instructions (else the check
    proves nothing). Screenshots of such a line in the Page tab."""
    problems, r = [], {}
    errors = []
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("!!MKB.disasm && !!MKB.symbols && !!MKB.stepper"), 60)
    page.evaluate("MKB.renderContext()")
    if any(cur for _, cur, _ in page.evaluate(NOPATH_MARKS_JS, "#context")):
        problems.append(f"{name}: paused at the start, Code at EIP marks the EIP line with ⊘")
    fn, n = NOPATH_WALKS[name]
    s = page.evaluate(f"MKB.symbols.symbols.find((s) => s.name === '{fn}')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        page.close()
        return problems + [f"{name}: Run to {fn} did not stop there: {status}"], r
    t0 = time.monotonic()
    w = page.evaluate(NOPATH_WALK_JS, n)
    r["walk"] = f"{w['steps']} steps in {time.monotonic() - t0:.0f} s, {w['executed']} instructions run"
    r["reached only indirectly (static)"] = w["only"]
    r["of them run"] = len(w["ran"])
    r["of them with an observed path in"] = len(w["seenIn"])
    r["⊘ left after the walk"] = w["none"]
    r["first stop on one"] = w["atEip"] and {k: (hex(v) if k == "pc" else v) for k, v in w["atEip"].items() if k != "marks"}
    if name.startswith("minified") and not w["ran"]:
        problems.append(f"{name}: the walk ran no instruction reached only indirectly: the check proves nothing")
    if w["nbad"]:
        problems.append(f"{name}: {w['nbad']} instructions that ran or have an observed path in carry ⊘, e.g. "
                        f"{[(hex(a), 'ran' if x else '', 'path in' if o else '') for a, x, o in w['bad'][:8]]}")
    if name.startswith("minified") and not w["atEip"]:
        problems.append(f"{name}: EIP never stopped on an instruction reached only indirectly")
    elif w["atEip"] and w["atEip"]["pc"] in w["atEip"]["marks"]:
        problems.append(f"{name}: Code at EIP marks EIP {w['atEip']['pc']:#x} ({w['atEip']['sym']}) with ⊘")
    # The drawn listings: Code at EIP now, and the Page tab at a few of the
    # instructions that ran.
    marks = page.evaluate(NOPATH_MARKS_JS, "#context")
    if any(cur for _, cur, _ in marks):
        problems.append(f"{name}: Code at EIP marks the EIP line with ⊘")
    for pa in w["ran"][:4]:
        page.evaluate(f"MKB.jumpTo({pa})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
        time.sleep(0.3)
        drawn = [a for a, _, _ in page.evaluate(NOPATH_MARKS_JS, "#listing")]
        ran = set(page.evaluate("(l) => l.filter((a) => MKB.stepper.executed(a))", drawn))
        if ran:
            problems.append(f"{name}: the Page listing of {pa:#x} marks {len(ran)} instructions that ran with ⊘, e.g. {sorted(ran)[:4]}")
        if pa in drawn:
            problems.append(f"{name}: the Page listing marks {pa:#x} (it ran) with ⊘")
    if w["ran"]:
        pa = w["ran"][0]
        page.evaluate(f"MKB.jumpTo({pa})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
        page.evaluate(f"document.querySelector('#listing .ins[data-addr=\"{pa}\"]').scrollIntoView({{block: 'center'}})")
        for width in (1280, 390):
            page.set_viewport_size({"width": width, "height": 1000 if width > 400 else 844})
            time.sleep(0.3)
            page.evaluate(f"document.querySelector('#listing .ins[data-addr=\"{pa}\"]').scrollIntoView({{block: 'center'}})")
            for scheme in ("light", "dark"):
                page.emulate_media(color_scheme=scheme)
                time.sleep(0.3)
                page.screenshot(path=str(shots / f"nopath-{name}-ran-{width}-{scheme}.png"))
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"{name}: horizontal scroll at {width} px")
        page.emulate_media(color_scheme="light")
        page.set_viewport_size({"width": 1280, "height": 1000})
    # The tooltip of a mark left (padding after a ret or jmp, a function
    # nothing calls) says what it rests on, the run included.
    titles = [t for _, _, t in page.evaluate(NOPATH_MARKS_JS, "#listing")]
    if page.get_attribute("#listing", "data-source") != "build":
        titles = []
    r["a ⊘ title"] = titles[0] if titles else None
    r["jit note"] = page.evaluate("MKB.jumpsJitNote()")
    jit = r["jit note"].startswith("v86")
    for t in titles[:1]:
        for want in ("No path leads here: nothing jumps or calls to this instruction", "whole disassembly", "address is not stored anywhere",
                     "does not fall through", "has not run while stepping") + (("JIT",) if jit else ()):
            if want not in t:
                problems.append(f"{name}: the ⊘ title lacks {want!r}: {t!r}")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_nopath(ctx):
    failed = False
    for name in [n for n in NOPATH_WALKS if n in ctx.entries and "disasm" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = nopath_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("nopath", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed
