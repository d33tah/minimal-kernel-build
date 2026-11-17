#!/usr/bin/env python3
"""The build kit of a kernel (plan E3): what the page needs to build the
kernel again in the browser (site/kit.js, with clang.wasm and the host
tools in wasm), made from CI's -g build of it.

  gen-kit.py --name NAME --tree TREE --out KITS [options] -- MAKE ARGS...
  gen-kit.py --index KITS
  gen-kit.py --strip TREE OUT.json   (strip(): the kernel objects without DWARF)

After the full build of TREE, the build's tools are replaced by wrappers
(clang, ld.lld, llvm-ar, llvm-nm, llvm-objcopy, nasm, sed and cat through
PATH; the tree's own host tools named by --tool in place), the build is
made to run its steps again (--touch-objects: every object is touched, so
kbuild makes the archives, links and the image again; --clean FILE: the
files are deleted first) with MAKE ARGS (the build's own command, plus
-j1), and every wrapped run records its argv, working directory, the
files it read and wrote, its stdin and stdout (a file, a pipe, nothing)
and, for a compile, the files it included (its dependency file, or the
same compile again with -MD). The wrappers are then removed and the
products must have the bytes they had before.

From the runs: the steps that lead to the products (--product), in order.
A pipe between two runs becomes the first one's stdout given as the
second one's stdin; output of a run that a later run has in an argument
(a shell command substitution, e.g. 0x$(nm | sed)) becomes a variable of
that argument; `{ cat FILES; printf SIZE; } > OUT` (kbuild's size_append)
becomes the step cat-size. A compile of an object that reads nothing a
step made is not a step but an object of the kit: its command, the files
it includes and its prebuilt bytes; with --kbuild the objects' commands
and dependencies come from kbuild's .cmd files (cmd_OBJ up to the first
`;`, deps_OBJ, source_OBJ). Every file a step or an object reads that no
step makes is a file of the kit; a file not in --sources-from's git tree
is "generated" (made by CI with a tool the kit does not run, e.g.
asm-offsets.h, the initramfs cpio).

Output: KITS/NAME.json (format below) and KITS/blobs/: every file and
object gzipped as SHA256.gz, every wasm module as SHA256.wasm (the page
fetches only what a build needs).

  { format: 1, name, dir (the build directory, absolute: -g records it),
    files: [[path, sha256, size, kind (0 source, 1 generated, 2 outside
             the tree, by its absolute path)]],
    objects: [{ out, argv, deps (indexes into files, the source first),
                sha256, size }],
    steps: [{ tool (clang, a module name, "sed", "cat-size"), argv
              (strings, or {var, pre, post}), stdin (a variable), stdout
              ({file} or {var}), inputs (paths), outputs [[path, sha256]],
              fresh (paths deleted before the step), mkdir }],
    modules: { name: { sha256, size } }, products: [[path, sha256, size]],
    shipped: { path, sha256, differs [[offset, length]] } }

Options:
  --kbuild                  objects from kbuild's .cmd files
  --touch-objects           touch every *.o of TREE before the run
  --clean FILE              delete FILE (or directory) before the run
  --tool PATH=WASM          the tree's host tool PATH runs as WASM in the page
  --module NAME=WASM        a PATH tool's wasm module (nasm)
  --product FILE            a product of the build (vmlinux, bzImage)
  --extra-input OBJ=FILE    a file OBJ's compile reads that its dependencies
                            leave out (.incbin)
  --sources-from DIR        the git tree TREE was copied from
  --shipped FILE=PATH       PATH is the bzImage the page boots (the plain
                            build): the byte ranges where the product FILE
                            differs from it are recorded (the -g build's
                            build-id note)
  --sources-list FILE       the source files (one path per line, as
                            --sources-from's git ls-files) of a tree that is
                            no git checkout (Debian's source package)
  --path-tool NAME          one more PATH tool to wrap: grep (run by the
                            page, as sed), or a host tool with --module
                            NAME=WASM (kgzip)
  --tool-input PATH=FILE    the --tool PATH also reads FILE (sorttable reads
                            System.map through its popen of grep)
  --recordmcount FLAGS      kbuild's cmd_record_mcount: a C object whose
                            command has FLAGS (CC_FLAGS_FTRACE) gets the post
                            step ./scripts/recordmcount OBJ (its module:
                            --module scripts/recordmcount=WASM)
  --unstripped JSON         {object: SHA-256} of the objects before their
                            debug sections were stripped (strip-objects.py):
                            gsha256 of the object, what a compile in the
                            page makes

An object may have "post": [{tool, argv}] (run on it after its compile, in
order) and "gsha256" (above).
"""

