"""Browser test suite jumps (test_browser.py --suites)."""

import json
import re
import time

from tests.browser.common import kernel_entries, no_hscroll, report, run_to_wait, SITE, wait_for
from tests.browser.types import boxed_line_ink
from tests.browser.jumps_inline import jump_inline_case, jump_inline_eip_check, JUMP_PATHS_JS
from tests.browser.jumps_live import jumps_live_case


# Jump paths (J1): where the oracle walk starts and how many single steps
# it takes, per kernel. minified-linux's walk starts once interrupts are
# on, so it crosses the timer interrupt (IRQ0), task switches and init's
# system calls (user code has no disasm.json: its pairs are skipped).
JUMP_WALKS = {"hello": ("hello_main", 300), "minified-linux": ("rest_init", 150000)}

# Single-step n instructions with the stepper itself and return its own
# EIP sequence (the address of each instruction executed, in order) and
# the observed paths recorded meanwhile (count deltas). The sequence
# starts with the instruction executed before the walk when the stepper
# has one (its transfer is recorded at the walk's first fetch).
JUMP_WALK_JS = """(n) => {
    const st = MKB.stepper, before = new Map();
    for (const [k, e] of st.jumps) before.set(k, e[3]);
    const seq = st.jLast >= 0 ? [st.lastPhys] : [];
    const irq0 = st.irq0;
    let steps = st.steps;
    for (let i = 0; i < n; i++) {
        st.run(1, -1, 100);
        if (st.steps !== steps) { seq.push(st.lastPhys); steps = st.steps; }
    }
    const diff = [];
    for (const [k, e] of st.jumps) { const c = e[3] - (before.get(k) || 0); if (c) diff.push([e[0], e[1], e[2], c]); }
    MKB.updateDebugUi();
    return { seq, diff, irq0: st.irq0 - irq0, pending: st.jPend };
}"""


def jump_oracle(entry, seq):
    """The paths of an EIP sequence: (from, to) -> [count, kind, direct
    target] for every pair where the next instruction is not the one after
    (address + length, from disasm.json), or where the first is an
    unconditional transfer; kind as test_jumps.decode reads the bytes
    (None: a plain instruction, so an interrupt or exception came in
    between). Two v86 behaviours the stepper's sequence shows: a rep
    string instruction stays at its address until done (no path), and sti
    with interrupts off runs the next instruction in the same step, so a
    pair from an sti that is not sti + 1 is the next instruction's."""
    import test_jumps
    insns = {pa: bytes.fromhex(raw) for pa, raw, _ in json.loads((SITE / entry["disasm"]).read_text())["insns"]}
    out, unknown = {}, set()
    for a, b in zip(seq, seq[1:]):
        if a not in insns:
            unknown.add(a)
            continue
        raw = insns[a]
        if raw == b"\xfb" and b != a + 1 and a + 1 in insns:
            a, raw = a + 1, insns[a + 1]
        d = test_jumps.decode(raw, 0)
        cond = d and d[0] in (test_jumps.JCC, test_jumps.LOOP) or raw.lstrip(bytes(test_jumps.PREFIXES))[:1] == b"\xce"
        if not d and b == a:
            continue
        if b == a + len(raw) and (not d or cond):
            continue
        direct = a + d[1] if d and d[0] in (test_jumps.JCC, test_jumps.LOOP, test_jumps.JMP, test_jumps.CALL) else -1
        out.setdefault((a, b), [0, d[0] if d else None, direct])[0] += 1
    return out, unknown


