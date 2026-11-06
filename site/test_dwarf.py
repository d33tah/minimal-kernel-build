#!/usr/bin/env python3
"""Check the DWARF shards of gen-dwarf.js against llvm-symbolizer --inlines.

  site/test_dwarf.py ELF SHARD_DIR [--samples N] [--strip PREFIX ...] [--seed S]

Picks N random addresses in the ELF's code (half inside function
symbols, half anywhere between the first and the last one), asks
`gen-dwarf.js --lookup` (the page's lookup code, dwarf.js) and
llvm-symbolizer for the frames there (function, file, line; innermost
first) and fails on any difference. llvm-symbolizer runs with --functions=short:
with linkage names (the default) it renames the outermost frame after the ELF symbol
(an alias such as __ia32_sys_ni_syscall for __do_sys_ni_syscall), while the
page shows the DWARF name there and the ELF symbol in its own row. Paths from llvm-symbolizer lose each
--strip prefix (the build directory) so both sides are tree-relative.
"""

import argparse
import json
import os
import pathlib
import random
import re
import subprocess
import sys

SITE = pathlib.Path(__file__).resolve().parent


def norm(path, strip):
    path = os.path.normpath(path) if path else path
    for s in strip:
        if path.startswith(s):
            path = path[len(s):]
    return re.sub(r"^(\.\./)+", "", path)


def loads(elf):
    out = subprocess.run(["llvm-readelf", "-l", "-W", elf], capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        f = line.split()
        if f and f[0] == "LOAD":
            va, pa, memsz = int(f[2], 16), int(f[3], 16), int(f[5], 16)
            res.append((va, va + memsz, pa))
    return res


def functions(elf):
    out = subprocess.run(["llvm-nm", "-S", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        f = line.split()
        if len(f) == 4 and f[2] in "tT" and int(f[1], 16) > 0:
            res.append((int(f[0], 16), int(f[1], 16)))
    return res


def symbolize(elf, vaddrs, strip):
    out = subprocess.run(["llvm-symbolizer", "--obj=" + elf, "--inlines", "--functions=short", "--output-style=JSON"],
                         input="".join(f"{a:#x}\n" for a in vaddrs), capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        frames = []
        for s in json.loads(line).get("Symbol", []):
            fn = s.get("FunctionName") or ""
            frames.append((None if fn in ("", "??") else fn, norm(s.get("FileName") or "", strip) if s.get("Line") else "",
                           s.get("Line") or 0))
        res.append(frames)
    return res


def page_frames(shards, paddrs):
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--lookup", shards], input="\n".join(map(str, paddrs)),
                         capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        fr = json.loads(line)["frames"] or []
        res.append([(f["func"], f["file"] if f["line"] else "", f["line"]) for f in fr])
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("shards")
    ap.add_argument("--samples", type=int, default=3000)
    ap.add_argument("--strip", action="append", default=[])
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rnd = random.Random(args.seed)
    lds = loads(args.elf)
    # Function symbols inside the loaded image (not the vDSO's, linked at 0).
    fns = [(a, n) for a, n in functions(args.elf) if any(lo <= a < hi for lo, hi, _ in lds)]
    # Half inside function symbols, half anywhere between the first and
    # the last one (padding, assembly, gaps between compile units).
    lo, hi = min(a for a, _ in fns), max(a + n for a, n in fns)
    vaddrs = []
    for k in range(args.samples):
        a, n = rnd.choice(fns)
        va = a + rnd.randrange(n) if k % 2 else rnd.randrange(lo, hi)
        if any(s <= va < e for s, e, _ in lds):
            vaddrs.append(va)

    def phys(va):
        return next(va - lo + pa for lo, hi, pa in lds if lo <= va < hi)
    want = symbolize(args.elf, vaddrs, args.strip)
    got = page_frames(args.shards, [phys(a) for a in vaddrs])
    bad, inlined = [], 0
    for va, w, g in zip(vaddrs, want, got):
        # No DWARF function (assembly): llvm-symbolizer names the ELF symbol;
        # the page does the same from symbols.json, so compare file:line only.
        if len(w) == 1 and len(g) <= 1 and (not g or g[0][0] is None):
            w = [(None, w[0][1], w[0][2])]
            g = g or [(None, "", 0)]
        if len(w) > 1:
            inlined += 1
        if w != g:
            bad.append((va, w, g))
    for va, w, g in bad[:10]:
        print(f"MISMATCH {va:#x}\n  llvm-symbolizer {w}\n  page            {g}")
    print(f"{args.elf}: {len(vaddrs) - len(bad)}/{len(vaddrs)} addresses agree with llvm-symbolizer --inlines "
          f"({inlined} inside inlined code)")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
