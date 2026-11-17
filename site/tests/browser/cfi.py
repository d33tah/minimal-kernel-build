"""Browser test suite cfi (test_browser.py --suites)."""

import base64
import json
import os
import pathlib
import re
import subprocess
import struct
import tempfile

from tests.dwarf.cfi import dwarfdump_cfi
from tests.browser.common import report, wait_for


# ---- CFI parsing and unwinding on a hand-written fixture ---------------------
# The kernels' CFI is .debug_frame with the rules compilers emit; this
# fixture (llvm-mc from assembly, linked by ld.lld at 0xc1000000) has the
# rest: .eh_frame (CIE "zR", pc-relative FDE pointers) and .debug_frame,
# DW_CFA_offset_extended(_sf), restore_extended, undefined, same_value,
# register, val_offset(_sf), def_cfa_sf, def_cfa_offset_sf,
# val_expression, def_cfa_expression, advance_loc4, set_loc (.debug_frame
# only: in .eh_frame its operand is in the FDE pointer encoding) and a CFA
# expression of DW_OP const2s, const4s, const1u, drop, neg, abs, mod, shr,
# xor (it evaluates to ESP+12). f2's return address is undefined (the
# outermost frame).
CFI_FIXTURE = """%(sections)s
	.text
	.globl f1
	.type f1,@function
f1:
	.cfi_startproc
	pushl %%ebp
	.cfi_def_cfa_offset 8
	.cfi_offset %%ebp, -8
	movl %%esp, %%ebp
	.cfi_def_cfa_register %%ebp
	pushl %%ebx
	.cfi_offset %%ebx, -12
	.cfi_register %%esi, %%edi
	.cfi_undefined %%eax
	.cfi_same_value %%ecx
	nop
	.cfi_remember_state
	.cfi_restore %%ebx
	.cfi_escape 0x11, 0x07, 0x7d
	.cfi_escape 0x14, 0x06, 0x02
	.cfi_escape 0x15, 0x02, 0x7e
	nop
	.cfi_restore_state
	.cfi_escape 0x05, 0x06, 0x04
	.cfi_escape 0x06, 0x06
	.cfi_escape 0x12, 0x04, 0x10
	nop
	.cfi_escape 0x13, 0x08
	nop
	.cfi_escape 0x16, 0x03, 0x02, 0x75, 0x00
	.cfi_escape 0x0f, 0x02, 0x74, 0x08
	nop
	.cfi_escape 0x04, 0x02, 0x00, 0x00, 0x00
	.cfi_escape 0x0f, 0x1c, 0x74, 0x00, 0x0b, 0xf8, 0xff, 0x22, 0x0d, 0x10, 0x00, 0x00, 0x00, 0x22, 0x08, 0x07, 0x13, 0x35, 0x1f, 0x19, 0x33, 0x1d, 0x31, 0x25, 0x31, 0x27, 0x22, 0x08, 0x04, 0x22
%(setloc)s	nop
	nop
	nop
	ret
	.cfi_endproc
	.size f1, .-f1
	.globl f2
	.type f2,@function
f2:
	.cfi_startproc
	.cfi_undefined %%eip
	nop
	ret
	.cfi_endproc
	.size f2, .-f2
"""
CFI_BASE, CFI_OFFSET = 0xc1000000, 0xc0000000


def cfi_fixture(tmp, kind):
    """The fixture as an ELF with .eh_frame or .debug_frame (and DWARF for
    the import: llvm-mc -g)."""
    s = CFI_FIXTURE % {"sections": "\t.cfi_sections .debug_frame" if kind == "debug_frame" else "",
                       # DW_CFA_set_loc 0xc100000c, then DW_CFA_def_cfa ESP+20.
                       "setloc": "\t.cfi_escape 0x01, 0x0c, 0x00, 0x00, 0xc1, 0x0c, 0x04, 0x14\n" if kind == "debug_frame" else ""}
    src, obj, elf = (tmp / f"cfi-{kind}.{x}" for x in ("s", "o", "elf"))
    src.write_text(s)
    subprocess.run(["llvm-mc", "-g", "-triple=i386-linux-gnu", "-filetype=obj", str(src), "-o", str(obj)], check=True)
    subprocess.run(["ld.lld", "-m", "elf_i386", "-e", "f1", f"-Ttext=0x{CFI_BASE:x}", str(obj), "-o", str(elf)], check=True)
    return elf


