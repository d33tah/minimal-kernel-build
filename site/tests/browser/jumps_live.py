"""Part of a browser suite (test_browser.py)."""

import json
import os
import re
import time

from tests.browser.common import import_inputs, kernel_entries, run_to_wait, show_tab, SITE, wait_for
from tests.browser.jumps_inline import NOPATH_MARKS_JS


# The jump path texts of a listing (selector): its ⊘ titles and, for
# #eip_jumps / #jump_info, their text.
JUMP_CLAIMS_JS = """(sel) => { const out = [];
  for (const box of document.querySelectorAll(sel)) {
    if (box.id === 'eip_jumps' || box.id === 'jump_info') out.push([box.id, box.textContent]);
    for (const s of box.querySelectorAll('.jnone')) out.push(['title', s.dataset.tip || s.title]); }
  return out; }"""


def jump_claims_live(claims, where):
    """Claims of "no direct path" made from instructions decoded live must
    say so: jumps from code outside the decoded range are not known."""
    bad = {}
    for kind, t in claims:
        low = t.lower()
        if "no jump or call in the disassembly" in low or "nothing jumps or calls here" in low:
            why = "claims what only the whole disassembly could tell"
        elif ("no direct path" in low or "without a direct path" in low) and not ("decoded live" in low or "live decode" in low):
            why = "says 'no direct path' without the live-decode scope"
        else:
            continue
        bad.setdefault((kind, why), []).append(t)
    return [f"{where}: {len(ts)} {kind} {why}, e.g. {ts[0][:220]!r}" for (kind, why), ts in bad.items()]


def text_page_without_symbol(page, name, where, r):
    """A page of a text section that no sized code symbol covers (Debian's
    .altinstr_aux): the live decode starts at the page (or the section)
    and says its first lines may be misaligned."""
    S = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())
    rx = re.compile(r"^\.(init\.|head\.|exit\.)?text|^\.text")
    pages = lambda a, n: range(a // 4096, (a + n - 1) // 4096 + 1)
    code = {p for s in S["symbols"] if s.get("size", 0) > 0 and rx.search(s.get("section", "")) for p in pages(s["paddr"], s["size"])}
    secs = [s for s in S.get("sections", []) if s.get("class") == "text"]
    cand = sorted({p for s in secs for p in pages(s["paddr"], s["size"])} - code)
    if not cand:
        return []
    p = cand[0]
    start = max(p * 4096, min(s["paddr"] for s in secs if s["paddr"] < p * 4096 + 4096 and s["paddr"] + s["size"] > p * 4096))
    page.evaluate(f"MKB.jumpTo({p * 4096})")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(p) and page.get_attribute("#listing", "data-source") == "live" and
             int(page.get_attribute("#listing", "data-insns") or 0) > 0, 30)
    first = page.evaluate("(() => { const e = document.querySelector('#listing .ins'); return e ? +e.dataset.addr : -1; })()")
    info = page.inner_text("#dump_info")
    r[where + " text page without a symbol"] = [hex(p * 4096), hex(first), info[:160]]
    if first != start or "the first lines outside a symbol may be misaligned" not in info:
        return [f"{where}: the text page {p * 4096:#x} without a symbol: first instruction {first:#x} (want {start:#x}), info {info[:200]!r}"]
    return []


