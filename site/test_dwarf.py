#!/usr/bin/env python3
"""Check the DWARF shards of gen-dwarf.js against llvm-symbolizer --inlines,
and its types.json against gdb.

  site/test_dwarf.py ELF SHARD_DIR [--samples N] [--strip PREFIX ...] [--seed S] [--types N]

Picks N random addresses in the ELF's code (half inside function
symbols, half anywhere between the first and the last one), asks
`gen-dwarf.js --lookup` (the page's lookup code, dwarf.js) and
llvm-symbolizer for the frames there (function, file, line; innermost
first) and fails on any difference. llvm-symbolizer runs with --functions=short:
with linkage names (the default) it renames the outermost frame after the ELF symbol
(an alias such as __ia32_sys_ni_syscall for __do_sys_ni_syscall), while the
page shows the DWARF name there and the ELF symbol in its own row. Paths from llvm-symbolizer lose each
--strip prefix (the build directory) so both sides are tree-relative.

--types N: N random bytes inside variables of types.json; `gen-dwarf.js
--where` (the page's dwWhere) names the variable and member path there;
gdb on the ELF must agree: the path is an expression whose address range
holds the byte, its type prints the same (whatis) with the same size, and
so does the variable's.
"""

import argparse
import json
import os
import pathlib
import random
import re
import subprocess
import sys
import tempfile

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


# Runs inside gdb: for each [name, function, path, address] (function: of a
# static local, else ""), the variable (global, else file-static) and then
# the members and elements of path applied by gdb; writes [address or None,
# sizeof, str(type), whether gdb's variable of that name was elsewhere, the
# ELF symbol at the address] per entry, or ["error", message, ELF symbol].
GDB_EVAL = r"""
import json, re, gdb
src, dst = json.load(open(SRC)), []
for name, fn, path, addr in src:
    try:
        sym = gdb.execute("info symbol %d" % addr, to_string=True).split(" in section")[0].split(" + ")[0].strip()
    except Exception:
        sym = ""
    try:
        if fn:
            v = gdb.parse_and_eval(fn + "::" + name)
        else:
            s = [x for x in (gdb.lookup_global_symbol(name), gdb.lookup_static_symbol(name)) if x and x.is_variable]
            # Without one (a declaration only, gdb resolves it through the ELF symbol), the expression.
            v = s[0].value() if s else gdb.parse_and_eval(name)
            if v.type.code == gdb.TYPE_CODE_FUNC:
                raise gdb.error("no variable " + name)
        # Another variable of that name (a static in another file, a copy
        # with only a constant value, a declaration): its type at the
        # address types.json gives.
        moved = v.address is None or int(v.address) != addr
        if moved:
            v = gdb.Value(addr).cast(v.type.pointer()).dereference()
        for field, index in re.findall(r"\.(\w+)|\[(\d+)\]", path[len(name):]):
            v = v[field] if field else v[int(index)]
        a = v.address
        dst.append([int(a) if a is not None else None, v.type.sizeof, str(v.type), moved, sym])
    except Exception as x:
        dst.append(["error", str(x), sym])
json.dump(dst, open(DST, "w"))
"""


def gdb_eval(elf, exprs):
    with tempfile.TemporaryDirectory() as tmp:
        src, dst, py = (os.path.join(tmp, n) for n in ("in.json", "out.json", "eval.py"))
        json.dump(exprs, open(src, "w"))
        with open(py, "w") as f:
            f.write(f"SRC, DST = {src!r}, {dst!r}\n" + GDB_EVAL)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "source " + py, elf], check=True, capture_output=True)
        return json.load(open(dst))


