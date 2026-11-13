#!/usr/bin/env python3
"""Check the DWARF shards of gen-dwarf.js against llvm-symbolizer --inlines,
and its types.json against gdb.

  site/test_dwarf.py ELF SHARD_DIR [--samples N] [--strip PREFIX ...] [--seed S] [--types N] [--cfi N] [--locals N] [--operands N] [--lines N] [--funcs N] [--vars N]

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

--locals N: N random addresses inside functions; the parameters and
local variables the page lists for the innermost frame there (inlined
call or function, `gen-dwarf.js --locals`) must be the ones gdb's `info
scope` lists, each with the same kind of location there. Except: gdb
also lists, as optimized out, the variables of an abstract lexical block
whose concrete copy clang emits without DW_AT_abstract_origin (gdb
matches blocks only by that attribute), at addresses outside that block
and as a second entry inside it; the page matches the block by its
variables' abstract origins (see --inline-blocks). Such an address passes
when llvm-dwarfdump shows each extra variable in such a concrete block
that does not cover the address, or covers it and the page lists the
variable with its location.

--operands N: N random instructions with a memory operand, half on the
stack, half at an absolute address; the names the page's listings give
them (`gen-dwarf.js --operands`, asmnames.js) must be gdb's: `info
symbol` for absolute addresses, the address gdb computes for each
parameter and local on a core with EIP at the instruction for stack
slots (see check_operands). Where gdb prints another name for the same
object, llvm-nm decides: several linker marks without a size at one
address (the page names all of them, gdb one), a static local (clang's
ELF symbol function.name, which gdb prints demangled oddly), and a mark
between two sections (gdb's info symbol finds nothing outside sections).

--lines N: the code of source lines (the Source panel's address list and
its "Run to file:line", `gen-dwarf.js --lines`, dwarf.js dwarfFileCode):
the lines llvm-symbolizer names at N random addresses inside functions.
Every sampled address lies in a row the page lists for its line; the
first and last byte of every row listed (up to 20 per line) and the
start of every inlined call listed are that line for llvm-symbolizer
(innermost frame; for a call the frame that called the inlined function);
every breakpoint address is an is_stmt row of that line in
llvm-dwarfdump --debug-line, and every is_stmt row of the line inside the
listed rows is a breakpoint address unless the row before it is an
is_stmt row of the same line.

--funcs N: the facts of N random functions (gen-dwarf.js --funcs, the
page's dwarf.js DwarfFuncs/dwFuncProto and unwind.js cfiFrameInfo), half
with code of their own, half only inlined. gdb, on the function's block
at its code (or at an inlined copy): its type as ptype prints it, the
file:line it is declared at, and external exactly when gdb's global
symbol of that name is this function; llvm-symbolizer --inlines at every inlined copy listed
(up to 20 each): the function is inlined there; llvm-dwarfdump
--debug-frame: the frame (largest ESP-based CFA, first EBP-based row,
saved registers) of every out-of-line copy; and, when .debug_info is
small enough to dump (the kernels built here), the number of inlined
copies of every function equals llvm-dwarfdump's count of
DW_TAG_inlined_subroutine DIEs with code whose abstract origin is it.

--vars N: where N variables of types.json are defined (half of them
sharing their name with another variable) and which one `file.c::name`
picks (`gen-dwarf.js --vars`, the page's dwarf.js dwVarDecl/dwVarsPick):
gdb's symbol of that variable (the global or file-static symbol of that
name at that address, a static local in its function's block) is
declared at the same file:line (gdb reads DW_AT_decl_file/decl_line, from
the DIE or the one it specifies; what gdb does not find by name, a
static local in an inner block, is the DIE llvm-dwarfdump --name lists
at that address; several definitions at one address, a __weak one and
the one the linker kept, are all listed), and for a name several variables
share, gdb's `&'file'::name` is the address the page picks for the
qualified name (the variable's file, and its shortest unique suffix);
a qualifier no definition matches, or that matches several files,
is refused with the list of definitions.

--cfi N: N random addresses; the call frame information row there
(`gen-dwarf.js --cfi`, the page's unwind.js cfiRow on the cfi-*.bin
shards) must read the same as the row llvm-dwarfdump --debug-frame (or
--eh-frame) evaluates for that FDE, and outside every FDE there is none.

  site/test_dwarf.py --eh-frame [N]

the same check on the .eh_frame of a small program built here with GCC
and clang (the kernels have .debug_frame only).

  site/test_dwarf.py --inline-blocks

the parameters and locals (`gen-dwarf.js --locals`) at every instruction
of an always_inline function built here with clang -O0 -g: a block's
variables are listed only inside the concrete block, with a location,
and no name twice. clang gives the concrete copy of a block no
DW_AT_abstract_origin (only its variables refer to the abstract ones),
and the inlined call has more children than its abstract function (a
nested inlined call), so the block is matched by its variables.
"""

import argparse
import base64
import gzip
import json
import os
import pathlib
import random
import re
import struct
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
    # name -> addresses of the ELF symbols (clang names a static local function.name).
    nm_at = {}
    for f in (line.split() for line in subprocess.run(["llvm-nm", "--defined-only", elf], capture_output=True, text=True,
                                                       check=True).stdout.splitlines()):
        if len(f) >= 3:
            nm_at.setdefault(f[-1], set()).add(int(f[0], 16))
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
        fn = v[6] if len(v) > 6 else ""
        if other and (re.fullmatch(re.escape(v[0]) + r"(\.\d+)?", gv[-1]) or
                      fn and virt(v[1]) in nm_at.get(fn + "." + v[0], ())):
            # gdb finds no variable of this name, or another one: Function::name
            # does not look into inner blocks (statement-expression macros:
            # WARN_ONCE, ...) nor into a function that only exists inlined,
            # and several statics may share a name. The ELF symbol at the
            # address (name, or name.N for a static local; clang 22 names a
            # static local function.name, which gdb prints oddly: llvm-nm)
            # confirms the variable and its address.
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


# Runs inside gdb: info scope at each address, after a line "@@ADDR
# KEEP" with one 0/1 per symbol of the blocks info scope walks, in its
# order: 1 for a parameter or variable (a constant one too), 0 for the
# function itself, a label (one without an address is listed as
# "optimized out"), a type or an enumerator.
GDB_SCOPE = r"""
import gdb
def enumerator(s):
    t = s.type.strip_typedefs() if s.type is not None else None
    return t is not None and t.code == gdb.TYPE_CODE_ENUM and any(f.name == s.name for f in t.fields())
for va in VAS:
    keep = []
    try:
        b = gdb.block_for_pc(va)
        while b is not None:
            keep += [int((s.is_argument or s.is_variable or (s.is_constant and not enumerator(s))) and
                         s.addr_class != gdb.SYMBOL_LOC_LABEL and not (s.type is not None and str(s.type) == "__CORE_ADDR")) for s in b]
            if b.function is not None:
                break
            b = b.superblock
        text = gdb.execute("info scope *%#x" % va, to_string=True)
    except Exception as e:
        text = "gdb error: %s" % e
    print("@@%#x %s" % (va, "".join(map(str, keep))))
    print(text)
"""