# In the page: the ELF through dwarfworker.js (as "From disk / Symbols"
# opens a vmlinux), its CFI table back, then unwind.js on it.
CFI_TABLE_JS = """async ([b64, offset]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search, { type: 'module' });
    const ask = (m) => new Promise((res) => { w.onmessage = (e) => res(e.data); w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'cfi.elf'), offset });
    const c = o.ok ? await ask({ type: 'cfi' }) : null;
    w.terminate();
    window.__cfi = c && c.table;
    return { open: o.ok ? 'ok' : o.why, source: c && c.source, why: c && c.why };
}"""

CFI_ROWS_JS = """(pas) => pas.map((pa) => { const r = MKB.cfiRow(window.__cfi, pa); return r === null ? null : r.why ? 'why: ' + r.why : MKB.cfiRowText(r); })"""

# unwind() from regs over a memory of { address: value }; the code is the
# fixture's; entry: [name, start, end] of code that is entry code (it
# called the frame inside it with pt_regs).
CFI_UNWIND_JS = """([regs, mem, lo, hi, offset, entry]) => {
    const sym = (pa) => entry && pa >= entry[1] - offset && pa < entry[2] - offset ? [entry[0], entry[1] - offset] : null;
    const env = { rowAt: (pa) => MKB.cfiRow(window.__cfi, pa), read32: (va) => mem[va >>> 0],
                  toPhys: (va) => (va >= lo && va < hi ? va - offset : null), symAt: sym, isEntry: (n) => !!entry && n === entry[0] };
    const u = MKB.unwind(regs, env);
    return { state: u.state, why: u.why, frames: u.frames.map((f) => ({ pc: f.pc, method: f.method, cfa: f.cfa, regs: f.regs })) };
}"""


