#!/usr/bin/env python3
"""SHA-256 over files and every site module they import (relative ES
module imports, followed), for a CI cache key that changes when the code
a cached job ran changes, and only then.

  site/deps-hash.py FILE...    prints the hash; with -v also the files
"""
import hashlib
import pathlib
import re
import sys

IMPORT = re.compile(r"""(?:^|\n)\s*(?:import|export)\b[^;]*?\bfrom\s*["'](\.{1,2}/[^"']+)["']|import\(\s*["'](\.{1,2}/[^"']+)["']\s*\)""")


def closure(files):
    seen, todo = set(), [pathlib.Path(f).resolve() for f in files]
    while todo:
        f = todo.pop()
        if f in seen:
            continue
        if not f.is_file():
            sys.exit(f"deps-hash: {f}: no such file")
        seen.add(f)
        if f.suffix in (".js", ".mjs", ".jsx"):
            for m in IMPORT.finditer(f.read_text()):
                todo.append((f.parent / (m.group(1) or m.group(2))).resolve())
    return sorted(seen)


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    if not args:
        sys.exit(__doc__)
    files = closure(args)
    root = pathlib.Path.cwd().resolve()
    h = hashlib.sha256()
    for f in files:
        rel = str(f.relative_to(root)) if f.is_relative_to(root) else str(f)
        h.update(rel.encode() + b"\0" + hashlib.sha256(f.read_bytes()).digest())
        if "-v" in sys.argv:
            print(rel, file=sys.stderr)
    print(h.hexdigest())
    return 0


if __name__ == "__main__":
    sys.exit(main())