import argparse
import base64
import gzip
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

ME = pathlib.Path(__file__).resolve()
REAL = ".mkb-real"
PATH_TOOLS = ["clang", "ld.lld", "llvm-ar", "llvm-nm", "llvm-objcopy", "nasm", "sed", "cat"]
CLANG_TOOLS = {"clang", "ld.lld", "llvm-ar", "llvm-nm", "llvm-objcopy"}
BUILTINS = {"sed", "cat", "grep"}
SMALL = 1 << 16


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def rel(tree, cwd, p):
    """p (as a tool saw it in cwd) relative to tree, or None outside it."""
    a = os.path.normpath(os.path.join(cwd, p))
    return os.path.relpath(a, tree) if a.startswith(tree + "/") else None


def arg_paths(a):
    if a.startswith("-Wp,"):
        return []
    if a.startswith("-"):
        return [a.split("=", 1)[1]] if "=" in a else []
    return [a]


def depfile_paths(text):
    """The prerequisites of a make-syntax dependency file (one target)."""
    text = re.sub(r"\$\(wildcard [^)]*\)", " ", text.replace("\\\n", " "))
    out = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        rhs = line.split(":", 1)[1]
        out += [w.replace("\\ ", " ") for w in re.findall(r"(?:\\ |[^\s])+", rhs)]
    return out


def shell_words(s):
    """POSIX shell words of s up to the first unquoted ; & | (as
    check-wasm-objects.mjs's shellWords)."""
    words, w, i = [], None, 0
    while i < len(s):
        c = s[i]
        if c == "'":
            j = s.index("'", i + 1)
            w = (w or "") + s[i + 1:j]
            i = j
        elif c == '"':
            j = i + 1
            w = w or ""
            while s[j] != '"':
                if s[j] == "\\" and s[j + 1] in '"\\$`\n':
                    j += 1
                w += s[j]
                j += 1
            i = j
        elif c == "\\":
            i += 1
            w = (w or "") + s[i]
        elif c in ";&|":
            break
        elif c.isspace():
            if w is not None:
                words.append(w)
                w = None
        else:
            w = (w or "") + c
        i += 1
    if w is not None:
        words.append(w)
    return words


# --- the wrapper (one run of a tool during the capture) ---

def fdlink(n):
    try:
        return os.readlink(f"/proc/self/fd/{n}")
    except OSError:
        return None


