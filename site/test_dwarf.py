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
its "Run to file:line", `gen-dwarf.js --lines`, dwarftables.js dwarfFileCode):
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
page's dwarffuncs.js DwarfFuncs/dwFuncProto and unwind.js cfiFrameInfo), half
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
picks (`gen-dwarf.js --vars`, the page's dwarflocals.js dwVarDecl/dwVarsPick):
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
import random
import sys

from tests.dwarf.common import functions, loads, page_frames, symbolize
from tests.dwarf.types import check_types
from tests.dwarf.cfi import check_cfi, eh_frame_selftest
from tests.dwarf.locals import check_locals
from tests.dwarf.operands import check_operands
from tests.dwarf.lines import check_lines
from tests.dwarf.funcs import check_funcs
from tests.dwarf.vars import check_vars
from tests.dwarf.inline_blocks import inline_blocks_selftest


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
