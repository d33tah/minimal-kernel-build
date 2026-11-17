"""Part of a browser suite (test_browser.py)."""

import base64
import json
import re
import subprocess

from tests.browser.common import frames_agree
from tests.browser.cfi import CFI_BASE, CFI_OFFSET, cfi_with_section
from tests.browser.dwarf_forms_checks import dwf_dwarfdump_exprs, DWF_JS


# DWARF no compiler here writes, assembled by hand (llvm-mc): a DWARF 4
# unit with a name in DW_FORM_indirect, a .debug_str name, .debug_loc and
# .debug_ranges lists that start with a base address selection, a
# location in DW_FORM_block4 (DW_OP_addr + DW_OP_plus_uconst), an
# enumerator whose value is a block, a line program with
# DW_LNS_fixed_advance_pc, DW_LNS_set_isa and an opcode the header
# declares (opcode_base 14); a DWARF 5 unit without a line program, its
# variable's type in the first unit (DW_FORM_ref_addr), a .debug_loclists
# list with DW_LLE_start_length and DW_LLE_default_location, a DIE with a
# DW_FORM_data16 value, and variables in g located by
# DW_OP_implicit_value, pieces of a register and a constant, a 4-bit
# DW_OP_bit_piece, DW_OP_regx (MM0) and an unknown operation. Against
# llvm-symbolizer, gdb and llvm-dwarfdump, the values against gdb's on a
# core file with EIP in g.
DWF_HAND_S = """	.text
	.globl f
	.type f,@function
f:
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	ret
	.size f, .-f
	.globl g
	.type g,@function
g:
	nop
	nop
	nop
	ret
	.size g, .-g
	.data
	.globl gv
	.p2align 2
gv:
	.long 1, 2, 3

	.section .debug_abbrev,"",@progbits
	.uleb128 1
	.uleb128 0x11
	.byte 1
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x1b
	.uleb128 0x08
	.uleb128 0x10
	.uleb128 0x17
	.uleb128 0x11
	.uleb128 0x01
	.uleb128 0x55
	.uleb128 0x17
	.byte 0, 0
	.uleb128 2
	.uleb128 0x2e
	.byte 1
	.uleb128 0x03
	.uleb128 0x16
	.uleb128 0x11
	.uleb128 0x01
	.uleb128 0x12
	.uleb128 0x06
	.uleb128 0x3a
	.uleb128 0x0b
	.uleb128 0x3b
	.uleb128 0x0b
	.uleb128 0x27
	.uleb128 0x19
	.uleb128 0x49
	.uleb128 0x13
	.byte 0, 0
	.uleb128 3
	.uleb128 0x34
	.byte 0
	.uleb128 0x03
	.uleb128 0x0e
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x02
	.uleb128 0x17
	.byte 0, 0
	.uleb128 4
	.uleb128 0x24
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x0b
	.uleb128 0x0b
	.uleb128 0x3e
	.uleb128 0x0b
	.byte 0, 0
	.uleb128 5
	.uleb128 0x34
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0x19
	.uleb128 0x02
	.uleb128 0x04
	.byte 0, 0
	.uleb128 6
	.uleb128 0x04
	.byte 1
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x0b
	.uleb128 0x0b
	.uleb128 0x49
	.uleb128 0x13
	.byte 0, 0
	.uleb128 7
	.uleb128 0x28
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x1c
	.uleb128 0x0a
	.byte 0, 0
	.uleb128 8
	.uleb128 0x11
	.byte 1
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x11
	.uleb128 0x01
	.uleb128 0x12
	.uleb128 0x06
	.byte 0, 0
	.uleb128 9
	.uleb128 0x2e
	.byte 1
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x11
	.uleb128 0x01
	.uleb128 0x12
	.uleb128 0x06
	.byte 0, 0
	.uleb128 10
	.uleb128 0x34
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x49
	.uleb128 0x10
	.uleb128 0x02
	.uleb128 0x17
	.byte 0, 0
	.uleb128 11
	.uleb128 0x36
	.byte 0
	.uleb128 0x1c
	.uleb128 0x1e
	.uleb128 0x0b
	.uleb128 0x0b
	.byte 0, 0
	.uleb128 12
	.uleb128 0x34
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x49
	.uleb128 0x10
	.uleb128 0x02
	.uleb128 0x18
	.byte 0, 0
	.byte 0

	.section .debug_info,"",@progbits
.Lcu1:
	.long .Lcu1_end - .Lcu1 - 4
	.short 4
	.long 0
	.byte 4
	.uleb128 1
	.asciz "h.c"
	.asciz "/hand"
	.long .Lline
	.long 0
	.long .Lranges
	.uleb128 2
	.uleb128 0x08
	.asciz "f"
	.long f
	.long 8
	.byte 1
	.byte 5
	.long .Lint - .Lcu1
	.uleb128 3
	.long .Lstr_lv
	.long .Lint - .Lcu1
	.long .Lloc
	.byte 0
.Lint:
	.uleb128 4
	.asciz "int"
	.byte 4
	.byte 5
.Llong:
	.uleb128 4
	.asciz "long long int"
	.byte 8
	.byte 5
	.uleb128 5
	.asciz "second"
	.long .Lint - .Lcu1
	.long 7
	.byte 0x03
	.long gv
	.byte 0x23, 4
.Lenum:
	.uleb128 6
	.asciz "mode"
	.byte 2
	.long .Lint - .Lcu1
	.uleb128 7
	.asciz "M_LOW"
	.byte 2, 0x34, 0x12
	.byte 0
	.uleb128 5
	.asciz "gmode"
	.long .Lenum - .Lcu1
	.long 5
	.byte 0x03
	.long gv
	.byte 0
.Lcu1_end:
.Lcu2:
	.long .Lcu2_end - .Lcu2 - 4
	.short 5
	.byte 1
	.byte 4
	.long 0
	.uleb128 8
	.asciz "k.c"
	.long g
	.long 4
	.uleb128 9
	.asciz "g"
	.long g
	.long 4
	.uleb128 10
	.asciz "w"
	.long .Lint
	.long .Lloclist
	.uleb128 11
	.quad 0x1122334455667788, 0x99aabbccddeeff00
	.byte 16
	.uleb128 12
	.asciz "iv"
	.long .Lint
	.uleb128 6
	.byte 0x9e, 4, 42, 0, 0, 0
	.uleb128 12
	.asciz "ll"
	.long .Llong
	.uleb128 11
	.byte 0x50, 0x93, 4, 0x9e, 4, 1, 0, 0, 0, 0x93, 4
	.uleb128 12
	.asciz "bf"
	.long .Lint
	.uleb128 4
	.byte 0x51, 0x9d, 4, 0
	.uleb128 12
	.asciz "mx"
	.long .Lint
	.uleb128 2
	.byte 0x90, 29
	.uleb128 12
	.asciz "bad"
	.long .Lint
	.uleb128 1
	.byte 0xff
	.byte 0
	.byte 0
.Lcu2_end:

	.section .debug_str,"MS",@progbits,1
.Lstr_lv:
	.asciz "lv"

	.section .debug_loc,"",@progbits
.Lloc:
	.long 0xffffffff, f
	.long 0, 2
	.short 1
	.byte 0x50
	.long 2, 6
	.short 2
	.byte 0x74, 8
	.long 0, 0

	.section .debug_ranges,"",@progbits
.Lranges:
	.long 0xffffffff, f
	.long 0, 8
	.long 0, 0

	.section .debug_loclists,"",@progbits
	.long .Lll_end - .Lll_start
.Lll_start:
	.short 5
	.byte 4, 0
	.long 0
.Lloclist:
	.byte 8
	.long g
	.uleb128 2
	.uleb128 1
	.byte 0x51
	.byte 5
	.uleb128 2
	.byte 0x74, 4
	.byte 0
.Lll_end:

	.section .debug_line,"",@progbits
.Lline:
	.long .Lline_end - .Lline - 4
	.short 4
	.long .Lprog - .Lhdr
.Lhdr:
	.byte 1, 1, 1, -5 & 255, 14, 14
	.byte 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1
	.byte 0
	.asciz "h.c"
	.byte 0, 0, 0
	.byte 0
.Lprog:
	.byte 0, 5, 2
	.long f
	.byte 3, 9
	.byte 1
	.byte 9
	.short 2
	.byte 12, 0
	.byte 13, 5
	.byte 3, 1
	.byte 1
	.byte 9
	.short 6
	.byte 0, 1, 1
.Lline_end:
"""