def wrap(cfgfile, key, args):
    cfg = json.loads(pathlib.Path(cfgfile).read_text())
    tree, real = cfg["tree"], cfg["real"][key]
    t0 = time.monotonic_ns()
    cwd = os.getcwd()
    cand = {}
    for a in args:
        for p in arg_paths(a):
            r = rel(tree, cwd, p)
            if r is not None:
                cand[r] = os.path.join(tree, r)
    before = {}
    for r, ap in cand.items():
        if os.path.isfile(ap):
            st = os.stat(ap)
            before[r] = (st.st_mtime_ns, st.st_ino, st.st_size, sha256(pathlib.Path(ap).read_bytes()))
    fd0, fd1 = fdlink(0), fdlink(1)
    p = subprocess.run([real] + args, stdout=subprocess.PIPE)
    sys.stdout.buffer.write(p.stdout)
    sys.stdout.flush()
    t1 = time.monotonic_ns()
    inputs, outputs, fresh = [], [], []
    for r, ap in cand.items():
        st = os.stat(ap) if os.path.isfile(ap) else None
        old = before.get(r)
        if st and (old is None or (st.st_mtime_ns, st.st_ino) != old[:2]):
            outputs.append([r, sha256(pathlib.Path(ap).read_bytes()), st.st_size])
            if old is None:
                fresh.append(r)
        elif old:
            inputs.append([r, old[3], old[2]])
    deps = None
    tool = os.path.basename(key)
    out = args[args.index("-o") + 1] if "-o" in args[:-1] else None
    out_rel = rel(tree, cwd, out) if out else None
    scratch = out_rel is None or any(x.startswith(".tmp") for x in out_rel.split("/"))
    if p.returncode == 0 and tool == "clang" and not scratch:
        dep = next((a.split(",")[-1] for a in args if a.startswith(("-Wp,-MMD,", "-Wp,-MD,"))), None)
        if dep is None and "-MF" in args[:-1]:
            dep = args[args.index("-MF") + 1]
        if dep is not None:
            deps = depfile_paths(pathlib.Path(cwd, dep).read_text())
        elif "-c" in args or "-E" in args:
            # No dependency file: the same compile once more with -MD.
            with tempfile.TemporaryDirectory() as td:
                a2 = list(args)
                a2[a2.index("-o") + 1] = os.path.join(td, "o")
                subprocess.run([real] + a2 + ["-MD", "-MF", os.path.join(td, "d")], check=True)
                deps = depfile_paths(pathlib.Path(td, "d").read_text())
    elif p.returncode == 0 and tool == "nasm" and not scratch:
        deps = depfile_paths(subprocess.run([real, "-M"] + args, stdout=subprocess.PIPE, check=True).stdout.decode())
    if deps is not None:
        deps = [rel(tree, cwd, d) or os.path.normpath(os.path.join(cwd, d)) for d in deps]
    rec = {"key": key, "args": args, "cwd": cwd, "pid": os.getpid(), "ppid": os.getppid(), "t0": t0, "t1": t1,
           "fd0": fd0, "fd1": fd1, "code": p.returncode, "inputs": inputs, "outputs": outputs, "fresh": fresh,
           "deps": deps, "stdout": [sha256(p.stdout), len(p.stdout),
                                    base64.b64encode(p.stdout).decode() if len(p.stdout) <= SMALL else None]}
    with open(cfg["log"], "a") as f:
        f.write(json.dumps(rec) + "\n")
    sys.exit(p.returncode)


# --- the capture ---

def capture(a, tree, make):
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="mkb-kit-"))
    bindir = tmp / "bin"
    bindir.mkdir()
    log = tmp / "runs.jsonl"
    cfg = {"tree": str(tree), "log": str(log), "real": {}}
    wrappers = []
    for name in PATH_TOOLS + a.path_tool:
        real = shutil.which(name)
        if not real and name == "nasm":
            continue  # only hello's build runs it; a build that would fails without it
        if not real:
            sys.exit(f"{name}: not in PATH")
        cfg["real"][name] = real
        wrappers.append((bindir / name, name, False))
    for t in a.tool:
        path = t.split("=", 1)[0]
        cfg["real"][path] = str(tree / (path + REAL))
        wrappers.append((tree / path, path, True))
    cfgfile = tmp / "cfg.json"
    cfgfile.write_text(json.dumps(cfg))
    before = {f: sha256((tree / f).read_bytes()) for f in a.product}
    moved = []
    try:
        for w, key, in_tree in wrappers:
            if in_tree:
                os.rename(w, str(w) + REAL)
                moved.append(w)
            w.write_text(f"#!/bin/sh\nexec {sys.executable} {ME} --wrap {cfgfile} {key} \"$@\"\n")
            w.chmod(0o755)
        if a.touch_objects:
            now = time.time() + 1
            # Not a host program's objects (kbuild: compiled without
            # -D__KERNEL__): make would link the program again over its
            # wrapper (Debian's arch/x86/tools/relocs).
            cmds = kbuild_objects(tree) if a.kbuild else {}
            for o in tree.rglob("*.o"):
                c = cmds.get(str(o.relative_to(tree)))
                if c is None or "-D__KERNEL__" in c[0]:
                    os.utime(o, (now, now))
            time.sleep(1.1)
        for f in a.clean:
            p = tree / f
            if p.is_dir():
                shutil.rmtree(p)
            else:
                p.unlink(missing_ok=True)
        env = dict(os.environ, PATH=f"{bindir}:{os.environ['PATH']}")
        with open(tmp / "make.log", "w") as mlog:
            # stdin /dev/null: a CI step's stdin may be a pipe, which every
            # tool would inherit and look like reading.
            r = subprocess.run(make + ["-j1"], cwd=tree, env=env, stdin=subprocess.DEVNULL, stdout=mlog,
                               stderr=subprocess.STDOUT)
        if r.returncode:
            sys.stdout.write((tmp / "make.log").read_text()[-4000:])
            sys.exit(f"{a.name}: the build under the wrappers failed ({r.returncode})")
        runs = [json.loads(x) for x in log.read_text().splitlines()]
        if a.log:
            shutil.copyfile(log, a.log)
    finally:
        for w in moved:
            os.replace(str(w) + REAL, w)
        shutil.rmtree(tmp)
    bad = [f for f, h in before.items() if sha256((tree / f).read_bytes()) != h]
    if bad:
        sys.exit(f"{a.name}: the build under the wrappers made other bytes: {bad}")
    return runs


