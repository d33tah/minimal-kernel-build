#!/usr/bin/env python3
"""Write symbols.json for the page's memory lab from a kernel ELF.

  site/gen-symbols.py [--trim] ELF OUT.json [NAME]

--trim (for big distro kernels): keep only symbols with a size, all code
symbols (t/T) and global data (D/B/R); drop local data objects and the
export/tracing bookkeeping (__ksymtab_*, __kstrtab*, __crc_*,
__UNIQUE_ID_*, __initcall*, __setup_*, __param_*, __func__*, ...), which
the page never shows usefully.

Symbols come from `llvm-nm -nS ELF` (override with $NM). Section headers
and program headers are read from the ELF itself (32-bit little-endian
only, which is all the page boots). Physical addresses follow from the
PT_LOAD segments: paddr = vaddr - (p_vaddr - p_paddr), so for the
minified Linux paddr = vaddr - 0xC0000000 and for hello paddr = vaddr.

Output:
  {"name": ..., "sections": [{name, class, vaddr, paddr, size}],
   "symbols": [{name, vaddr, paddr, size, type, section}]}

class is one of text, rodata, data, bss, brk, other.
"""

import json
import os
import struct
import subprocess
import sys


def sec_class(name, flags, stype):
    if name == ".brk":
        return "brk"
    if stype == 8:                      # SHT_NOBITS
        return "bss"
    if flags & 0x4:                     # SHF_EXECINSTR
        return "text"
    if ".rodata" in name or not flags & 0x1:   # not SHF_WRITE
        return "rodata"
    if ".data" in name:
        return "data"
    return "other"


def read_elf(path):
    b = open(path, "rb").read()
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
        sys.exit(f"{path}: not a 32-bit little-endian ELF")
    (e_phoff, e_shoff) = struct.unpack_from("<II", b, 0x1c)
    (e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = \
        struct.unpack_from("<HHHHH", b, 0x2a)
    loads = []
    for i in range(e_phnum):
        (p_type, _, p_vaddr, p_paddr, _, p_memsz) = \
            struct.unpack_from("<IIIIII", b, e_phoff + i * e_phentsize)
        if p_type == 1 and p_memsz:
            loads.append((p_vaddr, p_vaddr + p_memsz, p_vaddr - p_paddr))
    shdrs = [struct.unpack_from("<IIIIIIIIII", b, e_shoff + i * e_shentsize)
             for i in range(e_shnum)]
    stroff = shdrs[e_shstrndx][4]

    def sname(off):
        end = b.index(b"\0", stroff + off)
        return b[stroff + off:end].decode()

    def paddr(va):
        for lo, hi, delta in loads:
            if lo <= va < hi:
                return va - delta
        return None

    sections = []
    for (nm, stype, flags, addr, _, size, *_rest) in shdrs:
        if not flags & 0x2 or not size:     # SHF_ALLOC, non-empty
            continue
        name = sname(nm)
        pa = paddr(addr)
        if pa is None:
            continue
        sections.append({"name": name, "class": sec_class(name, flags, stype),
                         "vaddr": addr, "paddr": pa, "size": size})
    sections.sort(key=lambda s: s["vaddr"])
    return sections, paddr


DROP_PREFIXES = ("__ksymtab_", "__kstrtab_", "__kstrtabns_", "__crc_", "__UNIQUE_ID_", "__initcall",
                 "__setup_", "__param_", "__func__", "__already_done", "__print_once", "__warned",
                 "__key.", "__compound_literal", "__tpstrtab_", "__bpf_trace_", "__event_", "event_class_",
                 "trace_event_fields_", "print_fmt_", "__SCT__", "__SCK__")


def keep_trimmed(sym):
    if not sym["size"] or sym["name"].startswith(DROP_PREFIXES):
        return False
    return sym["type"] in "tTDBR"


def main():
    args = sys.argv[1:]
    trim = "--trim" in args
    args = [a for a in args if a != "--trim"]
    if len(args) not in (2, 3):
        sys.exit(__doc__)
    elf, out = args[0], args[1]
    name = args[2] if len(args) == 3 else os.path.basename(elf)
    sections, paddr = read_elf(elf)
    nm = os.environ.get("NM", "llvm-nm")
    lines = subprocess.run([nm, "-nS", elf], check=True, capture_output=True,
                           text=True).stdout.splitlines()
    symbols = []
    for line in lines:
        parts = line.split()
        if len(parts) == 4:
            va, size, typ, sym = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        elif len(parts) == 3:
            va, size, typ, sym = int(parts[0], 16), 0, parts[1], parts[2]
        else:
            continue                        # undefined symbols have no address
        if typ in "aAuUvVwW":               # absolute or not a location
            continue
        pa = paddr(va)
        if pa is None:
            continue
        sec = next((s["name"] for s in sections
                    if s["vaddr"] <= va < s["vaddr"] + s["size"]), "")
        symbols.append({"name": sym, "vaddr": va, "paddr": pa, "size": size,
                        "type": typ, "section": sec})
    if trim:
        symbols = [s for s in symbols if keep_trimmed(s)]
    with open(out, "w") as f:
        json.dump({"name": name, "sections": sections, "symbols": symbols},
                  f, separators=(",", ":"))
        f.write("\n")
    print(f"{out}: {len(sections)} sections, {len(symbols)} symbols")


if __name__ == "__main__":
    main()