def check_types(elf, shards, n, rnd, lds):
    """Variables and member paths of types.json against gdb; returns the problems."""
    T = json.load(open(os.path.join(shards, "types.json")))
    count = {}
    for v in T["vars"]:
        count[v[0]] = count.get(v[0], 0) + 1
    # Names gdb resolves to this one variable: unique, or Function::name for a static local.
    # (A static local of a function DWARF does not name has no name gdb finds.)
    pool = [v for v in T["vars"] if v[3] > 0 and (len(v) > 6 and v[6] or not v[5] & 4 and count[v[0]] == 1)]
    pick = [rnd.choice(pool) for _ in range(n)]
    pas = [v[1] + rnd.randrange(v[3]) for v in pick]
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--where", shards], input="\n".join(map(str, pas)),
                         capture_output=True, text=True, check=True).stdout
    where = [json.loads(line) for line in out.splitlines()]

    def virt(pa):
        return next(pa - p + lo for lo, hi, p in lds if p <= pa < p + hi - lo)

    exprs = []
    for v, w in zip(pick, where):
        fn = v[6] if len(v) > 6 else ""
        exprs += [[v[0], fn, v[0], virt(v[1])], [w.get("var", v[0]), fn if w.get("var") == v[0] else "", w.get("path", v[0]), virt(v[1])]]
    got = gdb_eval(elf, exprs)
    sp = lambda t: re.sub(r"\s+", "", t or "")
    bad, bits, padding, unions, hidden, moved = [], 0, 0, 0, 0, 0
    for k, (v, pa, w) in enumerate(zip(pick, pas, where)):
        gv, gp = got[2 * k], got[2 * k + 1]
        va = virt(pa)
        if w.get("var") != v[0]:
            # A smaller variable inside this one (an alias) may win; it must hold the byte too.
            if not w.get("var"):
                bad.append((hex(pa), v[0], "no variable named", w))
                continue
        # gdb's variable may be the declaration: an array of unknown size.
        decl = gv[0] != "error" and "[]" in gv[2] and sp(re.sub(r"\[\d+\]", "[]", w["varType"], count=1)) == sp(gv[2])
        other = gv[0] == "error" or gv[3] and not decl and sp(gv[2]) != sp(w["varType"])
        if other and re.fullmatch(re.escape(v[0]) + r"(\.\d+)?", gv[-1]):
            # gdb finds no variable of this name, or another one: Function::name
            # does not look into inner blocks (statement-expression macros:
            # WARN_ONCE, ...) nor into a function that only exists inlined,
            # and several statics may share a name. The ELF symbol at the
            # address (name, or name.N for a static local) confirms the
            # variable and its address.
            hidden += 1
            continue
        if gv[0] == "error" or gp[0] == "error":
            bad.append((hex(pa), w, gv, gp))
            continue
        moved += gv[3]
        if w["var"] == v[0] and not decl and (sp(gv[2]) != sp(w["varType"]) or (w["varSize"] and gv[1] != w["varSize"])):
            bad.append((hex(pa), "variable", v[0], w["varType"], w["varSize"], gv))
        if sp(gp[2]) != sp(w["type"]) or (w["size"] and gp[1] != w["size"]):
            bad.append((hex(pa), "leaf", w["path"], w["type"], w["size"], gp))
        if w["note"].startswith("bit"):
            bits += 1
        elif gp[0] is None or not gp[0] <= va < gp[0] + max(gp[1], 1):
            bad.append((hex(pa), "address", w["path"], gp))
        padding += w["note"] == "padding"
        unions += bool(w.get("unions"))
    for b in bad[:10]:
        print("MISMATCH", b)
    print(f"{elf}: types.json {len(T['vars'])} variables, {len(T['types'])} types; {n - hidden - len(bad)}/{n - hidden} "
          f"random bytes agree with gdb (variable, member path, type, size; {bits} in bit fields, {padding} padding, "
          f"{unions} in unions, {moved} where gdb's variable of that name is another one; {hidden} more in variables "
          f"gdb cannot find by name, checked by ELF symbol)")
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("shards")
    ap.add_argument("--samples", type=int, default=3000)
    ap.add_argument("--strip", action="append", default=[])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--types", type=int, default=0, help="also check N bytes of types.json against gdb")
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
    tbad = check_types(args.elf, args.shards, args.types, rnd, lds) if args.types else []
    sys.exit(1 if bad or tbad else 0)


if __name__ == "__main__":
    main()