# --- from the runs to the kit ---

def kbuild_objects(tree):
    """kbuild's compile commands: {object: (argv, deps)} from .cmd files."""
    objs = {}
    for cmd in tree.rglob(".*.cmd"):
        text = cmd.read_text(errors="replace")
        m = re.search(r"^(?:saved)?cmd_(\S+) := (.*)$", text, re.M)
        if not m:
            continue
        obj = m.group(1)
        argv = shell_words(m.group(2).replace("$(pound)", "#").replace("$$", "$"))
        if not argv or argv[0] != "clang":
            continue
        deps = []
        src = re.search(r"^source_\S+ := (.*)$", text, re.M)
        if src:
            deps.append(src.group(1).strip())
        d = re.search(r"^deps_\S+ :=((?:[^\n]*\\\n)*[^\n]*)", text, re.M)
        if d:
            deps += depfile_paths("x:" + d.group(1))
        objs[obj] = (argv, list(dict.fromkeys(os.path.normpath(p) for p in deps)))
    return objs


def make_kit(a, tree, runs):
    t = str(tree)
    runs.sort(key=lambda r: r["t0"])
    for r in runs:
        if r["cwd"] != t:
            sys.exit(f"{a.name}: {r['key']} ran in {r['cwd']}, not in the tree")
        if r["code"] != 0 and not r["outputs"]:
            r["skip"] = True
    file_out = lambda r: rel(t, t, r["fd1"]) if r["fd1"] and r["fd1"].startswith(t + "/") else None
    pipe = lambda s: s if s and s.startswith("pipe:") else None
    # The run writing into the pipe a run reads: forked by the same shell
    # just before it (a pipe's inode number is used again once the pipe
    # is gone, and either wrapper may start first).
    producer = {}
    for r in runs:
        if pipe(r["fd0"]):
            ws = [x for x in runs if x["fd1"] == r["fd0"] and x["ppid"] == r["ppid"] and x["pid"] < r["pid"]]
            if ws:
                p = max(ws, key=lambda x: x["pid"])
                producer[id(r)] = p
                p["read"] = True
                p["t0"] = min(p["t0"], r["t0"] - 1)  # the writer's step comes first
    runs.sort(key=lambda r: r["t0"])
    steps = []
    nvar = 0
    # Files a tool reads that its arguments do not name (sorttable's popen
    # of grep on System.map).
    tool_inputs = {}
    for e in a.tool_input:
        k, f = e.split("=", 1)
        tool_inputs.setdefault(k, []).append(f)
    for r in runs:
        if r.get("skip"):
            continue
        tool = os.path.basename(r["key"]) if r["key"] in PATH_TOOLS + a.path_tool else r["key"]
        r["step"] = {"run": r, "tool": tool, "argv": [r["key"]] + r["args"],
                     "inputs": [x[0] for x in r["inputs"]] + (r["deps"] or []) + tool_inputs.get(r["key"], []),
                     "outputs": [[p, h] for p, h, _ in r["outputs"]], "fresh": list(r["fresh"]), "uses": []}
    for r in runs:
        if r.get("skip"):
            continue
        st = r["step"]
        tool = st["tool"]
        if pipe(r["fd0"]):
            prod = producer.get(id(r))
            if prod and "step" in prod:
                if "var" not in prod["step"]:
                    prod["step"]["var"] = f"v{nvar}"
                    nvar += 1
                st["stdin"] = prod["step"]["var"]
                st["uses"].append(prod["step"])
            elif tool in BUILTINS and not r["inputs"]:
                # Reads a shell builtin's output (echo ... | sed | xargs); a
                # cat or sed of files does not read the pipe it was given.
                st["glue"] = True
        fo = file_out(r)
        if fo:
            st["stdout"] = {"file": fo}
            st["outputs"].append([fo, r["stdout"][0]])
        elif pipe(r["fd1"]) and not r.get("read"):
            # Read by the shell: a command substitution if a later run's
            # argument has the text.
            text = base64.b64decode(r["stdout"][2] or "").decode(errors="replace").rstrip("\n")
            nxt = next((x for x in runs if x["t0"] > r["t1"] and not x.get("skip")), None)
            hit = next((j for j, v in enumerate(nxt["args"]) if text and text in v), None) if nxt else None
            if hit is not None and not st.get("glue"):
                st["var"] = f"v{nvar}"
                nvar += 1
                v = nxt["args"][hit]
                k = v.index(text)
                nxt.setdefault("subst", {})[hit] = {"var": st["var"], "pre": v[:k], "post": v[k + len(text):], "from": st}
            else:
                st["glue"] = True
        steps.append(st)
    for st in steps:
        for j, s in (st["run"].get("subst") or {}).items():
            st["argv"][j + 1] = {"var": s["var"], "pre": s["pre"], "post": s["post"]}
            st["uses"].append(s["from"])
    # Every file a wrapped run wrote: none of them may become a file of
    # the kit (it would stay CI's bytes after an edit).
    written = {p for st in steps for p, _ in st["outputs"]}
    steps = [s for s in steps if not s.get("glue")]

    # The steps that lead to the products; a use (stdin, variable) of an
    # earlier step keeps that step.
    need = set(a.product)
    for st in reversed(steps):
        if st.get("needed") or any(p in need for p, _ in st["outputs"]):
            st["needed"] = True
            need |= set(st["inputs"])
            for u in st["uses"]:
                u["needed"] = True

    # A file whose last writer's bytes are not its final bytes: kbuild's
    # size_append after a cat, else an error.
    final = {}
    for st in steps:
        for p, h in st["outputs"]:
            final[p] = (st, h)
    for p, (st, h) in final.items():
        f = tree / p
        if not st.get("needed") or not f.is_file():
            continue
        b = f.read_bytes()
        if sha256(b) == h:
            continue
        if st["tool"] == "cat" and st.get("stdout", {}).get("file") == p:
            n = st["run"]["stdout"][1]
            size = sum(x[2] for x in st["run"]["inputs"])
            if sha256(b[:n]) == h and b[n:] == size.to_bytes(4, "little"):
                st["tool"] = "cat-size"
                st["outputs"] = [[p, sha256(b)]]
                continue
        sys.exit(f"{a.name}: {p} changed after {st['tool']} wrote it ({' '.join(map(str, st['argv']))})")
    steps = [s for s in steps if s.get("needed")]
    made = set()
    for st in steps:
        st["reads_made"] = any(p in made for p in st["inputs"])
        made |= {p for p, _ in st["outputs"]}

    extra = {}
    for e in a.extra_input:
        o, f = e.split("=", 1)
        extra.setdefault(o, []).append(f)
    objects = {}
    # A compile of an object that reads nothing a step made: an object.
    for st in list(steps):
        out = [p for p, _ in st["outputs"]]
        if st["tool"] in ("clang", "nasm") and len(out) == 1 and out[0].endswith(".o") and not st["reads_made"] \
                and not st.get("var") and "stdin" not in st:
            deps = [p for p in st["inputs"] if p != out[0]]
            objects[out[0]] = (st["argv"], deps)
            steps.remove(st)
    made = set()
    for st in steps:
        made |= {p for p, _ in st["outputs"]}
    if a.kbuild:
        cmds = kbuild_objects(tree)
        for st in steps:
            for p in st["inputs"]:
                if p in cmds and p not in made:
                    objects[p] = cmds[p]
    for st in steps:
        st["inputs"] = list(dict.fromkeys(st["inputs"]))

    # Files: every input no step makes, every object's dependencies (a
    # path outside the tree, e.g. clang's own headers, stays absolute).
    files = {}

    def add(p):
        if p not in made and p not in objects and p not in files:
            if not (tree / p).is_file():
                sys.exit(f"{a.name}: {p} is read but not a file")
            if p in written:
                sys.exit(f"{a.name}: {p} is read, and a run of the build wrote it that is not a step of the kit")
            files[p] = None
    for st in steps:
        for p in st["inputs"]:
            add(p)
    for o, (argv, deps) in objects.items():
        for p in deps + extra.get(o, []):
            add(p)
    tracked = set()
    if a.sources_list:
        tracked = set(pathlib.Path(a.sources_list).read_text().splitlines())
    elif a.sources_from:
        tracked = set(subprocess.run(["git", "-c", "safe.directory=*", "-C", a.sources_from, "ls-files"], stdout=subprocess.PIPE,
                                     check=True, text=True).stdout.splitlines())
    blobs = pathlib.Path(a.out) / "blobs"
    blobs.mkdir(parents=True, exist_ok=True)

    def put(p):
        b = (tree / p).read_bytes()
        h = sha256(b)
        dest = blobs / (h + ".gz")
        if not dest.exists():
            tmp = dest.with_name(f".{h}.{os.getpid()}")
            with open(tmp, "wb") as f, gzip.GzipFile(fileobj=f, mode="wb", compresslevel=9, mtime=0, filename="") as g:
                g.write(b)
            os.replace(tmp, dest)
        return h, len(b)

    flist = []
    index = {}
    for p in sorted(files):
        h, n = put(p)
        index[p] = len(flist)
        flist.append([p, h, n, 2 if p.startswith("/") else 0 if (not tracked or p in tracked) else 1])

    modules = {}
    wasm = dict(x.split("=", 1) for x in a.tool + a.module)

    def module_of(key):
        """The module a tool runs as in the page (clang.wasm: "clang")."""
        if os.path.basename(key) in CLANG_TOOLS:
            return "clang"
        if key not in wasm:
            sys.exit(f"{a.name}: no wasm module for {key}")
        data = pathlib.Path(wasm[key]).read_bytes()
        h = sha256(data)
        mod = os.path.basename(key)
        if modules.get(mod, {}).get("sha256", h) != h:
            sys.exit(f"{a.name}: two modules named {mod}")
        modules[mod] = {"sha256": h, "size": len(data)}
        dest = blobs / (h + ".wasm")
        if not dest.exists():
            shutil.copyfile(wasm[key], dest)
        return mod

    unstripped = json.loads(pathlib.Path(a.unstripped).read_text()) if a.unstripped else {}
    olist = []
    for o in sorted(objects):
        argv, deps = objects[o]
        if argv[0] != "clang" and argv[0] != "nasm":
            sys.exit(f"{a.name}: {o} is made by {argv[0]}")
        ds = list(dict.fromkeys(d for d in deps + extra.get(o, []) if d != o))
        missing = [d for d in ds if d not in index]
        if missing:
            sys.exit(f"{a.name}: {o}: dependencies not in the kit: {missing}")
        h, n = put(o)
        ent = {"out": o, "argv": argv, "deps": [index[d] for d in ds], "sha256": h, "size": n}
        # kbuild's cmd_record_mcount: after the compile of a C object whose
        # flags have CC_FLAGS_FTRACE, recordmcount rewrites it in place.
        if a.recordmcount and argv[0] == "clang" and ds and ds[0].endswith(".c") and a.recordmcount in " ".join(argv):
            ent["post"] = [{"tool": module_of("scripts/recordmcount"), "argv": ["./scripts/recordmcount", o]}]
        if o in unstripped:
            ent["gsha256"] = unstripped[o]
        olist.append(ent)

    for o in olist:
        module_of(o["argv"][0])
    out_steps = []
    for st in steps:
        tool = st["tool"]
        o = {"tool": tool if tool in ("sed", "cat", "grep", "cat-size") else module_of(st["run"]["key"]), "argv": st["argv"]}
        if tool == "cat-size":
            o["argv"] = ["cat"] + [x[0] for x in st["run"]["inputs"]]
        for k in ("stdin", "stdout"):
            if k in st:
                o[k] = st[k]
        if st.get("var") and "stdout" not in st:
            o["stdout"] = {"var": st["var"]}
        o["inputs"] = st["inputs"]
        o["outputs"] = st["outputs"]
        o["fresh"] = st["fresh"]
        o["mkdir"] = sorted({os.path.dirname(p) for p, _ in st["outputs"] if os.path.dirname(p)})
        out_steps.append(o)
    products = []
    for p in a.product:
        b = (tree / p).read_bytes()
        products.append([p, sha256(b), len(b)])
    kit = {"format": 1, "name": a.name, "dir": t, "files": flist, "objects": olist, "steps": out_steps,
           "modules": modules, "products": products}
    for s in a.shipped:
        p, path = s.split("=", 1)
        b, c = (tree / p).read_bytes(), pathlib.Path(path).read_bytes()
        if len(b) != len(c):
            sys.exit(f"{a.name}: {p} is {len(b)} bytes, the shipped {path} {len(c)}")
        ranges = []
        for i in range(len(b)):
            if b[i] != c[i]:
                if ranges and ranges[-1][0] + ranges[-1][1] == i:
                    ranges[-1][1] += 1
                else:
                    ranges.append([i, 1])
        kit["shipped"] = {"path": p, "sha256": sha256(c), "differs": ranges}
    out = pathlib.Path(a.out) / (a.name + ".json")
    out.write_text(json.dumps(kit, separators=(",", ":")) + "\n")
    gen = [f[0] for f in flist if f[3]]
    print(f"{a.name}: {len(olist)} objects, {len(flist)} files ({len(gen)} generated), {len(out_steps)} steps, "
          f"modules {sorted(modules)}; kit.json {out.stat().st_size} B")
    print(f"  generated (as CI made them; the kit does not make them again): {' '.join(gen)}")
    for st in out_steps:
        print("   ", st["tool"], " ".join(x if isinstance(x, str) else "$(" + x["var"] + ")" for x in st["argv"])[:160],
              "<" + st["stdin"] if "stdin" in st else "", ">" + json.dumps(st["stdout"]) if "stdout" in st else "")
    return 0