def gdb_scopes(elf, vaddrs):
    """gdb's `info scope *ADDR` for each address: [(name, class)] of the
    innermost function or inlined call's block and the blocks around
    ADDR inside it, classes as gen-dwarf.js --locals names them; None
    where gdb fails."""
    with tempfile.TemporaryDirectory() as tmp:
        py = os.path.join(tmp, "scope.py")
        pathlib.Path(py).write_text(f"VAS = {list(vaddrs)!r}\n" + GDB_SCOPE)
        out = subprocess.run(["gdb", "-nx", "-batch", "-ex", "set width 0", "-ex", "set pagination off", "-x", py, elf],
                             capture_output=True, text=True).stdout

    def cls(desc, at):
        desc = desc.strip()
        if desc.startswith("multi-location:"):
            for m in re.finditer(r"Range (0x[0-9a-f]+)-(0x[0-9a-f]+): (.*?)(?=\n *(?:Base address 0x[0-9a-f]+ +)?Range |\Z)", desc, re.S):
                if int(m.group(1), 16) <= at < int(m.group(2), 16):
                    return cls(m.group(3), at)
            return "out"
        if desc.startswith("optimized out"):
            return "out"
        if "piece" in desc:
            return "pieces"
        m = re.match(r"a variable in \$(\w+)", desc)
        if m:
            return "reg:" + m.group(1).upper()
        if desc.startswith("a constant") or desc.startswith("constant"):
            return "const"
        return "expr"

    res = {}
    for part in out.split("\n@@")[0:]:
        part = part[2:] if part.startswith("@@") else part
        head, _, body = part.partition("\n")
        f = head.split()
        if not f:
            continue
        va, keep = int(f[0], 16), f[1] if len(f) > 1 else ""
        if body.startswith("gdb error: "):
            # gdb's own info scope fails at some addresses (e.g. "Corrupted
            # DWARF2 expression"): nothing to compare there.
            res[va] = None
            continue
        syms = re.findall(r"^Symbol (\S+) is (.*?)(?=^Symbol |\Z)", body, re.S | re.M)
        if len(syms) != len(keep):
            res[va] = None
            continue
        res[va] = [(n, cls(d, va)) for (n, d), k in zip(syms, keep) if k == "1"]
    return res


def dwarfdump_tree(elf, off):
    """llvm-dwarfdump's subtree at DIE off: [(offset, depth, tag, text)]."""
    tree = subprocess.run(["llvm-dwarfdump", f"--debug-info={off:#x}", "-c", elf], capture_output=True, text=True).stdout
    return [(int(m.group(1), 16), len(m.group(2)), m.group(3), m.group(4))
            for m in re.finditer(r"^(0x[0-9a-f]+):( +)DW_TAG_(\w+)(.*?)(?=^0x[0-9a-f]+: |\Z)", tree, re.M | re.S)]


def dwarfdump_blocks_without_origin(elf, va, func=None):
    """The lexical blocks without DW_AT_abstract_origin in the innermost
    function or inlined call at va (named func, when given: a caller's
    frame) (llvm-dwarfdump --lookup, then its
    subtree; blocks of nested inlined calls left out): [(ranges, names)],
    names being the variables and parameters of the abstract block the
    block's own variables refer to (DW_AT_abstract_origin), nested blocks
    included."""
    look = subprocess.run(["llvm-dwarfdump", f"--lookup={va:#x}", elf], capture_output=True, text=True).stdout
    deepest = re.findall(r"^(0x[0-9a-f]+): +DW_TAG_(?!compile_unit)", look, re.M)
    if not deepest:
        return []
    # --lookup shows the innermost DIEs only; -p gives the chain around them.
    chain = subprocess.run(["llvm-dwarfdump", f"--debug-info={deepest[-1]}", "-p", elf], capture_output=True, text=True).stdout
    fn = [(m.group(1), m.group(2)) for m in re.finditer(
        r"^(0x[0-9a-f]+): +DW_TAG_(?:subprogram|inlined_subroutine)(.*?)(?=^0x[0-9a-f]+: |\Z)", chain, re.M | re.S)]
    if func is not None:
        fn = [(o, t) for o, t in fn if re.search(r'DW_AT_(?:name|abstract_origin|specification)\s+\((?:0x[0-9a-f]+ )?"' + re.escape(func) + '"', t)]
    if not fn:
        return []
    dies = dwarfdump_tree(elf, int(fn[-1][0], 16))
    if not dies:
        return []
    m = re.search(r"DW_AT_abstract_origin\s+\((0x[0-9a-f]+)", dies[0][3])
    ab = dwarfdump_tree(elf, int(m.group(1), 16)) if m else []
    at = {o: k for k, (o, *_) in enumerate(ab)}

    def under(ds, k):
        d = ds[k][1]
        for x in ds[k + 1:]:
            if x[1] <= d:
                break
            yield x
    out = []
    for k, (off, d, tag, text) in enumerate(dies):
        if tag != "lexical_block" or "DW_AT_abstract_origin" in text:
            continue
        up, nested = d, False
        for _, dd, t, _ in reversed(dies[:k]):
            if dd < up:
                up = dd
                if t == "inlined_subroutine" and dd > dies[0][1]:
                    nested = True
        if nested:
            continue
        ranges = [(int(x, 16), int(y, 16)) for x, y in re.findall(r"\[(0x[0-9a-f]+), (0x[0-9a-f]+)\)", text.split("DW_AT_ranges", 1)[1])] \
            if "DW_AT_ranges" in text else []
        lo = re.search(r"DW_AT_low_pc\s+\((0x[0-9a-f]+)\)", text)
        hi = re.search(r"DW_AT_high_pc\s+\((0x[0-9a-f]+)\)", text)
        if lo and hi:
            ranges.append((int(lo.group(1), 16), int(hi.group(1), 16)))
        names = set()
        for _, _, t, tx in under(dies, k):
            m = re.search(r"DW_AT_abstract_origin\s+\((0x[0-9a-f]+)", tx)
            if t not in ("variable", "formal_parameter") or not m or int(m.group(1), 16) not in at:
                continue
            j = at[int(m.group(1), 16)]
            # The abstract block holding the origin, and everything under it.
            p = next((i for i in range(j - 1, -1, -1) if ab[i][1] < ab[j][1]), None)
            if p is not None and ab[p][2] == "lexical_block":
                for _, _, t2, tx2 in under(ab, p):
                    n = re.search(r'DW_AT_name\s+\("([^"]+)"\)', tx2)
                    if t2 in ("variable", "formal_parameter") and n:
                        names.add(n.group(1))
        out.append((ranges, names))
    return out


def gdb_extra_explained(elf, va, theirs, mine, func=None):
    """Whether gdb's list at va is the page's plus variables gdb lists as
    optimized out from an abstract lexical block that a concrete block
    without DW_AT_abstract_origin stands for (gdb's inherit_abstract_dies
    only follows the block's own DW_AT_abstract_origin, which clang does
    not emit): each extra one is a variable of such a block that does not
    cover va (out of scope there), or that covers va and the page lists
    (gdb lists it twice)."""
    extra = list(theirs)
    for x in mine:
        if x not in extra:
            return False
        extra.remove(x)
    if not extra or any(c != "out" for _, c in extra):
        return False
    blocks = dwarfdump_blocks_without_origin(elf, va, func)
    for name, _ in extra:
        if not any(name in names and (not any(a <= va < b for a, b in ranges) or any(nm == name for nm, _ in mine))
                   for ranges, names in blocks):
            return False
    return True


