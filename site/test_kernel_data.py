#!/usr/bin/env python3
"""Check that every kernel the page offers has the data files each debug
feature fetches, so no kernel quietly loses a feature.

  site/test_kernel_data.py [SITE_DIR]

Reads SITE_DIR/kernels.json (site/gen-kernels.sh) and, for every entry,
the files the page loads for:
  symbols         symbols.json (gen-symbols.py)
  line info and   dwarf/index.json and every <hex>.bin shard it lists
  inline chains   (gen-dwarf.js; dbginfo.js)
  line -> address the shards in format MKBD 2 (with the is_stmt breakpoint
                  addresses) and a nonzero breakpoint count in index.json
  C source        src/index.json and src/<path>.gz for every file
                  files.json names, unless index.json says why it is
                  missing (gen-src.js; source.js)
  types, globals  dwarf/types.json (dbginfo.js; vars.js)
  defined at      types.json format 2: files, every variable's file index
                  in it; each of those files shipped in src/ or said to
                  be missing (vars.js "defined at" opens it)
  coverage tree   dwarf/files.json with instruction counts (coverage.js)
  CFI unwind      every cfi-<hex>.bin the index lists (unwind.js)
  params, locals  every locals-<hex>.json.gz the index lists
  function facts  every funcs-<k>.json.gz the index counts, each with
                  functions, together as many as index.json says
                  (fnfacts.js)
  task names      current_task and struct task_struct with comm and pid
                  in types.json (vars.js varsTaskLayout); a kernel
                  listed in NO_TASKS must have neither, so the page's
                  "not known" is the truth there
Fails on any missing or empty file, listing all of them.
"""
import gzip
import json
import pathlib
import sys

SITE = pathlib.Path(__file__).resolve().parent

# Kernels without tasks: the hello kernel is one function, no scheduler.
NO_TASKS = {"hello"}


def nonempty(p):
    return p.is_file() and p.stat().st_size > 0


def check_dwarf(site, d, problems):
    """The DWARF files of directory d; returns (index, files, types) or Nones."""
    idx_path = d / "index.json"
    if not nonempty(idx_path):
        problems.append(f"{idx_path}: missing")
        return None, None, None
    idx = json.loads(idx_path.read_text())
    if not idx["shards"]:
        problems.append(f"{idx_path}: no line-table shards")
    for s in idx["shards"]:
        if not nonempty(d / f"{s:x}.bin"):
            problems.append(f"{d}/{s:x}.bin: missing (line info and inline chains)")
        elif (d / f"{s:x}.bin").read_bytes()[:5] != b"MKBD\x02":
            problems.append(f"{d}/{s:x}.bin: not format MKBD 2 (no breakpoint addresses for line -> address)")
    if not idx.get("breakpoints"):
        problems.append(f"{idx_path}: no breakpoint addresses (line -> address, Run to file:line)")
    cfi = idx.get("cfi") or {}
    if not cfi.get("shards"):
        problems.append(f"{idx_path}: no call frame information ({cfi.get('why', 'no cfi member')})")
    for s in cfi.get("shards", []):
        if not nonempty(d / f"cfi-{s:x}.bin"):
            problems.append(f"{d}/cfi-{s:x}.bin: missing (CFI unwind)")
    locals_ = idx.get("locals") or {}
    if not locals_.get("shards"):
        problems.append(f"{idx_path}: no parameter and local shards")
    for s in locals_.get("shards", []):
        p = d / f"locals-{s:x}.json.gz"
        if not nonempty(p):
            problems.append(f"{p}: missing (parameters and locals)")
        elif not json.loads(gzip.decompress(p.read_bytes())).get("fns"):
            problems.append(f"{p}: no functions")
    funcs = idx.get("funcs") or {}
    if not funcs.get("shards"):
        problems.append(f"{idx_path}: no function facts (funcs-*.json.gz)")
    nfns = 0
    for k in range(funcs.get("shards", 0)):
        p = d / f"funcs-{k:x}.json.gz"
        if not nonempty(p):
            problems.append(f"{p}: missing (function facts)")
            continue
        nfns += len(json.loads(gzip.decompress(p.read_bytes())).get("fns") or [])
    if funcs.get("shards") and nfns != funcs.get("functions"):
        problems.append(f"{d}: {nfns} functions in funcs-*.json.gz, index.json says {funcs.get('functions')}")
    files = types = None
    fp = d / idx.get("files", "files.json")
    if nonempty(fp):
        files = json.loads(fp.read_text())
        if not files["files"] or not files["funcs"]:
            problems.append(f"{fp}: no files or functions (coverage tree)")
        if not files.get("insns"):
            problems.append(f"{fp}: no instruction counts ({files.get('insnsWhy')})")
    else:
        problems.append(f"{fp}: missing (coverage tree)")
    tp = d / idx.get("types", "types.json")
    if nonempty(tp):
        types = json.loads(tp.read_text())
        if not types["types"]:
            problems.append(f"{tp}: no types")
        if types.get("format") != 2 or "files" not in types:
            problems.append(f"{tp}: not format 2 with files (variables do not say where they are defined)")
        else:
            bad = [v[0] for v in types["vars"] if len(v) > 7 and not 0 <= v[7] < len(types["files"])]
            if bad:
                problems.append(f"{tp}: file index out of range for {bad[:5]}")
    else:
        problems.append(f"{tp}: missing (types and globals)")
    return idx, files, types