def index(kits):
    """KITS/index.json: the kits and their sizes (the budget)."""
    kits = pathlib.Path(kits)
    blobs = kits / "blobs"
    gz = lambda h, ext: (blobs / (h + ext)).stat().st_size
    out, seen = {"kits": {}}, set()
    print(f"{'kit':22} {'files':>6} {'raw MB':>8} {'gz MB':>7} {'objects':>8} {'raw MB':>8} {'gz MB':>7} {'modules gz':>10} {'json':>8}")
    for f in sorted(kits.glob("*.json")):
        if f.name == "index.json":
            continue
        k = json.loads(f.read_text())
        fr = sum(x[2] for x in k["files"])
        fg = sum(gz(x[1], ".gz") for x in k["files"])
        orr = sum(x["size"] for x in k["objects"])
        og = sum(gz(x["sha256"], ".gz") for x in k["objects"])
        mg = sum(gz(m["sha256"], ".wasm") for m in k["modules"].values())
        jg = len(gzip.compress(f.read_bytes(), 9))
        out["kits"][k["name"]] = {"json": f.name, "files": len(k["files"]), "filesBytes": fr, "filesGz": fg,
                                  "objects": len(k["objects"]), "objectsBytes": orr, "objectsGz": og,
                                  "modulesGz": mg, "jsonGz": jg}
        seen |= {x[1] + ".gz" for x in k["files"]} | {x["sha256"] + ".gz" for x in k["objects"]} | \
            {m["sha256"] + ".wasm" for m in k["modules"].values()}
        print(f"{k['name']:22} {len(k['files']):6} {fr / 1e6:8.2f} {fg / 1e6:7.2f} {len(k['objects']):8} "
              f"{orr / 1e6:8.2f} {og / 1e6:7.2f} {mg / 1e6:10.2f} {jg / 1e6:8.3f}")
    stray = sorted(p.name for p in blobs.iterdir() if p.name not in seen)
    if stray:
        sys.exit(f"blobs no kit uses: {stray[:5]}")
    out["blobsBytes"] = sum((blobs / n).stat().st_size for n in seen)
    out["blobs"] = len(seen)
    (kits / "index.json").write_text(json.dumps(out, indent=1) + "\n")
    print(f"all kits: {out['blobs']} blobs, {out['blobsBytes'] / 1e6:.2f} MB on the site (shared files counted once)")
    return 0


