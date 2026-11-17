"""Browser test suite runto (test_browser.py --suites)."""

import json
import re
import time

from tests.browser.common import kernel_entries, machine_do, report, SITE, wait_for


def cpu_state_case(browser, base, shots, name):
    """The Debugger's machine-state sections at the end of a boot: CR4,
    GDTR, IDTR and TR set by the kernel, the GDT's kernel code segment
    (base 0, limit 4 GiB), IDT gate 0x80 at entry_INT80_32, the master
    PIC's IMR equal to the kernel's cached_irq_mask, PIT channel 0 at
    HZ=250 (reload 4773), and the translate box mapping start_kernel's
    virtual address to its physical one; no horizontal scroll at 390 px."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&speed=full#debugger")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 300, 1):
        return ["no end of the boot"], r
    page.evaluate("document.querySelectorAll('#cpu_state details').forEach((d) => d.open = true)")
    time.sleep(0.5)
    row = lambda sec, reg: page.inner_text(f"#cpu_{sec} [data-reg='{reg}']") \
        if page.locator(f"#cpu_{sec} [data-reg='{reg}']").count() else ""
    for sec, reg in (("control", "CR4"), ("tables", "GDTR"), ("tables", "IDTR"), ("tables", "TR")):
        r[reg] = row(sec, reg)
    if r["CR4"].split()[1:2] == ["0x00000000"]:
        problems.append("CR4 is 0: " + r["CR4"])
    for reg in ("GDTR", "IDTR", "TR"):
        if "base 0x00000000" in r[reg] or not r[reg]:
            problems.append(f"{reg} not set: {r[reg]!r}")
    page.click("#view_gdt")
    gdt = page.inner_text("#cpu_table")
    r["gdt_0x60"] = next((l for l in gdt.splitlines() if l.startswith("0x0060")), "")
    if not all(w in r["gdt_0x60"] for w in ("code", "base 0x00000000", "limit 0xffffffff", "32-bit")):
        problems.append("GDT entry 0x60 is not the flat 32-bit kernel code segment: " + r["gdt_0x60"])
    page.click("#view_idt")
    r["idt_0x80"] = next((l for l in page.inner_text("#cpu_table").splitlines() if l.startswith("0x80")), "")
    if "entry_INT80_32" not in r["idt_0x80"]:
        problems.append("IDT gate 0x80 does not point at entry_INT80_32: " + r["idt_0x80"])
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    cim = next((x for x in syms if x["name"] == "cached_irq_mask"), None)
    r["pic"] = row("irq", "PIC master")
    m = re.search(r"IMR 0x([0-9a-f]{2})", r["pic"])
    if cim and m:
        cached = page.evaluate(f"MKB.stepper.cpu.mem8[{cim['paddr']}]")
        r["imr"], r["cached_master_mask"] = m.group(1), f"{cached:02x}"
        if int(m.group(1), 16) != cached:
            problems.append(f"PIC IMR 0x{m.group(1)} != the kernel's cached_irq_mask 0x{cached:02x}")
    else:
        problems.append(f"no PIC IMR or cached_irq_mask: {r['pic']!r}")
    r["pit"] = row("irq", "PIT ch0")
    if "reload 4773 " not in r["pit"]:
        problems.append("PIT channel 0 is not at HZ=250: " + r["pit"])
    sk = next(x for x in syms if x["name"] == "start_kernel")
    page.fill("#xlate_in", hex(sk["vaddr"]))
    page.click("#xlate_go")
    r["translate"] = page.inner_text("#xlate_out").splitlines()[-2:]
    if f"physical 0x{sk['paddr']:08x}" not in page.inner_text("#xlate_out"):
        problems.append(f"translate {hex(sk['vaddr'])} did not give {hex(sk['paddr'])}: {r['translate']}")
    if page.is_enabled("#dbg_pause"):
        machine_do(page, "click", "#dbg_pause")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    cpu_state_tools(page, sk, problems, r)
    page.screenshot(path=str(shots / f"cpu-state-{name}-1280.png"), full_page=True)
    page.set_viewport_size({"width": 390, "height": 900})
    time.sleep(0.5)
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("horizontal scroll at 390 px with the machine state open")
    page.eval_on_selector("#cpu_state", "e => e.scrollIntoView()")
    page.screenshot(path=str(shots / f"cpu-state-{name}-390.png"))
    if errors:
        problems.append(f"page errors: {errors}")
    page.close()
    return problems, r


# Hand-made PAE tables in the top 64 KiB of RAM (saved and put back by the
# caller): PDPT, PD and PT 4 KiB apart. Linear 0x40000000-0x7fffffff (PDPTE
# 1) -> the PD; its PDE 0 -> the PT, whose PTE 5 maps 0x40005000 -> 0x123000;
# its PDE 1 is a 2 MiB page 0x40200000 -> 0x200000; its PDE 2 is not
# present; its PDE 3 names a page table past the end of RAM (0xfffff000). The
# PT's PTE 6 is not present. PDPTE 2 is not present.
PAE_JS = """(on) => {
    const c = MKB.stepper.cpu, m = c.mem8, dv = new DataView(m.buffer, m.byteOffset, m.length);
    const S = (m.length - 0x10000) & ~0xfff;
    if (!on) {
        m.set(window.__paeSaved.bytes, S);
        c.cr[3] = window.__paeSaved.cr3;
        c.cr[4] = window.__paeSaved.cr4;
        return null;
    }
    window.__paeSaved = { bytes: m.slice(S, S + 0x3000), cr3: c.cr[3], cr4: c.cr[4] };
    m.fill(0, S, S + 0x3000);
    const q = (a, lo) => { dv.setUint32(a, lo >>> 0, true); dv.setUint32(a + 4, 0, true); };
    q(S + 1 * 8, (S + 0x1000) | 1);
    q(S + 0x1000, (S + 0x2000) | 3);
    q(S + 0x1000 + 8, 0x200000 | 0x83);
    q(S + 0x1000 + 3 * 8, 0xfffff000 | 1);
    q(S + 0x2000 + 5 * 8, 0x123000 | 3);
    c.cr[3] = S;
    c.cr[4] = c.cr[4] | 0x20;
    return S;
}"""


# Saves (on) or puts back (off) the top 64 KiB of RAM and CR0, CR3, CR4,
# IDTR; returns S, the first of those bytes.
HAND_SAVE_JS = """(on) => {
    const c = MKB.stepper.cpu, m = c.mem8, S = (m.length - 0x10000) & ~0xfff;
    if (!on) {
        const v = window.__handSaved;
        m.set(v.bytes, S);
        [c.cr[0], c.cr[3], c.cr[4], c.idtr_offset[0], c.idtr_size[0]] = v.regs;
        return null;
    }
    window.__handSaved = { bytes: m.slice(S, S + 0x10000), regs: [c.cr[0], c.cr[3], c.cr[4], c.idtr_offset[0], c.idtr_size[0]] };
    return S;
}"""


def hand_tables(page, sk, xlate, problems, r):
    """Hand-made tables in the top 64 KiB of RAM: a 2-level page directory
    whose PDEs make runs (contiguous 4 MiB pages, contiguous page tables, a
    gap), the page-directory view's lines for them; paging off (CR0.PG
    clear): translate says so and maps linear to physical, the view says
    paging is off; an IDT with a gate to code no symbol covers, one not
    present, an empty one, a present one of type 0 and a trap gate of DPL
    3, each line as written here."""
    S = page.evaluate(HAND_SAVE_JS, True)
    try:
        pd = page.evaluate("""(S) => { const c = MKB.stepper.cpu, dv = new DataView(c.mem8.buffer, c.mem8.byteOffset, c.mem8.length);
            new Uint8Array(c.mem8.buffer, c.mem8.byteOffset + S, 0x10000).fill(0);
            const e = (i, v) => dv.setUint32(S + i * 4, v >>> 0, true);
            e(0, 0x000083); e(1, 0x400083); e(2, 0x800083);
            e(4, (S + 0x1000) | 3); e(5, (S + 0x2000) | 3); e(6, (S + 0x4000) | 3);
            e(8, 0xc00083);
            c.cr[3] = S; c.cr[4] = (c.cr[4] & ~0x20) | 0x10;
            document.getElementById('pd_view').click();
            return document.getElementById('xlate_out').textContent; }""", S)
        # (Enter in the field translates too.)
        page.fill("#xlate_in", "0x00812345")
        page.press("#xlate_in", "Enter")
        big = page.inner_text("#xlate_out")
        page.evaluate("() => { const c = MKB.stepper.cpu; c.cr[0] = c.cr[0] & ~0x80000000; }")
        off = xlate("0x1234")
        page.click("#pd_view")
        off_pd = page.inner_text("#xlate_out")
    finally:
        page.evaluate(HAND_SAVE_JS, False)
    want = ["0x00000000-0x00bfffff 3 PDEs, 4 MiB pages from 0x00000000 flags 0x83",
            f"0x01000000-0x017fffff 2 PDEs, page tables from 0x{S + 0x1000:08x} flags 0x03",
            f"0x01800000-0x01bfffff page tables from 0x{S + 0x4000:08x} flags 0x03",
            "0x02000000-0x023fffff 4 MiB pages from 0x00c00000 flags 0x83"]
    r["hand_pd"] = pd.splitlines()
    if pd.splitlines() != want:
        problems.append(f"hand-made page directory: {pd.splitlines()}, want {want}")
    if not big.endswith("physical 0x00812345 (4 MiB page)"):
        problems.append(f"hand-made page directory: 0x00812345 in PDE 2's 4 MiB page: {big!r}")
    r["paging_off"] = [off.splitlines(), off_pd]
    if off.splitlines()[:3] != ["linear 0x00001234", "paging off (CR0.PG = 0): linear = physical", "physical 0x00001234"] \
            or off_pd != "Paging is off.":
        problems.append(f"paging off: translate {off!r}, page directory {off_pd!r}")
    # The IDT: its first five gates written over the kernel's own (put back
    # after); gate 0 to mapped RAM that no symbol of symbols.json covers.
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    mapped = page.evaluate("""() => { const out = [];
        for (let pa = 0x100000; pa < MKB.stepper.cpu.mem8.length; pa += 0x1000)
            if (MKB.pageWalk(MKB.stepper.cpu, pa + 0xc0000000).phys === pa) out.push(pa + 0x100);
        return out; }""")
    free = next((a for a in mapped if not any(x["paddr"] <= a < x["paddr"] + max(x["size"], 1) for x in syms)), -1)
    if free < 0:
        problems.append(f"no mapped RAM without a symbol for an IDT gate: {free:#x}")
        return
    lin = free + 0xC0000000
    # start_kernel's second instruction (disasm.json): the IDT view names code at instruction starts only.
    insns = json.loads((SITE / kernel_entries()["minified-linux"]["disasm"]).read_text())["insns"]
    second = next(a for a, *_ in insns if a > sk["paddr"]) - sk["paddr"]
    gates = [(lin, 0x60, 0x8E), (0x12345678, 0x60, 0x0E), (0, 0, 0), (sk["vaddr"] + second, 0x60, 0x80), (sk["vaddr"], 0x60, 0xEF)]
    idt = page.evaluate("""(gates) => { const c = MKB.stepper.cpu, dv = new DataView(c.mem8.buffer, c.mem8.byteOffset, c.mem8.length);
        const at = MKB.pageWalk(c, c.idtr_offset[0]).phys, saved = c.mem8.slice(at, at + gates.length * 8), size = c.idtr_size[0];
        try {
            gates.forEach(([off, sel, acc], i) => {
                dv.setUint32(at + i * 8, ((off & 0xffff) | sel << 16) >>> 0, true);
                dv.setUint32(at + i * 8 + 4, ((off & 0xffff0000) | acc << 8) >>> 0, true); });
            c.idtr_size[0] = gates.length * 8 - 1;
            document.getElementById('view_idt').click();
            return document.getElementById('cpu_table').textContent;
        } finally { c.mem8.set(saved, at); c.idtr_size[0] = size; } }""", gates)
    want = [f"0x00 32-bit interrupt gate 0x0060:0x{lin:08x} DPL 0 ", "0x01 not present",
            f"0x03 type 0 0x0060:0x{sk['vaddr'] + second:08x} DPL 0 start_kernel+{second:02x}",
            f"0x04 32-bit trap gate 0x0060:0x{sk['vaddr']:08x} DPL 3 start_kernel"]
    r["hand_idt"] = idt.splitlines()
    if idt.split("\n") != want:
        problems.append(f"hand-made IDT: {idt.splitlines()}, want {want}")


def cpu_state_tools(page, sk, problems, r):
    """The Debugger's paging and register tools on a paused machine: the
    translate box takes a symbol name and refuses junk, the page directory
    overview's run holding start_kernel maps it where translate does, a
    hole in it translates to a page fault; PAE (no shipped kernel turns it
    on) on hand-made tables: a 4 KiB and a 2 MiB page and a missing PDPTE;
    the GDT and IDT views say "not mapped" for a table at an unmapped
    address; hand_tables; MMX and SSE registers show once they hold a
    value, and a changed row is marked."""
    def xlate(text):
        page.fill("#xlate_in", text)
        page.click("#xlate_go")
        return page.inner_text("#xlate_out")
    want = f"physical 0x{sk['paddr']:08x}"
    out = xlate("start_kernel")
    r["translate_name"] = out.splitlines()[-2:]
    if want not in out:
        problems.append(f"translate start_kernel (by name) did not give {want}: {out!r}")
    out = xlate("no_such_symbol_xyz")
    if out != "Give a hex address or a symbol name.":
        problems.append(f"translate of an unknown name: {out!r}")
    page.click("#pd_view")
    pd = page.inner_text("#xlate_out").splitlines()
    r["pd_lines"] = len(pd)
    runs = []
    for line in pd:
        m = re.match(r"0x([0-9a-f]{8})-0x([0-9a-f]{8}) (?:(\d+) PDEs, )?(4 MiB pages|page tables) from 0x([0-9a-f]{8}) flags 0x([0-9a-f]{2})$", line)
        if not m:
            problems.append(f"page directory line not understood: {line!r}")
            continue
        lo, hi, n = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3) or 1)
        if hi + 1 - lo != n << 22:
            problems.append(f"page directory line: {n} PDEs do not span {line!r}")
        runs.append((lo, hi, m.group(4), int(m.group(5), 16)))
    hit = [x for x in runs if x[0] <= sk["vaddr"] <= x[1]]
    r["pd_start_kernel"] = hit
    if len(hit) != 1:
        problems.append(f"no single page directory run holds start_kernel 0x{sk['vaddr']:08x}: {hit} of {len(runs)}")
    elif hit[0][2] == "4 MiB pages" and hit[0][3] + sk["vaddr"] - hit[0][0] != sk["paddr"]:
        problems.append(f"the page directory maps start_kernel to 0x{hit[0][3] + sk['vaddr'] - hit[0][0]:08x}, translate to 0x{sk['paddr']:08x}")
    hole = next((a for a in range(0, 1 << 32, 1 << 22) if not any(x[0] <= a <= x[1] for x in runs)), None)
    if hole is None:
        problems.append("the page directory has no hole to translate")
    else:
        out = xlate(hex(hole + 0x1234))
        r["hole"] = out.splitlines()[-1]
        if not out.endswith("not mapped (page fault)"):
            problems.append(f"translate 0x{hole + 0x1234:08x} (no PDE in the overview): {out!r}")
    # PAE.
    page.evaluate(PAE_JS, True)
    try:
        pae = {a: xlate(a) for a in ("0x40005abc", "0x40212345", "0x80000000", "0x40006000", "0x40400000", "0x40600000")}
        page.click("#pd_view")
        pae["pd"] = page.inner_text("#xlate_out")
    finally:
        page.evaluate(PAE_JS, False)
    r["pae"] = {k: v.splitlines()[-1] for k, v in pae.items()}
    if not pae["0x40005abc"].endswith("physical 0x00123abc (4 KiB page)") or pae["0x40005abc"].count(" at ") != 3:
        problems.append(f"PAE: 0x40005abc through PDPTE, PDE, PTE to 0x00123abc: {pae['0x40005abc']!r}")
    if not pae["0x40212345"].endswith("physical 0x00212345 (2 MiB page)"):
        problems.append(f"PAE: 0x40212345 in a 2 MiB page at 0x200000: {pae['0x40212345']!r}")
    if not pae["0x80000000"].endswith("not mapped (page fault)"):
        problems.append(f"PAE: 0x80000000 (PDPTE 2 not present): {pae['0x80000000']!r}")
    # A missing PTE and PDE: the walk stops at that level, its entry 0; a
    # page table past the end of RAM: its PTE "beyond RAM".
    for a, level, value in (("0x40006000", "PTE", "= 0x00000000 "), ("0x40400000", "PDE", "= 0x00000000 "),
                            ("0x40600000", "PTE", "at 0xfffff000 = beyond RAM")):
        ls = pae[a].splitlines()
        if ls[-1] != "not mapped (page fault)" or not ls[-2].startswith(level + " ") or value not in ls[-2] + " ":
            problems.append(f"PAE: {a} should stop at its {level} ({value.strip()}): {ls}")
    pdl = pae["pd"].splitlines()
    if len(pdl) != 4 or "→ PD" not in pdl[1] or not pdl[2].endswith("not present"):
        problems.append(f"PAE page directory: the four PDPTEs, 1 present, 2 not: {pdl}")
    if xlate(hex(sk["vaddr"])).find(want) < 0:
        problems.append("translate after the PAE test: start_kernel no longer maps (CR3/CR4 not restored?)")
    hand_tables(page, sk, xlate, problems, r)
    # GDT and IDT at an unmapped linear address.
    if hole is not None:
        saved = page.evaluate(f"""() => {{ const c = MKB.stepper.cpu, s = [c.gdtr_offset[0], c.idtr_offset[0]];
            c.gdtr_offset[0] = c.idtr_offset[0] = {hole}; return s; }}""")
        try:
            page.click("#view_gdt")
            gdt = page.inner_text("#cpu_table").splitlines()
            page.click("#view_idt")
            idt = page.inner_text("#cpu_table").splitlines()
        finally:
            page.evaluate("(s) => { MKB.stepper.cpu.gdtr_offset[0] = s[0]; MKB.stepper.cpu.idtr_offset[0] = s[1]; }", saved)
        r["gdt_unmapped"], r["idt_unmapped"] = gdt[:1], idt[:1]
        if not gdt or not all(l.endswith(" not mapped") for l in gdt):
            problems.append(f"GDT at an unmapped address: {gdt[:3]}")
        if not idt or not all(l.endswith(" not mapped") for l in idt):
            problems.append(f"IDT at an unmapped address: {idt[:3]}")
    # MMX and SSE registers: shown once they hold a value; a changed row marked.
    fpu = lambda: page.evaluate("""() => { MKB.renderCpuState(); return [...document.querySelectorAll('#cpu_fpu [data-reg]')]
        .map((e) => [e.dataset.reg, e.textContent, e.classList.contains('chg')]); }""")
    before = fpu()
    saved = page.evaluate("""() => { const c = MKB.stepper.cpu, s = [Array.from(c.fpu_st), Array.from(c.reg_xmm32s), c.fpu_stack_empty[0]];
        c.fpu_st[4] = 0x12345678; c.reg_xmm32s[0] = 0x3f800000; return s; }""")
    try:
        mmx = fpu()
        page.evaluate("MKB.stepper.cpu.fpu_stack_empty[0] = MKB.stepper.cpu.fpu_stack_empty[0] & 0xfe")
        st = fpu()
    finally:
        page.evaluate("""(s) => { const c = MKB.stepper.cpu; c.fpu_st.set(s[0]); c.reg_xmm32s.set(s[1]); c.fpu_stack_empty[0] = s[2];
            MKB.renderCpuState(); }""", saved)
    names = lambda rows: [x[0] for x in rows]
    row = lambda rows, n: next((x for x in rows if x[0] == n), None)
    r["fpu_rows"] = [len(before), len(mmx), len(st)]
    if "MM1" in names(before) or "XMM0" in names(before):
        problems.append(f"MMX/SSE rows shown while unused: {names(before)}")
    if not row(mmx, "MM1") or "12345678" not in row(mmx, "MM1")[1] or "ST0" in names(mmx):
        problems.append(f"MM1 = 0x12345678 not shown (or ST rows while the stack is empty): {names(mmx)} {row(mmx, 'MM1')}")
    if not row(mmx, "XMM0") or not row(mmx, "XMM0")[1].rstrip().endswith("3f800000"):
        problems.append(f"XMM0 = 0x3f800000 not shown: {row(mmx, 'XMM0')}")
    if "ST0" not in names(st) or not row(st, "FTW") or not row(st, "FTW")[2]:
        problems.append(f"ST0 in use: ST rows, FTW marked changed: {names(st)[:12]} {row(st, 'FTW')}")


def runto_refusals(page, syms, problems, r):
    """Paused at the reset vector: Step N from the keyboard (Enter in its
    field) steps that many; a Run to the page refuses says why in the
    Debugger's status and the machine stays where it is (the similar names
    and sections from symbols.json)."""
    s0 = page.evaluate("MKB.stepper.steps")
    page.fill("#dbg_n", "3")
    page.press("#dbg_n", "Enter")
    r["step_n_enter"] = wait_for(lambda: page.evaluate("MKB.stepper.steps") == s0 + 3
                                 and page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    if not r["step_n_enter"]:
        problems.append(f"Enter in the Step N field (3): {page.evaluate('MKB.stepper.steps') - s0} steps")
    secs = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["sections"]
    data = next(x for x in syms if x["name"] == "jiffies")
    sec = next(x for x in secs if x["paddr"] <= data["paddr"] < x["paddr"] + x["size"])
    not_code = f"{data['paddr']:#x} is in {sec['name']} ({sec['class']}), not code: the CPU never executes it, so Run to would not stop"
    typo = "kernel_ini"
    like = sorted(dict.fromkeys(x["name"] for x in syms if typo in x["name"].lower()), key=lambda n: (not n.startswith(typo), len(n)))
    word = f"{data['vaddr']:x}"  # a kernel virtual address that is also a word (no symbol has that name)
    assert word[0] in "abcdef" and not any(x["name"] == word for x in syms)
    want = {
        typo: f"Run to {typo}: no symbol of that name; similar: {', '.join(like)}. Run to takes a function name, a hex address or file:line.",
        "jiffies": f"Run to jiffies: {not_code}",
        word: f"Run to {not_code}.",
        "free text": "Run to free text: not a function name, hex address or file:line",
        "0x1ffffffff": "Run to 0x1ffffffff: not a function name, hex address or file:line",
    }
    eip = page.get_attribute("#regs", "data-phys")
    got = {}
    for text, w in want.items():
        page.fill("#dbg_addr", text)
        page.press("#dbg_addr", "Enter")
        wait_for(lambda: page.inner_text("#dbg_status") == w, 5)
        got[text] = page.inner_text("#dbg_status")
        if got[text] != w or page.get_attribute("#dbg_state", "data-state") != "paused" or page.get_attribute("#regs", "data-phys") != eip:
            problems.append(f"Run to {text!r}: status {got[text]!r}, want {w!r}; state {page.get_attribute('#dbg_state', 'data-state')}")
    r["refused"] = got


def runto_case(browser, base, shots):
    """Run to rest_init on the minified Linux stops there with no fault,
    with and without fast-forward. The fast Run to used to write its HLT
    breakpoint into the bzImage payload still lying at rest_init's
    physical address (the minified Linux's vmlinux runs where its bzImage
    is loaded), which ended halted in exc_page_fault after 72,644
    instructions."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    ri = next(x for x in syms if x["name"] == "rest_init")
    for ff in ("", "&fastforward=0"):
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto(f"{base}/index.html?kernel=minified-linux&paused=1{ff}#debugger")
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
            problems.append(f"did not start paused ({ff or 'fast'})")
            page.close()
            continue
        if not ff:
            runto_refusals(page, syms, problems, r)
        page.fill("#dbg_addr", hex(ri["vaddr"]))
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                      and page.get_attribute("#regs", "data-phys") == str(ri["paddr"]), 180)
        status = page.inner_text("#dbg_status")
        r[ff or "fast"] = status
        if not ok or "Halted" in status:
            problems.append(f"Run to rest_init ({ff or 'fast'}): {status}")
        page.close()
    return problems, r


def suite_runto(ctx):
    failed = report("runto", *runto_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("cpu_state", *cpu_state_case(ctx.browser, ctx.base, ctx.shots, "minified-linux"))
