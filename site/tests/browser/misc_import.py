"""Part of a browser suite (test_browser.py)."""

import json
import pathlib
import shutil
import struct
import tempfile

from tests.browser.common import gate_on_screen, import_inputs, kernel_entries, show_tab, SITE, wait_for


def import_case(browser, base, shots):
    """Imported System.map / vmlinux give the same symbols as symbols.json."""
    problems, r = [], {}
    files = import_inputs()
    if not files:
        return ["no System.map/vmlinux to import"], r
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")

    def at_start_kernel(path):
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        if path:
            page.set_input_files("#symfile", str(path))
            wait_for(lambda: page.get_attribute("#sym_status", "data-state") in ("imported", "error"), 10)
            r[path.name + "_status"] = page.inner_text("#sym_status")
            if page.get_attribute("#sym_status", "data-state") != "imported":
                problems.append(f"{path.name} not imported: {page.inner_text('#sym_status')}")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("stepped", "paused", "jit"), 30)
        page.fill("#dbg_addr", hex(sk["vaddr"]))
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                      and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 60)
        cur = page.get_attribute("#cur_sym", "data-name") if ok else None
        listing_note = None
        if path:
            show_tab(page, "page")
            listing_note = page.inner_text("#dump_info")
            page.set_viewport_size({"width": 390, "height": 900})
            for t in ("memory", "page", "debugger"):
                show_tab(page, t)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{path.name}: page scrolls horizontally at 390 px on the {t} tab")
            page.screenshot(path=str(shots / f"import-{path.name}-390.png"))
        page.close()
        if errors:
            problems.append("page errors: " + "; ".join(errors))
        return cur, listing_note

    base_cur, _ = at_start_kernel(None)
    r["symbols.json"] = base_cur
    for path in files:
        cur, note = at_start_kernel(path)
        r[path.name] = cur
        if cur != base_cur or cur != "start_kernel":
            problems.append(f"{path.name}: current symbol at start_kernel is {cur!r}, symbols.json gives {base_cur!r}")
        # No disasm.json for imported symbols: the listing decodes guest memory.
        if not note or "decoded live" not in note:
            problems.append(f"{path.name}: the listing is not decoded live: {note!r}")
        # A counted full-speed boot with the imported symbols: the timeline has start_kernel, panic is found.
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
        page.evaluate("window.oldStepper = MKB.stepper")
        page.set_input_files("#symfile", str(path))
        wait_for(lambda: page.evaluate("MKB.stepper && MKB.stepper !== window.oldStepper && !!MKB.stepper.end"), 60)
        sk_frame = page.evaluate("""() => { const st = MKB.G.stepper; let best = null;
            for (let k = 0; k < st.fr.n; k++) if (st.nodeName(st.fr.node(k)) === 'start_kernel') {
                const incl = (st.fr.end(k) < 0 ? st.steps : st.fr.end(k)) - st.fr.start(k);
                if (!best || incl > best.incl) best = { incl, depth: st.fr.depth(k) }; }
            return best && { ...best, total: st.steps - MKB.runStart() }; }""")
        r[path.name + "_start_kernel"] = sk_frame
        if not sk_frame or sk_frame["incl"] < sk_frame["total"] / 2:
            problems.append(f"{path.name}: start_kernel missing or small in the timeline: {sk_frame}")
        if page.get_attribute("#end_banner", "data-kind") != "panic":
            problems.append(f"{path.name}: panic not detected with imported symbols")
        page.close()
    # A malformed file: a clear error, the previous symbols stay.
    page = browser.new_page(viewport={"width": 390, "height": 900})
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    before = page.evaluate("MKB.symbols.symbols.length")
    bad = pathlib.Path(tempfile.mkdtemp()) / "notes.txt"
    bad.write_text("this is not a symbol map\nat all\nreally\n")
    page.set_input_files("#symfile", str(bad))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "error", 5)
    r["malformed"] = page.inner_text("#sym_status")
    if page.get_attribute("#sym_status", "data-state") != "error" or "previous symbols stay" not in r["malformed"]:
        problems.append("malformed file: no clear error: " + r["malformed"])
    if page.evaluate("MKB.symbols.symbols.length") != before or page.evaluate("!!MKB.symbols.imported"):
        problems.append("malformed file replaced the symbols")
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("the error message makes the page scroll horizontally at 390 px")
    page.screenshot(path=str(shots / "import-malformed-390.png"))
    # Debian's /boot/System.map-<version> placeholder: say where the real map is.
    deb = bad.parent / "System.map-5.10.0-30-686"
    deb.write_text("ffffffffffffffff B The real System.map is in the linux-image-<version>-dbg package\n")
    page.set_input_files("#symfile", str(deb))
    wait_for(lambda: "placeholder" in page.inner_text("#sym_status"), 5)
    r["debian_placeholder"] = page.inner_text("#sym_status")
    if "-dbg" not in r["debian_placeholder"] or page.evaluate("!!MKB.symbols.imported"):
        problems.append("Debian placeholder System.map: no pointer to the -dbg package: " + r["debian_placeholder"])
    page.close()
    return problems, r