def vmlinux_objects(tree, cmds):
    """The objects of vmlinux (vmlinux.a's members) and of the boot image
    (arch/x86/boot/, with a compile command) of a built kbuild tree."""
    if (tree / "vmlinux.a").is_file():
        members = subprocess.run(["llvm-ar", "t", str(tree / "vmlinux.a")], stdout=subprocess.PIPE, check=True,
                                 text=True).stdout.split()
    else:
        # No vmlinux.a (before Linux 6.1): every compiled object but the host tools'.
        members = [o for o in cmds if o.endswith(".o") and not o.startswith(("scripts/", "tools/"))]
    return sorted({os.path.relpath(os.path.join(tree, m), tree) for m in members} |
                  {o for o in cmds if o.startswith("arch/x86/boot/") and o.endswith(".o")})


def budget(tree):
    """What a kit of a built kbuild tree would weigh (no capture): the
    objects in vmlinux.a (llvm-ar t) and under arch/x86/boot/ with a
    compile command, and the files those include, raw and gzipped."""
    tree = pathlib.Path(tree).resolve()
    cmds = kbuild_objects(tree)
    objs = vmlinux_objects(tree, cmds)
    deps = sorted({d for o in objs if o in cmds for d in cmds[o][1]})
    size = lambda ps: (sum((tree / p).stat().st_size for p in ps),
                       sum(len(gzip.compress((tree / p).read_bytes(), 9)) for p in ps))
    o_raw, o_gz = size(objs)
    d_raw, d_gz = size([d for d in deps if (tree / d).is_file()])
    print(f"kit budget of {tree}: {len(objs)} objects ({sum(o not in cmds for o in objs)} without a compile command) "
          f"{o_raw / 1e6:.1f} MB, gzip -9 {o_gz / 1e6:.1f} MB; {len(deps)} sources and headers {d_raw / 1e6:.1f} MB, "
          f"gzip -9 {d_gz / 1e6:.1f} MB; total gzipped {(o_gz + d_gz) / 1e6:.1f} MB")
    return 0


