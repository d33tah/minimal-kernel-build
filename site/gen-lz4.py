#!/usr/bin/env python3
"""Vendor lz4-wasm (lz4_flex's LZ4 block compressor built to WebAssembly)
into site/lz4/.

The snapshots (snapshots.js) are compressed in snapworker.js with it;
lz4frame.js (this repository's code) loads the module and writes and
reads the LZ4 frame format around its blocks. The package's JavaScript
glue is an ES module for bundlers, so only the .wasm and the licences are
taken; lz4frame.js has the few lines of glue the module needs.

  site/gen-lz4.py           download the pinned npm tarball (its sha512
                            checked), write lz4/
  site/gen-lz4.py --check   offline: the files in lz4/ have the SHA-256 of
                            the pinned upstream files (CI runs this)

Pin and licence: site/THIRD_PARTY.md.
"""
import base64
import hashlib
import io
import sys
import tarfile
import urllib.request
from pathlib import Path

VERSION = "0.9.2"
TARBALL = f"https://registry.npmjs.org/lz4-wasm/-/lz4-wasm-{VERSION}.tgz"
INTEGRITY = "sha512-8Y/OvG/nTQNWR242NhLmjI7VzLMRKFaxg/+Pxi/vkczgFdrS4qiJ8l6n30XyNJyc0TB5C66Ncdjq345Kx5XsvQ=="
FILES = {
    "lz4_wasm_bg.wasm": "c858f9884d964ec49294b0c09d3330d6b0a640da850dbeb5f9c491e1a3dd58cb",
    "LICENSE_MIT": "57d06b12e86105dcf1f21f1b195ff5849c6fe5c72523b1351ffec2d82a99737e",
    "LICENSE_APACHE": "62c7a1e35f56406896d7aa7ca52d0cc0d272ac022b5d2796e7d6905db8a3636a",
}
DIR = Path(__file__).resolve().parent / "lz4"


def check():
    bad = []
    for name, sha in FILES.items():
        p = DIR / name
        if not p.is_file():
            bad.append(f"{p}: missing")
        elif hashlib.sha256(p.read_bytes()).hexdigest() != sha:
            bad.append(f"{p}: differs from lz4-wasm {VERSION}'s")
    for b in bad:
        print(b, file=sys.stderr)
    if not bad:
        print(f"lz4/: lz4-wasm {VERSION}, {len(FILES)} files as published")
    return 1 if bad else 0


def fetch():
    data = urllib.request.urlopen(TARBALL, timeout=60).read()
    algo, want = INTEGRITY.split("-", 1)
    if base64.b64encode(hashlib.new(algo, data).digest()).decode() != want:
        sys.exit(f"{TARBALL}: {algo} differs from the pinned integrity")
    DIR.mkdir(exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        for name in FILES:
            (DIR / name).write_bytes(tar.extractfile("package/" + name).read())
    return check()


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        sys.exit(check())
    if sys.argv[1:]:
        sys.exit(__doc__)
    sys.exit(fetch())