def tiny_elf(symtab=True, sections=True, elf_class=1, text_sym=False):
    """An ELF32 i386 file built here: one PT_LOAD (0xc0100000 at 1 MiB, 8
    KiB), sections .text (in it), .outside (allocated, in no PT_LOAD),
    .comment (not allocated), .shstrtab and, with symtab, .symtab and
    .strtab whose symbols are in .comment and .outside, with text_sym
    also tiny_func in .text (0xc0100010, 32 bytes, global)."""
    names = [".text", ".outside", ".comment", ".shstrtab"] + ([".symtab", ".strtab"] if symtab else [])
    shstr = b"\0" + b"".join(n.encode() + b"\0" for n in names)
    off = lambda n: shstr.index(b"\0" + n.encode() + b"\0") + 1
    strtab = b"\0in_comment\0outside\0tiny_func\0"
    syms = bytes(16) + struct.pack("<IIIBBH", 1, 0x10, 4, 0x12, 0, 3) + struct.pack("<IIIBBH", 12, 0xd0000000, 4, 0x11, 0, 2)
    if text_sym:
        syms += struct.pack("<IIIBBH", 20, 0xc0100010, 0x20, 0x12, 0, 1)
    body = 52 + 32
    data = {".text": bytes(0x100), ".outside": bytes(16), ".comment": b"made here\0", ".shstrtab": shstr, ".symtab": syms, ".strtab": strtab}
    at, blob = {}, b""
    for n in names:
        at[n] = body + len(blob)
        blob += data[n]
    shoff = body + len(blob)
    sh = bytes(40)
    for n in names:
        typ, flags, addr, link, ent = {".text": (1, 6, 0xc0100000, 0, 0), ".outside": (1, 3, 0xd0000000, 0, 0), ".comment": (1, 0, 0, 0, 0),
                                       ".shstrtab": (3, 0, 0, 0, 0), ".symtab": (2, 0, 0, names.index(".strtab") + 1 if symtab else 0, 16),
                                       ".strtab": (3, 0, 0, 0, 0)}[n]
        sh += struct.pack("<IIIIIIIIII", off(n), typ, flags, addr, at[n], len(data[n]), link, 0, 4, ent)
    nsec = len(names) + 1
    hdr = b"\x7fELF" + bytes([elf_class, 1, 1]) + bytes(9) + struct.pack(
        "<HHIIIIIHHHHHH", 2, 3, 1, 0xc0100000, 52, shoff if sections else 0, 0, 52, 32, 1, 40, nsec if sections else 0,
        names.index(".shstrtab") + 1)
    ph = struct.pack("<IIIIIIII", 1, at[".text"], 0xc0100000, 0x100000, 0x100, 0x2000, 7, 0x1000)
    return hdr + ph + blob + sh


