#!/usr/bin/env python3
"""Fail when a site script or a Python test file is over 800 lines.

R4 split every module over ~800 lines by concern; E8 grew three of them
past it again (A2). The lint job runs this so a file that grows too long
fails CI instead of waiting for an audit. Checked: the files git tracks
under the site directory that are page or tool scripts (.js, .jsx, .mjs,
.cjs) or Python tests (test_*.py and everything under tests/).

Usage: site/check-length.py [--root DIR]   (default: this script's directory)
       site/check-length.py --self-test    (a too-long file fails, 800 lines pass)
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

MAX_LINES = 800
SCRIPT_SUFFIXES = (".js", ".jsx", ".mjs", ".cjs")


def checked(rel):
    """Whether the tracked file rel (a path relative to the site root) is held to MAX_LINES."""
    p = Path(rel)
    if p.suffix in SCRIPT_SUFFIXES:
        return True
    return p.suffix == ".py" and (p.name.startswith("test_") or p.parts[0] == "tests")


def too_long(root):
    """[(rel, lines)] of the checked files under root over MAX_LINES, and how many were checked."""
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--", "."], check=True,
                         capture_output=True).stdout.decode()
    files = sorted(f for f in out.split("\0") if f and checked(f))
    long = []
    for rel in files:
        with open(Path(root) / rel, "rb") as f:
            n = sum(1 for _ in f)
        if n > MAX_LINES:
            long.append((rel, n))
    return long, len(files)


def run(root):
    long, n = too_long(root)
    if long:
        for rel, lines in long:
            print(f"check-length: {rel} has {lines} lines (more than {MAX_LINES}): split it by concern", file=sys.stderr)
        return 1
    print(f"check-length: {n} files, none over {MAX_LINES} lines")
    return 0


def self_test():
    """The real command on a scratch repository: 801 lines fail, 800 pass, other files are not held."""
    me = Path(__file__).resolve()
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        subprocess.run(["git", "init", "-q", str(root)], check=True)
        (root / "tests").mkdir()
        files = {
            "ok.js": MAX_LINES, "ok.jsx": MAX_LINES, "tests/ok.py": MAX_LINES,
            "gen-tool.py": MAX_LINES + 100,  # not a test: not held
            "notes.txt": MAX_LINES + 100,
            "untracked.js": 0,               # written below, never added
        }
        for rel, n in files.items():
            (root / rel).write_text("x\n" * n)
        subprocess.run(["git", "-C", str(root), "add", "--", *[f for f in files if f != "untracked.js"]], check=True)
        (root / "untracked.js").write_text("x\n" * (MAX_LINES + 1))

        def cli():
            return subprocess.run([sys.executable, str(me), "--root", str(root)], capture_output=True, text=True)

        r = cli()
        assert r.returncode == 0, "800-line files failed: " + r.stderr
        for rel in ("big.js", "big.jsx", "big.mjs", "test_big.py", "tests/big.py"):
            (root / rel).write_text("x\n" * (MAX_LINES + 1))
            subprocess.run(["git", "-C", str(root), "add", "--", rel], check=True)
            r = cli()
            assert r.returncode == 1 and rel in r.stderr, rel + " with 801 lines passed: " + r.stdout + r.stderr
            subprocess.run(["git", "-C", str(root), "rm", "-q", "--cached", "--", rel], check=True)
            (root / rel).unlink()
        # A last line without a newline counts too.
        (root / "edge.js").write_text("x\n" * MAX_LINES + "x")
        subprocess.run(["git", "-C", str(root), "add", "--", "edge.js"], check=True)
        r = cli()
        assert r.returncode == 1 and "edge.js has 801 lines" in r.stderr, "801 lines without a final newline passed"
    print("check-length: self-test passed (801 lines fail for .js, .jsx, .mjs and Python tests; 800 pass; other files are not held)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=str(Path(__file__).resolve().parent), help="the site directory (a git work tree)")
    ap.add_argument("--self-test", action="store_true", help="check that a too-long file fails")
    a = ap.parse_args()
    return self_test() if a.self_test else run(a.root)


if __name__ == "__main__":
    sys.exit(main())
