#!/usr/bin/env python3
"""Check the page's static jump paths (jumps.js jumpsStatic over
disasm.json) against the kernel ELF.

  site/test_jumps.py ELF DISASM.json SYMBOLS.json [--samples N]

The page reads each instruction's text (llvm-objdump's AT&T) for the
kind of transfer and its target. This check runs llvm-objdump -d over the
ELF again and decodes every instruction's bytes itself: prefixes, the
opcode (and ModRM.reg for 0xff) give the kind, rel8/rel16/rel32 or the
far pointer gives the target, mapped to a physical address through the
ELF's PT_LOAD segments. For every instruction the page's kind, target
and fall-through must equal the decoded ones, and for every target the
page's list of static sources must equal the instructions that jump or
call there; the paths into one of N sampled functions are printed as an
example. Also checks
the decoder against the mnemonic llvm-objdump prints, so the two
readings do not share a mistake.

The ⊘ mark (jumpPaths noDirect, without a run): the instructions the
page marks must be exactly the ones this check finds unreachable from the
ELF on its own: no direct jump or call goes there, an instruction ends
right before it and does not fall through (jmp, ret, iret, ...; nothing
known before: no mark), and its address is nowhere in the image: no
aligned 32-bit word of a data section, no number in another instruction's
operands as llvm-objdump prints them (movl $fn, pushl $fn), no fixup of
__ex_table, and no entry of a Linux stub array whose start is taken
(STUB_ARRAYS: early_idt_handler_array + i * 9, irq_entries_start and
spurious_entries_start + i * 8). The marks left are counted by what they
are and sampled.
Exit status 1 on any difference.
"""
import argparse
import bisect
import json
import os
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

SITE = Path(__file__).resolve().parent
sys.path.insert(0, str(SITE))
from importlib import import_module  # noqa: E402

gen_disasm = import_module("gen-disasm")

# Kinds as in jumps.js JK.
JCC, JMP, CALL, RET, LOOP, INT, IRET, SYS, IJMP, ICALL, LJMP, LCALL, LRET, EXC, IRQ, UD2 = range(1, 17)
NAMES = {JCC: "jcc", JMP: "jmp", CALL: "call", RET: "ret", LOOP: "loop", INT: "int", IRET: "iret", SYS: "sys",
         IJMP: "ijmp", ICALL: "icall", LJMP: "ljmp", LCALL: "lcall", LRET: "lret", UD2: "ud2"}
# Kinds after which the next instruction does not run.
NO_FALL = {JMP, IJMP, LJMP, RET, LRET, IRET}
PREFIXES = {0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3}
# The mnemonic family llvm-objdump prints for each kind (prefixes dropped).
MNEMONIC = {JCC: r"j(?!mp)[a-z]+", JMP: r"jmp[lw]?", IJMP: r"jmp[lw]?", CALL: r"call[lw]?", ICALL: r"call[lw]?",
            RET: r"ret[lw]?", LRET: r"lret[lw]?", IRET: r"iret[lw]?", LOOP: r"(loop[a-z]*|je?cxz)",
            INT: r"(int3?|into|int1)", UD2: r"ud[012]", SYS: r"sys(enter|exit|call|ret)l?",
            LJMP: r"ljmp[lw]?", LCALL: r"lcall[lw]?"}
SYSCALLS = {0x05: "syscall", 0x07: "sysret", 0x34: "sysenter", 0x35: "sysexit"}


def s32(b, n):
    return int.from_bytes(b[:n], "little", signed=True)