def strip(tree, out):
    """The debug sections of a built kbuild tree's kernel objects (a
    compile command with -D__KERNEL__: not the host programs') removed in
    place (llvm-objcopy --strip-debug), for a kit whose link needs no DWARF
    (Debian's: 339 MB of -g objects); OUT: {object: SHA-256 before}, for
    --unstripped. The build's make then links the stripped objects."""
    tree = pathlib.Path(tree).resolve()
    before = {}
    raw = [0, 0]
    cmds = kbuild_objects(tree)
    # Only what vmlinux and the boot image link: an object inside another
    # product (the vDSO, whose build-id hashes its debug info into the
    # kernel's rodata) keeps its bytes.
    for o in vmlinux_objects(tree, cmds):
        p = tree / o
        if o not in cmds or "-D__KERNEL__" not in cmds[o][0] or not p.is_file():
            continue
        b = p.read_bytes()
        before[o] = sha256(b)
        subprocess.run(["llvm-objcopy", "--strip-debug", str(p)], check=True)
        raw[0] += len(b)
        raw[1] += p.stat().st_size
    pathlib.Path(out).write_text(json.dumps(before, indent=0, sort_keys=True) + "\n")
    print(f"stripped {len(before)} objects: {raw[0] / 1e6:.1f} MB -> {raw[1] / 1e6:.1f} MB")
    return 0


