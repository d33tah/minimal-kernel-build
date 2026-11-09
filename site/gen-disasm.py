#!/usr/bin/env python3
"""Write disasm.json for the page's memory lab from a kernel ELF.

  site/gen-disasm.py ELF OUT.json

Runs `llvm-objdump -d` (override with $OBJDUMP) over every executable
section and maps each instruction to its physical address through the
PT_LOAD segments, like gen-symbols.py does for symbols.

Output:
  {"insns": [[paddr, "hex bytes", "mnemonic operands"], ...],
   "labels": [[paddr, "symbol"], ...],
   "taken": [[paddr, "data" | "code" | "extable" | "array"], ...]}

"taken" lists the instructions whose address the kernel holds somewhere,
so an indirect jump or call, or the exception fixup, can reach them
without a direct path: an aligned 32-bit word in a data section (function
pointers, switch jump tables, the IDT's tables), 32 bits inside another
instruction's bytes (an immediate or displacement: movl $fn, pushl $fn),
or a fixup in __ex_table (relative entries: insn, fixup, data).
An array of entry stubs is reached as its start + n * stride
(idt_setup_early_handler: early_idt_handler_array + i * 9): inside code
whose start is taken, the instructions after a jmp or ret that are not
padding (nop, int3, zero bytes) are "array" when there are at least 3 and
their offsets from the start share a stride of 4 or more.

The page shows the instructions of a selected 4 KiB page and marks the ones
whose bytes in guest memory no longer match the bytes the linker produced.
"""

import json
import math
import os
import re
import struct
import subprocess
import sys

LABEL = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s((?:[0-9a-f]{2} ?)+)\s*(?:\t(.*))?$")


def load_offsets(path):
    b = open(path, "rb").read()
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
        sys.exit(f"{path}: not a 32-bit little-endian ELF")
    e_phoff = struct.unpack_from("<I", b, 0x1c)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", b, 0x2a)
    loads = []
    for i in range(e_phnum):
        p_type, _, p_vaddr, p_paddr, _, p_memsz = \
            struct.unpack_from("<IIIIII", b, e_phoff + i * e_phentsize)
        if p_type == 1 and p_memsz:
            loads.append((p_vaddr, p_vaddr + p_memsz, p_vaddr - p_paddr))
    return loads


def load_sections(path):
    """[(name, vaddr, file bytes, executable)] of the allocated PROGBITS sections."""
    b = open(path, "rb").read()
    e_shoff = struct.unpack_from("<I", b, 0x20)[0]
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", b, 0x2e)
    hdr = [struct.unpack_from("<IIIIII", b, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    strtab = hdr[e_shstrndx][4]
    out = []
    for name, sh_type, flags, addr, off, size in hdr:
        if sh_type == 1 and flags & 2 and size:
            n = b[strtab + name:b.index(b"\0", strtab + name)].decode()
            out.append((n, addr, b[off:off + size], bool(flags & 4)))
    return out


NO_FALL = re.compile(r"^(?:jmp|ljmp|ret|lret|iret|sysexit|sysret)")
PADDING = re.compile(r"^(?:nop|int3)")


def stub_arrays(insns, labels, phys):
    """{paddr: "array"} of the stubs after each label in phys (see the docstring)."""
    out = {}
    bounds = sorted({pa for pa, _ in labels}) + [float("inf")]
    k = 0
    for lo, hi in zip(bounds, bounds[1:]):
        while k < len(insns) and insns[k][0] < lo:
            k += 1
        if lo not in phys:
            continue
        offs = []
        for j in range(k, len(insns) - 1):
            (pa, raw, text), nxt = insns[j], insns[j + 1]
            if nxt[0] >= hi:
                break
            if NO_FALL.match(text) and pa + len(raw) // 2 == nxt[0] and not PADDING.match(nxt[2]) and not nxt[1].startswith("00"):
                offs.append(nxt[0] - lo)
        if len(offs) >= 3 and math.gcd(*offs) >= 4:
            for o in offs:
                out[lo + o] = "array"
    return out


def taken_addresses(sections, insns, labels, loads, to_phys):
    """{paddr: how} of the instruction starts whose virtual address the
    image holds (see the docstring)."""
    starts = {pa for pa, _, _ in insns}
    phys = {}

    def note(va, how):
        pa = to_phys(loads, va)
        if pa is not None and pa in starts and pa not in phys:
            phys[pa] = how
    for name, addr, data, exe in sections:
        if name == "__ex_table":
            for k in range(0, len(data) - 11, 12):
                note((addr + k + 4 + struct.unpack_from("<i", data, k + 4)[0]) & 0xFFFFFFFF, "extable")
        elif not exe:
            for k in range(-addr % 4, len(data) - 3, 4):
                note(struct.unpack_from("<I", data, k)[0], "data")
    for pa, raw, _ in insns:
        b = bytes.fromhex(raw)
        for k in range(len(b) - 3):
            note(struct.unpack_from("<I", b, k)[0], "code")
    for pa, how in stub_arrays(insns, labels, phys).items():
        phys.setdefault(pa, how)
    return phys


def to_phys(loads, vaddr):
    for lo, hi, off in loads:
        if lo <= vaddr < hi:
            return vaddr - off
    return None


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, out = sys.argv[1:]
    loads = load_offsets(elf)
    text = subprocess.run([os.environ.get("OBJDUMP", "llvm-objdump"), "-d", elf],
                          capture_output=True, text=True, check=True).stdout
    insns, labels = [], []
    for line in text.splitlines():
        m = LABEL.match(line)
        if m:
            pa = to_phys(loads, int(m.group(1), 16))
            if pa is not None:
                labels.append([pa, m.group(2)])
            continue
        m = INSN.match(line)
        if not m:
            continue
        raw = m.group(2).replace(" ", "")
        asm = (m.group(3) or "").replace("\t", " ").strip()
        if not asm and insns:
            # objdump continues long encodings on a line with bytes only
            insns[-1][1] += raw
            continue
        pa = to_phys(loads, int(m.group(1), 16))
        if pa is not None:
            insns.append([pa, raw, asm])
    taken = taken_addresses(load_sections(elf), insns, labels, loads, to_phys)
    with open(out, "w") as f:
        json.dump({"insns": insns, "labels": labels, "taken": sorted([pa, how] for pa, how in taken.items())},
                  f, separators=(",", ":"))
    print(f"{out}: {len(insns)} instructions, {len(labels)} labels, {len(taken)} taken")


if __name__ == "__main__":
    main()
