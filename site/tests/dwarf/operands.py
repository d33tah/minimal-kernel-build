"""test_dwarf.py --operands: the names of memory operands against gdb."""

import base64
import json
import os
import pathlib
import re
import struct
import subprocess
import tempfile

from tests.dwarf.common import SITE


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
