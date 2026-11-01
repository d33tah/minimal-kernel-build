#!/usr/bin/env python3
"""Write sources.json: the C source line of every instruction in disasm.json.

  site/gen-source.py ELF_WITH_DEBUG_INFO SRC_ROOT DISASM_JSON OUT.json

ELF_WITH_DEBUG_INFO is a -g build of the same tree whose code is byte-
identical to the kernel the page boots (the workflow checks that). Each
instruction address of DISASM_JSON goes through `llvm-symbolizer
--inlining` (override with $SYMBOLIZER): innermost file:line and function,
plus the chain of callers it was inlined into. Paths are made relative to
SRC_ROOT.

Output (JSON, compact):
  files   [path, ...]                 relative to SRC_ROOT
  funcs   [name, ...]
  loc     [[paddr, file, line, func, [[file, line, func], ...]], ...]
          one entry where the location changes; it holds until the next
          entry; file -1 = no line information; the last item (inlined-at
          chain, outermost last) is left out when empty
  ranges  [[func, file, first, last], ...]  the lines of each function
  text    {file: [[first, [line text, ...]], ...]}  only those lines
"""

import json
import os
import struct
import subprocess
import sys


def load_offsets(path):
    b = open(path, "rb").read()
    e_phoff = struct.unpack_from("<I", b, 0x1c)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", b, 0x2a)
    loads = []
    for i in range(e_phnum):
        p_type, _, p_vaddr, p_paddr, _, p_memsz = \
            struct.unpack_from("<IIIIII", b, e_phoff + i * e_phentsize)
        if p_type == 1 and p_memsz:
            loads.append((p_paddr, p_paddr + p_memsz, p_vaddr - p_paddr))
    return loads


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    elf, root, disasm, out = sys.argv[1:]
    root = os.path.realpath(root)
    loads = load_offsets(elf)
    paddrs = [i[0] for i in json.load(open(disasm))["insns"]]

    def vaddr(pa):
        for lo, hi, off in loads:
            if lo <= pa < hi:
                return pa + off
        return pa

    res = subprocess.run([os.environ.get("SYMBOLIZER", "llvm-symbolizer"), "--obj=" + elf, "--inlining",
                          "--output-style=JSON"], input="".join(f"{vaddr(pa):#x}\n" for pa in paddrs),
                         capture_output=True, text=True, check=True).stdout.splitlines()
    if len(res) != len(paddrs):
        sys.exit(f"llvm-symbolizer answered {len(res)} of {len(paddrs)} addresses")
    files, funcs = {}, {}

    def fid(path):
        p = os.path.realpath(path) if path else ""
        if not p:
            return -1
        rel = os.path.relpath(p, root) if p.startswith(root + os.sep) else p
        return files.setdefault(rel, len(files))

    def fnid(name):
        return funcs.setdefault(name or "?", len(funcs))

    loc, prev = [], None
    span = {}  # (func, file) -> [first, last]
    for pa, line in zip(paddrs, res):
        frames = json.loads(line).get("Symbol", [])
        chain = []
        for fr in frames:
            f = fid(fr.get("FileName")) if fr.get("Line") else -1
            chain.append([f, fr.get("Line", 0), fnid(fr.get("FunctionName"))])
            if f >= 0:
                key = (chain[-1][2], f)
                lo = min(fr.get("StartLine") or fr["Line"], fr["Line"]) if fr.get("StartFileName") == fr.get("FileName") else fr["Line"]
                s = span.setdefault(key, [lo, fr["Line"]])
                s[0] = min(s[0], lo)
                s[1] = max(s[1], fr["Line"])
        inner = chain[0] if chain else [-1, 0, fnid("?")]
        key = (inner[0], inner[1], inner[2], tuple(map(tuple, chain[1:])))
        if key != prev:
            entry = [pa, inner[0], inner[1], inner[2]]
            if chain[1:]:
                entry.append(chain[1:])
            loc.append(entry)
            prev = key
    names = {v: k for k, v in files.items()}
    text, ranges = {}, []
    by_file = {}
    for (fn, f), (lo, hi) in span.items():
        ranges.append([fn, f, lo, hi + 1])
        by_file.setdefault(f, []).append((lo, hi + 1))
    for f, rs in by_file.items():
        path = os.path.join(root, names[f])
        try:
            lines = open(path, errors="replace").read().split("\n")
        except OSError:
            continue
        rs.sort()
        merged = []
        for lo, hi in rs:
            lo = max(1, lo - 2)
            if merged and lo <= merged[-1][1] + 1:
                merged[-1][1] = max(merged[-1][1], hi)
            else:
                merged.append([lo, hi])
        text[f] = [[lo, lines[lo - 1:hi]] for lo, hi in merged]
    data = {"files": [names[i] for i in range(len(names))], "funcs": [k for k, _ in sorted(funcs.items(), key=lambda kv: kv[1])],
            "loc": loc, "ranges": sorted(ranges), "text": text}
    with open(out, "w") as fh:
        json.dump(data, fh, separators=(",", ":"))
    nlines = sum(len(t) for rs in text.values() for _, t in rs)
    print(f"{out}: {len(loc)} locations, {len(files)} files, {len(funcs)} functions, {nlines} source lines, "
          f"{os.path.getsize(out)} bytes")


if __name__ == "__main__":
    main()
