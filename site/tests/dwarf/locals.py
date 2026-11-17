"""test_dwarf.py --locals: the parameters and locals against gdb's info scope."""

import json
import os
import pathlib
import re
import subprocess
import tempfile

from tests.dwarf.common import SITE


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