def decode(raw, va):
    """(kind, target va or -1, falls through) from an instruction's bytes, or None."""
    n, op16 = 0, False
    while n < len(raw) - 1 and raw[n] in PREFIXES:
        op16 |= raw[n] == 0x66
        n += 1
    b, end = raw[n], va + len(raw)
    if 0x70 <= b <= 0x7F:
        return JCC, end + s32(raw[n + 1:], 1), True
    if 0xE0 <= b <= 0xE3:
        return LOOP, end + s32(raw[n + 1:], 1), True
    if b in (0xE8, 0xE9):
        rel = s32(raw[n + 1:], 2 if op16 else 4)
        return (CALL if b == 0xE8 else JMP), (end + rel) & 0xFFFFFFFF, b == 0xE8
    if b == 0xEB:
        return JMP, end + s32(raw[n + 1:], 1), False
    if b in (0xEA, 0x9A):
        off = int.from_bytes(raw[n + 1:n + (3 if op16 else 5)], "little")
        return (LJMP if b == 0xEA else LCALL), off, b == 0x9A
    if b in (0xC2, 0xC3):
        return RET, -1, False
    if b in (0xCA, 0xCB):
        return LRET, -1, False
    if b == 0xCF:
        return IRET, -1, False
    if b in (0xCC, 0xCD, 0xCE, 0xF1):
        return INT, -1, True
    if b == 0x0F:
        b2 = raw[n + 1]
        if 0x80 <= b2 <= 0x8F:
            return JCC, (end + s32(raw[n + 2:], 2 if op16 else 4)) & 0xFFFFFFFF, True
        if b2 in (0x0B, 0xB9, 0xFF):
            return UD2, -1, True
        if b2 in SYSCALLS:
            return SYS, -1, b2 in (0x05, 0x34)
        return None
    if b == 0xFF:
        reg = raw[n + 1] >> 3 & 7
        kind = {2: ICALL, 3: LCALL, 4: IJMP, 5: LJMP}.get(reg)
        return (kind, -1, kind not in NO_FALL) if kind else None
    return None


def objdump(elf):
    """[(vaddr, raw bytes, text)] of every instruction llvm-objdump -d prints."""
    text = subprocess.run([os.environ.get("OBJDUMP", "llvm-objdump"), "-d", elf],
                          capture_output=True, text=True, check=True).stdout
    out = []
    for line in text.splitlines():
        m = gen_disasm.INSN.match(line)
        if not m:
            continue
        raw = bytes.fromhex(m.group(2).replace(" ", ""))
        asm = (m.group(3) or "").replace("\t", " ").strip()
        if not asm and out:
            out[-1] = (out[-1][0], out[-1][1] + raw, out[-1][2])
            continue
        out.append((int(m.group(1), 16), raw, asm))
    return out


def page_paths(disasm, symbols):
    """jumps.js jumpsStatic over disasm.json, as node runs it."""
    js = """
const J = require(process.argv[1]); const fs = require("fs");
const d = JSON.parse(fs.readFileSync(process.argv[2])), s = JSON.parse(fs.readFileSync(process.argv[3]));
const S = J.jumpsStatic(d.insns, J.jumpsToPhys(s.sections));
const inc = {}; for (const [pa, l] of S.inc) inc[pa] = l.map((i) => d.insns[i][0]);
const Sx = J.jumpsStatic(d.insns, J.jumpsToPhys(s.sections), d.taken);
const none = d.insns.map((x) => x[0]).filter((pa) => J.jumpPaths(Sx, null, pa).noDirect);
process.stdout.write(JSON.stringify({ pa: d.insns.map((x) => x[0]), kind: [...S.kind], fall: [...S.fall],
  target: [...S.target], inc, none }));
"""
    r = subprocess.run(["node", "-e", js, str(SITE / "jumps.js"), disasm, symbols],
                       capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


def sections(elf):
    """{name: (vaddr, bytes, executable)} of the ELF's allocated PROGBITS sections."""
    b = Path(elf).read_bytes()
    shoff, = struct.unpack_from("<I", b, 32)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 46)
    sh = [struct.unpack_from("<10I", b, shoff + k * shentsize) for k in range(shnum)]
    names = sh[shstrndx][4]
    out = {}
    for h in sh:
        if h[1] == 1 and h[2] & 2 and h[5]:
            name = b[names + h[0]:].split(b"\0", 1)[0].decode()
            out[name] = (h[3], b[h[4]:h[4] + h[5]], bool(h[2] & 4))
    return out


# Linux x86-32's arrays of entry stubs, reached as start + i * stride
# (EARLY_IDT_HANDLER_SIZE, IDT_ALIGN).
STUB_ARRAYS = {"early_idt_handler_array": 9, "irq_entries_start": 8, "spurious_entries_start": 8}