def jumps_live_case(browser, base, shots):
    """J2: with no disasm.json (the Debian sample, an imported vmlinux)
    static paths come only from the instructions decoded live (a page, or
    a few around EIP); the ⊘ tooltip, the list notes, the Page note and
    the Debugger's paths at EIP must say so instead of claiming nothing
    jumps or calls here. With disasm.json the wording names the whole
    disassembly (hello's padding after a ret)."""
    problems, r = [], {}
    cases = []
    deb = next((n for n in kernel_entries() if n.startswith("debian") and "initrd" not in kernel_entries()[n]), None)
    if deb:
        cases.append((deb, None))
    elf = import_inputs()
    if elf and "minified-linux" in kernel_entries():
        cases.append(("minified-linux", elf[1]))
    elif os.environ.get("CI"):
        problems.append("no vmlinux to import (kernels/import-test)")
    for name, imp in cases:
        where = name + (" + imported " + imp.name if imp else "")
        errors = []
        ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
        if imp:
            # Where clang.wasm is served (the live site, a local site with
            # the clang-wasm artifact; CI's site artifact has none) the
            # import makes the vmlinux's disasm.json in the page (E1,
            # genimport) and it arrives during the case; this case is about
            # the paths without one. On the context: a Worker's requests.
            ctx.route("**/clang/clang.wasm", lambda route: route.fulfill(status=404, body="no"))
        page = ctx.new_page()
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        if imp:
            page.evaluate("MKB.startPaused = true")
            page.set_input_files("#symfile", str(imp))
            wait_for(lambda: page.get_attribute("#sym_status", "data-state") in ("imported", "error"), 60)
            if page.get_attribute("#sym_status", "data-state") != "imported":
                problems.append(f"{where}: import failed: {page.inner_text('#sym_status')[:200]!r}")
                ctx.close()
                continue
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.evaluate("typeof window.MKB?.symbols !== 'undefined' && !!MKB.symbols && MKB.symbols.symbols.length > 0"), 120)
        sk = page.evaluate("MKB.symbols.symbols.find((s) => s.name === 'start_kernel')")
        page.fill("#dbg_addr", "start_kernel")
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {sk["paddr"]}, 400)
        if not ok:
            problems.append(f"{where}: Run to start_kernel did not stop there: {status!r}")
            ctx.close()
            continue
        if page.evaluate("!!MKB.disasm"):
            problems.append(f"{where}: has disasm.json, the case needs a live decode")
        wait_for(lambda: page.get_attribute("#eip_jumps", "data-pa") == str(sk["paddr"]), 20)
        wait_for(lambda: page.get_attribute("#context", "data-source") == "live", 20)
        r[where + " eip"] = page.inner_text("#eip_jumps")[:300]
        claims = page.evaluate(JUMP_CLAIMS_JS, "#context, #eip_jumps")
        # start_kernel's first instruction: callers are outside the
        # decoded range, so it must not be claimed unreachable.
        if not any("no direct path" in t.lower() for _, t in claims):
            r[where + " note"] = "no 'no direct path' claim at the start_kernel entry"
        problems += jump_claims_live(claims, where + " Code at EIP")
        show_tab(page, "page")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 int(page.get_attribute("#listing", "data-insns") or 0) > 0, 30)
        time.sleep(1)
        claims = page.evaluate(JUMP_CLAIMS_JS, "#listing, #jump_info")
        r[where + " page ⊘"] = sum(1 for k, _ in claims if k == "title")
        r[where + " page note"] = page.inner_text("#jump_info")[:300]
        problems += jump_claims_live(claims, where + " Page listing")
        # Z1: the live decode does not know what comes before its first
        # instruction, nor who calls a function: neither gets ⊘.
        for sel in ("#listing", "#context"):
            marked = [a for a, _, _ in page.evaluate(NOPATH_MARKS_JS, sel)]
            first = page.evaluate(f"(() => {{ const e = document.querySelector('{sel} .ins'); return e ? +e.dataset.addr : -1; }})()")
            entries = page.evaluate("(l) => l.filter((a) => MKB.symbols.symbols.some((s) => s.paddr === a && /^[Tt]$/.test(s.type || 'T')))", marked)
            r[f"{where} {sel} ⊘"] = len(marked)
            if first in marked:
                problems.append(f"{where} {sel}: ⊘ on the first instruction decoded ({first:#x}): nothing before it is known")
            if entries:
                problems.append(f"{where} {sel}: ⊘ on {len(entries)} function entries (their callers are not decoded), e.g. {[hex(a) for a in entries[:4]]}")
        # A list opened at an instruction with paths in shows the scope note.
        if page.locator("#listing .jc[data-dir=in]").count():
            page.click("#listing .jc[data-dir=in] >> nth=0")
            wait_for(lambda: page.is_visible("#jump_pop"), 5)
            notes = page.eval_on_selector_all("#jump_pop p.note", "ps => ps.map((p) => p.textContent)")
            problems += jump_claims_live([("list note", t) for t in notes], where + " list")
            if not any("decoded live" in t or "live decode" in t for t in notes):
                problems.append(f"{where}: the list does not say static paths are only from the live decode: {notes}")
            page.keyboard.press("Escape")
        page.screenshot(path=str(shots / f"jumps-live-{name}{'-import' if imp else ''}-page.png"))
        problems += stub_arrays_live(page, where, r, shots, name + ("-import" if imp else ""))
        if not imp:
            problems += text_page_without_symbol(page, name, where, r)
        if errors:
            problems.append(f"{where}: page errors: {errors[:3]}")
        ctx.close()
    # With disasm.json the claim is about the whole disassembly.
    if "hello" in kernel_entries():
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.evaluate("!!MKB.disasm && !!MKB.symbols"), 30)
        got = page.evaluate("""() => { const S = MKB.jumpsFor(MKB.disasm.insns);
          const pa = MKB.disasm.insns.map((x) => x[0]).find((a) => MKB.jumpPaths(S, null, a).noDirect);
          if (pa === undefined) return null;
          MKB.renderEipJumps(document.getElementById('eip_jumps'), S, pa); const t = document.getElementById('eip_jumps').textContent;
          const d = document.createElement('div'); d.innerHTML = MKB.jumpBadgesHtml(MKB.jumpPaths(S, null, pa), pa);
          const n = d.querySelector('.jnone'); return { pa, eip: t, title: n ? n.dataset.tip || n.title : null }; }""")
        r["hello build"] = got
        if not got:
            problems.append("hello: no instruction without a direct path in disasm.json")
        else:
            if "nothing jumps or calls here" not in got["eip"] or "decoded live" in got["eip"]:
                problems.append(f"hello: the build's wording at EIP changed: {got['eip']!r}")
            if not got["title"] or "whole disassembly" not in got["title"] or "decoded live" in got["title"]:
                problems.append(f"hello: the build's ⊘ title changed: {got['title']!r}")
        page.close()
    return problems, r