def jumps_case(browser, base, shots, name):
    """Jump paths (J1). Static: the Page listing's counts for a known jcc
    target equal the static sources jumps.js finds (test_jumps.py checks
    those against the ELF's bytes in the kernels job). Dynamic: a walk of
    JUMP_WALKS single steps run by the stepper; every non-fall-through
    pair of its own EIP sequence (the oracle, jump_oracle) must be
    recorded with the same count and a fitting kind, and nothing else.
    UI: the target's line shows "←n·k×", its list names each source with
    count and file:line, "go there" opens the Page tab at the source,
    Enter/Escape work from the keyboard, the arc of that jcc is drawn
    (taken), EIP's own paths are summarised in the Debugger, the JIT note
    is there after a Run to through the JIT. Screenshots light/dark at
    1280 and 390 px, no horizontal scroll."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    by_name = {s["name"]: s for s in syms}
    fn, n = JUMP_WALKS[name]
    errors = []
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    s = by_name[fn]
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    jit_note = page.evaluate("MKB.jumpsJitNote()")
    r["jit note"] = jit_note
    if page.evaluate("MKB.stepper.jitAfterEntry || MKB.stepper.jitBeforeEntry") and "JIT" not in jit_note:
        problems.append(f"{name}: the JIT ran but the page does not say paths are missing: {jit_note!r}")
    # EIP's own paths in the Debugger, at the function's entry: its
    # callers' calls (static), and the JIT note.
    eip = s["paddr"]
    P2 = page.evaluate(JUMP_PATHS_JS, eip)
    wait_for(lambda: page.get_attribute("#eip_jumps", "data-pa") == str(eip), 10)
    r["eip summary"] = page.inner_text("#eip_jumps")
    if page.get_attribute("#eip_jumps", "data-into") != str(len(P2["into"])) or "Paths into EIP: " + str(len(P2["into"])) not in r["eip summary"]:
        problems.append(f"{name}: the Debugger's paths into EIP {eip:#x} say {r['eip summary']!r}, want {len(P2['into'])}")
    if not any(e[2] and e[1] == 1 << page.evaluate("MKB.JK.call") for e in P2["into"]):
        problems.append(f"{name}: no static call into {fn} at EIP: {P2['into']}")
    if page.evaluate("MKB.stepper.jitAfterEntry") and "JIT" not in r["eip summary"]:
        problems.append(f"{name}: the Run to used the JIT but the Debugger's paths do not say so")
    jump_inline_eip_check(page, name, entry, eip, problems, r, shots)
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.locator("#context").screenshot(path=str(shots / f"jumps-{name}-context-1280-{scheme}.png"))
        page.locator("#eip_jumps").screenshot(path=str(shots / f"jumps-{name}-eip-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    t0 = time.monotonic()
    w = page.evaluate(JUMP_WALK_JS, n)
    r["walk"] = f"{len(w['seq'])} instructions in {time.monotonic() - t0:.1f} s, {w['irq0']} timer interrupts"
    want, unknown = jump_oracle(entry, w["seq"])
    r["sources outside disasm.json (skipped)"] = len(unknown)
    got = {(f, t): (c, mask) for f, t, mask, c in w["diff"] if f not in unknown}
    jk = page.evaluate("MKB.JK")
    trap = 1 << jk["irq"] | 1 << jk["exc"]
    kinds = {}
    for k in sorted(set(want) | set(got)):
        wc, wk, direct = want.get(k, (0, None, -1))
        gc, gm = got.get(k, (0, 0))
        if wc != gc:
            problems.append(f"{name}: path {k[0]:#x} -> {k[1]:#x}: recorded {gc}x, the EIP sequence has it {wc}x")
            continue
        # The kind: a direct transfer to its target is that kind; anything
        # else may also be (or be instead) an interrupt or exception that
        # came before the target's first instruction ran.
        if direct == k[1]:
            ok = gm == 1 << wk
        else:
            ok = gm and not gm & ~(trap | (1 << wk if wk else 0))
        if not ok:
            problems.append(f"{name}: path {k[0]:#x} -> {k[1]:#x}: kinds {page.evaluate('MKB.jumpKindsText', gm)!r}, the instruction is {wk}")
        kinds[page.evaluate("MKB.jumpKindsText", gm)] = kinds.get(page.evaluate("MKB.jumpKindsText", gm), 0) + gc
    r["paths in the walk"] = len(want)
    r["transfers by kind"] = kinds
    if name == "minified-linux" and not any("interrupt" in k for k in kinds):
        problems.append(f"{name}: no interrupt path in the walk ({w['irq0']} timer interrupts raised)")

    # A jcc target the walk took: the line's counts and its list.
    jcc = [(c, f, t) for (f, t), (c, k, d) in want.items() if k == 1 and d == t]
    if not jcc:
        problems.append(f"{name}: no taken jcc in the walk")
        page.close()
        return problems, r
    c, src, tgt = max(jcc)
    P = page.evaluate(JUMP_PATHS_JS, tgt)
    total = sum(e[3] for e in P["into"])
    r["jcc target"] = f"{tgt:#x} from {src:#x} taken {c}x in the walk; paths in {[(hex(e[0]), e[2], e[3]) for e in P['into']]}"
    st_src = [e for e in P["into"] if e[0] == src]
    if not st_src or not st_src[0][2] or st_src[0][3] < c:
        problems.append(f"{name}: {tgt:#x}'s paths in lack the static jcc from {src:#x} taken >= {c}x: {P['into']}")
    page.evaluate(f"MKB.jumpGo({tgt})")
    page.evaluate("MKB.jumpHit = -1")
    line = f'#listing .ins[data-addr="{tgt}"]'
    wait_for(lambda: page.locator(line).count() == 1, 20)
    badge = page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]')
    want_badge = f"←{len(P['into'])}" + (f"·{total:,}×" if total else "")
    r["badge"] = badge.inner_text() if badge.count() else None
    if r["badge"] != want_badge:
        problems.append(f"{name}: badge of {tgt:#x} is {r['badge']!r}, want {want_badge!r}")
    arcs = page.eval_on_selector_all("#listing svg.jarcs path.jarc", "ps => ps.map(p => [+p.dataset.from, +p.dataset.to, p.classList.contains('taken')])")
    r["arcs"] = f"{len(arcs)} drawn, {page.get_attribute('#listing', 'data-arcs-dropped')} not drawn"
    if [src, tgt, True] not in arcs and page.get_attribute("#listing", "data-page") == str(src // 4096):
        problems.append(f"{name}: no taken arc {src:#x} -> {tgt:#x} in the listing")
    if len(arcs) != int(page.get_attribute("#listing", "data-arcs") or -1):
        problems.append(f"{name}: {len(arcs)} arcs drawn, the listing says {page.get_attribute('#listing', 'data-arcs')}")
    # Keyboard: Enter opens the list, Escape closes it and gives the focus back.
    badge.focus()
    page.keyboard.press("Enter")
    if not wait_for(lambda: page.is_visible("#jump_pop"), 5):
        problems.append(f"{name}: Enter on the badge did not open the list")
    rows = page.eval_on_selector_all("#jump_pop li", "ls => ls.map(l => [+l.dataset.at, +l.dataset.count, l.dataset.stat === '1', l.textContent, !!l.querySelector('.srclink')])")
    r["list"] = [x[3] for x in rows[:4]]
    if sorted((x[0], x[1]) for x in rows) != sorted((e[0], e[3]) for e in P["into"]):
        problems.append(f"{name}: the list of {tgt:#x} is {[(x[0], x[1]) for x in rows]}, want {[(e[0], e[3]) for e in P['into']]}")
    row = [x for x in rows if x[0] == src]
    if row and ("conditional jump from" not in row[0][3] or f"taken {row[0][1]:,}×" not in row[0][3]):
        problems.append(f"{name}: the row of {src:#x} says {row[0][3]!r}")
    if row and "dwarf" in entry and not row[0][4]:
        problems.append(f"{name}: the row of {src:#x} has no file:line link: {row[0][3]!r}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jumps-{name}-list-1280-{scheme}.png"))
        # the source's name keeps its underscores (the button had clipped them)
        ink = boxed_line_ink(page, f'#jump_pop li[data-at="{src}"] button.jgo')
        if ink and not all(ok for _, ok in ink):
            problems.append(f"{name}: the list's button for {src:#x} hides underscores ({scheme}): {ink}")
        r[f"underscores {scheme}"] = ink
    page.emulate_media(color_scheme="light")
    # The listing is redrawn while the list is open (a scan does that; CI
    # once lost the focus this way): Escape still returns it to the badge.
    page.evaluate("MKB.inspect()")
    page.keyboard.press("Escape")
    if not wait_for(lambda: page.is_hidden("#jump_pop"), 5):
        problems.append(f"{name}: Escape did not close the list")
    elif not page.evaluate(f"document.activeElement && document.activeElement.matches('.jc[data-pa=\"{tgt}\"]')"):
        problems.append(f"{name}: Escape did not give the focus back to the badge")
    # "go there" from the list: the Page tab at the source, marked.
    badge = page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]')
    badge.click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    # The badge again closes the list, and so does the list's Close button.
    badge.click()
    again = wait_for(lambda: page.is_hidden("#jump_pop"), 5)
    badge.click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    page.click("#jump_pop button.jclose")
    closed = wait_for(lambda: page.is_hidden("#jump_pop"), 5)
    r["badge again, Close"] = [again, closed]
    if not again or not closed:
        problems.append(f"{name}: a second click on the badge closed the list: {again}; its Close button: {closed}")
    # Paths out of a return: none observed yet ("→?"), or the ones observed.
    outs = page.evaluate("""() => [...document.querySelectorAll('#listing .jc[data-dir="out"]')].map((b) => {
        const t = document.querySelector('#listing .ins[data-addr="' + b.dataset.pa + '"]');
        return [Number(b.dataset.pa), b.textContent, t ? t.textContent : '']; })""")
    for want_seen in (False, True):
        pick = next((o for o in outs if re.search(r"\bret", o[2]) and (o[1].endswith("?") != want_seen)), None)
        if not pick:
            if want_seen or name != "hello":  # hello's page has one return, and it ran
                problems.append(f"{name}: no return {'observed' if want_seen else 'with nothing observed'} in the listing ({len(outs)} out badges)")
            continue
        page.click(f'#listing .jc[data-dir="out"][data-pa="{pick[0]}"]')
        wait_for(lambda: page.is_visible("#jump_pop"), 5)
        notes = page.eval_on_selector_all("#jump_pop p.note", "ps => ps.map((p) => p.textContent)")
        n = page.locator("#jump_pop li").count()
        note = "A return: the targets listed are the ones observed while stepping." if want_seen else \
            "A return: where it goes is known only at run time, and no transfer from here has been observed yet."
        r[f"return {'seen' if want_seen else 'unseen'}"] = [hex(pick[0]), pick[1], n, notes[:1]]
        if note not in notes or (n == 0) == want_seen:
            problems.append(f"{name}: the paths out of the return at {pick[0]:#x} ({pick[1]!r}): {n} rows, notes {notes}, want {note!r}")
        page.keyboard.press("Escape")
        wait_for(lambda: page.is_hidden("#jump_pop"), 5)
    badge.click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    page.click(f'#jump_pop li[data-at="{src}"] button.jgo')
    hit = f'#listing .ins.jhit[data-addr="{src}"]'
    if not wait_for(lambda: page.locator(hit).count() == 1, 10):
        problems.append(f"{name}: go there did not open {src:#x} in the Page tab")
    elif not page.evaluate("""(sel) => { const l = document.querySelector(sel), b = document.querySelector('#listing').getBoundingClientRect(),
            r = l.getBoundingClientRect(); return r.top >= b.top && r.bottom <= b.bottom; }""", hit):
        problems.append(f"{name}: {src:#x} is not in view after go there")

    jump_inline_case(page, name, entry, want, problems, r, shots)

    # Phone: no horizontal scroll, the list fits, screenshots.
    page.set_viewport_size({"width": 390, "height": 844})
    time.sleep(0.5)
    page.evaluate(f"MKB.jumpGo({tgt})")
    wait_for(lambda: page.locator(line).count() == 1, 10)
    time.sleep(0.5)
    if not no_hscroll(page, "#listing") or not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append(f"{name}: horizontal scroll at 390 px")
    page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]').click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    box = page.locator("#jump_pop").bounding_box()
    if box and (box["x"] < 0 or box["x"] + box["width"] > 390):
        problems.append(f"{name}: the list is outside the 390 px viewport: {box}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jumps-{name}-list-390-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_jumps(ctx):
    failed = False
    for name in [n for n in JUMP_WALKS if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = jumps_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("jumps", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = jumps_live_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("jumps_live", p, r, seconds=round(time.monotonic() - t0))
