"""test_dwarf.py --funcs: function facts against gdb, llvm-dwarfdump and llvm-symbolizer."""

import bisect
import gzip
import json
import os
import pathlib
import re
import subprocess
import tempfile

from tests.dwarf.common import norm, SITE, symbolize
from tests.dwarf.cfi import dwarfdump_cfi


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


def addrx_resolver(elf):
    """(DIE offset, index) -> the address DW_OP_addrx index names in that
    DIE's unit (clang's DWARF 5: the unit's DW_AT_addr_base into
    .debug_addr), or None; llvm-dwarfdump prints only the index."""
    units, cur = [], None
    out = subprocess.run(["llvm-dwarfdump", "--debug-info", "--recurse-depth=0", elf], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = re.match(r"^0x([0-9a-f]+): \w+ Unit: .*\(next unit at 0x([0-9a-f]+)\)", line)
        if m:
            cur = [int(m.group(1), 16), int(m.group(2), 16), None]
            units.append(cur)
            continue
        m = re.match(r"^\s+DW_AT_addr_base\s+\(0x([0-9a-f]+)\)", line)
        if m and cur:
            cur[2] = int(m.group(1), 16)
    # Each table: a header of 8 bytes (DWARF32), then the addresses; a
    # unit's addr_base is the offset of its first address.
    tables, off, addrs = {}, 0, None
    for line in subprocess.run(["llvm-dwarfdump", "--debug-addr", elf], capture_output=True, text=True, check=True).stdout.splitlines():
        m = re.match(r"^Address table header: length = 0x([0-9a-f]+), format = DWARF32", line)
        if m:
            addrs = tables[off + 8] = []
            off += 4 + int(m.group(1), 16)
            continue
        m = re.fullmatch(r"0x([0-9a-f]+)", line.strip())
        if m and addrs is not None:
            addrs.append(int(m.group(1), 16))
    starts = [u[0] for u in units]

    def resolve(die, idx):
        k = bisect.bisect_right(starts, die) - 1
        if k < 0 or die >= units[k][1] or units[k][2] is None:
            return None
        t = tables.get(units[k][2])
        return t[idx] if t is not None and idx < len(t) else None
    return resolve


def dwarfdump_named(elf, names, strip):
    """llvm-dwarfdump -c --name for each name (one pass over .debug_info):
    the DIEs named so directly or through DW_AT_abstract_origin, and all
    their children, by offset: {offset: {tag, name, file, line, origin,
    code, type, params: [{name, type}], varargs}} (params of a
    subprogram: its formal_parameter children; addr: a fixed location,
    DW_OP_addr or DW_OP_addrx [+ DW_OP_plus_uconst], or the address of a
    global clang shrank); type texts as
    llvm-dwarfdump prints them, None for no DW_AT_type."""
    cmd = ["llvm-dwarfdump", "-c"] + ["--name=" + n for n in names] + [elf]
    dies, stack, addrx = {}, [], []
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    cur, in_ranges, off = None, False, 0
    for line in p.stdout:
        m = re.match(r"^0x([0-9a-f]+):( *)(DW_TAG_\w+|NULL)", line)
        if m:
            depth = len(m.group(2)) // 2
            del stack[depth:]
            if m.group(3) == "NULL":
                cur = None
                continue
            off = int(m.group(1), 16)
            cur = dies.setdefault(off, {"tag": m.group(3), "type": None, "params": [], "varargs": False, "code": False})
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
            # Also a global clang shrank: its value is the bytes there
            # (DW_OP_deref_size n [* 1] [+ 0], DW_OP_stack_value).
            k = r"DW_OP_(?:lit\d+|constu 0x[0-9a-f]+)(?:, DW_OP_not)?"
            a = re.fullmatch(r"DW_OP_addr(x?) 0x([0-9a-f]+)(?:, DW_OP_plus_uconst 0x([0-9a-f]+)|, DW_OP_deref_size 0x[0-9a-f]+"
                             r"(?:, " + k + r", DW_OP_mul)?(?:, " + k + r", DW_OP_plus)?, DW_OP_stack_value)?", v)
            if a and a.group(1):
                addrx.append((off, int(a.group(2), 16), int(a.group(3) or "0", 16), "addr"))
            elif a:
                cur["addr"] = int(a.group(2), 16) + int(a.group(3) or "0", 16)
            # A global clang split into pieces at their own addresses: each piece's address.
            piece = r"(?:DW_OP_addr(x?) 0x([0-9a-f]+)(?:, DW_OP_plus_uconst 0x([0-9a-f]+))?, )?DW_OP_piece 0x[0-9a-f]+"
            if not a and re.fullmatch(piece + r"(?:, " + piece + r")+", v):
                cur["pieces"] = []
                for x, n, plus in re.findall(piece, v):
                    if not n:
                        continue
                    if x:
                        addrx.append((off, int(n, 16), int(plus or "0", 16), "pieces"))
                    else:
                        cur["pieces"].append(int(n, 16) + int(plus or "0", 16))
    p.wait()
    if addrx:
        resolve = addrx_resolver(elf)
        for off, idx, plus, key in addrx:
            base = resolve(off, idx)
            if base is not None and key == "addr":
                dies[off]["addr"] = base + plus
            elif base is not None:
                dies[off]["pieces"].append(base + plus)
    return dies

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