def main():
    if len(sys.argv) > 3 and sys.argv[1] == "--wrap":
        return wrap(sys.argv[2], sys.argv[3], sys.argv[4:])
    if len(sys.argv) == 4 and sys.argv[1] == "--strip":
        return strip(sys.argv[2], sys.argv[3])
    if len(sys.argv) == 3 and sys.argv[1] == "--index":
        return index(sys.argv[2])
    if len(sys.argv) == 3 and sys.argv[1] == "--budget":
        return budget(sys.argv[2])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--tree", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--kbuild", action="store_true")
    ap.add_argument("--touch-objects", action="store_true")
    ap.add_argument("--clean", action="append", default=[])
    ap.add_argument("--tool", action="append", default=[])
    ap.add_argument("--module", action="append", default=[])
    ap.add_argument("--product", action="append", default=[], required=True)
    ap.add_argument("--extra-input", action="append", default=[])
    ap.add_argument("--sources-from")
    ap.add_argument("--shipped", action="append", default=[])
    ap.add_argument("--sources-list")
    ap.add_argument("--path-tool", action="append", default=[])
    ap.add_argument("--recordmcount")
    ap.add_argument("--tool-input", action="append", default=[])
    ap.add_argument("--unstripped")
    ap.add_argument("--log", help="keep the wrappers' records (JSON lines) here")
    ap.add_argument("make", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    make = a.make[1:] if a.make[:1] == ["--"] else a.make
    if not make:
        ap.error("no build command after --")
    tree = pathlib.Path(a.tree).resolve()
    runs = capture(a, tree, make)
    return make_kit(a, tree, runs)


if __name__ == "__main__":
    sys.exit(main())
