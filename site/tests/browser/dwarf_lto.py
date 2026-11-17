"""Browser test suite dwarf_lto (test_browser.py --suites)."""

import base64
import os
import pathlib
import re
import subprocess
import tempfile

from tests.browser.common import report, wait_for


# ---- DWARF across compile units: a GCC LTO fixture --------------------------
# The shipped kernels' DWARF never refers from one compile unit into
# another; GCC's link-time optimization does it everywhere: the code's
# unit ("<artificial>") has a concrete DIE per function and variable whose
# DW_AT_abstract_origin (DW_FORM_ref_addr) is the early-debug DIE in the
# unit of its source file, with the name, file, line and type. Two files:
# a.c's helper is inlined into b.c's entry. Through dwarfworker.js like
# an import, every fact the page reads through those references
# (DwarfReader subprogramAt, declAt, typeAt, nameAt, foreignKids) must
# equal gdb's and llvm-dwarfdump's.
LTO_FIXTURE = {
    "a.c": "int counter;\nstatic int scale = 3;\n\nint helper(int x)\n{\n\tint y = x * scale;\n\n\tcounter += y;\n\treturn y + 1;\n}\n",
    "b.c": "int helper(int x);\n\nint entry(int n)\n{\n\tstatic int calls;\n\tint s = 0;\n\n\tcalls++;\n\tfor (int i = 0; i < n; i++)\n\t\ts += helper(i);\n\treturn s + calls;\n}\n",
}
LTO_BASE, LTO_OFFSET = 0xc1000000, 0xc0000000

LTO_OPEN_JS = """async ([b64, offset, pa]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search, { type: 'module' });
    // The answer of type t (the types answer comes after progress messages).
    const ask = (m, t) => new Promise((res) => { w.onmessage = (e) => { if (e.data.type === t || e.data.type === 'error') res(e.data); };
        w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'lto.elf'), offset }, 'open');
    if (!o.ok) { w.terminate(); return { open: o.why }; }
    const T = (await ask({ type: 'types' }, 'types')).types;
    const groups = [];
    for (let i = 0; i < o.units; i++) {
        const l = await ask({ type: 'locals', i }, 'locals');
        if (l.type === 'error') { w.terminate(); return { open: 'ok', error: l.message }; }
        T.types.push(...l.added);
        groups.push(...l.groups);
    }
    w.terminate();
    const name = (v) => [v[0], MKB.dwTypeName(T, v[1]), v[3] === null ? 'optimized out' : 'has a location'];
    return { open: 'ok', units: o.units,
        funcs: T.funcs.fns.map((e) => ({ name: e[0], at: T.funcs.files[e[1]] + ':' + e[2], proto: MKB.dwFuncProto(T, e),
            inlined: e[8].map((x) => x + offset) })),
        vars: T.vars.map((v) => ({ name: v[0], at: MKB.dwVarDeclText(T, v), type: MKB.dwTypeName(T, v[2]), va: v[1] + offset, scope: v[6] })),
        frames: MKB.dwarfLocalFrames(MKB.dwarfScopesAt(groups, pa)).map((f) => ({ func: f.func, inlined: f.inlined,
            params: f.params.map(name), locals: f.locals.map(name) })) };
}"""