def import_edges(browser, base, shots):
    """Symbol files the page refuses, each with its reason and the previous
    symbols kept: an x86-64 ELF, an ELF cut after 20 bytes, one without
    section headers, a stripped one (no .symtab), one whose symbols are all
    in a section that is not loaded or in no PT_LOAD (each built here), a
    System.map of one line, an offset that is not hex. System.maps without
    the section markers, placed: _text above 1 MiB (put at 1 MiB), data
    only above 0xc0000000 (PAGE_OFFSET) and below it (identity), an offset
    by hand; each symbol's physical address and size (a weak one too) and
    the sections (from the symbols' extent) as written here; an ELF with an
    offset by hand. A dragover may drop; a bzImage dropped with a
    System.map boots with those symbols; Forget then leaves it with none."""
    problems, r = [], {}
    tmp = pathlib.Path(tempfile.mkdtemp())
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=hello&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    before = page.evaluate("MKB.symbols.symbols.length")
    good = tiny_elf()
    refused = {
        "x86-64.elf": (tiny_elf(elf_class=2), "auto", "not a 32-bit little-endian ELF (only i386 kernels boot here)"),
        "cut.elf": (good[:20], "auto", "truncated ELF file"),
        "nosections.elf": (tiny_elf(sections=False), "auto", "ELF without section headers"),
        "stripped.elf": (tiny_elf(symtab=False), "auto", "this ELF has no .symtab (stripped?)"),
        "unplaced.elf": (good, "auto", "no symbols with an address in it"),
        "one.map": (b"c0100000 T _text\n", "auto", "no symbols found"),
        "badoffset.map": (b"c0100000 T _text\nc0100100 T _etext\n", "zz", 'offset must be "auto" or a hex number'),
    }
    r["refused"] = {}
    for name, (data, offset, why) in refused.items():
        (tmp / name).write_bytes(data)
        page.fill("#sym_offset", offset)
        page.set_input_files("#symfile", str(tmp / name))
        wait_for(lambda: page.inner_text("#sym_status").startswith(f"Could not use {name}:"), 10)
        got = page.inner_text("#sym_status")
        r["refused"][name] = got
        if got != f"Could not use {name}: {why}. The previous symbols stay." or page.evaluate("MKB.symbols.symbols.length") != before:
            problems.append(f"{name}: {got!r}, want the reason {why!r} and the previous {before} symbols")
    # Placed System.maps: (file, its lines, offset field, the offset, how).
    maps = [
        ("low.map", [(0x200000, "T", "foo"), (0x200080, "W", "weak"), (0x200100, "t", "bar"), (0x300000, "D", "baz"), (0x300010, "b", "qux")], "auto",
         0x100000, "auto: _text at 0x100000, where v86 puts the 32-bit kernel"),
        ("high-data.map", [(0xc0300000, "D", "a"), (0xc0300010, "D", "b")], "auto", 0xC0000000, "auto: vaddr - 0xC0000000 (i386 PAGE_OFFSET)"),
        ("low-data.map", [(0x1000, "D", "a"), (0x2000, "D", "b")], "auto", 0, "auto: identity"),
        ("byhand.map", [(0x200000, "T", "foo"), (0x200100, "t", "bar"), (0x300000, "D", "baz"), (0x300010, "b", "qux")], "0x1000",
         0x1000, "set by hand"),
    ]
    # A weak symbol (W) has the class of the section it is in; it makes no section.
    cls = {"T": "text", "t": "text", "W": "text", "D": "data", "d": "data", "B": "bss", "b": "bss"}
    r["placed"] = {}
    for name, rows, offset, off, how in maps:
        (tmp / name).write_text("".join(f"{a:08x} {t} {n}\n" for a, t, n in rows))
        page.fill("#sym_offset", offset)
        page.evaluate("MKB.startPaused = true")
        page.set_input_files("#symfile", str(tmp / name))
        wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported" and page.inner_text("#sym_status").startswith(name), 10)
        got = page.evaluate("[MKB.symbols.symbols.map((s) => [s.name, s.paddr, s.size]), MKB.symbols.sections.map((s) => [s.name, s.class, s.vaddr, s.size, s.paddr])]")
        status = page.inner_text("#sym_status")
        r["placed"][name] = status[:120]
        # Sizes: up to the next symbol, the last one to its section's end; sections: each class's extent.
        ext = {}
        for a, t, n in (x for x in rows if x[1] != "W"):
            lo, hi = ext.get(cls[t], (a, a + 1))
            ext[cls[t]] = (min(lo, a), max(hi, a + 1))
        secs = [["." + c, c, lo, hi - lo, (lo - off) & 0xffffffff] for c, (lo, hi) in ext.items()]
        syms = []
        for i, (a, t, n) in enumerate(rows):
            sec = ext[cls[t]]
            end = min(rows[i + 1][0] if i + 1 < len(rows) else sec[1], sec[1])
            syms.append([n, (a - off) & 0xffffffff, end - a if end > a else 0])
        want_status = f"{name} (System.map): {len(rows)} symbols, offset 0x{off:x}, {how}."
        if not status.startswith(want_status) or sorted(got[0]) != sorted(syms) or sorted(got[1]) != sorted(secs):
            problems.append(f"{name}: {status[:160]!r}, symbols {got[0]}, sections {got[1]}; want {want_status!r}, {syms}, {secs}")
    # An ELF with an offset by hand: its symbol placed by that, not by the PT_LOAD.
    (tmp / "tiny.elf").write_bytes(tiny_elf(text_sym=True))
    page.fill("#sym_offset", "0xc0000000")
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(tmp / "tiny.elf"))
    wait_for(lambda: page.inner_text("#sym_status").startswith("tiny.elf"), 10)
    r["tiny.elf"] = [page.inner_text("#sym_status")[:90], page.evaluate("MKB.symbols.symbols.map((s) => [s.name, s.paddr, s.size, s.type])")]
    if not r["tiny.elf"][0].startswith("tiny.elf (ELF): 1 symbols, offset 0xc0000000, set by hand.") or \
            r["tiny.elf"][1] != [["tiny_func", 0x100010, 0x20, "T"]]:
        problems.append(f"tiny.elf with the offset 0xc0000000: {r['tiny.elf']}")
    page.fill("#sym_offset", "auto")
    # Files dragged over the page may be dropped (the browser would open them otherwise).
    r["dragover_cancelled"] = page.evaluate("!document.body.dispatchEvent(new DragEvent('dragover', { bubbles: true, cancelable: true }))")
    if not r["dragover_cancelled"]:
        problems.append("a dragover is not cancelled: files cannot be dropped on the page")
    # A bzImage (hello's, from disk) dropped together with a System.map of it.
    hello_syms = json.loads((SITE / kernel_entries()["hello"]["symbols"]).read_text())["symbols"]
    smap = "".join(f"{x['vaddr']:08x} {x['type']} {x['name']}\n" for x in sorted(hello_syms, key=lambda x: x["vaddr"]))
    page.evaluate("""async (smap) => { const b = await (await fetch('kernels/hello/bzImage')).arrayBuffer();
      const dt = new DataTransfer(); dt.items.add(new File([b], 'mine.bzImage')); dt.items.add(new File([smap], 'mine.map'));
      document.body.dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); }""", smap)
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == "mine.bzImage" and page.get_attribute("#sym_status", "data-state") == "imported", 20)
    r["dropped"] = [page.evaluate("MKB.image && MKB.image.name"), page.inner_text("#sym_status")[:100]]
    if r["dropped"][0] != "mine.bzImage" or not r["dropped"][1].startswith(f"mine.map (System.map): {len(hello_syms)} symbols"):
        problems.append(f"a bzImage dropped with its System.map: {r['dropped']}")
    booted = wait_for(lambda: gate_on_screen(page), 60)
    show_tab(page, "machine")
    page.click("#sym_forget")
    wait_for(lambda: page.evaluate("MKB.symbols === null") and gate_on_screen(page), 60)
    r["forgotten"] = [booted, page.evaluate("MKB.symbols === null"), page.inner_text("#sym_status"), page.is_hidden("#sym_forget"), gate_on_screen(page)]
    if r["forgotten"] != [True, True, "", True, True]:
        problems.append(f"Forget on a bzImage from disk: [booted, no symbols, status, Forget hidden, booted again] {r['forgotten']}")
    page.screenshot(path=str(shots / "import-edges-1280.png"))
    # The Debugger at its halt (32-bit code): decoded live, no names for the memory operands (no symbols, no DWARF).
    show_tab(page, "debugger")
    wait_for(lambda: page.locator("#context .ins").count() > 0, 10)
    r["no_names"] = [page.locator("#context .ins").count(), page.get_attribute("#context", "data-ops-named"),
                     page.locator("#context .opn").count()]
    if not r["no_names"][0] or r["no_names"][1:] != ["0", 0]:
        problems.append(f"bzImage from disk without symbols: instructions at EIP, operands named {r['no_names']}")
    page.close()
    shutil.rmtree(tmp, ignore_errors=True)
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r
