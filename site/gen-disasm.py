#!/usr/bin/env python3
"""Write disasm.json for the page's memory lab from a kernel ELF.

  site/gen-disasm.py ELF OUT.json

Runs `llvm-objdump -d` (override with $OBJDUMP) over every executable
section and maps each instruction to its physical address through the
PT_LOAD segments, like gen-symbols.py does for symbols.

Output:
  {"insns": [[paddr, "hex bytes", "mnemonic operands"], ...],
   "labels": [[paddr, "symbol"], ...]}

The page shows the instructions of a selected 4 KiB page and marks the ones
whose bytes in guest memory no longer match the bytes the linker produced.
"""

import json
import os
import re
import struct
import subprocess
import sys

LABEL = re.compile(r"^([0-9a-f]+) <(.+)>:$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s((?:[0-9a-f]{2} )+)\s*(?:\t(.*))?$")


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
    with open(out, "w") as f:
        json.dump({"insns": insns, "labels": labels}, f, separators=(",", ":"))
    print(f"{out}: {len(insns)} instructions, {len(labels)} labels")


if __name__ == "__main__":
    main()