def check_src(d, files, types, problems):
    ip = d / "index.json"
    if not nonempty(ip):
        problems.append(f"{ip}: missing (C source)")
        return
    si = json.loads(ip.read_text())
    have, missing = set(si["files"]), si.get("missing", {})
    for f in have:
        if not nonempty(d / (f + ".gz")):
            problems.append(f"{d}/{f}.gz: listed in index.json but missing")
    for f in [x[0] for x in files["files"]] if files else []:
        if f not in have and f not in missing:
            problems.append(f"{ip}: {f} (has code lines) is neither shipped nor said to be missing")
    for f in types.get("files", []) if types else []:
        if f not in have and f not in missing:
            problems.append(f"{ip}: {f} (a variable is defined there) is neither shipped nor said to be missing")


def check_tasks(name, types, problems):
    if types is None:
        return
    var_names = {v[0] for v in types["vars"]}
    ts = [t for t in types["types"] if len(t) > 3 and t[0] == 7 and t[1] == "task_struct" and t[3]]
    members = {m[0] for t in ts for m in t[3]}
    has = "current_task" in var_names and {"comm", "pid"} <= members
    if name in NO_TASKS:
        if "current_task" in var_names or ts:
            problems.append(f"{name}: listed as a kernel without tasks, but its DWARF has current_task or task_struct")
    elif not has:
        problems.append(f"{name}: task names need current_task and task_struct.comm/.pid in types.json "
                        f"(current_task: {'current_task' in var_names}, members: {sorted(members & {'comm', 'pid'})})")


def main():
    site = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SITE
    entries = json.loads((site / "kernels.json").read_text())["kernels"]
    problems, rows = [], []
    for e in entries:
        name = e["name"]
        for key in ("symbols", "dwarf", "src"):
            if key not in e:
                problems.append(f"{name}: kernels.json has no {key} (gen-kernels.sh found no {key} data)")
        if not nonempty(site / e["path"]):
            problems.append(f"{name}: {e['path']} missing")
        if "symbols" in e and not nonempty(site / e["symbols"]):
            problems.append(f"{name}: {e['symbols']} missing")
        idx = files = types = None
        if "dwarf" in e:
            idx, files, types = check_dwarf(site, site / e["dwarf"], problems)
        if "src" in e:
            check_src(site / e["src"], files, types, problems)
        check_tasks(name, types, problems)
        if idx:
            rows.append(f"{name}: {len(idx['shards'])} line shards, {len(idx['cfi'].get('shards', []))} CFI shards "
                        f"({idx['cfi'].get('fdes')} FDEs), {len(idx['locals'].get('shards', []))} locals shards "
                        f"({idx['locals'].get('functions')} functions), {(idx.get('funcs') or {}).get('shards')} function facts shards "
                        f"({(idx.get('funcs') or {}).get('functions')} functions), {len(files['files']) if files else 0} files, "
                        f"{idx.get('variables')} variables, {idx.get('typeCount')} types")
    print("\n".join(rows))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print(f"FAIL: {len(problems)} problems", file=sys.stderr)
        return 1
    print(f"PASS: {len(entries)} kernels have every feature's data")
    return 0


if __name__ == "__main__":
    sys.exit(main())