def cfi_unwind_checks(page, hi, problems, r):
    """unwind() through the fixture's rows from registers and a made-up
    stack: the return address and saved registers are where the row says,
    each kind of rule gives the caller's value, and the walk ends or stops
    with its reason."""
    B = CFI_BASE
    ra_f2 = B + 0xe  # the return lands after f2's first byte: f2's row, EIP undefined: the outermost frame

    def walk(eip, regs, mem):
        return page.evaluate(CFI_UNWIND_JS, [{"eip": eip, **regs}, {str(k): v for k, v in mem.items()}, B, hi, CFI_OFFSET])

    def caller(u):
        return {int(k): v for k, v in u["frames"][1]["regs"].items()} if len(u["frames"]) > 1 else {}
    cases = []
    # Row 0xc1000001: CFA=ESP+8, EBP=[CFA-8], EIP=[CFA-4].
    u = walk(B + 1, {"esp": 0x1000, "ebp": 0x1}, {0x1000: 0x2222, 0x1004: ra_f2})
    cases.append(("push ebp", u["state"] == "ok" and len(u["frames"]) == 2 and u["frames"][1]["pc"] == ra_f2 and
                  caller(u).get(5) == 0x2222 and caller(u).get(4) == 0x1008 and u["frames"][1]["method"] == "cfi", u))
    u = walk(B + 1, {"esp": 0x1000}, {0x1000: 0x2222})
    cases.append(("return address unreadable", u["state"] == "stopped" and u["why"] == "the return address at CFA-4 cannot be read", u))
    # Row 0xc1000003: CFA=EBP+8 with EBP unknown.
    u = walk(B + 3, {"esp": 0x1000}, {})
    cases.append(("CFA register unknown", u["why"] == "CFA = EBP+8, and EBP is not known here", u))
    # Row 0xc1000004: ESI=EDI (register rule), EBX=[CFA-12], EAX undefined, ECX same.
    u = walk(B + 4, {"esp": 0x2ff0, "ebp": 0x3000, "edi": 0x99, "ecx": 0x11, "eax": 0x55},
             {0x3000: 0x5555, 0x3004: ra_f2, 0x2ffc: 0x3333})
    c = caller(u)
    cases.append(("register, same, undefined", u["state"] == "ok" and c.get(6) == 0x99 and c.get(1) == 0x11 and 0 not in c and
                  c.get(3) == 0x3333 and c.get(5) == 0x5555, u))
    # Row 0xc1000005: EDX=CFA+8 and ESI=CFA-8 (val_offset), EDI=[CFA+12].
    u = walk(B + 5, {"esp": 0x2ff0, "ebp": 0x3000}, {0x3000: 0x5555, 0x3004: ra_f2, 0x3014: 0x7777})
    c = caller(u)
    cases.append(("val_offset", u["state"] == "ok" and c.get(2) == 0x3010 and c.get(6) == 0x3000 and c.get(7) == 0x7777, u))
    # Row 0xc1000009: CFA=DW_OP_breg4 ESP+8, EBX=DW_OP_breg5 EBP+0 (val_expression).
    u = walk(B + 9, {"esp": 0x4000, "ebp": 0x4444}, {0x4000: 0x6000, 0x4004: ra_f2})
    c = caller(u)
    cases.append(("expressions", u["state"] == "ok" and u["frames"][0]["cfa"] == 0x4008 and c.get(3) == 0x4444 and c.get(5) == 0x6000, u))
    u = walk(B + 9, {"ebp": 0x4444}, {})
    cases.append(("CFA expression without ESP", u["state"] == "stopped" and (u["why"] or "").startswith("CFA (DW_OP_breg4 ESP+8): "), u))
    # Row 0xc100000b: the long CFA expression, ESP+12.
    u = walk(B + 0xb, {"esp": 0x5000, "ebp": 0x1}, {0x5000: 0x1, 0x5004: 0x6000, 0x5008: ra_f2})
    cases.append(("CFA expression ops", u["state"] == "ok" and u["frames"][0]["cfa"] == 0x500c, u))
    # The caller's row (0xc1000006: CFA=ESP-64) puts its CFA below this frame's: a corrupt stack.
    u = walk(B + 1, {"esp": 0x1000}, {0x1000: 0x2222, 0x1004: B + 7, 0xfc4: ra_f2})
    cases.append(("stack does not grow", u["state"] == "stopped" and (u["why"] or "").startswith("the stack does not grow back up"), u))
    u = walk(0x1234, {"esp": 0x1000}, {})
    cases.append(("not code", u["why"] == "0x1234 is not code of this kernel", u))
    # f2 as entry code: the pt_regs at the CFA of f1's frame (17 words:
    # EBX ... EIP at 12, CS at 13, EFLAGS at 14).
    entry = ["asm_fixture", B + 0xd, B + 0xf]

    def walk_entry(mem):
        return page.evaluate(CFI_UNWIND_JS, [{"eip": B + 1, "esp": 0x1000}, {str(k): v for k, v in mem.items()}, B, hi, CFI_OFFSET, entry])
    u = walk_entry({0x1000: 0x2222, 0x1004: B + 0xe})
    cases.append(("pt_regs unreadable", u["why"] == "asm_fixture: its saved registers at 0x1008 cannot be read", u))
    regs = {0x1008 + 4 * k: 0 for k in range(17)}
    u = walk_entry({0x1000: 0x2222, 0x1004: B + 0xe, **regs, 0x1008 + 4 * 13: 0x1234, 0x1008 + 4 * 14: 0x202})
    cases.append(("no pt_regs", u["why"] == "asm_fixture: no saved registers (pt_regs) at 0x1008 (CS 0x1234, EFLAGS 0x202)", u))
    r["unwind"] = {name: ok for name, ok, _ in cases}
    for name, ok, u in cases:
        if not ok:
            problems.append(f"unwind on the CFI fixture, {name}: {json.dumps(u)[:400]}")


