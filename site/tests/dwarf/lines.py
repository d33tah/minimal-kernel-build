"""test_dwarf.py --lines: the code of source lines (line -> address)."""

import json
import os
import subprocess

from tests.dwarf.common import SITE, symbolize


def page_lines(shards, queries):
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--lines", shards], input="".join(q + "\n" for q in queries),
                         capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def dwarfdump_stmt_rows(elf, files):
    """{(file, line): [(vaddr, is_stmt, previous row (vaddr, file, line, is_stmt) or None)]} for the files
    asked for (tree-relative; matched as a path suffix), from llvm-dwarfdump --debug-line. Only the last
    row at an address counts (as for the page and llvm-symbolizer)."""
    res = {}
    dirs, names, want, rows = {}, {}, {}, []

    def flush():
        # rows of one sequence: keep the last row per address
        eff = []
        for r in rows:
            if eff and eff[-1][0] == r[0]:
                eff[-1] = r
            else:
                eff.append(r)
        for k, (va, fi, ln, st, end) in enumerate(eff):
            if end or fi not in want:
                continue
            prev = eff[k - 1] if k else None
            res.setdefault((want[fi], ln), []).append(
                (va, st, (prev[0], want.get(prev[1]), prev[2], prev[3]) if prev and not prev[4] else None))
        rows.clear()

    p = subprocess.Popen(["llvm-dwarfdump", "--debug-line", elf], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    cur = None
    for line in p.stdout:
        if line.startswith("0x"):
            if want:
                f = line.split()
                end = "end_sequence" in f
                rows.append((int(f[0], 16), int(f[3]), int(f[1]), "is_stmt" in f, end))
                if end:
                    flush()
        elif line.startswith("debug_line["):
            dirs, names, want, cur = {}, {}, {}, None
        elif line.startswith("include_directories["):
            k = int(line[line.index("[") + 1:line.index("]")])
            dirs[k] = line.split("= ", 1)[1].strip().strip('"')
        elif line.startswith("file_names["):
            cur = int(line[line.index("[") + 1:line.index("]")])
        elif cur is not None and line.strip().startswith("name:"):
            names[cur] = [line.split('"')[1], 0]
        elif cur is not None and line.strip().startswith("dir_index:"):
            names[cur][1] = int(line.split()[1])
            name, d = names[cur]
            full = name if name.startswith("/") else os.path.join(dirs.get(d, ""), name)
            if not full.startswith("/") and d:
                full = os.path.join(dirs.get(0, ""), full)
            full = os.path.normpath(full)
            hit = next((f for f in files if full == f or full.endswith("/" + f)), None)
            if hit:
                want[cur] = hit
    p.wait()
    return res


def check_lines(elf, shards, n, rnd, lds, fns, strip):
    def phys(va):
        return next(va - lo + pa for lo, hi, pa in lds if lo <= va < hi)

    def virt(pa):
        return next(pa - p0 + lo for lo, hi, p0 in lds if p0 <= pa < p0 + hi - lo)
    vaddrs = []
    for _ in range(n):
        a, sz = rnd.choice(fns)
        vaddrs.append(a + rnd.randrange(sz))
    want = symbolize(elf, vaddrs, strip)
    samples = [(va, w[0][1], w[0][2]) for va, w in zip(vaddrs, want) if w and w[0][2]]
    targets = sorted({(f, ln) for _, f, ln in samples})
    got = dict(zip(targets, page_lines(shards, [f"{f}:{ln}" for f, ln in targets])))
    bad = []
    # Every sampled address is in a row of its line.
    for va, f, ln in samples:
        g, pa = got[(f, ln)], phys(va)
        if g.get("none") or not any(a <= pa < b for a, b, _ in g["rows"]):
            bad.append(f"{f}:{ln}: {va:#x} (llvm-symbolizer) is not in the rows the page lists: {str(g)[:300]}")
    # Every row and inlined call listed is that line.
    probes = []
    for (f, ln), g in got.items():
        rows = g.get("rows", [])
        for a, b, _ in rnd.sample(rows, min(20, len(rows))):
            probes += [(f, ln, "row", virt(a)), (f, ln, "row", virt(b - 1))]
        calls = g.get("calls", [])
        for a, _, name, _ in rnd.sample(calls, min(10, len(calls))):
            probes.append((f, ln, "call " + str(name), virt(a)))
    for (f, ln, kind, va), w in zip(probes, symbolize(elf, [p[3] for p in probes], strip)):
        if kind == "row":
            ok = bool(w) and (w[0][1], w[0][2]) == (f, ln)
        else:
            name = kind[5:]
            ok = any((w[k][1], w[k][2]) == (f, ln) and w[k - 1][0] == name for k in range(1, len(w)))
        if not ok:
            bad.append(f"{f}:{ln}: {kind} at {va:#x} is {w} for llvm-symbolizer")
    # Breakpoint addresses are the line's is_stmt rows.
    dd = dwarfdump_stmt_rows(elf, sorted({f for f, _ in targets}))
    nbp = nstmt = 0
    for (f, ln), g in got.items():
        rows = g.get("rows", [])
        rowbp = {x for _, _, bp in rows for x in bp}
        stmts = {phys(va): prev for va, st, prev in dd.get((f, ln), []) if st}
        nbp += len(rowbp)
        for x in rowbp:
            if x not in stmts:
                bad.append(f"{f}:{ln}: breakpoint {x:#x} is not an is_stmt row of the line in llvm-dwarfdump")
        for x, prev in stmts.items():
            if not any(a <= x < b for a, b, _ in rows):
                continue
            nstmt += 1
            same = prev is not None and prev[3] and prev[1] == f and prev[2] == ln
            if x not in rowbp and not same:
                bad.append(f"{f}:{ln}: is_stmt row {x:#x} (after {prev}) is not a breakpoint address")
    for b in bad[:10]:
        print("LINES " + b)
    print(f"{elf}: lines {len(targets) - len({b.split(': ')[0] for b in bad})}/{len(targets)} agree "
          f"({len(samples)} sampled addresses, {len(probes)} rows/calls checked with llvm-symbolizer, "
          f"{nbp} breakpoint addresses, {nstmt} is_stmt rows in listed rows checked with llvm-dwarfdump)")
    return bad
