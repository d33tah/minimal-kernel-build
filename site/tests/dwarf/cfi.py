"""test_dwarf.py --cfi and --eh-frame: the CFI rows against llvm-dwarfdump."""

import json
import os
import pathlib
import re
import subprocess
import tempfile

from tests.dwarf.common import loads, SITE


def dwarfdump_cfi(elf):
    """Every FDE of the ELF as llvm-dwarfdump evaluates it: [(lo, hi, [(row
    start, row text), ...]), ...] by virtual address (.debug_frame, else .eh_frame)."""
    fdes = []
    for flag in ("--debug-frame", "--eh-frame"):
        out = subprocess.run(["llvm-dwarfdump", flag, elf], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            m = re.search(r" FDE cie=\w+ pc=([0-9a-f]+)\.\.\.([0-9a-f]+)$", line)
            if m:
                fdes.append((int(m.group(1), 16), int(m.group(2), 16), []))
                continue
            m = re.match(r"^  0x([0-9a-f]+): (CFA=.*)$", line)
            if m and fdes:
                fdes[-1][2].append((int(m.group(1), 16), m.group(2)))
        if fdes:
            break
    return [f for f in fdes if f[0] and f[1] > f[0]]


def check_cfi(elf, shards, n, rnd, lds):
    """N random addresses: half inside FDEs (the row llvm-dwarfdump evaluates
    there must equal the page's, unwind.js cfiRow), half anywhere in the
    code (no FDE there: the page must have no row either)."""
    fdes = dwarfdump_cfi(elf)
    if not fdes:
        print(f"{elf}: no call frame information")
        return []

    def phys(va):
        return next((va - lo + pa for lo, hi, pa in lds if lo <= va < hi), None)
    lo_all, hi_all = min(f[0] for f in fdes), max(f[1] for f in fdes)
    starts = sorted(fdes)
    picks = []
    for k in range(n):
        if k % 2:
            f = rnd.choice(fdes)
            picks.append(f[0] + rnd.randrange(f[1] - f[0]))
        else:
            picks.append(rnd.randrange(lo_all, hi_all))
    picks = [va for va in picks if phys(va) is not None]
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--cfi", shards], input="\n".join(str(phys(a)) for a in picks),
                         capture_output=True, text=True, check=True).stdout
    got = [json.loads(l) for l in out.splitlines()]
    import bisect
    keys = [f[0] for f in starts]
    bad, inside, exprs = [], 0, 0
    for va, g in zip(picks, got):
        i = bisect.bisect_right(keys, va) - 1
        f = starts[i] if i >= 0 and starts[i][0] <= va < starts[i][1] else None
        want = None
        if f:
            inside += 1
            rows = [r for r in f[2] if r[0] <= va]
            want = rows[-1][1] if rows else None
        have = g.get("row") or g.get("why")
        if want and "DW_OP" in want:
            exprs += 1
        if want != have:
            bad.append((va, want, have))
    for va, w, g in bad[:10]:
        print(f"CFI MISMATCH {va:#x}\n  llvm-dwarfdump {w}\n  page           {g}")
    print(f"{elf}: {len(picks) - len(bad)}/{len(picks)} addresses agree with llvm-dwarfdump's CFI rows "
          f"({inside} inside {len(fdes)} FDEs, {exprs} with DWARF expressions)")
    return bad


# For --eh-frame: functions whose CFI uses the usual rules, a realigned
# stack (GCC: DW_CFA_def_cfa_expression and DW_CFA_expression) and varargs.
EH_FRAME_C = r"""
#include <stdarg.h>
int g[64];
__attribute__((noinline)) int leaf(int a) { return g[a & 63] + a; }
__attribute__((noinline)) int mid(int a, int b) { int x[8]; for (int i = 0; i < 8; i++) x[i] = leaf(a + i) * b; return x[a & 7] + leaf(b); }
__attribute__((noinline, force_align_arg_pointer)) int aligned(int a) { volatile double d __attribute__((aligned(32))) = a; return mid(a, (int)d) + 1; }
int vsum(int n, ...) { va_list ap; va_start(ap, n); int s = 0; while (n--) s += va_arg(ap, int) + mid(s, n); va_end(ap); return s; }
void _start(void) { g[0] = aligned(3) + vsum(3, 1, 2, 3); for (;;); }
"""


def eh_frame_selftest(n, rnd):
    """The kernels here have .debug_frame only; the .eh_frame path (CIE
    augmentation "zR", pc-relative pointers) is checked on a small i386
    program built with GCC and with clang -fasynchronous-unwind-tables."""
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "t.c")
        pathlib.Path(src).write_text(EH_FRAME_C)
        for cc in (["gcc", "-fno-stack-protector"], ["clang", "-fuse-ld=lld"]):
            elf, out = os.path.join(tmp, cc[0] + ".elf"), os.path.join(tmp, cc[0])
            subprocess.run(cc + ["-m32", "-g", "-O2", "-fasynchronous-unwind-tables", "-fno-pie", "-nostdlib", "-static",
                                 "-Wl,-Ttext=0xc0100000", "-o", elf, src], check=True)
            sections = subprocess.run(["llvm-readelf", "-S", elf], capture_output=True, text=True, check=True).stdout
            if ".eh_frame " not in sections or ".debug_frame" in sections:
                bad.append(f"{cc[0]}: expected .eh_frame only")
                continue
            subprocess.run(["node", str(SITE / "gen-dwarf.js"), elf, out], check=True, capture_output=True)
            bad += check_cfi(elf, out, n, rnd, loads(elf))
    return bad