# CFI the assembler does not write: .eh_frame built here byte by byte
# into a copy of the fixture (the section header points at bytes appended
# to the file): CIE version 1 with "zPLR" (a personality pointer, an LSDA
# in each FDE), FDE pointers as uleb128, sleb128, pc-relative sdata2 and
# udata2 (an FDE of no code here), DW_CFA_expression and DW_CFA_restore
# of a CIE rule (rows against llvm-dwarfdump's), and
# DW_CFA_GNU_negative_offset_extended (against GNU readelf's, which
# llvm-dwarfdump does not know); broken copies give their error.
def cfi_uleb(v):
    out = bytearray()
    while True:
        b, v = v & 0x7f, v >> 7
        out.append(b | (0x80 if v else 0))
        if not v:
            return bytes(out)


def cfi_sleb(v):
    out = bytearray()
    while True:
        b, v = v & 0x7f, v >> 7
        if (v == 0 and not b & 0x40) or (v == -1 and b & 0x40):
            return bytes(out + bytes([b]))
        out.append(b | 0x80)


def cfi_entry(body):
    """A length-prefixed CIE/FDE padded with DW_CFA_nop to 4 bytes."""
    body += bytes(-(len(body) + 4) % 4)
    return struct.pack("<I", len(body)) + body


def cfi_cie(eh, ver, aug, augdata, init):
    body = struct.pack("<I", 0 if eh else 0xffffffff) + bytes([ver]) + aug.encode() + b"\0" + (b"\4\0" if ver >= 4 else b"")
    body += cfi_uleb(1) + cfi_sleb(-4) + (b"\x08" if ver == 1 else cfi_uleb(8))
    if aug.startswith("z"):
        body += cfi_uleb(len(augdata)) + augdata
    return cfi_entry(body + init)


def cfi_eh_frame(sec_addr, f2=None):
    """The crafted .eh_frame at virtual sec_addr: [(lo, hi)] of its FDEs with code."""
    out = bytearray()

    def fde(cie_at, pc, rng, aug, prog):
        here = len(out)
        out.extend(cfi_entry(struct.pack("<I", here + 4 - cie_at) + pc + rng + (cfi_uleb(len(aug)) + aug if aug is not None else b"") + prog))
    init = bytes([0x0c, 4, 4, 0x88, 1])  # CFA=ESP+4, EIP=[CFA-4]
    a = len(out)
    out.extend(cfi_cie(True, 1, "zPLR", bytes([0x00]) + struct.pack("<I", 0) + bytes([0x00, 0x01]), init))
    # f1 [+0, +6): uleb128 pointers; CFA+8, EBP, EBX at CFA+12 (negative
    # offset extended), EIP restored to the CIE's rule, EDI by expression.
    fde(a, cfi_uleb(CFI_BASE), cfi_uleb(6), struct.pack("<I", 0),
        bytes([0x41, 0x0e, 8, 0x85, 2, 0x41, 0x83, 3, 0x88, 3, 0x41, 0xc8, 0x41, 0x10, 7, 2, 0x75, 0]))
    c = len(out)
    out.extend(cfi_cie(True, 3, "zR", bytes([0x09]), init))
    fde(c, cfi_sleb(CFI_BASE + 6), cfi_sleb(4), b"", bytes([0x42, 0x0e, 12]))
    d = len(out)
    out.extend(cfi_cie(True, 1, "zR", bytes([0x1a]), init))
    here = len(out) + 8
    fde(d, struct.pack("<h", CFI_BASE + 10 - (sec_addr + here)), struct.pack("<h", 3), b"", bytes([0x41, 0x0e, 16]))
    e = len(out)
    out.extend(cfi_cie(True, 1, "zR", bytes([0x02]), init))
    fde(e, struct.pack("<H", 0x1234), struct.pack("<H", 2), b"", b"")
    out.extend(bytes(4))
    return bytes(out), [(CFI_BASE, CFI_BASE + 6), (CFI_BASE + 6, CFI_BASE + 10), (CFI_BASE + 10, CFI_BASE + 13)]


