#!/usr/bin/env python3
"""Build the kernels' build host tools as wasm32-wasip1 programs for the
page's Worker (clang.js, wasi.js), with the wasi-sdk that gen-clang-wasm.sh
pins (plan E0d). The browser runs them after clang.wasm's compile and link
to make a bzImage.

  gen-host-tools.py --wasi-sdk DIR kbuild TREE OUT   the host tools a built kbuild TREE
                                                    used: scripts/kallsyms, sorttable,
                                                    recordmcount, arch/x86/tools/relocs,
                                                    arch/x86/boot/tools/build,
                                                    arch/x86/boot/compressed/mkpiggy
  gen-host-tools.py --wasi-sdk DIR hello TREE OUT    hello's tools/build
  gen-host-tools.py --wasi-sdk DIR nasm OUT          nasm from its pinned release tarball
  gen-host-tools.py --check OUT                      OUT/nasm.wasm and OUT/LICENSE.nasm are
                                                    the pinned build (offline)

kbuild: each tool is compiled from the tree's own source with the tree's
own host compile command (the kbuild .cmd file next to the tool, so the
same defines, include paths and -O level), with --target=wasm32-wasip1 in
place of the host. A tool the tree did not build is left out. hello: the
compile command of hello/Makefile (make -n). OUT/tools.json lists what
was built (tool path in the tree -> wasm file, the compile command).

What WASI and wasi-libc lack (mmap written back to the file, a thread,
sorttable's popen of grep and awk) comes from hosttools/wasi-shim.c,
included and linked into every tool; the kernel sources are not changed.
Their wasm memory may grow to 1 GiB (MAX_MEMORY).
<elf.h> is musl's from the wasi-libc of wasi-sdk 29 (pinned below by
SHA-256), since wasi-libc does not install it.

nasm: the release tarball pinned by SHA-256, configured for
wasm32-wasip1 and stripped; the result is pinned too (two builds in
different directories gave the same bytes) and checked after the build
and by --check. Its licence (BSD-2-Clause) goes to OUT/LICENSE.nasm.
"""

import argparse
import hashlib
import json
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

SITE = pathlib.Path(__file__).resolve().parent
SHIM = SITE / "hosttools"

# musl's elf.h at the wasi-libc commit of wasi-sdk 29 (gen-clang-wasm.sh's
# WASI_LIBC_COMMIT); MIT (clang/LICENSE.wasi-libc-musl).
ELF_H_URL = ("https://raw.githubusercontent.com/WebAssembly/wasi-libc/"
             "ac020b86fd44bafe60aa4fa12f407d16e3731329/libc-top-half/musl/include/elf.h")
ELF_H_SHA256 = "ebfb1791bae850a20dbd4ede3908365d63a38c3677f89397d36ea5a706ec6f4a"

NASM_VERSION = "2.16.03"
NASM_URL = f"https://www.nasm.us/pub/nasm/releasebuilds/{NASM_VERSION}/nasm-{NASM_VERSION}.tar.xz"
NASM_TAR_SHA256 = "1412a1c760bbd05db026b6c0d1657affd6631cd0a63cddb6f73cc6d4aa616148"
NASM_SUMS = {
    "nasm.wasm": "9f6346a0c130eb74ba1a8458d62aaf1515aa0cb7ba62530685684bb56b17818b",
    "LICENSE.nasm": "1563996c52e220e15ef2418e67d39488255aa8c28c89e617074d3afe3ee329e0",
}

# The most wasm memory a kernel tool may grow to. Debian's largest input is
# its 245 MB vmlinux; without a limit, recordmcount on fs/open.o cut to
# half grew its memory until Chromium's renderer crashed (Firefox and
# WebKit trapped with an out-of-bounds access).
MAX_MEMORY = 1 << 30

KBUILD_TOOLS = [
    "scripts/kallsyms",
    "scripts/sorttable",
    "scripts/recordmcount",
    "arch/x86/tools/relocs",
    "arch/x86/boot/tools/build",
    "arch/x86/boot/compressed/mkpiggy",
]