def check_locals(elf, shards, n, rnd, lds, fns):
    """N random addresses inside functions: the variables the page lists
    for the innermost frame there (gen-dwarf.js --locals: dwarf.js
    dwarfScopesAt / dwarfLocalFrames / dwLocAt on the locals-*.json.gz
    shards) must be the ones gdb's info scope lists, each with the same
    kind of location there (none, which register, constant, other)."""
    def phys(va):
        return next((va - lo + pa for lo, hi, pa in lds if lo <= va < hi), None)
    picks = []
    for _ in range(n):
        a, size = rnd.choice(fns)
        va = a + rnd.randrange(size)
        if phys(va) is not None:
            picks.append(va)
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--locals", shards], input="\n".join(str(phys(a)) for a in picks),
                         capture_output=True, text=True, check=True).stdout
    got = [json.loads(l) for l in out.splitlines()]
    want = gdb_scopes(elf, picks)
    bad, nvars, located, failed, nofn, blockvars = [], 0, 0, 0, 0, 0
    for va, g in zip(picks, got):
        if want.get(va, []) is None:
            failed += 1
            continue
        if not g["frames"]:
            # No DWARF function there (assembly): gdb lists the file's
            # static block instead, the page has no frame to show.
            nofn += 1
            continue
        mine = sorted((v[0], v[1]) for v in g["frames"][0]["vars"]) if g["frames"] else []
        theirs = sorted(want.get(va, []))
        nvars += len(theirs)
        located += sum(1 for _, c in theirs if c != "out")
        if mine != theirs:
            if gdb_extra_explained(elf, va, theirs, mine):
                blockvars += 1
                nvars -= len(theirs) - len(mine)
                continue
            bad.append((va, theirs, mine, g["frames"][0]["func"] if g["frames"] else None))
    for va, w, g, fn in bad[:10]:
        print(f"LOCALS MISMATCH {va:#x} in {fn}\n  gdb  {w}\n  page {g}")
    print(f"{elf}: {len(picks) - len(bad) - failed - nofn}/{len(picks) - failed - nofn} addresses list the same parameters and locals "
          f"as gdb's info scope ({nvars} variables, {located} with a location there; {nofn} more outside DWARF functions, "
          f"gdb's info scope failed at {failed}; at {blockvars} of them gdb also lists, as optimized out, variables of a block "
          f"whose concrete copy has no DW_AT_abstract_origin: out of scope there or listed twice, checked with llvm-dwarfdump)")
    return bad


def write_core(path, regs, pages):
    """An i386 ELF core file: one NT_PRSTATUS with the registers (elf_prstatus,
    pr_reg = ebx ecx edx esi edi ebp eax ds es fs gs orig_eax eip cs eflags esp ss)
    and a PT_LOAD per 4 KiB page of memory, at its virtual address."""
    order = ["ebx", "ecx", "edx", "esi", "edi", "ebp", "eax", "ds", "es", "fs", "gs", None, "eip", "cs", "eflags", "esp", "ss"]
    prreg = struct.pack("<17I", *[(regs.get(k, 0) if k else 0xffffffff) & 0xffffffff for k in order])
    prstatus = bytes(72) + prreg + bytes(4)
    note = struct.pack("<III", 5, len(prstatus), 1) + b"CORE\0\0\0\0" + prstatus
    data = [base64.b64decode(b) for _, b in pages]
    phnum = 1 + len(pages)
    off = 52 + 32 * phnum
    ph = [struct.pack("<8I", 4, off, 0, 0, len(note), 0, 0, 4)]
    off += len(note)
    for (va, _), d in zip(pages, data):
        ph.append(struct.pack("<8I", 1, off, va, 0, len(d), len(d), 6, 4096))
        off += len(d)
    eh = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + bytes(9), 4, 3, 1, 0, 52, 0, 0, 52, 32, phnum, 40, 0, 0)
    pathlib.Path(path).write_bytes(eh + b"".join(ph) + note + b"".join(data))



# Runs inside gdb: ADDRS (absolute operands) get `info symbol`; for each
# [core, page path or ""] of CORES (one per stack operand: a core with
# EIP at the instruction and made-up ESP/EBP) the parameters and locals
# of the frame there, inlined calls included, that have an address: [name,
# address, size, frame], and [frame, address, size] of the page's path in
# each frame where it is an lvalue.
GDB_OPERANDS = r"""
import gdb, json
syms = []
for a in ADDRS:
    try:
        syms.append(gdb.execute("info symbol %d" % a, to_string=True).strip())
    except Exception as e:
        syms.append("error: %s" % e)
stack = []
for core, path in CORES:
    try:
        gdb.execute("core-file " + core, to_string=True)
        f, held, frames = gdb.newest_frame(), [], []
        while f is not None:
            frames.append(f)
            if f.type() != gdb.INLINE_FRAME:
                break
            f = f.older()
        for fi, f in enumerate(frames):
            try:
                b = f.block()
            except RuntimeError:
                continue
            while b is not None:
                for s in b:
                    if not (s.is_argument or s.is_variable) or s.addr_class in (gdb.SYMBOL_LOC_STATIC, gdb.SYMBOL_LOC_LABEL):
                        continue
                    try:
                        v = s.value(f)
                        if v.address is not None:
                            held.append([s.name, int(v.address), v.type.sizeof, fi])
                    except gdb.error:
                        pass
                if b.function is not None:
                    break
                b = b.superblock
        # The page's path in each frame (a name may be another variable in an inlined call).
        at = []
        for fi, f in enumerate(frames if path else []):
            try:
                f.select()
                v = gdb.parse_and_eval(path)
                if v.address is not None:
                    at.append([fi, int(v.address), v.type.sizeof])
            except gdb.error:
                pass
        stack.append([held, at])
    except Exception as e:
        stack.append("error: %s" % e)
json.dump([syms, stack], open(DST, "w"))
"""

# Stack operands the page can name (base ESP or EBP, no index) and
# absolute ones, as llvm-objdump prints them.
OP_STACK = re.compile(r"(?:^|[\s,])(-?0x[0-9a-f]+)?\(%(esp|ebp)\)(?:,|$)")
OP_ABS = re.compile(r"(?:^|[\s,*]|%[fg]s:)(-?0x[0-9a-f]+)(?:,|$)")