def cfi_with_section(b, name, content=None, rename=None):
    """ELF bytes with section name's contents replaced by content (appended
    to the file) or the section renamed."""
    x = bytearray(b)
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x2e)
    strs = struct.unpack_from("<I", b, shoff + shstrndx * shentsize + 16)[0]
    for k in range(shnum):
        h = shoff + k * shentsize
        n = strs + struct.unpack_from("<I", b, h)[0]
        if b[n:b.index(0, n)] != name.encode():
            continue
        if rename:
            x[n:n + len(rename)] = rename.encode()
        if content is not None:
            struct.pack_into("<II", x, h + 16, len(x), len(content))
            x += content
        return bytes(x), struct.unpack_from("<I", b, h + 12)[0]
    raise KeyError(name)


CFI_ERR_JS = """async ([b64, offset]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search, { type: 'module' });
    const ask = (m) => new Promise((res) => { w.onmessage = (e) => res(e.data); w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'cfi.elf'), offset });
    const c = o.ok ? await ask({ type: 'cfi' }) : null;
    w.terminate();
    window.__cfi = c && c.table;
    return c ? c.why || c.message || c.source : o.why;
}"""


def cfi_crafted_checks(page, tmp, problems, r):
    base = cfi_fixture(tmp, "eh_frame").read_bytes()
    _, eh_addr = cfi_with_section(base, ".eh_frame", b"")
    eh, fdes_want = cfi_eh_frame(eh_addr)
    hi = CFI_BASE + 0xf

    def rows_check(kind, data, flag):
        elf = tmp / f"crafted-{kind}.elf"
        elf.write_bytes(data)
        src = page.evaluate(CFI_ERR_JS, [base64.b64encode(data).decode(), CFI_OFFSET])
        fdes = dwarfdump_cfi(str(elf))
        want = {}
        for va in range(CFI_BASE, hi + 1):
            f = next((f for f in fdes if f[0] <= va < f[1]), None)
            prev = [t for a, t in (f[2] if f else []) if a <= va]
            want[va] = prev[-1] if prev else None
        have = dict(zip(want, page.evaluate(CFI_ROWS_JS, [va - CFI_OFFSET for va in want])))
        bad = [(hex(va), want[va], have[va]) for va in want if want[va] != have[va]]
        r[f"crafted {kind}"] = f"{src}: {len(want) - len(bad)}/{len(want)} addresses, {len(fdes)} FDEs"
        if src != flag or bad or not fdes:
            problems.append(f"crafted {kind}: {src}, rows differ from llvm-dwarfdump: {bad[:4]} ({fdes})")
        return fdes
    fdes = rows_check("eh_frame", cfi_with_section(base, ".eh_frame", eh)[0], ".eh_frame")
    if sorted((f[0], f[1]) for f in fdes if f[0] >= CFI_BASE) != fdes_want:
        problems.append(f"crafted .eh_frame: llvm-dwarfdump's FDEs {[(hex(f[0]), hex(f[1])) for f in fdes]}, built {fdes_want}")
    # At +4 (CFA=ESP+8): EDI by its DW_CFA_expression rule ([EBP+0]), EBX
    # at CFA-12, the return address at CFA-4.
    u = page.evaluate(CFI_UNWIND_JS, [{"eip": CFI_BASE + 4, "esp": 0x1000, "ebp": 0x2000},
                                      {"4100": CFI_BASE + 0xb, "8192": 0x7777, "4092": 0x3333},
                                      CFI_BASE, hi, CFI_OFFSET])
    regs = {int(k): v for k, v in u["frames"][1]["regs"].items()} if len(u["frames"]) > 1 else {}
    r["crafted unwind"] = u
    if u["frames"][0]["cfa"] != 0x1008 or regs.get(7) != 0x7777 or regs.get(3) != 0x3333:
        problems.append(f"crafted .eh_frame: unwind from +4: {u}")
    info = page.evaluate("(pa) => MKB.cfiFrameText(MKB.cfiFrameInfo(window.__cfi, pa))", hi + 0x100 - CFI_OFFSET)
    if info != "no call frame information":
        problems.append(f"crafted .eh_frame: the frame of code without an FDE: {info!r}")
    init = bytes([0x0c, 4, 4, 0x88, 1])
    # DW_CFA_GNU_negative_offset_extended EBX 3: EBX at CFA+12 from +1.
    cie = cfi_cie(True, 1, "zR", bytes([0x1b]), init)
    neg = cfi_with_section(base, ".eh_frame", cie + cfi_entry(struct.pack("<I", len(cie) + 4) +
                           struct.pack("<ii", CFI_BASE - eh_addr - len(cie) - 8, 13) + b"\0" + bytes([0x41, 0x2f, 3, 3])) + bytes(4))[0]
    (tmp / "crafted-neg.elf").write_bytes(neg)
    page.evaluate(CFI_ERR_JS, [base64.b64encode(neg).decode(), CFI_OFFSET])
    have = page.evaluate(CFI_ROWS_JS, [0x1000001])[0]
    gnu = subprocess.run(["readelf", "-wF", str(tmp / "crafted-neg.elf")], capture_output=True, text=True).stdout
    m = re.search(r"^\s+LOC\s+CFA\s+ebx\s+ra\s*\n(?:.*\n)*?c1000001 esp\+4\s+(\S+)\s+c-4", gnu, re.M)
    r["crafted negative offset"] = [have, m and m.group(1)]
    if not m or m.group(1) != "c+12" or have != "CFA=ESP+4: EBX=[CFA+12], EIP=[CFA-4]":
        problems.append(f"DW_CFA_GNU_negative_offset_extended: the page {have!r}, readelf -wF {gnu[-400:]!r}")
    # CFA expressions (DW_CFA_def_cfa_expression), one FDE per byte of f1:
    # a 4-byte DW_OP_const_type added to ESP, then expressions that give
    # no CFA, each with its reason.
    exprs = [(bytes([0x74, 0, 0xa4, 0, 4, 8, 0, 0, 0, 0x22]), None),
             (bytes([0x22]), "DWARF expression stack underflow"), (bytes([0x50]), "in register EAX"),
             (bytes([0x90, 0x21]), "in register ?"), (bytes([0xfa, 0, 0, 0, 0]), "optimized out (DW_OP_GNU_parameter_ref: the value its caller passed, not kept)"),
             (bytes([0xe0]), "DWARF expression op 0xe0 is not supported"), (b"", "empty DWARF expression"),
             (bytes([0xa4, 0, 8]) + bytes(8), "a 8-byte typed constant is not supported")]
    content = bytearray(cie)
    # Two more with a constant CFA each, [+8, +10) 0x10008 and [+10, +11)
    # 0x4008 (another 8 KiB stack): +9 returns to +11, which returns to +9.
    # At +11: CFA = [ESP] and EDI = [[EBP]] (DW_OP_deref in both).
    for k, n, e in [(k, 1, e) for k, (e, _) in enumerate(exprs)] + [(8, 2, bytes([0x0c]) + struct.pack("<I", 0x10008)),
                                                                    (10, 1, bytes([0x0c]) + struct.pack("<I", 0x4008)),
                                                                    (11, 1, bytes([0x74, 0, 0x06]))]:
        at = len(content)
        content += cfi_entry(struct.pack("<I", at + 4) + struct.pack("<ii", CFI_BASE + k - eh_addr - at - 8, n) + b"\0" +
                             bytes([0x0f]) + cfi_uleb(len(e)) + e + (bytes([0x10, 7, 3, 0x75, 0, 0x06]) if k == 11 else b""))
    page.evaluate(CFI_ERR_JS, [base64.b64encode(cfi_with_section(base, ".eh_frame", bytes(content) + bytes(4))[0]).decode(), CFI_OFFSET])
    for k, (e, why) in enumerate(exprs):
        u = page.evaluate(CFI_UNWIND_JS, [{"eip": CFI_BASE + k, "esp": 0x1000}, {"4100": CFI_BASE + 0x20}, CFI_BASE, hi, CFI_OFFSET])
        r[f"crafted CFA expression {k}"] = [u["state"], u["why"], u["frames"][0]["cfa"]]
        if (why is None and u["frames"][0]["cfa"] != 0x1008) or (why and not (u["why"] or "").endswith("): " + why)):
            problems.append(f"crafted CFA expression {e.hex()}: {u}, want {why or 'CFA ESP+8'}")
    u = page.evaluate(CFI_UNWIND_JS, [{"eip": CFI_BASE + 9, "esp": 0x1000}, {"65540": CFI_BASE + 11, "16388": CFI_BASE + 9}, CFI_BASE, hi, CFI_OFFSET])
    r["crafted loop"] = u["why"]
    d = page.evaluate(CFI_UNWIND_JS, [{"eip": CFI_BASE + 11, "esp": 0x1000, "ebp": 0x3000},
                                      {"4096": 0x2008, "8196": CFI_BASE + 14, "12288": 0x3100, "12544": 0x7777}, CFI_BASE, hi, CFI_OFFSET])
    r["crafted deref"] = d
    if d["frames"][0]["cfa"] != 0x2008 or len(d["frames"]) < 2 or d["frames"][1]["regs"].get("7") != 0x7777:
        problems.append(f"crafted CFA expressions, DW_OP_deref: {d}")
    if u["why"] != "a loop: 0xc1000009 with CFA 0x10008 again" or len(u["frames"]) != 3:
        problems.append(f"crafted CFA expressions, a loop: {u}")
    # Broken copies.

    def one(cie):
        """cie and an FDE of f1 (pc-relative sdata4, no augmentation data)."""
        return cie + cfi_entry(struct.pack("<I", len(cie) + 4) + struct.pack("<ii", CFI_BASE - eh_addr - len(cie) - 8, 13) + b"\0") + bytes(4)
    broken = [
        ("augmentation", one(cfi_cie(True, 1, "eh", b"", init)), 'unsupported CIE augmentation "eh"'),
        ("encoding", one(cfi_cie(True, 1, "zR", bytes([0x04]), init)), "unsupported .eh_frame pointer encoding 0x4"),
        ("application", one(cfi_cie(True, 1, "zR", bytes([0x33]), init)), "unsupported .eh_frame pointer application 0x33"),
        ("64-bit", struct.pack("<I", 0xffffffff) + bytes(12), "64-bit DWARF CFI is not supported"),
    ]
    for what, content, want in broken:
        have = page.evaluate(CFI_ERR_JS, [base64.b64encode(cfi_with_section(base, ".eh_frame", content)[0]).decode(), CFI_OFFSET])
        r[f"crafted {what}"] = have
        if have != want:
            problems.append(f"crafted .eh_frame, {what}: {have!r}, want {want!r}")
    # An opcode DWARF does not define: the rows of its FDE say so.
    cie = cfi_cie(True, 1, "zR", bytes([0x1b]), init)
    content = cie + cfi_entry(struct.pack("<I", len(cie) + 4) + struct.pack("<ii", CFI_BASE - eh_addr - len(cie) - 8, 13) + b"\0" + bytes([0x3f])) + bytes(4)
    page.evaluate(CFI_ERR_JS, [base64.b64encode(cfi_with_section(base, ".eh_frame", content)[0]).decode(), CFI_OFFSET])
    got = page.evaluate(CFI_ROWS_JS, [0x1000000, 0x1000002]) + [page.evaluate("(pa) => MKB.cfiFrameText(MKB.cfiFrameInfo(window.__cfi, pa))", 0x1000002)]
    want = ["why: unknown DW_CFA opcode 0x3f"] * 2 + ["call frame information unreadable: unknown DW_CFA opcode 0x3f"]
    r["crafted unknown opcode"] = got
    if got != want:
        problems.append(f"crafted .eh_frame, an unknown opcode: {got}, want {want}")
    have = page.evaluate(CFI_ERR_JS, [base64.b64encode(cfi_with_section(base, ".eh_frame", rename=".eh_xxxxx")[0]).decode(), CFI_OFFSET])
    if have != "this vmlinux has no call frame information (.debug_frame or .eh_frame)":
        problems.append(f"an ELF without CFI: {have!r}")