def elf_symbols(elf):
    """{name: (vaddr, size)} of the ELF's .symtab."""
    b = Path(elf).read_bytes()
    shoff, = struct.unpack_from("<I", b, 32)
    shentsize, shnum = struct.unpack_from("<HH", b, 46)
    sh = [struct.unpack_from("<10I", b, shoff + k * shentsize) for k in range(shnum)]
    out = {}
    for h in sh:
        if h[1] == 2:
            strs = sh[h[6]][4]
            for k in range(h[4], h[4] + h[5], 16):
                name, value, size = struct.unpack_from("<III", b, k)
                out.setdefault(b[strs + name:].split(b"\0", 1)[0].decode(), (value, size))
    return out


NUMBER = re.compile(r"(?<![\w%])\$?(-?0x[0-9a-f]+)")


def unreached(elf, insns):
    """{vaddr: (previous instruction's text, this one's, its bytes)} of the
    instructions no path can reach (see the docstring), from the ELF."""
    taken, targets = set(), set()
    for name, (addr, data, exe) in sections(elf).items():
        if name == "__ex_table":
            for k in range(0, len(data) // 12 * 12, 12):
                taken.add((addr + k + 4 + s32(data[k + 4:], 4)) & 0xFFFFFFFF)
        elif not exe:
            for k in range((4 - addr % 4) % 4, len(data) - 3, 4):
                taken.add(int.from_bytes(data[k:k + 4], "little"))
    end = {}
    for va, raw, text in insns:
        d = decode(raw, va) if raw else None
        if d and d[1] >= 0:
            if not (d[0] in (JCC, LOOP) and d[1] == va + len(raw)):
                targets.add(d[1])
        else:
            ops = re.sub(r"\s+#.*$|<[^>]*>", "", text).split(None, 1)
            for n in NUMBER.findall(ops[1] if len(ops) > 1 else ""):
                taken.add(int(n, 16) & 0xFFFFFFFF)
        end[va + len(raw)] = (text, not d or d[2])
    syms = elf_symbols(elf)
    for name, stride in STUB_ARRAYS.items():
        if name in syms and syms[name][0] in taken:
            start, size = syms[name]
            taken.update(range(start, start + max(size, stride), stride))
    out = {}
    for va, raw, text in insns:
        prev = end.get(va)
        if prev and not prev[1] and va not in targets and va not in taken:
            out[va] = (prev[0], text, raw)
    return out


def none_kind(text, prev, at, raw):
    """What a ⊘ instruction is, for the count: "padding" (nop, int3, zero
    bytes llvm-objdump decodes as addb), "function entry" (nothing calls
    it), or code after a jump."""
    mn = text.split(" ", 1)[0]
    if mn in ("nop", "nopl", "nopw", "int3") or raw[:1] == b"\0":
        what = "padding"
    elif at.endswith("+0x0"):
        what = "function entry nothing calls"
    else:
        what = "code inside a function"
    return f"{what} after {prev.split(' ', 1)[0]}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("disasm")
    ap.add_argument("symbols")
    ap.add_argument("--samples", type=int, default=50, help="functions to pick the printed example from")
    args = ap.parse_args()

    loads = gen_disasm.load_offsets(args.elf)
    page = page_paths(args.disasm, args.symbols)
    at = {pa: i for i, pa in enumerate(page["pa"])}
    problems, counts = [], {}
    want_inc = {}
    insns = objdump(args.elf)
    mnemonic_bad = 0
    for va, raw, text in insns:
        pa = gen_disasm.to_phys(loads, va)
        if pa is None:
            continue
        d = decode(raw, va) if raw else None
        words = [w for w in re.sub(r"\s+#.*$", "", text).split() if w.lower() not in
                 ("lock", "rep", "repe", "repne", "notrack", "bnd", "cs", "ds", "es", "ss", "fs", "gs", "data16", "addr16")]
        mn = (words[0] if words else "").lower()
        # the decoder and llvm-objdump's mnemonic must agree on what transfers
        family = [k for k, rx in MNEMONIC.items() if re.fullmatch(rx, mn)]
        if (d is None) != (not family) or (d and d[0] not in family):
            mnemonic_bad += 1
            if mnemonic_bad <= 10:
                problems.append(f"{pa:#x} {raw.hex()} {text!r}: decoded {d and NAMES[d[0]]}, objdump says {mn}")
        if pa not in at:
            problems.append(f"{pa:#x} {text!r}: not in disasm.json")
            continue
        i = at[pa]
        kind, target, fall = (d[0], d[1], d[2]) if d else (0, -1, True)
        tpa = -1
        if target >= 0:
            tpa = gen_disasm.to_phys(loads, target)
            # a jcc or loop to the next instruction is no path (both sides are the same)
            if kind in (JCC, LOOP) and target == va + len(raw):
                tpa = -1
            elif tpa is None:
                tpa = -2
            else:
                want_inc.setdefault(tpa, []).append(pa)
        if kind:
            counts[NAMES[kind]] = counts.get(NAMES[kind], 0) + 1
        got = (page["kind"][i], page["target"][i], bool(page["fall"][i]))
        if got != (kind, tpa, fall):
            problems.append(f"{pa:#x} {text!r}: page {got}, ELF bytes {(kind, tpa, fall)}")
    got_inc = {int(k): sorted(v) for k, v in page["inc"].items()}
    want_inc = {k: sorted(v) for k, v in want_inc.items()}
    for t in sorted(set(got_inc) | set(want_inc)):
        if got_inc.get(t) != want_inc.get(t):
            problems.append(f"paths into {t:#x}: page {got_inc.get(t)}, ELF {want_inc.get(t)}")

    # Detail for sampled functions: every instruction's paths in and out.
    syms = [s for s in json.loads(Path(args.symbols).read_text())["symbols"]
            if s["size"] > 0 and s["section"].startswith((".text", ".init.text", ".head.text"))]
    rnd = random.Random(1)
    sample = rnd.sample(syms, min(args.samples, len(syms)))
    detail = 0
    for s in sample:
        for pa in page["pa"]:
            if s["paddr"] <= pa < s["paddr"] + s["size"]:
                detail += 1
    print(f"test_jumps: {len(insns)} instructions, transfers by kind {json.dumps(counts, sort_keys=True)}, "
          f"{len(want_inc)} static targets; {len(sample)} sampled functions ({detail} instructions); "
          f"{len(problems)} problems")
    if sample:
        s = max(sample, key=lambda x: sum(1 for pa in want_inc if x["paddr"] <= pa < x["paddr"] + x["size"]))
        ex = [(hex(t), [hex(f) for f in want_inc[t]]) for t in sorted(want_inc) if s["paddr"] <= t < s["paddr"] + s["size"]][:5]
        print(f"  e.g. {s['name']}: paths into {ex}")
    # The ⊘ marks.
    want = {}
    for va, why in unreached(args.elf, insns).items():
        pa = gen_disasm.to_phys(loads, va)
        if pa is not None:
            want[pa] = why
    got = set(page["none"])
    funcs = sorted((s["paddr"], s["name"]) for s in json.loads(Path(args.symbols).read_text())["symbols"] if s["size"] > 0)

    def where(pa):
        k = bisect.bisect_right(funcs, (pa, "\uffff")) - 1
        return f"{funcs[k][1]}+{pa - funcs[k][0]:#x}" if k >= 0 else hex(pa)
    for pa in sorted(got - set(want))[:20]:
        problems.append(f"⊘ at {pa:#x} {where(pa)}: the ELF has a path in (a direct jump or call, fall-through or its address taken)")
    for pa in sorted(set(want) - got)[:20]:
        problems.append(f"no ⊘ at {pa:#x} {where(pa)} {want[pa]}: nothing in the ELF reaches it")
    if got != set(want):
        problems.append(f"⊘: page {len(got)}, ELF {len(want)}, {len(got ^ set(want))} differ")
    kinds = {}
    for pa in sorted(want):
        kinds.setdefault(none_kind(want[pa][1], want[pa][0], where(pa), want[pa][2]), []).append(pa)
    print(f"  ⊘ left: {len(want)} of {len(page['pa'])} instructions")
    for k, l in sorted(kinds.items(), key=lambda x: -len(x[1])):
        print(f"    {len(l)} {k}, e.g. " + "; ".join(f"{pa:#x} {where(pa)} {want[pa][1]!r}" for pa in l[:3]))
    for p in problems[:30]:
        print("  " + p)
    print(f"test_jumps: {len(problems)} problems in all")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
