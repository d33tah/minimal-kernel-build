"""test_dwarf.py --inline-blocks: locals of an always_inline function's blocks."""

import json
import os
import pathlib
import re
import subprocess
import tempfile

from tests.dwarf.common import SITE


# For --inline-blocks: an inlined call (sum in _start) whose lexical block
# has no DW_AT_abstract_origin and whose own children differ from its
# abstract function's (the inlined call of twice).
INLINE_BLOCKS_C = r"""
static inline __attribute__((always_inline)) int twice(int x) { return x * 2; }
struct node { struct node *next; int v; };
static inline __attribute__((always_inline)) int sum(struct node *n)
{
	int a = twice(n->v);
	{
		struct node *head = n->next;
		int inner = head ? head->v : 0;
		a += inner;
	}
	return a + 1;
}
int sink;
void _start(void) { struct node y = { 0, 2 }, x = { &y, 1 }; sink = sum(&x); for (;;); }
"""


def inline_blocks_selftest():
    """Every instruction of the inlined sum: n and a with a location; head
    and inner listed exactly inside the concrete block (llvm-dwarfdump's
    DW_AT_low_pc/high_pc), with a location; no name listed twice."""
    bad = []
    with tempfile.TemporaryDirectory() as tmp:
        src, elf, out = os.path.join(tmp, "t.c"), os.path.join(tmp, "t.elf"), os.path.join(tmp, "t")
        pathlib.Path(src).write_text(INLINE_BLOCKS_C)
        subprocess.run(["clang", "-fuse-ld=lld", "-m32", "-g", "-O0", "-fno-pie", "-nostdlib", "-static",
                        "-Wl,-Ttext=0xc0100000", "-o", elf, src], check=True)
        subprocess.run(["node", str(SITE / "gen-dwarf.js"), elf, out], check=True, capture_output=True)
        dump = subprocess.run(["llvm-dwarfdump", "--debug-info", elf], capture_output=True, text=True, check=True).stdout
        pcs = lambda chunk: [int(x, 16) for x in re.findall(r"DW_AT_(?:low|high)_pc\s+\((0x[0-9a-f]+)\)", chunk)[:2]]
        inl = re.search(r"DW_TAG_inlined_subroutine\s+DW_AT_abstract_origin\s+\(0x[0-9a-f]+ \"sum\"\)(.*?)(?=\n0x[0-9a-f]+: {6}\S|\Z)", dump, re.S)
        blk = re.search(r"DW_TAG_lexical_block\s+DW_AT_low_pc.*?\n\s*DW_AT_high_pc[^\n]*", inl.group(1), re.S) if inl else None
        if not inl or not blk or "DW_AT_abstract_origin" in blk.group(0):
            return [f"clang's DWARF is not the shape this test needs: {dump}"]
        lo, hi = pcs(inl.group(0))
        blo, bhi = pcs(blk.group(0))
        if re.search(r"DW_TAG_inlined_subroutine", inl.group(1)) is None:
            return ["clang inlined no call inside sum: the test needs one"]
        insns = [int(m.group(1), 16) for m in re.finditer(r"^\s*([0-9a-f]+):", subprocess.run(
            ["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={lo}", f"--stop-address={hi}", elf],
            capture_output=True, text=True, check=True).stdout, re.M)]
        got = subprocess.run(["node", str(SITE / "gen-dwarf.js"), "--locals", out], input="\n".join(map(str, insns)),
                             capture_output=True, text=True, check=True).stdout
        for a, line in zip(insns, got.splitlines()):
            f = json.loads(line)["frames"]
            f = f[0] if f and f[0]["func"] == "sum" else next((x for x in f if x["func"] == "sum"), None)
            if f is None:
                bad.append(f"{a:#x}: no frame of sum")
                continue
            names = [v[0] for v in f["vars"]]
            want = ["n", "a"] + (["head", "inner"] if blo <= a < bhi else [])
            if sorted(names) != sorted(want):
                bad.append(f"{a:#x} ({'inside' if blo <= a < bhi else 'outside'} the block {blo:#x}-{bhi:#x}): page lists {f['vars']}, want {want}")
            elif any(v[1] == "out" for v in f["vars"]):
                bad.append(f"{a:#x}: optimized out in {f['vars']}")
        for b in bad[:10]:
            print("INLINE BLOCK", b)
        print(f"inline blocks: {len(insns) - len(bad)}/{len(insns)} instructions of the inlined sum list n, a and the block's head, inner "
              f"exactly inside {blo:#x}-{bhi:#x}")
    return bad
