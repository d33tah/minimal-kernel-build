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
readings do not share a mistake. Exit status 1 on any difference.
"""
import argparse
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
process.stdout.write(JSON.stringify({ pa: d.insns.map((x) => x[0]), kind: [...S.kind], fall: [...S.fall],
  target: [...S.target], inc }));
"""
    r = subprocess.run(["node", "-e", js, str(SITE / "jumps.js"), disasm, symbols],
                       capture_output=True, text=True, check=True)
    return json.loads(r.stdout)


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
    for p in problems[:30]:
        print("  " + p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
