#!/usr/bin/env python3
"""Prove that the -g build the page's debug data comes from has the same
code as the kernel the page boots.

  site/check-code-identity.py PLAIN_ELF PLAIN_IMAGE DEBUG_ELF DEBUG_IMAGE [--self-test]

PLAIN_* is the build that is shipped (its ELF and the bzImage the page
boots), DEBUG_* the copy built with -g. Passes when
  1. both ELFs give the same `llvm-objcopy -O binary` image, except inside
     note sections (the GNU build-id is a hash over the whole ELF, debug
     info included, so it differs), and
  2. the plain ELF's binary image is inside PLAIN_IMAGE (the shipped
     bzImage carries this kernel), and the two bzImages are byte-identical
     except inside that copy's note sections.
--self-test then flips one code byte of the debug files (must fail) and,
when there is a note, one note byte (must still pass).
"""
import os
import struct
import subprocess
import sys
import tempfile

SHT_NOTE, SHT_NOBITS, SHF_ALLOC = 7, 8, 2


def binary(elf):
    """llvm-objcopy -O binary output of elf."""
    with tempfile.TemporaryDirectory() as t:
        out = os.path.join(t, "bin")
        subprocess.run(["llvm-objcopy", "-O", "binary", elf, out], check=True)
        return open(out, "rb").read()


def note_ranges(elf):
    """[start, end) of each note section inside elf's binary image: the
    image starts at the lowest load address (LMA) of an allocated section
    with contents."""
    b = open(elf, "rb").read()
    if b[:4] != b"\x7fELF" or b[4] != 1:
        sys.exit(f"{elf}: not a 32-bit ELF")
    e_phoff, e_shoff = struct.unpack_from("<II", b, 28)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", b, 42)
    loads = [struct.unpack_from("<IIIIII", b, e_phoff + i * e_phentsize) for i in range(e_phnum)]
    loads = [p for p in loads if p[0] == 1]   # PT_LOAD: type, offset, vaddr, paddr, filesz, memsz

    def lma(off, addr):
        for _, p_off, p_va, p_pa, p_fs, _ in loads:
            if p_off <= off < p_off + p_fs:
                return addr - p_va + p_pa
        return None
    secs = []
    for i in range(e_shnum):
        _, typ, flags, addr, off, size = struct.unpack_from("<IIIIII", b, e_shoff + i * e_shentsize)
        if flags & SHF_ALLOC and typ != SHT_NOBITS and size:
            secs.append((typ, lma(off, addr), size))
    base = min(s[1] for s in secs if s[1] is not None)
    return [(a - base, a - base + n) for typ, a, n in secs if typ == SHT_NOTE and a is not None]


def outside(diffs, ranges):
    return [d for d in diffs if not any(lo <= d < hi for lo, hi in ranges)]


def differ(a, b):
    return [i for i in range(min(len(a), len(b))) if a[i] != b[i]]


def check(plain_bin, debug_bin, notes, plain_img, debug_img):
    """Problems (empty: identical code), and what was checked."""
    problems = []
    if len(plain_bin) != len(debug_bin):
        problems.append(f"binary images differ in size: {len(plain_bin)} vs {len(debug_bin)} bytes")
    bad = outside(differ(plain_bin, debug_bin), notes)
    if bad:
        problems.append(f"binary images differ outside note sections at {len(bad)} bytes, first at offset {bad[0]:#x}")
    at = plain_img.find(plain_bin)
    if at < 0:
        problems.append("the plain ELF's binary image is not inside the shipped bzImage")
        return problems, ""
    if len(plain_img) != len(debug_img):
        problems.append(f"bzImages differ in size: {len(plain_img)} vs {len(debug_img)} bytes")
    d = differ(plain_img, debug_img)
    bad = outside(d, [(at + lo, at + hi) for lo, hi in notes])
    if bad:
        problems.append(f"bzImages differ outside the kernel's note sections at {len(bad)} bytes, first at offset {bad[0]:#x}")
    return problems, (f"binary image {len(plain_bin)} B at bzImage offset {at:#x}; bzImages {len(plain_img)} B, "
                      f"{len(d)} bytes differ, all inside note sections {[(hex(at + lo), hex(at + hi)) for lo, hi in notes]}")


def main():
    args = [a for a in sys.argv[1:] if a != "--self-test"]
    if len(args) != 4:
        sys.exit(__doc__)
    plain_elf, plain_image, debug_elf, debug_image = args
    plain_bin, debug_bin = binary(plain_elf), binary(debug_elf)
    notes = note_ranges(plain_elf)
    if note_ranges(debug_elf) != notes:
        sys.exit(f"FAIL: note sections differ: {note_ranges(debug_elf)} vs {notes}")
    plain_img, debug_img = open(plain_image, "rb").read(), open(debug_image, "rb").read()
    problems, what = check(plain_bin, debug_bin, notes, plain_img, debug_img)
    if problems:
        print("\n".join(f"::error::{debug_elf}: {p}" for p in problems), file=sys.stderr)
        print(f"FAIL: the -g build of {plain_elf} does not have the code of the kernel that boots", file=sys.stderr)
        return 1
    print(f"PASS: {debug_elf} has the code of {plain_elf} / {plain_image}: {what}")
    if "--self-test" in sys.argv:
        at = plain_img.find(plain_bin)
        code = next(i for i in range(len(debug_bin)) if not any(lo <= i < hi for lo, hi in notes) and debug_bin[i])
        flip = lambda b, i: b[:i] + bytes([b[i] ^ 1]) + b[i + 1:]
        if not check(plain_bin, flip(debug_bin, code), notes, plain_img, debug_img)[0] or \
                not check(plain_bin, debug_bin, notes, plain_img, flip(debug_img, at + code))[0]:
            print("FAIL: self-test: a flipped code byte was not caught", file=sys.stderr)
            return 1
        if notes:
            n = notes[0][0] + (notes[0][1] - notes[0][0]) - 1
            if check(plain_bin, flip(debug_bin, n), notes, plain_img, flip(debug_img, at + n))[0]:
                print("FAIL: self-test: a flipped note byte was reported", file=sys.stderr)
                return 1
        print(f"self-test: a flipped code byte (offset {code:#x}) fails the check" +
              (", a flipped build-id byte does not" if notes else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
