"""test_dwarf.py --vars: where variables are defined and file::name picking against gdb."""

import json
import os
import subprocess
import tempfile

from tests.dwarf.common import norm, SITE
from tests.dwarf.funcs import dwarfdump_named


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
            try:
                # Not an lvalue (a global clang shrank or split: gdb computes
                # its value): 'file'::name resolves; where that file's
                # symbol of the name is declared.
                gdb.parse_and_eval("'%s'::%s" % (qual, name))
                s = [y for y in list(gdb.lookup_static_symbols(name)) + [gdb.lookup_global_symbol(name)]
                     if y is not None and y.symtab and y.symtab.filename == qual]
                if s:
                    r["qual"] = {"file": s[0].symtab.filename, "line": s[0].line}
            except Exception:
                pass
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
    # gdb's 'file'::name in a long session can give another file's static
    # of that name (kernel/reboot.c::poweroff_work gave
    # kernel/power/poweroff.c's on the clang Debian, a fresh gdb gives
    # reboot.c's): a disagreement is asked again in a gdb of its own.
    retried = 0
    for k, (v, _, qual, p) in enumerate(probes):
        if qual and isinstance(got[k]["qual"], int) and got[k]["qual"] != virt(v[1]):
            got[k]["qual"] = gdb_vars(elf, [p])[0]["qual"]
            retried += 1
    # What gdb does not find by name (a static local in an inner block or in
    # a function that only exists inlined): llvm-dwarfdump --name, the DIE
    # whose DW_OP_addr location is the variable's address, each attribute
    # through DW_AT_abstract_origin / specification when the DIE lacks it.
    dd = dwarfdump_named(elf, sorted({v[0] for (v, *_), g in zip(probes, got) if g["why"]}), strip)
    by_addr = {}
    for off, x in dd.items():
        if x["tag"] == "DW_TAG_variable":
            for a in ([x["addr"]] if "addr" in x else []) + x.get("pieces", []):
                by_addr.setdefault(a, []).append(off)

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
            if isinstance(g["qual"], dict) and v[5] & 24:
                # A global clang shrank or split has no address in gdb: its
                # 'file'::name symbol must be declared where the page's is.
                if (norm(g["qual"]["file"], strip), g["qual"]["line"]) != (d["file"], d["line"]):
                    bad.append(f"{qual}::{v[0]}: gdb's 'file'::name is declared at {g['qual']}, the page's at {d['file']}:{d['line']}")
                else:
                    qok += 1
            elif not isinstance(g["qual"], int) or g["qual"] != virt(v[1]):
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
          f"{unknown} neither confirms (DW_OP_addrx read through the unit's .debug_addr), {nalso} with more than one definition at their address, "
          f"{qok} file::name picks = gdb &'file'::name (+ {len(quals) // 2} shortest suffixes; {retried} asked again in a fresh gdb), "
          f"{len(neg)} refusals; {len(bad)} problems")
    return bad