def cfi_case(browser, base, shots):
    """The fixture's CFI, .eh_frame and .debug_frame, opened in the page
    like an imported vmlinux: every row equals llvm-dwarfdump's (the long
    CFA expression's row: its value, as dwExprText abbreviates the rarer
    ops), no row outside the FDEs, the frame summary of f1, then unwind()."""
    problems, r = [], {}
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1")
    wait_for(lambda: page.evaluate("typeof window.MKB?.cfiRow === 'function' && typeof window.MKB?.unwind === 'function'"), 30)
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        for kind in ("eh_frame", "debug_frame"):
            elf = cfi_fixture(pathlib.Path(tmp), kind)
            fdes = dwarfdump_cfi(str(elf))
            got = page.evaluate(CFI_TABLE_JS, [base64.b64encode(elf.read_bytes()).decode(), CFI_OFFSET])
            r[kind] = got
            if got["open"] != "ok" or got["source"] != "." + kind:
                problems.append(f"{kind}: the fixture did not open with its CFI: {got}")
                continue
            hi = max(f[1] for f in fdes)
            rows = [(va, text) for f in fdes for va, text in f[2]]
            want = {}
            for va in range(CFI_BASE, hi + 2):
                f = next((f for f in fdes if f[0] <= va < f[1]), None)
                prev = [t for a, t in (f[2] if f else []) if a <= va]
                want[va] = prev[-1] if prev else None
            have = dict(zip(want, page.evaluate(CFI_ROWS_JS, [va - CFI_OFFSET for va in want])))
            bad = [(hex(va), want[va], have[va]) for va in want
                   if want[va] != have[va] and not (want[va] and "DW_OP_const2s" in want[va])]
            r[kind + "_rows"] = f"{len(want) - len(bad)}/{len(want)} addresses, {len(rows)} rows"
            if bad or len(rows) < 10:
                problems.append(f"{kind}: rows differ from llvm-dwarfdump: {bad[:4]} ({len(rows)} rows)")
            if kind == "debug_frame" and "CFA=ESP+20" not in (want.get(CFI_BASE + 0xc) or ""):
                problems.append(f"debug_frame: llvm-dwarfdump has no DW_CFA_set_loc row at 0xc100000c: {want.get(CFI_BASE + 0xc)}")
            info = page.evaluate("(pa) => MKB.cfiFrameText(MKB.cfiFrameInfo(window.__cfi, pa))", CFI_BASE + 4 - CFI_OFFSET)
            r[kind + "_frame"] = info
            # .debug_frame's last row (DW_CFA_set_loc) is CFA=ESP+20.
            want_info = (f"frame pointer: CFA = EBP+8 from +0x3 (before that at most ESP+{20 if kind == 'debug_frame' else 8}); the call frame information does not "
                         "say how far below EBP the frame goes; saves EDI at CFA+12, EBP at CFA-8, EBX at CFA-12; a CFA expression somewhere")
            if info != want_info:
                problems.append(f"{kind}: f1's frame summary {info!r}, want {want_info!r}")
            if kind == "eh_frame":
                cfi_unwind_checks(page, hi, problems, r)
        cfi_crafted_checks(page, pathlib.Path(tmp), problems, r)
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_cfi(ctx):
    return report("cfi", *cfi_case(ctx.browser, ctx.base, ctx.shots))
