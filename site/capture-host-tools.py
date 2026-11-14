#!/usr/bin/env python3
"""Record what a kernel build's host tools read and wrote (plan E0d), as
fixtures for test_clang_worker.py --tool-fixtures: the page's Worker runs
the wasm build of each tool (gen-host-tools.py) on the same input, and the
output must equal what the native tool wrote in CI.

  capture-host-tools.py --name NAME --tree TREE --out DIR [options] -- MAKE ARGS...

After a full build of TREE, each named tool is replaced by a wrapper, the
files named by --rm are deleted and the build command runs again (the same
command as the build, so nothing else is rebuilt): the steps after the
deleted files run once more, through the wrappers. A wrapper runs the real
tool and writes DIR/NAME-TOOL-N.json: the argv, the working directory, the
SHA-256 of every file it was given (the arguments naming files that exist,
plus --extra files; a file the tool rewrites with the same bytes is left
out, so the tool must make it) and of every file it wrote (arguments whose
file was rewritten) and of its stdout ("-"). The bytes are stored once in
DIR/blobs/SHA256. Then the tools are put back, and the files named by
--same must have the bytes they had before (the second run made the same
build).

Options:
  --tool PATH[=GLOB,...]    a host program built in TREE (e.g. scripts/kallsyms); with
                            GLOBs, only runs with an argument matching one are recorded
  --path-tool NAME[=GLOB,...]  a program found in PATH (nasm, llvm-objcopy, ld.lld,
                            llvm-nm): a wrapper of that name goes first in PATH
  --module TOOL=FILE        the wasm file the page runs for TOOL (copied to
                            DIR/modules/NAME/); llvm-* and ld.lld run in clang.wasm
  --extra TOOL=FILE         a file TOOL reads that is not an argument (sorttable: System.map)
  --rm FILE                 delete before the second build
  --same FILE               must be byte-identical after the second build
"""

import argparse
import fnmatch
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ME = pathlib.Path(__file__).resolve()
REAL = ".mkb-real"


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def put_blob(out, data):
    h = sha256_bytes(data)
    p = out / "blobs" / h
    if not p.exists():
        p.parent.mkdir(parents=True, exist_ok=True)
        tmp = p.with_name(f".{h}.{os.getpid()}")
        tmp.write_bytes(data)
        os.replace(tmp, p)
    return h


def wrap(cfg_path, key, args):
    """Run the real tool for key with args; record the run when it matches."""
    cfg = json.loads(pathlib.Path(cfg_path).read_text())
    t = cfg["tools"][key]
    argv = [t["real"]] + args
    if t["globs"] and not any(fnmatch.fnmatch(a, g) for a in args for g in t["globs"]):
        os.execv(t["real"], argv)
    cwd = pathlib.Path.cwd()
    names = [a for a in args if not a.startswith("-") and (cwd / a).is_file()] + \
        [f for f in t["extra"] if (cwd / f).is_file()]
    names = list(dict.fromkeys(names))
    before = {}
    for n in names:
        p = cwd / n
        st = p.stat()
        before[n] = (p.read_bytes(), st.st_mtime_ns, st.st_ino)
    r = subprocess.run(argv, stdout=subprocess.PIPE)
    sys.stdout.buffer.write(r.stdout)
    sys.stdout.flush()
    out = pathlib.Path(cfg["out"])
    files, outputs = [], [["-", put_blob(out, r.stdout)]]
    written = [a for a in args if not a.startswith("-") and (cwd / a).is_file()]
    for n in dict.fromkeys(names + written):
        p = cwd / n
        if os.path.isabs(n) or ".." in pathlib.PurePath(n).parts:
            sys.exit(f"capture-host-tools: {key}: {n} is not under {cwd}")
        st = p.stat() if p.exists() else None
        old = before.get(n)
        rewritten = st and (old is None or (st.st_mtime_ns, st.st_ino) != old[1:])
        data = p.read_bytes() if st else None
        if old and not (rewritten and data == old[0]):
            files.append([n, put_blob(out, old[0])])
        if rewritten:
            outputs.append([n, put_blob(out, data)])
    rec = {"name": cfg["name"], "tool": t["tool"], "module": t["module"], "args": [t["argv0"]] + args,
           "cwd": str(cwd), "code": r.returncode, "files": files, "outputs": outputs}
    fd, tmp = tempfile.mkstemp(dir=out, prefix=f".{cfg['name']}-{t['tool']}-")
    with os.fdopen(fd, "w") as f:
        json.dump(rec, f, indent=1)
    os.replace(tmp, out / f"{cfg['name']}-{t['tool']}-{sha256_bytes(json.dumps(rec['args']).encode())[:12]}.json")
    sys.exit(r.returncode)