def sha256(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def fetch(url, dest, want):
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                data = r.read()
            break
        except OSError as e:
            if attempt == 4:
                raise
            print(f"fetch {url}: {e}, again", file=sys.stderr)
    got = hashlib.sha256(data).hexdigest()
    if got != want:
        sys.exit(f"{url}: SHA-256 {got}, pinned {want}")
    pathlib.Path(dest).write_bytes(data)


class Wasm:
    def __init__(self, sdk, work):
        self.sdk = pathlib.Path(sdk).resolve()
        self.cc = [str(self.sdk / "bin" / "clang"), "--target=wasm32-wasip1"]
        self.inc = pathlib.Path(work) / "include"
        self.inc.mkdir(parents=True, exist_ok=True)
        fetch(ELF_H_URL, self.inc / "elf.h", ELF_H_SHA256)
        self.shim = pathlib.Path(work) / "wasi-shim.o"
        subprocess.run(self.cc + ["-O2", "-Wall", "-Wextra", "-Werror", "-c", "-o", str(self.shim),
                                  str(SHIM / "wasi-shim.c")], check=True)

    def host_command(self, argv, out):
        """A host compile+link command (compiler first) for wasm: the
        compiler replaced, dependency-file flags dropped, the output named
        out, the shim and elf.h added."""
        args, skip = [], False
        for a in argv[1:]:
            if skip:
                skip = False
                continue
            if a.startswith("-Wp,-MMD") or a.startswith("-Wp,-MD"):
                continue
            if a == "-o":
                skip = True
                continue
            args.append(a)
        return self.cc + ["-I", str(self.inc), "-include", str(SHIM / "wasi-shim.h")] + args + \
            [str(self.shim), "-Wl,--strip-all", f"-Wl,--max-memory={MAX_MEMORY}", "-o", str(out)]


def kbuild_cmd(tree, target):
    """The command kbuild recorded for target (its .cmd file)."""
    d, name = os.path.split(target)
    f = pathlib.Path(tree) / d / f".{name}.cmd"
    if not f.exists():
        return None
    for line in f.read_text().splitlines():
        for key in (f"cmd_{target} := ", f"savedcmd_{target} := "):
            if line.startswith(key):
                return line[len(key):]
    sys.exit(f"{f}: no command for {target}")


def build_kbuild(w, tree, out):
    tree = pathlib.Path(tree).resolve()
    built = {}
    for tool in KBUILD_TOOLS:
        if not (tree / tool).exists():
            continue
        cmd = kbuild_cmd(tree, tool)
        if cmd is None:
            sys.exit(f"{tree}/{tool}: built, but no .cmd file")
        argv = shlex.split(cmd)
        objs = [a for a in argv if a.endswith(".o")]
        wasm = out / (os.path.basename(tool) + ".wasm")
        if objs:
            # A host program of several objects: compile each with its own
            # recorded command (to a .o of our own), then link those.
            with tempfile.TemporaryDirectory() as tmp:
                ours, compiles = [], []
                for o in objs:
                    oargv = shlex.split(kbuild_cmd(tree, o))
                    mine = pathlib.Path(tmp) / os.path.basename(o)
                    c = w.host_command(oargv, mine)
                    # -c: compile only (the shim and strip are for the link).
                    c = [x for x in c if x != str(w.shim) and not x.startswith("-Wl,")]
                    subprocess.run(c, cwd=tree, check=True)
                    compiles.append(shlex.join(oargv))
                    ours.append(str(mine))
                link = [a if not a.endswith(".o") else ours[objs.index(a)] for a in argv]
                subprocess.run(w.host_command(link, wasm), cwd=tree, check=True)
            built[tool] = {"wasm": wasm.name, "command": cmd, "objects": compiles}
        else:
            subprocess.run(w.host_command(argv, wasm), cwd=tree, check=True)
            built[tool] = {"wasm": wasm.name, "command": cmd}
        built[tool]["source_sha256"] = {
            str(p.relative_to(tree)): sha256(p) for p in sorted((tree / os.path.dirname(tool)).glob(os.path.basename(tool) + "*.c"))}
        print(f"{tool} -> {wasm} ({wasm.stat().st_size} B)")
    return built


def build_hello(w, tree, out):
    tree = pathlib.Path(tree).resolve()
    lines = subprocess.run(["make", "-s", "-n", "-B", "-C", str(tree), "LLVM=1", "tools/build"],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    cmd = next(x for x in lines if " -o tools/build " in x)
    wasm = out / "build.wasm"
    subprocess.run(w.host_command(shlex.split(cmd), wasm), cwd=tree, check=True)
    print(f"tools/build -> {wasm} ({wasm.stat().st_size} B)")
    return {"tools/build": {"wasm": wasm.name, "command": cmd, "source_sha256": {"tools/build.c": sha256(tree / "tools/build.c")}}}


def build_nasm(w, out):
    with tempfile.TemporaryDirectory() as tmp:
        tar = pathlib.Path(tmp) / "nasm.tar.xz"
        fetch(NASM_URL, tar, NASM_TAR_SHA256)
        with tarfile.open(tar) as t:
            t.extractall(tmp, filter="data")
        src = pathlib.Path(tmp) / f"nasm-{NASM_VERSION}"
        cc = " ".join(w.cc)
        subprocess.run(["./configure", "--host=wasm32-wasip1", f"CC={cc}", "CFLAGS=-O2",
                        "LDFLAGS=-Wl,--strip-all", f"AR={w.sdk}/bin/llvm-ar", f"RANLIB={w.sdk}/bin/llvm-ranlib"],
                       cwd=src, check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["make", f"-j{os.cpu_count() or 1}", "nasm"], cwd=src, check=True, stdout=subprocess.DEVNULL)
        shutil.copy(src / "nasm", out / "nasm.wasm")
        shutil.copy(src / "LICENSE", out / "LICENSE.nasm")
    print(f"nasm {NASM_VERSION} -> {out / 'nasm.wasm'} ({(out / 'nasm.wasm').stat().st_size} B)")
    return {"nasm": {"wasm": "nasm.wasm", "version": NASM_VERSION, "tarball_sha256": NASM_TAR_SHA256}}


def check_nasm(out):
    bad = [f"{n}: {sha256(out / n) if (out / n).exists() else 'missing'}, pinned {want}"
           for n, want in NASM_SUMS.items() if not (out / n).exists() or sha256(out / n) != want]
    if bad:
        print("\n".join(bad), file=sys.stderr)
        return 1
    print(f"{out}: nasm {NASM_VERSION} for wasm32-wasip1, SHA-256 {NASM_SUMS['nasm.wasm']}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wasi-sdk", help="wasi-sdk 29 directory (gen-clang-wasm.sh's tarball)")
    ap.add_argument("--check", metavar="OUT", help="check OUT/nasm.wasm and OUT/LICENSE.nasm against the pins")
    ap.add_argument("kind", nargs="?", choices=["kbuild", "hello", "nasm"])
    ap.add_argument("paths", nargs="*", help="TREE OUT (kbuild, hello) or OUT (nasm)")
    a = ap.parse_args()
    if a.check:
        return check_nasm(pathlib.Path(a.check))
    if not a.kind or not a.wasi_sdk or len(a.paths) != (1 if a.kind == "nasm" else 2):
        ap.error("need --wasi-sdk DIR and kbuild TREE OUT | hello TREE OUT | nasm OUT")
    out = pathlib.Path(a.paths[-1]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        w = Wasm(a.wasi_sdk, work)
        if a.kind == "kbuild":
            built = build_kbuild(w, a.paths[0], out)
        elif a.kind == "hello":
            built = build_hello(w, a.paths[0], out)
        else:
            built = build_nasm(w, out)
    old = json.loads((out / "tools.json").read_text()) if (out / "tools.json").exists() else {}
    old.update(built)
    (out / "tools.json").write_text(json.dumps(old, indent=1, sort_keys=True) + "\n")
    if a.kind == "nasm":
        return check_nasm(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