def dwf_hand_checks(page, tmp, symbolize, gdb_scopes):
    problems, r = [], {}
    (tmp / "h.s").write_text(DWF_HAND_S)
    elf = tmp / "h.elf"
    try:
        subprocess.run(["llvm-mc", "-triple=i386-linux-gnu", "-filetype=obj", "h.s", "-o", "h.o"], cwd=tmp, check=True, capture_output=True)
        subprocess.run(["ld.lld", "-m", "elf_i386", "-e", "f", f"-Ttext=0x{CFI_BASE:x}", "h.o", "-o", str(elf)], cwd=tmp, check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return [f"hand-written DWARF: did not build: {getattr(e, 'stderr', '') or e}"[:300]], r
    nm = {l.split()[2]: int(l.split()[0], 16) for l in
          subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
    lo, hi = nm["f"], nm["g"] + 4
    from tests.dwarf.operands import write_core
    regs = {"eip": nm["g"] + 1, "esp": 0x10000, "eax": 0x11223344, "ecx": 0x55, "cs": 0x60, "eflags": 0x202}
    write_core(tmp / "core", regs, [(0x10000, base64.b64encode(bytes(4096)).decode())])
    probe = [nm["g"] + 1 - CFI_OFFSET, {"0": regs["eax"], "1": regs["ecx"], "4": regs["esp"]}, {}]
    got = page.evaluate(DWF_JS, [base64.b64encode(elf.read_bytes()).decode(), CFI_OFFSET, lo - CFI_OFFSET, hi - CFI_OFFSET, "h.c", probe])
    if got.get("open") != "ok" or got.get("error"):
        return [f"hand-written DWARF: did not open: {str(got)[:300]}"], r
    r["versions"] = got["versions"]
    if got["versions"] != [4, 5]:
        problems.append(f"hand-written DWARF: versions {got['versions']}")
    vas = list(range(lo, hi))
    want = symbolize(str(elf), vas, [])
    bad = [(hex(va), g, w) for va, g, w in zip(vas, got["frames"], want) if not frames_agree(g or [], w)]
    if bad:
        problems.append(f"hand-written DWARF: frames differ from llvm-symbolizer: {bad[:3]}")
    sym = dict(zip(vas, want))
    code = {line: rows for line, rows, _ in got["code"]} if isinstance(got["code"], list) else {}
    r["code"] = code
    if sorted(code) != [10, 11] or any(sym[va][0][2] != line for line, rows in code.items() for a, b in rows
                                       for va in (a + CFI_OFFSET, b - 1 + CFI_OFFSET)):
        problems.append(f"hand-written DWARF: the code of h.c: {got['code']}, llvm-symbolizer {want}")
    gdb = subprocess.run(["gdb", "-nx", "-batch", "-ex", "ptype f", "-ex", "print &second", "-ex", "whatis gmode", "-ex", "print/d (int)M_LOW",
                          str(elf)], capture_output=True, text=True).stdout
    fn = {f["name"]: f for f in got["funcs"]}
    vs = {v["name"]: v for v in got["vars"]}
    m = re.search(r"\(int \*\) (0x[0-9a-f]+)", gdb)
    have = [fn.get("f", {}).get("type"), fn.get("f", {}).get("at"), vs.get("second", {}).get("va"), vs.get("gmode", {}).get("type"),
            got["enums"].get("mode")]
    r["facts"] = have
    if have != ["int (void)", "h.c:5", m and int(m.group(1), 16), "enum mode", [["M_LOW", int(re.findall(r"= (\d+)", gdb)[-1])]]] or \
            "type = int (void)" not in gdb or "type = enum mode" not in gdb:
        problems.append(f"hand-written DWARF: f, second, gmode, M_LOW: {have}, gdb {gdb!r}")
    mine = {(n, a, b): t for n, a, b, t in got["exprs"] if n in ("lv", "w")}
    theirs = {k: v for k, v in dwf_dwarfdump_exprs(elf).items() if k[0] in ("lv", "w")}
    r["exprs"] = sorted(mine.items(), key=str)
    if mine != theirs or len(theirs) != 4:
        problems.append(f"hand-written DWARF: locations {mine}, llvm-dwarfdump {theirs}")
    # gdb's info scope in f (it cannot read w's default location in g).
    scopes = gdb_scopes(str(elf), range(nm["f"], nm["g"]))
    bad = [(hex(va), l, scopes.get(va)) for va, l in zip(vas, got["locals"]) if va < nm["g"] and
           (not l or sorted(map(tuple, l[0]["vars"])) != sorted(scopes.get(va) or []))]
    w = [[x for x in l[0]["vars"] if x[0] == "w"] if l else None for va, l in zip(vas, got["locals"]) if va >= nm["g"]]
    r["w"] = w
    if bad or w != [[["w", "reg:ECX"]]] * 2 + [[["w", "expr"]]] * 2:
        problems.append(f"hand-written DWARF: locals: {bad[:3]}, w in g {w}")
    # Without .debug_loc / .debug_loclists (renamed): lv and w have no location there.
    for sec, var, va in ((".debug_loc", "lv", nm["f"]), (".debug_loclists", "w", nm["g"])):
        data = cfi_with_section(elf.read_bytes(), sec, rename=sec[:-1] + "x")[0]
        g = page.evaluate(DWF_JS, [base64.b64encode(data).decode(), CFI_OFFSET, va - CFI_OFFSET, va + 1 - CFI_OFFSET, "h.c", None])
        have = [x for x in (g.get("locals") or [[{"vars": []}]])[0][0]["vars"] if x[0] == var]
        r["without " + sec] = have
        if have != [[var, "out"]]:
            problems.append(f"hand-written DWARF without {sec}: {var} at {va:#x}: {have}")
    # The values in g on the core: gdb's bytes, or both without one.
    script = tmp / "values.py"
    script.write_text("import gdb, json\nout = {}\nfor n in ('w', 'iv', 'll', 'bf', 'mx', 'bad'):\n"
                      "    try:\n        out[n] = list(bytes(gdb.parse_and_eval(n).bytes))\n"
                      "    except Exception as e:\n        out[n] = 'error: ' + str(e)\nprint('@@' + json.dumps(out))\n")
    out = subprocess.run(["gdb", "-nx", "-batch", "-ex", f"core-file {tmp / 'core'}", "-x", str(script), str(elf)], capture_output=True, text=True).stdout
    theirs = json.loads(out.split("@@", 1)[1]) if "@@" in out else {}
    mine = got.get("values", {})
    r["values"] = [mine, theirs]
    for n in ("w", "iv", "ll"):
        if mine.get(n) != theirs.get(n):
            problems.append(f"hand-written DWARF: {n} in g: the page {mine.get(n)}, gdb {theirs.get(n)}")
    for n, page_why, gdb_why in (("bf", "a bit piece (4 bits at bit 0) is not supported", "optimized out"), ("mx", "in register 29, which is not known in this frame", "not available"),
                                 ("bad", "DWARF operation 0xff is not supported", "Unhandled DWARF expression opcode 0xff")):
        if gdb_why not in str(theirs.get(n)) or page_why not in str(mine.get(n)):
            problems.append(f"hand-written DWARF: {n} in g: the page {mine.get(n)!r}, gdb {theirs.get(n)!r}")
    return problems, r