def check_operands(elf, shards, n, rnd, lds):
    """N random instructions with a memory operand, half on the stack
    (disp(%esp), disp(%ebp)), half at an absolute address. Absolute: the
    variable or ELF symbol the page names (`gen-dwarf.js --operands`,
    asmnames.js) and its offset must be what gdb's `info symbol` says, and
    nothing named where gdb has no symbol. Stack: on a core with EIP at
    the instruction and made-up ESP and EBP, gdb computes the address of
    every parameter and local there (its own CFI unwinder, frame base and
    location lists); the operand's address must lie in the variable the
    page names, the page's member path must be at that byte, and the page
    must name one where gdb has a variable holding the operand."""
    dump = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", elf], capture_output=True, text=True, check=True).stdout
    stack, absl = [], []
    branch = re.compile(r"^(j|call|loop)")
    for line in dump.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+(\S.*)$", line)
        if not m:
            continue
        text = m.group(2).split("#")[0].split(" <")[0].strip()
        ops = text.split(None, 1)[1] if " " in text or "\t" in text else ""
        if OP_STACK.search(ops):
            stack.append((int(m.group(1), 16), text))
        elif not branch.match(text) and OP_ABS.search(ops) and "$" not in OP_ABS.search(ops).group(0):
            absl.append((int(m.group(1), 16), text))
    pick = rnd.sample(stack, min(n // 2, len(stack))) + rnd.sample(absl, min(n - n // 2, len(absl)))

    def phys(va):
        return next((va - lo + pa for lo, hi, pa in lds if lo <= va < hi), None)
    pick = [(va, t) for va, t in pick if phys(va) is not None]
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--operands", shards],
                         input="".join(f"{phys(va)} {va} {t.replace(chr(9), ' ')}\n" for va, t in pick),
                         capture_output=True, text=True, check=True).stdout
    got = [json.loads(line) for line in out.splitlines()]
    # ESP and EBP far apart: a variable at ESP+k never holds an operand at EBP+k'.
    esp, ebp = 0xc7f00800, 0xc7f80800
    with tempfile.TemporaryDirectory() as tmp:
        addrs, cores, which = [], [], []
        for k, ((va, text), g) in enumerate(zip(pick, got)):
            ms = OP_STACK.search(text.split(None, 1)[1])
            if ms:
                ea = ((esp if ms.group(2) == "esp" else ebp) + int(ms.group(1) or "0", 16)) & 0xffffffff
                mine = [x for x in g["names"] if re.search(r"\(%e[sb]p\)$", x["op"])]
                core = os.path.join(tmp, f"c{k}")
                write_core(core, {"eip": va, "esp": esp, "ebp": ebp, "cs": 0x60, "ss": 0x68, "ds": 0x7b, "es": 0x7b, "eflags": 0x202},
                           [(r & ~0xfff, base64.b64encode(bytes(4096)).decode()) for r in (esp, ebp)])
                path = mine[0]["name"].lstrip("&") if mine else ""
                path = re.sub(r"\+\d+$", "", path)
                cores.append([core, path])
                which.append(("stack", va, text, ea, mine[0] if mine else None))
            else:
                a = int(OP_ABS.search(text.split(None, 1)[1]).group(1), 16) & 0xffffffff
                mine = [x for x in g["names"] if not re.search(r"\(", x["op"])]
                addrs.append(a)
                which.append(("abs", va, text, a, mine[0] if mine else None))
        dst, py = os.path.join(tmp, "out.json"), os.path.join(tmp, "ops.py")
        pathlib.Path(py).write_text(f"ADDRS, CORES, DST = {addrs!r}, {cores!r}, {dst!r}\n" + GDB_OPERANDS)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", "source " + py, elf], capture_output=True, text=True)
        syms, frames = json.load(open(dst))
    bad, named, nstack, nabs, gdbonly, failed, errors, nearest = [], {"stack": 0, "abs": 0}, 0, 0, 0, 0, [], 0
    nm = subprocess.run(["llvm-nm", "-S", "--defined-only", elf], capture_output=True, text=True, check=True).stdout
    sized = {f[3] for f in (line.split() for line in nm.splitlines()) if len(f) == 4 and int(f[1], 16) > 0}
    # name -> addresses, and the marks without a size (linker symbols) per address.
    nm_at, marks = {}, {}
    for f in (line.split() for line in nm.splitlines()):
        if len(f) >= 3:
            nm_at.setdefault(f[-1], set()).add(int(f[0], 16))
            if len(f) == 3 or int(f[1], 16) == 0:
                marks.setdefault(int(f[0], 16), set()).add(f[-1])
    secs = [(int(m.group(1), 16), int(m.group(2), 16)) for m in re.finditer(
        r"^\s*\[\s*\d+\]\s+\S+\s+\S+\s+([0-9a-f]{8})\s+[0-9a-f]+\s+([0-9a-f]+)\s+\S+\s+\S*A", subprocess.run(
            ["llvm-readelf", "-SW", elf], capture_output=True, text=True, check=True).stdout, re.M)]
    aliased = static = nosec = 0
    ia = istack = 0
    for kind, va, text, ea, mine in which:
        if kind == "abs":
            s = syms[ia]
            ia += 1
            nabs += 1
            m = re.match(r"^(\S+)(?: \+ (\d+))? in section", s)
            if not m:
                # gdb's info symbol looks only inside sections; a linker mark
                # between two sections (_etext, _edata) is the page's name there.
                if mine and s.startswith("No symbol matches") and mine["kind"] == "sym" and mine["varOff"] == 0 and \
                        set(mine.get("aliases") or [mine["var"]]) == marks.get(ea, set()) and \
                        not any(lo <= ea < lo + size for lo, size in secs if size):
                    nosec += 1
                    continue
                if mine:
                    bad.append((hex(va), text, "page", mine["name"], "gdb", s))
                continue
            if not mine:
                if m.group(1) not in sized:
                    nearest += 1  # a symbol without a size (a linker mark; trimmed from a distro's symbols.json), or gdb's nearest one before
                else:
                    bad.append((hex(va), text, "page names nothing", "gdb", s))
                continue
            named["abs"] += 1
            off = int(m.group(2) or 0)
            if mine["varOff"] == off and mine.get("aliases") and m.group(1) in mine["aliases"] and \
                    set(mine["aliases"]) == marks.get(ea, set()):
                # Linker marks at one address: the page names all of them,
                # gdb picks one.
                aliased += 1
                continue
            if mine["varOff"] == off and mine.get("scope") and m.group(1).startswith(mine["scope"]) and \
                    (ea - off) in nm_at.get(mine["scope"] + "." + mine["var"], ()):
                # A static local: clang's ELF symbol is function.name, which
                # gdb prints demangled in odd ways (pt_regs_nr..dummy for
                # pt_regs_nr.__dummy, sanitize_boot_params[scratch]); the
                # page names the C variable.
                static += 1
                continue
            if not (mine["var"] == m.group(1) or re.fullmatch(re.escape(mine["var"]) + r"\.\d+", m.group(1))) or mine["varOff"] != off:
                bad.append((hex(va), text, "page", mine["name"], mine["var"], mine["varOff"], "gdb", s))
            continue
        fr = frames[istack]
        istack += 1
        nstack += 1
        if isinstance(fr, str):
            failed += 1
            if len(errors) < 3:
                errors.append((hex(va), text, fr))
            continue
        held, at = fr
        hold = [h for h in held if h[1] <= ea < h[1] + max(h[2], 1)]
        holders = {h[0] for h in hold}
        if not mine:
            if holders:
                gdbonly += 1
                bad.append((hex(va), text, "page names nothing", "gdb", sorted(holders)))
            continue
        named["stack"] += 1
        root = re.match(r"&?([A-Za-z_]\w*)", mine["name"]).group(1)
        if root not in holders:
            bad.append((hex(va), text, "page", mine["name"], "gdb", sorted(holders), held))
        elif not any(a[1] <= ea < a[1] + max(a[2], 1) for a in at if any(h[0] == root and h[3] == a[0] for h in hold)):
            bad.append((hex(va), text, "page path", mine["name"], "gdb", at, hex(ea)))
    for b in bad[:12]:
        print("OPERAND MISMATCH", b)
    for e in errors:
        print("gdb failed:", e)
    print(f"{elf}: {len(pick) - len(bad) - failed}/{len(pick) - failed} memory operands agree with gdb ({nabs} absolute, "
          f"{named['abs']} named, {nearest} named by gdb from a symbol without a size; {nstack} on the stack, {named['stack']} named as a parameter or local; "
          f"{gdbonly} gdb names and the page does not; gdb failed on {failed} cores; {aliased} linker marks sharing an address that the page "
          f"names all of and gdb one of, {static} static locals gdb names by clang's function.name ELF symbol, {nosec} linker marks between "
          f"sections where gdb has no symbol, each checked with llvm-nm)")
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


# Runs inside gdb: for each [name, address, outermost] the function block
# named name around the address (outermost: the function itself, else an
# inlined copy starting there) -> [ptype text, declaring file, line,
# whether gdb's global symbol of that name is this function (C external
# linkage)], or ["error", message].
GDB_FUNCS = r"""
import json, gdb
src, dst = json.load(open(SRC)), []
for name, addr, outermost in src:
    try:
        b, hit = gdb.block_for_pc(addr), None
        while b is not None and not b.is_static and not b.is_global:
            if b.function is not None and b.function.name == name and (outermost or b.start == addr):
                hit = b
                if not outermost:
                    break
            b = b.superblock
        if hit is None:
            raise gdb.error("no block of %s at %#x" % (name, addr))
        f = hit.function
        g = gdb.lookup_global_symbol(name)
        ext = g is not None and g.symtab is not None and f.symtab is not None and (g.symtab.filename, g.line) == (f.symtab.filename, f.line)
        dst.append([str(f.type), f.symtab.filename if f.symtab else "", f.line, ext])
    except Exception as x:
        dst.append(["error", str(x)])
json.dump(dst, open(DST, "w"))
"""


def gdb_funcs(elf, probes):
    with tempfile.TemporaryDirectory() as tmp:
        src, dst, py = (os.path.join(tmp, n) for n in ("in.json", "out.json", "funcs.py"))
        json.dump(probes, open(src, "w"))
        with open(py, "w") as f:
            f.write(f"SRC, DST = {src!r}, {dst!r}\n" + GDB_FUNCS)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "source " + py, elf], check=True, capture_output=True)
        return json.load(open(dst))