# Linux's arrays of entry stubs (Z2) and the function that puts
# start + i * stride into the IDT. Nothing jumps to a stub after the first
# and the stub before it ends in a jmp, so without the rule each one would
# carry ⊘.
STUB_ARRAYS = {"early_idt_handler_array": "idt_setup_early_handler",
               "irq_entries_start": "idt_setup_apic_and_irq_gates",
               "spurious_entries_start": "idt_setup_apic_and_irq_gates"}

# The live-decoded instructions of the Page listing in [lo, hi) after a
# jump (padding int3/nop is not a stub), the ⊘ marks among them and, per
# stub, what Paths at EIP says about how it is reached.
STUB_LIVE_JS = """([lo, hi]) => { const box = document.getElementById('listing'), S = box._jS;
  const marks = new Set();
  for (const n of box.querySelectorAll('.jnone')) {
    let e = n.previousElementSibling; while (e && !e.matches('.ins')) e = e.previousElementSibling;
    if (e) marks.add(+e.dataset.addr); }
  const out = [];
  for (const e of box.querySelectorAll('.ins')) {
    const a = +e.dataset.addr; if (a < lo || a >= hi) continue;
    const i = S ? MKB.jumpsIndex(S, a) : -1, text = i >= 0 ? S.insns[i][2] : '';
    let eip = '';
    if (S && i >= 0) { MKB.renderEipJumps(document.getElementById('eip_jumps'), S, a); eip = document.getElementById('eip_jumps').textContent; }
    out.push([a, text, marks.has(a), eip]); }
  MKB.renderContext();
  return out; }"""


def stub_arrays_live(page, where, r, shots, tag):
    """Z2: in a live decode (no disasm.json) the stubs of Linux's entry
    stub arrays have no ⊘ and say how they are reached (start + i *
    stride, from the function that fills the IDT); padding after a stub's
    jmp may keep ⊘."""
    problems = []
    for name, setter in STUB_ARRAYS.items():
        sym = page.evaluate("(n) => MKB.symbols.symbols.find((s) => s.name === n) || null", name)
        if not sym:
            r[f"{where} {name}"] = "not in the symbol table"
            if name == "early_idt_handler_array":
                problems.append(f"{where}: no early_idt_handler_array symbol")
            continue
        lo, hi = sym["paddr"], sym["paddr"] + sym["size"]
        page.evaluate("(a) => MKB.jumpTo(a)", lo)
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 page.locator(f"#listing .ins[data-addr='{lo}']").count() > 0, 30)
        time.sleep(0.5)
        lines = page.evaluate(STUB_LIVE_JS, [lo, hi])
        pad = re.compile(r"^(nop|int3)")
        stubs = [x for x in lines if not pad.match(x[1])]
        bad = [f"{a:#x} {t}" for a, t, m, _ in stubs if m]
        r[f"{where} {name}"] = (f"{len(lines)} instructions, {len(stubs)} not padding, {len(bad)} with ⊘, "
                                f"{sum(1 for x in lines if x[2])} ⊘ on padding" if not bad else bad[:4])
        if not stubs:
            problems.append(f"{where}: {name}: no instructions decoded in the Page listing")
            continue
        if bad:
            problems.append(f"{where}: {name}: ⊘ on {len(bad)} stubs, e.g. {bad[:4]}")
        # The stubs after the first (the first instruction after a jmp, past
        # any padding): Paths at EIP names the array, the stride and the setter.
        later = [b for a, b in zip(stubs, stubs[1:]) if a[1].startswith("jmp")]
        if len(later) < 3:
            problems.append(f"{where}: {name}: expected at least 3 stubs after the first, got {len(later)}")
        for a, t, _, eip in later:
            if not (f"{name} + " in eip and setter in eip):
                problems.append(f"{where}: {name}+{a - lo:#x}: Paths at EIP does not say how the stub is reached: {eip[:300]!r}")
                break
        if later:
            r[f"{where} {name} text"] = later[0][3][:300]
        if name == "early_idt_handler_array":
            page.screenshot(path=str(shots / f"jumps-live-{tag}-stubs.png"))
    return problems
