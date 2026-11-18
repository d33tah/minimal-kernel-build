"""test_dwarf.py --types: types.json against gdb."""

import json
import os
import re
import subprocess
import tempfile

from tests.dwarf.common import SITE


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
    if not pool:
        # The hello kernel: its only DW_TAG_variables are unnamed string literals.
        print(f"{elf}: types.json {len(T['vars'])} variables, {len(T['types'])} types; no variable gdb can name: nothing to check")
        return []
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
    # name -> {address: size} of the ELF symbols. clang names a static local
    # function.name, and the next ones of that name in the same function
    # (one per inner block: WARN_ONCE's __msg, ratelimit's _rs)
    # function.name.N: those are under function.name too.
    nm_at, names_at = {}, {}
    for f in (line.split() for line in subprocess.run(["llvm-nm", "-S", "--defined-only", elf], capture_output=True, text=True,
                                                       check=True).stdout.splitlines()):
        if len(f) >= 3:
            size = int(f[1], 16) if len(f) == 4 else None
            nm_at.setdefault(f[-1], {})[int(f[0], 16)] = size
            names_at.setdefault(int(f[0], 16), {})[f[-1]] = size
            m = re.fullmatch(r"([^.]+\.[^.]+)\.\d+", f[-1])
            if m:
                nm_at.setdefault(m.group(1), {})[int(f[0], 16)] = size
    bad, bits, padding, unions, hidden, moved, shrunk, pieces = [], 0, 0, 0, 0, 0, 0, 0
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
        fn = v[6] if len(v) > 6 else ""
        static = nm_at.get(fn + "." + v[0], {}) if fn else {}
        if other and (re.fullmatch(re.escape(v[0]) + r"(\.\d+)?", gv[-1]) or virt(v[1]) in static):
            # gdb finds no variable of this name, or another one: Function::name
            # does not look into inner blocks (statement-expression macros:
            # WARN_ONCE, ...) nor into a function that only exists inlined,
            # and several statics may share a name (gdb takes the first). The
            # ELF symbol at the address (name, or name.N for a static local;
            # clang 22 names a static local function.name or function.name.N,
            # which gdb prints oddly, "neigh_valid_get_req..msg": llvm-nm)
            # confirms the variable and its address, and clang's symbol its size.
            if virt(v[1]) in static and static[virt(v[1])] is not None and w["varSize"] and static[virt(v[1])] != w["varSize"]:
                bad.append((hex(pa), "size", v[0], fn, w["varSize"], "ELF symbol", static[virt(v[1])]))
                continue
            hidden += 1
            continue
        if v[5] & 16 and w.get("var") == v[0]:
            # A piece of a variable clang split (DW_OP_piece; LLVM's SRA of a
            # global): gdb reads the variable from its pieces and gives it no
            # address. The type is gdb's; clang's ELF symbol name.N of the
            # piece's size is at the piece's address.
            sym = [s for s, size in names_at.get(virt(v[1]), {}).items() if re.fullmatch(re.escape(v[0]) + r"\.\d+", s) and size == v[3]]
            if gv[0] == "error" or sp(gv[2]) != sp(w["varType"]) or not sym:
                bad.append((hex(pa), "piece", v[0], w["varType"], v[3], gv, sorted(names_at.get(virt(v[1]), {}))))
            else:
                pieces += 1
            continue
        if gv[0] == "error" or gp[0] == "error":
            bad.append((hex(pa), w, gv, gp))
            continue
        moved += gv[3]
        # A global clang shrank (flag 8): gdb's sizeof is its C type's, the
        # page's size the bytes clang keeps (DW_OP_deref_size), no more than that.
        want_size = w["size"] if v[5] & 8 and w["path"] == v[0] else w["varSize"]
        if v[5] & 8:
            shrunk += 1
            if not 0 < v[3] <= gv[1]:
                bad.append((hex(pa), "shrunk", v[0], v[3], gv))
        if w["var"] == v[0] and not decl and (sp(gv[2]) != sp(w["varType"]) or (want_size and gv[1] != want_size)):
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
          f"{unions} in unions, {moved} where gdb's variable of that name is another one, {shrunk} in globals clang shrank, "
          f"{pieces} in pieces of globals clang split (checked by ELF symbol); "
          f"{hidden} more in variables gdb cannot find by name, checked by ELF symbol)")
    return bad