def dwarfdump_inline_counts(elf, strip):
    """{(name, file, line): DW_TAG_inlined_subroutine DIEs with code (a non-empty range) whose abstract origin (through
    abstract_origin / specification) declares that function}, from llvm-dwarfdump --debug-info."""
    dies, cur, in_ranges = {}, None, False
    p = subprocess.Popen(["llvm-dwarfdump", "--debug-info", elf], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    for line in p.stdout:
        m = re.match(r"^0x([0-9a-f]+):\s+(DW_TAG_\w+)", line)
        if m:
            cur = dies[int(m.group(1), 16)] = {"tag": m.group(2)}
            continue
        r = re.match(r"^\s+\[(0x[0-9a-f]+), (0x[0-9a-f]+)\)", line)
        if r and in_ranges and cur is not None:
            cur["code"] = cur.get("code") or int(r.group(2), 16) > int(r.group(1), 16)
            continue
        m = re.match(r"^\s+DW_AT_(\w+)\s+\((.*?)\)?$", line)
        in_ranges = bool(m) and m.group(1) == "ranges"
        if m and cur is not None:
            k, v = m.groups()
            if k in ("abstract_origin", "specification"):
                cur[k] = int(v.split()[0], 16)
            elif k in ("name", "decl_file"):
                cur[k] = v.strip('"')
            elif k == "decl_line":
                cur[k] = int(v)
            elif k in ("low_pc", "high_pc"):
                cur[k] = int(v.split()[0], 16)
                cur["code"] = cur.get("code") or cur.get("high_pc", 0) > cur.get("low_pc", 1 << 40)
        elif not line.strip():
            cur = None
    p.wait()

    def key(off):
        name = file = line = None
        for _ in range(8):
            d = dies.get(off)
            if d is None:
                break
            name = name or d.get("name")
            if file is None and "decl_file" in d:
                file, line = norm(d["decl_file"], strip), d.get("decl_line", 0)
            off = d.get("abstract_origin", d.get("specification"))
            if off is None:
                break
        return name, file, line
    counts = {}
    for d in dies.values():
        if d["tag"] == "DW_TAG_inlined_subroutine" and d.get("code") and "abstract_origin" in d:
            k = key(d["abstract_origin"])
            counts[k] = counts.get(k, 0) + 1
    return counts


def dwarfdump_named(elf, names, strip):
    """llvm-dwarfdump -c --name for each name (one pass over .debug_info):
    the DIEs named so directly or through DW_AT_abstract_origin, and all
    their children, by offset: {offset: {tag, name, file, line, origin,
    code, type, params: [{name, type}], varargs}} (params of a
    subprogram: its formal_parameter children; addr: a fixed location,
    DW_OP_addr [+ DW_OP_plus_uconst]); type texts as llvm-dwarfdump
    prints them, None for no DW_AT_type."""
    cmd = ["llvm-dwarfdump", "-c"] + ["--name=" + n for n in names] + [elf]
    dies, stack = {}, []
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    cur, in_ranges = None, False
    for line in p.stdout:
        m = re.match(r"^0x([0-9a-f]+):( *)(DW_TAG_\w+|NULL)", line)
        if m:
            depth = len(m.group(2)) // 2
            del stack[depth:]
            if m.group(3) == "NULL":
                cur = None
                continue
            cur = dies.setdefault(int(m.group(1), 16), {"tag": m.group(3), "type": None, "params": [], "varargs": False, "code": False})
            parent = stack[-1] if stack else None
            if parent is not None and parent["tag"] == "DW_TAG_subprogram" and cur is not parent:
                if m.group(3) == "DW_TAG_formal_parameter" and cur not in parent["params"]:
                    parent["params"].append(cur)
                elif m.group(3) == "DW_TAG_unspecified_parameters":
                    parent["varargs"] = True
            stack.append(cur)
            continue
        if cur is None:
            continue
        r = re.match(r"^\s+\[(0x[0-9a-f]+), (0x[0-9a-f]+)\)", line)
        if r and in_ranges:
            cur["code"] = cur["code"] or int(r.group(2), 16) > int(r.group(1), 16)
            continue
        # A multi-line value (DW_AT_ranges, location lists) ends on later lines.
        m = re.match(r"^\s+DW_AT_(\w+)\s+\((.*?)\)?$", line)
        in_ranges = bool(m) and m.group(1) == "ranges"
        if not m:
            continue
        k, v = m.groups()
        if k == "type":
            cur["type"] = v.split('"', 1)[1][:-1] if '"' in v else v
        elif k == "name":
            cur["name"] = v.strip('"')
        elif k == "decl_file":
            cur["file"] = norm(v.strip('"'), strip)
        elif k == "decl_line":
            cur["line"] = int(v)
        elif k in ("abstract_origin", "specification"):
            cur.setdefault("origin", int(v.split()[0], 16))
        elif k in ("low_pc", "high_pc"):
            cur[k] = int(v.split()[0], 16)
            cur["code"] = cur["code"] or cur.get("high_pc", 0) > cur.get("low_pc", 1 << 40)
        elif k == "location":
            a = re.fullmatch(r"DW_OP_addr 0x([0-9a-f]+)(?:, DW_OP_plus_uconst 0x([0-9a-f]+))?", v)
            if a:
                cur["addr"] = int(a.group(1), 16) + int(a.group(2) or "0", 16)
    p.wait()
    return dies


GDB_VARS = r"""
import json, gdb
src, dst = json.load(open(SRC)), []
def at(sym):
    try:
        a = sym.value().address
        return int(a) if a is not None else None
    except Exception:
        return None
for name, fn, addr, qual, *deep in src:
    r = {"file": None, "line": None, "why": None, "qual": None}
    try:
        cands = []
        if fn:
            f = gdb.lookup_global_symbol(fn) or gdb.lookup_static_symbol(fn)
            if f is not None and f.type is not None and f.type.code == gdb.TYPE_CODE_FUNC:
                b = gdb.block_for_pc(int(f.value().address))
                s = gdb.lookup_symbol(name, b)[0] if b is not None else None
                cands = [s] if s is not None else []
        else:
            cands = [x for x in [gdb.lookup_global_symbol(name)] + list(gdb.lookup_static_symbols(name)) if x is not None]
        hit = [x for x in cands if x.is_variable and at(x) == addr]
        if hit:
            r["file"] = hit[0].symtab.filename if hit[0].symtab else None
            r["line"] = hit[0].line
        elif deep and deep[0] and not fn:
            # The global symbol gdb finds may be a declaration (extern in a
            # header): the definitions `info variables` lists, each placed
            # by &'file'::name (about 2 s per name on Debian's vmlinux).
            out, f = gdb.execute("info variables -q -n ^%s$" % name, to_string=True), None
            for l in out.splitlines():
                if l.startswith("File "):
                    f = l[5:-1]
                elif f and l.split(":")[0].isdigit() and r["file"] is None:
                    try:
                        if int(gdb.parse_and_eval("&'%s'::%s" % (f, name))) == addr:
                            r["file"], r["line"] = f, int(l.split(":")[0])
                    except Exception:
                        pass
            if r["file"] is None:
                r["why"] = "gdb has no symbol %s at %#x (%d of that name; info variables: %r)" % (name, addr, len(cands), out[:200])
        else:
            r["why"] = "gdb has no symbol %s at %#x (%d of that name)" % (name, addr, len(cands))
    except Exception as x:
        r["why"] = str(x)
    if qual:
        try:
            r["qual"] = int(gdb.parse_and_eval("&'%s'::%s" % (qual, name)))
        except Exception as x:
            r["qual"] = "error: " + str(x)
    dst.append(r)
json.dump(dst, open(DST, "w"))
"""


def gdb_vars(elf, probes):
    """[name, function or "" (a static local), virtual address, file qualifier or "", optionally deep] -> gdb's view
    of that variable: {file, line (where gdb's symbol at that address is declared), why (none found), qual
    (&'file'::name)}; deep: when gdb's symbol of that name is not the variable (a declaration), the definition
    `info variables` lists at that address (slow)."""
    with tempfile.TemporaryDirectory() as tmp:
        src, dst, py = (os.path.join(tmp, n) for n in ("in.json", "out.json", "vars.py"))
        json.dump(probes, open(src, "w"))
        with open(py, "w") as f:
            f.write(f"SRC, DST = {src!r}, {dst!r}\n" + GDB_VARS)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "source " + py, elf], check=True, capture_output=True)
        return json.load(open(dst))


