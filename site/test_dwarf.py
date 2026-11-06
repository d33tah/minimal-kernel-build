#!/usr/bin/env python3
"""Check the DWARF shards of gen-dwarf.js against llvm-symbolizer --inlines,
and its types.json against gdb.

  site/test_dwarf.py ELF SHARD_DIR [--samples N] [--strip PREFIX ...] [--seed S] [--types N] [--cfi N] [--locals N] [--operands N] [--lines N]

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
scope` lists, each with the same kind of location there.

--operands N: N random instructions with a memory operand, half on the
stack, half at an absolute address; the names the page's listings give
them (`gen-dwarf.js --operands`, asmnames.js) must be gdb's: `info
symbol` for absolute addresses, the address gdb computes for each
parameter and local on a core with EIP at the instruction for stack
slots (see check_operands).

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

--cfi N: N random addresses; the call frame information row there
(`gen-dwarf.js --cfi`, the page's unwind.js cfiRow on the cfi-*.bin
shards) must read the same as the row llvm-dwarfdump --debug-frame (or
--eh-frame) evaluates for that FDE, and outside every FDE there is none.

  site/test_dwarf.py --eh-frame [N]

the same check on the .eh_frame of a small program built here with GCC
and clang (the kernels have .debug_frame only).
"""

import argparse
import base64
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
    bad, nvars, located, failed, nofn = [], 0, 0, 0, 0
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
            bad.append((va, theirs, mine, g["frames"][0]["func"] if g["frames"] else None))
    for va, w, g, fn in bad[:10]:
        print(f"LOCALS MISMATCH {va:#x} in {fn}\n  gdb  {w}\n  page {g}")
    print(f"{elf}: {len(picks) - len(bad) - failed - nofn}/{len(picks) - failed - nofn} addresses list the same parameters and locals "
          f"as gdb's info scope ({nvars} variables, {located} with a location there; {nofn} more outside DWARF functions, "
          f"gdb's info scope failed at {failed})")
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
    ia = istack = 0
    for kind, va, text, ea, mine in which:
        if kind == "abs":
            s = syms[ia]
            ia += 1
            nabs += 1
            m = re.match(r"^(\S+)(?: \+ (\d+))? in section", s)
            if not m:
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
          f"{gdbonly} gdb names and the page does not; gdb failed on {failed} cores)")
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


def main():
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
    sys.exit(1 if bad or tbad or cbad or lbad or obad or nbad else 0)


if __name__ == "__main__":
    main()