def lto_fixture(tmp):
    """a.c and b.c compiled with gcc -flto -g -O2 for i386 and linked at
    LTO_BASE; None (and why) when gcc cannot."""
    for n, t in LTO_FIXTURE.items():
        (tmp / n).write_text(t)
    flags = ["-m32", "-flto", "-g", "-O2", "-ffreestanding", "-fno-pic", "-fno-asynchronous-unwind-tables"]
    elf = tmp / "lto.elf"
    try:
        for n in LTO_FIXTURE:
            subprocess.run(["gcc", *flags, "-c", n, "-o", n[:-2] + ".o"], cwd=tmp, check=True, capture_output=True, text=True)
        subprocess.run(["gcc", *flags, "-nostdlib", "-no-pie", "-Wl,-e,entry", f"-Wl,-Ttext=0x{LTO_BASE:x}", "a.o", "b.o",
                        "-o", str(elf)], cwd=tmp, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return None, f"gcc: {getattr(e, 'stderr', '') or e}"[:300]
    return elf, None


def lto_dies(dump):
    """llvm-dwarfdump's DIEs: [{"tag", "DW_AT_...": value as printed}]."""
    out = []
    for block in re.split(r"\n(?=0x[0-9a-f]+:)", dump):
        m = re.match(r"0x[0-9a-f]+:\s+(DW_TAG_\w+)", block)
        if m:
            out.append({"tag": m.group(1), **{a: v for a, v in re.findall(r"^\s+(DW_AT_\w+)\s+\((.*)\)$", block, re.M)}})
    return out


def dwarf_lto_case(browser, base, shots):
    problems, r = [], {}
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        tmp = pathlib.Path(tmp)
        elf, why = lto_fixture(tmp)
        if not elf:
            return [f"the GCC LTO fixture did not build: {why}"], r
        r["ref_addr"] = subprocess.run(["llvm-dwarfdump", "-v", "--debug-info", str(elf)], capture_output=True,
                                       text=True).stdout.count("[DW_FORM_ref_addr]")
        if not r["ref_addr"]:
            problems.append("the fixture's DWARF has no reference into another unit (DW_FORM_ref_addr): it tests nothing here")
        dies = lto_dies(subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout)
        # helper is only inlined: at its call gdb's innermost scope is helper's.
        inl = next((d for d in dies if d["tag"] == "DW_TAG_inlined_subroutine" and d.get("DW_AT_abstract_origin", "").endswith('"helper"')), None)
        if not inl or "DW_AT_low_pc" not in inl:
            return problems + [f"the fixture has no inlined call of helper: {dies}"[:600]], r
        call = int(inl["DW_AT_low_pc"], 16)
        r["helper_call"] = hex(call)
        gdb = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info functions ^entry$", "-ex", "info variables ^counter$",
                              "-ex", f"info scope *{call:#x}", "-ex", "info scope entry", "-ex", "ptype counter", str(elf)],
                             capture_output=True, text=True).stdout
        nm = {l.split()[2]: int(l.split()[0], 16) for l in
              subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
        page = browser.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        page.goto(f"{base}/index.html?kernel=hello&paused=1")
        wait_for(lambda: page.evaluate("typeof window.MKB?.dwFuncProto === 'function' && typeof window.MKB?.dwarfLocalFrames === 'function'"), 30)
        got = page.evaluate(LTO_OPEN_JS, [base64.b64encode(elf.read_bytes()).decode(), LTO_OFFSET, call - LTO_OFFSET])
        page.close()
    r["page"] = got
    if got.get("open") != "ok" or got.get("error"):
        return problems + [f"the fixture did not open: {got}"], r
    fn = {f["name"]: f for f in got["funcs"]}
    # entry: gdb's "info functions" line (b.c, "3:\tint entry(int);").
    m = re.search(r"File (?:[^\n]*/)?b\.c:\n(\d+):\tint entry\(int\);", gdb)
    want = f"b.c:{m.group(1)}" if m else None
    e = fn.get("entry")
    if not m or not e or e["at"] != want or e["proto"] != "int entry(int n)":
        problems.append(f"entry: the page says {e}, gdb {want} int entry(int) ({gdb[:300]!r})")
    # helper (inlined only, so not in gdb's function list): the abstract
    # DIE's decl_file/decl_line in llvm-dwarfdump, its call in gdb's scope.
    h = fn.get("helper")
    d = next((d for d in dies if d["tag"] == "DW_TAG_subprogram" and d.get("DW_AT_name") == '"helper"' and
              "DW_AT_declaration" not in d), {})
    want = f"{pathlib.Path(d['DW_AT_decl_file'].strip(chr(34))).name}:{d['DW_AT_decl_line']}" if "DW_AT_decl_line" in d else None
    if not want or not h or h["at"] != want or h["proto"] != "int helper(int x)" or call not in h["inlined"]:
        problems.append(f"helper: the page says {h}, llvm-dwarfdump {want}, inlined at {call:#x}")
    if f"Symbol helper is a function at address {call:#x}" not in gdb:
        problems.append(f"gdb does not put helper's inlined call at {call:#x}: {gdb[:400]!r}")
    # counter: declared where llvm-dwarfdump's DIE in a.c's unit says (gdb
    # 15.1 of Ubuntu 24.04 files it under b.c in "info variables", gdb 16
    # under a.c), gdb's ptype, llvm-nm's address.
    d = next((d for d in dies if d["tag"] == "DW_TAG_variable" and d.get("DW_AT_name") == '"counter"' and
              "DW_AT_decl_line" in d), {})
    want = f"{pathlib.Path(d['DW_AT_decl_file'].strip(chr(34))).name}:{d['DW_AT_decl_line']}" if d else None
    v = next((v for v in got["vars"] if v["name"] == "counter"), None)
    if not want or not v or v["at"] != want or v["type"] != "int" or "type = int" not in gdb or v["va"] != nm.get("counter"):
        problems.append(f"counter: the page says {v}, llvm-dwarfdump {want}, gdb's ptype {'type = int' in gdb}, nm {nm.get('counter')}")
    # entry's static local: its DIE in the code's unit is in entry's
    # concrete DIE, which has no name (nameAt through its abstract origin).
    # Where: gdb's info scope of entry; declared: llvm-dwarfdump's DIE.
    m = re.search(r"Symbol calls is (?:in )?static storage at address (0x[0-9a-f]+)", gdb)
    d = next((d for d in dies if d["tag"] == "DW_TAG_variable" and d.get("DW_AT_name") == '"calls"'), {})
    v = next((v for v in got["vars"] if v["name"] == "calls"), None)
    if not m or not v or v["at"] != f"b.c:{d.get('DW_AT_decl_line')}" or v["scope"] != "entry" or v["va"] != int(m.group(1), 16):
        problems.append(f"calls: the page says {v}, gdb {m and m.group(0)} in entry, llvm-dwarfdump b.c:{d.get('DW_AT_decl_line')}")
    # Locals at the inlined call: helper's x and y, then entry's; names as
    # gdb's info scope lists them, x optimized out as gdb says.
    fr = got["frames"]
    scope = re.findall(r"Symbol (\w+) is ([^\n]*)", gdb.split("Scope for entry:")[0])
    inner = [n for n, what in scope if not what.startswith("a function")]
    r["gdb_scope"] = scope
    if len(fr) != 2 or fr[0]["func"] != "helper" or not fr[0]["inlined"] or fr[1]["func"] != "entry" or fr[1]["inlined"]:
        problems.append(f"frames at {call:#x}: {fr}, want helper (inlined) in entry")
    else:
        mine = [v[0] for v in fr[0]["params"] + fr[0]["locals"]]
        if sorted(mine) != sorted(inner) or any(v[1] != "int" for v in fr[0]["params"] + fr[0]["locals"]):
            problems.append(f"helper's variables at {call:#x}: {fr[0]}, gdb lists {inner}")
        for n, what in scope:
            v = next((v for v in fr[0]["params"] + fr[0]["locals"] if v[0] == n), None)
            if v and (what.startswith("optimized out")) != (v[2] == "optimized out"):
                problems.append(f"{n} at {call:#x}: the page says {v[2]}, gdb {what!r}")
        outer = re.findall(r"Symbol (\w+) is", gdb.split("Scope for entry:")[1].split("type =")[0]) if "Scope for entry:" in gdb else []
        theirs = [v[0] for v in fr[1]["params"] + fr[1]["locals"]]
        if not outer or any(n not in theirs for n in outer) or any(v[1] != "int" for v in fr[1]["params"] + fr[1]["locals"]):
            problems.append(f"entry's variables at {call:#x}: {fr[1]}, gdb's scope of entry {outer}")
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_dwarf_lto(ctx):
    return report("dwarf_lto", *dwarf_lto_case(ctx.browser, ctx.base, ctx.shots))