def page_vars(shards, exprs):
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--vars", shards], input="\n".join(exprs),
                         capture_output=True, text=True, check=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def check_vars(elf, shards, n, rnd, lds, strip):
    """Where types.json's variables are defined, and file::name picking, against gdb; returns the problems."""
    T = json.load(open(os.path.join(shards, "types.json")))
    named = [v for v in T["vars"] if v[0]]
    count = {}
    for v in named:
        count[v[0]] = count.get(v[0], 0) + 1
    if not named:
        print(f"{elf}: vars: no named variable: nothing to check")
        return []
    if "files" not in T:
        print(f"VARS {shards}/types.json has no files: no variable says where it is defined")
        return [f"{shards}/types.json has no files"]

    def virt(pa):
        return next(pa - p0 + lo for lo, hi, p0 in lds if p0 <= pa < p0 + hi - lo)
    shared = [v for v in named if count[v[0]] > 1]
    pick = rnd.sample(named, min(n - n // 2, len(named))) + (rnd.sample(shared, min(n // 2, len(shared))) if shared else [])
    names = sorted({v[0] for v in pick})
    page = {r["expr"]: r for r in page_vars(shards, names)}
    bad, probes, nshared = [], [], 0
    for v in pick:
        r = page.get(v[0])
        d = next((x for x in r["defs"] if x["pa"] == v[1]), None) if r else None
        if d is None:
            bad.append(f"{v[0]} at {v[1]:#x}: gen-dwarf.js --vars does not list it")
            continue
        fid = v[7] if len(v) > 7 else -1
        if (d["file"], d["line"]) != ((T["files"][fid], v[8]) if fid >= 0 else (None, None)):
            bad.append(f"{v[0]} at {v[1]:#x}: --vars says {d['file']}:{d['line']}, types.json {v[7:]}")
        if len(r["defs"]) != count[v[0]]:
            bad.append(f"{v[0]}: --vars lists {len(r['defs'])} definitions, types.json has {count[v[0]]}")
        # A file-static that shares its name: the page's file::name picks it when its file is the only one.
        qual = ""
        if count[v[0]] > 1 and not v[5] & 4 and d["file"] and sum(1 for x in r["defs"] if x["file"] == d["file"]) == 1:
            qual = d["file"]
            nshared += 1
        probes.append((v, d, qual, [v[0], v[6] if v[5] & 4 and len(v) > 6 else "", virt(v[1]), qual]))
    # The qualified names: the full path and the shortest unique suffix of it.
    quals = []
    for v, d, qual, _ in probes:
        if qual:
            parts = qual.split("/")
            others = [x["file"] for x in page[v[0]]["defs"] if x["file"] and x["file"] != qual]
            short = next("/".join(parts[k:]) for k in range(len(parts) - 1, -1, -1)
                         if not any(o == "/".join(parts[k:]) or o.endswith("/" + "/".join(parts[k:])) for o in others))
            quals += [qual + "::" + v[0], short + "::" + v[0]]
    qpage = {r["expr"]: r for r in page_vars(shards, quals)} if quals else {}
    got = gdb_vars(elf, [p for *_, p in probes])
    # What gdb does not find by name (a static local in an inner block or in
    # a function that only exists inlined): llvm-dwarfdump --name, the DIE
    # whose DW_OP_addr location is the variable's address, each attribute
    # through DW_AT_abstract_origin / specification when the DIE lacks it.
    dd = dwarfdump_named(elf, sorted({v[0] for (v, *_), g in zip(probes, got) if g["why"]}), strip)
    by_addr = {}
    for off, x in dd.items():
        if "addr" in x and x["tag"] == "DW_TAG_variable":
            by_addr.setdefault(x["addr"], []).append(off)

    def decl(off):
        file = line = name = None
        for _ in range(8):
            x = dd.get(off)
            if x is None:
                break
            name, file, line = name or x.get("name"), file or x.get("file"), line or x.get("line")
            off = x.get("origin")
            if off is None:
                break
        return name, file, line
    agree = unknown = qok = by_dd = 0
    nalso = 0
    for (v, d, qual, _), g in zip(probes, got):
        # Several units may define the variable at its address (a __weak definition and the one the linker kept).
        places = {(d["file"], d["line"] or None)} | {(f, ln) for f, ln in d["also"]}
        nalso += bool(d["also"])
        if g["why"]:
            hits = {decl(off)[1:] for off in by_addr.get(virt(v[1]), []) if decl(off)[0] == v[0]}
            if not hits:
                unknown += 1
            elif hits != places:
                bad.append(f"{v[0]} at {v[1]:#x}: page defined at {places}, llvm-dwarfdump {hits}")
            else:
                by_dd += 1
        elif not g["line"] and not d["file"]:
            # No DW_AT_decl_file / decl_line (GCC's __func__): gdb puts the symbol in its unit's file at line 0.
            agree += 1
        elif (norm(g["file"], strip) if g["file"] else None, g["line"] or None) not in places:
            bad.append(f"{v[0]} at {v[1]:#x}: page defined at {places}, gdb {g['file']}:{g['line']}")
        else:
            agree += 1
        if qual:
            for q in {qual + "::" + v[0], quals[quals.index(qual + "::" + v[0]) + 1]}:
                pr = qpage.get(q)
                if not pr or not pr.get("pick") or pr["pick"]["pa"] != v[1]:
                    bad.append(f"{q}: page picks {pr and (pr.get('pick') or pr.get('why'))}, want {v[1]:#x}")
            if not isinstance(g["qual"], int) or g["qual"] != virt(v[1]):
                bad.append(f"{qual}::{v[0]}: gdb &'file'::name is {g['qual']}, the page's variable is at {virt(v[1]):#x}")
            else:
                qok += 1
    # Refusals: a qualifier no definition matches; one several files end with.
    neg = [("no/such/file.c", names[0])]
    for name in sorted({v[0] for v in shared}):
        files = [x["file"] for x in page.get(name, {"defs": []})["defs"] if x["file"]]
        base = {}
        for f in files:
            base.setdefault(f.rsplit("/", 1)[-1], set()).add(f)
        amb = next((b for b, fs in sorted(base.items()) if len(fs) > 1), None)
        if amb:
            neg.append((amb, name))
            break
    for (q, name), r in zip(neg, page_vars(shards, [q + "::" + name for q, name in neg])):
        want = "ambiguous" if "/" not in q else "not defined in"
        if r.get("pick") or want not in (r.get("why") or ""):
            bad.append(f"{q}::{name}: page {r.get('pick') or r.get('why')!r}, want a refusal ({want})")
    if len(probes) and unknown > len(probes) // 10:
        bad.append(f"gdb and llvm-dwarfdump confirm only {len(probes) - unknown} of {len(probes)} sampled variables")
    for b in bad[:12]:
        print("VARS " + b)
    print(f"{elf}: vars: {len(named)} variables ({len(shared)} share a name), {len(pick)} sampled: {agree} defined-at = gdb, "
          f"{by_dd} = llvm-dwarfdump --name (gdb does not find them by name: static locals in inner blocks or inlined-only functions), "
          f"{unknown} neither confirms (clang's DW_OP_addrx, which llvm-dwarfdump prints as an index), {nalso} with more than one definition at their address, "
          f"{qok} file::name picks = gdb &'file'::name (+ {len(quals) // 2} shortest suffixes), {len(neg)} refusals; {len(bad)} problems")
    return bad

# GCC's base type names as the page (and gdb) spell them.
BASE_NAMES = [("long long unsigned int", "unsigned long long"), ("long long int", "long long"), ("long unsigned int", "unsigned long"),
              ("short unsigned int", "unsigned short"), ("long int", "long"), ("short int", "short")]


def ctype(t):
    """A C type text without struct/union/enum keywords, base names as gdb writes them, single spaces."""
    t = re.sub(r"\b(struct|union|enum) ", "", t or "void")
    for a, b in BASE_NAMES:
        t = re.sub(r"\b" + a + r"\b", b, t)
    return re.sub(r"\s+", " ", t).replace(" *", "*").replace("* ", "*").strip()


def frame_of_rows(rows):
    """The page's cfiFrameInfo from llvm-dwarfdump's rows of one FDE: (max ESP-based CFA offset, offset of
    the first EBP-based row or -1, its CFA-EBP, {register: first CFA offset saved})."""
    mx, fp, fpoff, saved = 0, -1, 0, {}
    lo = rows[0][0] if rows else 0
    for start, text in rows:
        m = re.match(r"CFA=(\w+)([+-]\d+)?", text)
        if m and m.group(1) == "ESP":
            mx = max(mx, int(m.group(2) or 0))
        elif m and m.group(1) == "EBP" and fp < 0:
            fp, fpoff = start - lo, int(m.group(2) or 0)
        for reg, off in re.findall(r"(\w+)=\[CFA([+-]\d+)\]", text):
            if reg != "EIP" and reg not in saved:
                saved[reg] = int(off)
    return mx, fp, fpoff, saved


def check_funcs(elf, shards, n, rnd, lds, strip):
    index = json.load(open(os.path.join(shards, "index.json")))
    entries = []
    for k in range(index["funcs"]["shards"]):
        F = json.loads(gzip.decompress(pathlib.Path(shards, f"funcs-{k:x}.json.gz").read_bytes()))
        entries += [(e, F["files"][e[1]] if e[1] >= 0 else None) for e in F["fns"]]

    def virt(pa):
        return next(pa - p0 + lo for lo, hi, p0 in lds if p0 <= pa < p0 + hi - lo)
    ool = [x for x in entries if x[0][7]]
    inl = [x for x in entries if not x[0][7] and x[0][8]]
    pick = rnd.sample(ool, min(n - n // 2, len(ool))) + rnd.sample(inl, min(n // 2, len(inl)))
    names = sorted({e[0] for e, _ in pick})
    out = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--funcs", shards], input="\n".join(names),
                         capture_output=True, text=True, check=True).stdout
    page = {}
    for line in out.splitlines():
        r = json.loads(line)
        page[r["name"]] = r["fns"]
    fdes = {f[0]: f for f in dwarfdump_cfi(elf)}
    syms = {}
    for line in subprocess.run(["llvm-nm", "--defined-only", elf], capture_output=True, text=True, check=True).stdout.splitlines():
        f = line.split()
        if len(f) == 3:
            syms.setdefault(int(f[0], 16), set()).add(f[2])
    bad, probes, gprobes, nframes, ncopies, nasm = [], [], [], 0, 0, 0
    # gdb versions space "char *const *" differently: compare without spaces.
    sp = lambda t: re.sub(r"\s+", "", t or "")
    for e, file in pick:
        g = next((x for x in page.get(e[0], []) if x["file"] == file and x["line"] == e[2]), None)
        if g is None:
            bad.append(f"{e[0]}: gen-dwarf.js --funcs does not list {file}:{e[2]}")
            continue
        if len(g["copies"]) != len(e[8]) or len(g["ool"]) != len(e[7]):
            bad.append(f"{e[0]}: the lookup lists other code than the shard")
        where = None
        for o in [r for parts in g["ool"] for r in parts]:
            va = virt(o["lo"])
            # gdb checks the copy with the function's own ELF symbol (a GCC
            # clone's DIE, foo.isra.0, lists only the parameters it kept).
            if where is None and e[0] in syms.get(va, ()):
                where = [e[0], va, True]
            f = fdes.get(va)
            fr = o["frame"]
            if f is None:
                if fr:
                    bad.append(f"{e[0]} at {va:#x}: no FDE in llvm-dwarfdump, the page has a frame {fr}")
                continue
            nframes += 1
            mx, fp, fpoff, saved = frame_of_rows(f[2])
            have = (fr["max"], fr["fp"], fr["fpOff"], {["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"][int(r)]: v
                                                       for r, v in fr["saved"].items()}) if fr and not fr.get("why") else fr
            if have != (mx, fp, fpoff, saved) or fr["fde"] != [o["lo"], o["lo"] + f[1] - f[0]]:
                bad.append(f"{e[0]} at {va:#x}: frame {fr}, llvm-dwarfdump {(mx, fp, fpoff, saved)} {f[1] - f[0]} bytes")
        for c in rnd.sample(g["copies"], min(20, len(g["copies"]))):
            probes.append((e[0], virt(c)))
        if where:
            gprobes.append((e, file, g, where))
    for (name, va), fr in zip(probes, symbolize(elf, [va for _, va in probes], strip)):
        ncopies += 1
        if name not in [f[0] for f in fr[:-1]]:
            bad.append(f"{name}: inlined copy at {va:#x} is {fr} for llvm-symbolizer")
    got = gdb_funcs(elf, [w for *_, w in gprobes])
    for (e, file, g, w), r in zip(gprobes, got):
        if r[0] == "error":
            bad.append(f"{e[0]} at {w[1]:#x}: gdb: {r[1]}")
            continue
        if file is None:
            # Assembly (GNU as -g): the DWARF has a name and code only; the page says so, gdb shows "void (void)" in the .S file.
            nasm += 1
            if not r[1].endswith(".S") or g["prototyped"] or g["file"] is not None:
                bad.append(f"{e[0]}: no declaring file on the page {g}, gdb {r}")
            continue
        if sp(r[0]) != sp(g["type"]) or (norm(r[1], strip), r[2]) != (file, e[2]) or r[3] != g["external"]:
            bad.append(f"{e[0]}: page {g['type']!r} at {file}:{e[2]} external={g['external']}, "
                       f"gdb ptype {r[0]!r} at {norm(r[1], strip)}:{r[2]} global={r[3]}")
    # llvm-dwarfdump --name: the DIE that declares each sampled function
    # (its prototype), its out-of-line copies and its inlined copies.
    dd = dwarfdump_named(elf, names, strip)

    def key(off):
        name = file = line = None
        for _ in range(8):
            d = dd.get(off)
            if d is None:
                break
            name = name or d.get("name")
            if file is None and "file" in d:
                file, line = d["file"], d.get("line", 0)
            off = d.get("origin")
            if off is None:
                break
        return name, file, line
    keyed = {off: key(off) for off in dd}
    nproto = 0
    for e, file in pick:
        k = (e[0], file, e[2])
        g = next((x for x in page.get(e[0], []) if x["file"] == file and x["line"] == e[2]), None)
        if g is None or file is None:
            continue
        decl = [d for off, d in dd.items() if d["tag"] == "DW_TAG_subprogram" and "origin" not in d and keyed[off] == k]
        copies = sum(1 for off, d in dd.items() if d["tag"] == "DW_TAG_inlined_subroutine" and d["code"] and keyed[off] == k)
        ool = sum(1 for off, d in dd.items() if d["tag"] == "DW_TAG_subprogram" and d["code"] and keyed[off] == k)
        if copies != len(g["copies"]) or ool != len(g["ool"]):
            bad.append(f"{e[0]} ({file}:{e[2]}): {len(g['copies'])} inlined copies, {len(g['ool'])} out of line; "
                       f"llvm-dwarfdump {copies}, {ool}")
        if not decl:
            bad.append(f"{e[0]} ({file}:{e[2]}): no declaring DIE in llvm-dwarfdump --name")
            continue
        d = max(decl, key=lambda x: len(x["params"]))
        ps = [ctype(x["type"]) for x in d["params"]] + (["..."] if d["varargs"] else [])
        want = ctype(d["type"]) + "(" + ", ".join(ps) + ")"
        have = ctype(g["type"]).replace("(void)", "()")
        nproto += 1
        if have.replace(" (", "(") != want.replace(" (", "(") or [n for n, _ in e[6]] != [x["name"] for x in d["params"]]:
            bad.append(f"{e[0]}: page {g['proto']!r}, llvm-dwarfdump {want!r} params {[x['name'] for x in d['params']]}")
    ncount = None
    if os.path.getsize(elf) < 50 << 20:
        dd = dwarfdump_inline_counts(elf, strip)
        ncount = 0
        for e, file in entries:
            want = dd.get((e[0], file, e[2]), 0)
            ncount += 1
            if want != len(e[8]):
                bad.append(f"{e[0]} ({file}:{e[2]}): {len(e[8])} inlined copies, llvm-dwarfdump {want}")
        known = {(e[0], file, e[2]) for e, file in entries}
        for k, v in dd.items():
            if k not in known and k[0]:
                bad.append(f"{k}: {v} inlined copies in llvm-dwarfdump, no facts on the page")
    for b in bad[:12]:
        print("FUNCS " + b)
    print(f"{elf}: funcs: {len(entries)} functions in {index['funcs']['shards']} shards; {len(pick)} sampled: "
          f"{nproto} prototypes, inlined and out-of-line copy counts vs llvm-dwarfdump --name, {len(gprobes)} with code vs gdb "
          f"(ptype, file:line, external = gdb's global symbol; {nasm} assembly functions without a declaration), {nframes} frames vs llvm-dwarfdump --debug-frame, "
          f"{ncopies} inlined copies vs llvm-symbolizer; "
          + (f"inlined copy counts of all {ncount} functions vs llvm-dwarfdump" if ncount is not None else
             "inlined copy counts of the others not checked (.debug_info too big to dump whole here)") + f"; {len(bad)} problems")
    return bad


# For --inline-blocks: an inlined call (sum in _start) whose lexical block
# has no DW_AT_abstract_origin and whose own children differ from its
# abstract function's (the inlined call of twice).
INLINE_BLOCKS_C = r"""
static inline __attribute__((always_inline)) int twice(int x) { return x * 2; }
struct node { struct node *next; int v; };
static inline __attribute__((always_inline)) int sum(struct node *n)
{
	int a = twice(n->v);
	{
		struct node *head = n->next;
		int inner = head ? head->v : 0;
		a += inner;
	}
	return a + 1;
}
int sink;
void _start(void) { struct node y = { 0, 2 }, x = { &y, 1 }; sink = sum(&x); for (;;); }
"""


def inline_blocks_selftest():
    """Every instruction of the inlined sum: n and a with a location; head
    and inner listed exactly inside the concrete block (llvm-dwarfdump's
    DW_AT_low_pc/high_pc), with a location; no name listed twice."""
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        src, elf, out = os.path.join(tmp, "t.c"), os.path.join(tmp, "t.elf"), os.path.join(tmp, "t")
        pathlib.Path(src).write_text(INLINE_BLOCKS_C)
        subprocess.run(["clang", "-fuse-ld=lld", "-m32", "-g", "-O0", "-fno-pie", "-nostdlib", "-static",
                        "-Wl,-Ttext=0xc0100000", "-o", elf, src], check=True)
        subprocess.run(["node", str(SITE / "gen-dwarf.js"), elf, out], check=True, capture_output=True)
        dump = subprocess.run(["llvm-dwarfdump", "--debug-info", elf], capture_output=True, text=True, check=True).stdout
        pcs = lambda chunk: [int(x, 16) for x in re.findall(r"DW_AT_(?:low|high)_pc\s+\((0x[0-9a-f]+)\)", chunk)[:2]]
        inl = re.search(r"DW_TAG_inlined_subroutine\s+DW_AT_abstract_origin\s+\(0x[0-9a-f]+ \"sum\"\)(.*?)(?=\n0x[0-9a-f]+: {6}\S|\Z)", dump, re.S)
        blk = re.search(r"DW_TAG_lexical_block\s+DW_AT_low_pc.*?\n\s*DW_AT_high_pc[^\n]*", inl.group(1), re.S) if inl else None
        if not inl or not blk or "DW_AT_abstract_origin" in blk.group(0):
            return [f"clang's DWARF is not the shape this test needs: {dump}"]
        lo, hi = pcs(inl.group(0))
        blo, bhi = pcs(blk.group(0))
        if re.search(r"DW_TAG_inlined_subroutine", inl.group(1)) is None:
            return ["clang inlined no call inside sum: the test needs one"]
        insns = [int(m.group(1), 16) for m in re.finditer(r"^\s*([0-9a-f]+):", subprocess.run(
            ["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={lo}", f"--stop-address={hi}", elf],
            capture_output=True, text=True, check=True).stdout, re.M)]
        got = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--locals", out], input="\n".join(map(str, insns)),
                             capture_output=True, text=True, check=True).stdout
        for a, line in zip(insns, got.splitlines()):
            f = json.loads(line)["frames"]
            f = f[0] if f and f[0]["func"] == "sum" else next((x for x in f if x["func"] == "sum"), None)
            if f is None:
                bad.append(f"{a:#x}: no frame of sum")
                continue
            names = [v[0] for v in f["vars"]]
            want = ["n", "a"] + (["head", "inner"] if blo <= a < bhi else [])
            if sorted(names) != sorted(want):
                bad.append(f"{a:#x} ({'inside' if blo <= a < bhi else 'outside'} the block {blo:#x}-{bhi:#x}): page lists {f['vars']}, want {want}")
            elif any(v[1] == "out" for v in f["vars"]):
                bad.append(f"{a:#x}: optimized out in {f['vars']}")
        for b in bad[:10]:
            print("INLINE BLOCK", b)
        print(f"inline blocks: {len(insns) - len(bad)}/{len(insns)} instructions of the inlined sum list n, a and the block's head, inner "
              f"exactly inside {blo:#x}-{bhi:#x}")
    return bad


def main():
    if sys.argv[1:2] == ["--inline-blocks"]:
        sys.exit(1 if inline_blocks_selftest() else 0)
    if sys.argv[1:2] == ["--eh-frame"]:
        sys.exit(1 if eh_frame_selftest(int(sys.argv[2]) if len(sys.argv) > 2 else 2000, random.Random(1)) else 0)
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("shards")
    ap.add_argument("--samples", type=int, default=3000)
    ap.add_argument("--strip", action="append", default=[])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--types", type=int, default=0, help="also check N bytes of types.json against gdb")
    ap.add_argument("--cfi", type=int, default=0, help="also check the CFI rows at N addresses against llvm-dwarfdump")
    ap.add_argument("--locals", type=int, default=0, help="also check the parameters and locals at N addresses against gdb")
    ap.add_argument("--operands", type=int, default=0, help="also check the names of N instructions' memory operands against gdb")
    ap.add_argument("--lines", type=int, default=0, help="also check the code of the source lines at N addresses (line -> address)")
    ap.add_argument("--funcs", type=int, default=0, help="also check the facts of N functions against gdb, llvm-dwarfdump and llvm-symbolizer")
    ap.add_argument("--vars", type=int, default=0, help="also check where N variables are defined and file::name picking against gdb")
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
    cbad = check_cfi(args.elf, args.shards, args.cfi, rnd, lds) if args.cfi else []
    lbad = check_locals(args.elf, args.shards, args.locals, rnd, lds, fns) if args.locals else []
    obad = check_operands(args.elf, args.shards, args.operands, rnd, lds) if args.operands else []
    nbad = check_lines(args.elf, args.shards, args.lines, rnd, lds, fns, args.strip) if args.lines else []
    fbad = check_funcs(args.elf, args.shards, args.funcs, rnd, lds, args.strip) if args.funcs else []
    vbad = check_vars(args.elf, args.shards, args.vars, rnd, lds, args.strip) if args.vars else []
    sys.exit(1 if bad or tbad or cbad or lbad or obad or nbad or fbad or vbad else 0)


if __name__ == "__main__":
    main()