def main():
    if len(sys.argv) > 3 and sys.argv[1] == "wrap":
        return wrap(sys.argv[2], sys.argv[3], sys.argv[4:])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tool", action="append", default=[])
    ap.add_argument("--path-tool", action="append", default=[])
    ap.add_argument("--module", action="append", default=[])
    ap.add_argument("--extra", action="append", default=[])
    ap.add_argument("--rm", action="append", default=[])
    ap.add_argument("--same", action="append", default=[])
    ap.add_argument("make", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    make = a.make[1:] if a.make[:1] == ["--"] else a.make
    if not make:
        ap.error("no build command after --")
    tree = pathlib.Path(a.tree).resolve()
    out = pathlib.Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    modules = dict(m.split("=", 1) for m in a.module)
    extra = {}
    for e in a.extra:
        k, v = e.split("=", 1)
        extra.setdefault(k, []).append(v)

    def spec(s):
        name, _, globs = s.partition("=")
        return name, [g for g in globs.split(",") if g]

    tmp = pathlib.Path(tempfile.mkdtemp(prefix="mkb-capture-"))
    cfg = {"name": a.name, "out": str(out), "tools": {}}
    wrappers = []
    for s in a.tool:
        path, globs = spec(s)
        tool = os.path.basename(path)
        cfg["tools"][path] = {"real": str(tree / (path + REAL)), "tool": tool, "argv0": path, "globs": globs,
                              "extra": extra.get(tool, []), "module": None}
        wrappers.append((tree / path, path, True))
    bindir = tmp / "bin"
    bindir.mkdir()
    for s in a.path_tool:
        name, globs = spec(s)
        real = shutil.which(name)
        if not real:
            sys.exit(f"{name}: not in PATH")
        cfg["tools"][name] = {"real": real, "tool": name, "argv0": name, "globs": globs,
                              "extra": extra.get(name, []), "module": None}
        wrappers.append((bindir / name, name, False))
    for key, t in cfg["tools"].items():
        m = modules.get(t["tool"])
        if m == "clang":
            t["module"] = "clang"
        elif m:
            dest = out / "modules" / a.name / (t["tool"] + ".wasm")
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(m, dest)
            t["module"] = str(dest.relative_to(out))
        else:
            sys.exit(f"no --module for {t['tool']}")
    cfgfile = tmp / "capture.json"
    cfgfile.write_text(json.dumps(cfg))
    same = {f: sha256_bytes((tree / f).read_bytes()) for f in a.same}
    before = {p.name for p in out.glob(f"{a.name}-*.json")}
    moved = []
    try:
        for w, key, in_tree in wrappers:
            if in_tree:
                os.rename(w, str(w) + REAL)
                moved.append(w)
            w.write_text(f"#!/bin/sh\nexec {sys.executable} {ME} wrap {cfgfile} {key} \"$@\"\n")
            w.chmod(0o755)
        for f in a.rm:
            (tree / f).unlink(missing_ok=True)
        env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}")
        subprocess.run(make, cwd=tree, env=env, check=True)
    finally:
        for w in moved:
            os.replace(str(w) + REAL, w)
        shutil.rmtree(tmp)
    problems = [f"{f}: {sha256_bytes((tree / f).read_bytes())} after the second build, {h} before"
                for f, h in same.items() if sha256_bytes((tree / f).read_bytes()) != h]
    new = sorted(p.name for p in out.glob(f"{a.name}-*.json") if p.name not in before)
    for key, t in cfg["tools"].items():
        mine = [n for n in new if n.startswith(f"{a.name}-{t['tool']}-")]
        bad = [n for n in mine if json.loads((out / n).read_text())["code"] != 0]
        print(f"{a.name}: {t['tool']}: {len(mine)} runs recorded" + (f", {len(bad)} failed" if bad else ""))
        if not mine:
            problems.append(f"{t['tool']}: no run recorded")
        if bad:
            problems.append(f"{t['tool']}: runs that failed in the build: {bad}")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
