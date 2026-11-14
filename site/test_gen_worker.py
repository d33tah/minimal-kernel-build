#!/usr/bin/env python3
"""The generators in the browser (plan E1): genworker.js (kgen.js, with
llvm-nm and llvm-objdump of clang/clang.wasm on wasi.js) makes the data
files of every kernel the site ships from the ELFs CI's generators read,
and they must equal CI's files in site/kernels/<name>/:

  symbols.json, disasm.json   from the plain build's ELF (gen-symbols.py,
                              gen-disasm.py; Debian: --trim, no disasm.json)
  dwarf/                      from the -g build's ELF (gen-dwarf.js)
  src/                        from that dwarf/ and the source tree
                              (gen-src.js); the tree here is CI's src/
                              files, unpacked one directory down
                              (linux/<path>, as a picked folder may hold it)

The set of files must be CI's, and every file CI's bytes, with one stated
normalisation: gzip. CI compresses with zlib level 9 (Node), the browser
with CompressionStream, which writes other bytes for the same data (and
Node 20 and 22 differ from each other too). So a .gz file is compared
after gunzip, and the counts of gzipped bytes in dwarf/index.json
(locals.gzBytes, funcs.gzBytes) and src/index.json (gzipBytes) are set to
0 on both sides first; every other byte must be equal. The page and this
script each apply the normalisation (JavaScript there, Python here) and
the two must agree on CI's files.

Also: an input that is not an ELF and an ELF cut to half must fail with
the reason, and hello's symbols.json must differ from hello-os's (the
check tells outputs apart). Per browser and kernel it prints the Worker's
times and the peak RSS of the browser.

Usage:
  site/test_gen_worker.py --elfs DIR [--browsers chromium,firefox,webkit] [--json OUT]
  site/test_gen_worker.py --elfs DIR --base https://d33.pl/minimal-kernel-build

DIR holds <name>.plain.elf and <name>.elf (-g) for hello, hello-os,
minified-linux, minified-linux-os, and debian-6.1-686.elf (the -dbg
vmlinux: both). With --base the deployed site is tested (clang.wasm and
CI's files from there); ELFs above 64 MiB are left out (Playwright's
route cannot carry them).

Needs: pip install playwright && playwright install chromium firefox webkit
"""

import argparse
import gzip
import hashlib
import json
import pathlib
import re
import shutil
import sys
import time
import urllib.request

from playwright.sync_api import BrowserType, sync_playwright

import jscov
import test_browser as tb
from test_clang_worker import PeakRss

SITE = tb.SITE
RAW_LAUNCH = BrowserType.launch  # before jscov.install wraps it (Chromium only)

# name, the ELF's file name in CI (dwarf/index.json "source"), plain ELF,
# -g ELF, gen-symbols.py --trim, disasm.json
KERNELS = [
    ("hello", "kernel.elf", "hello.plain.elf", "hello.elf", False, True),
    ("hello-os", "kernel.elf", "hello-os.plain.elf", "hello-os.elf", False, True),
    ("minified-linux", "vmlinux", "minified-linux.plain.elf", "minified-linux.elf", False, True),
    ("minified-linux-os", "vmlinux", "minified-linux-os.plain.elf", "minified-linux-os.elf", False, True),
    ("debian-6.1-686", "vmlinux-6.1.0-50-686", "debian-6.1-686.elf", "debian-6.1-686.elf", True, False),
]

GZ_COUNT = re.compile(rb'"(gzBytes|gzipBytes)":\d+')

RUN_JS = """async ({ k, elfs, ci }) => {
  const hex = async (b) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', b))].map((x) => x.toString(16).padStart(2, '0')).join('');
  const gunzip = async (b) => new Uint8Array(await new Response(new Blob([b]).stream().pipeThrough(new DecompressionStream('gzip'))).arrayBuffer());
  // The normalisation (test_gen_worker.py's docstring).
  const norm = async (path, b) => {
    if (path.endsWith('.gz'))
      return gunzip(b);
    if (path === 'dwarf/index.json' || path === 'src/index.json')
      return new TextEncoder().encode(new TextDecoder().decode(b).replace(/"(gzBytes|gzipBytes)":\\d+/g, '"$1":0'));
    return b;
  };
  const sums = async (files) => {
    const out = [];
    for (const [p, b] of files)
      out.push([p, await hex(await norm(p, b)), b.length, await hex(b)]);
    return out;
  };
  const get = async (url) => { const r = await fetch(url); if (!r.ok) throw new Error(url + ': HTTP ' + r.status); return r; };
  const res = { ua: navigator.userAgent, name: k.name, times: {}, ms: {} };
  const clang = new URL('clang/clang.wasm', location.href).href;
  const w = new ClangWorker(withV('genworker.js'));
  const plain = await (await get(elfs + k.plain)).blob();
  let t = performance.now();
  const a = await w.request({ op: 'gen', file: plain, name: k.elf, label: k.name, clang, trim: k.trim, disasm: k.disasm, dwarf: false });
  res.ms.plain = a.ms; res.times.plain = a.times; res.log = a.log;
  const dbg = k.debug === k.plain ? plain : await (await get(elfs + k.debug)).blob();
  const b = await w.request({ op: 'gen', file: dbg, name: k.elf, clang, symbols: false, disasm: false });
  res.ms.dwarf = b.ms; res.times.dwarf = b.times; res.log = res.log.concat(b.log);
  // The source tree: CI's src/ files, unpacked under linux/.
  const ix = await (await get(ci + 'src/index.json')).json();
  const tree = [];
  for (const p of ix.files)
    tree.push(['linux/' + p, new Blob([await gunzip(new Uint8Array(await (await get(ci + 'src/' + p.split('/').map(encodeURIComponent).join('/') + '.gz')).arrayBuffer()))])]);
  const dwarf = b.files.map(([p, d]) => [p.slice('dwarf/'.length), d]);
  const c = await w.request({ op: 'src', dwarf, tree, origin: ix.origin, notes: ix.notes });
  res.ms.src = c.ms; res.log = res.log.concat(c.log); res.pastEnd = c.pastEnd;
  res.files = await sums([...a.files, ...b.files, ...c.files]);
  res.why = { ...a.why, ...b.why };
  // CI's files as the page reads them, normalised here too.
  // One at a time: Debian's ~3,500 at once failed to fetch.
  res.ci = [];
  for (const p of k.ciFiles)
    res.ci.push(...await sums([[p, new Uint8Array(await (await get(ci + p.split('/').map(encodeURIComponent).join('/'))).arrayBuffer())]]));
  if (k.name === 'hello') {
    // Not an ELF; the plain ELF cut to half (its section headers are at the end).
    for (const [what, file] of [['notElf', new Blob(['not an ELF file at all, just text'])], ['half', plain.slice(0, plain.size >> 1)]])
      try { await w.request({ op: 'gen', file, name: 'x', clang }); res[what] = 'made files'; } catch (e) { res[what] = e.message; }
  }
  w.terminate();
  return res;
}"""


def norm(path, b):
    if path.endswith(".gz"):
        return gzip.decompress(b)
    if path in ("dwarf/index.json", "src/index.json"):
        return GZ_COUNT.sub(rb'"\1":0', b)
    return b


def ci_list(kdir):
    """CI's files of a kernel directory (local), relative paths."""
    out = []
    for p in sorted(kdir.rglob("*")):
        rel = p.relative_to(kdir).as_posix()
        if p.is_file() and (rel in ("symbols.json", "disasm.json") or rel.startswith(("dwarf/", "src/"))):
            out.append(rel)
    return out


def ci_list_remote(base, name):
    """The same from a deployed site: what the index files name."""
    get = lambda p: urllib.request.urlopen(f"{base}/kernels/{name}/{p}", timeout=60).read()
    out = ["symbols.json"]
    try:
        get("disasm.json")
        out.append("disasm.json")
    except OSError:
        pass
    ix = json.loads(get("dwarf/index.json"))
    out += ["dwarf/index.json", "dwarf/" + ix["files"], "dwarf/" + ix["types"]]
    out += [f"dwarf/{s:x}.bin" for s in ix["shards"]]
    out += [f"dwarf/cfi-{s:x}.bin" for s in (ix["cfi"].get("shards") or [])]
    out += [f"dwarf/locals-{s:x}.json.gz" for s in ix["locals"]["shards"]]
    out += [f"dwarf/funcs-{k:x}.json.gz" for k in range(ix["funcs"]["shards"])]
    sx = json.loads(get("src/index.json"))
    out += ["src/index.json"] + [f"src/{p}.gz" for p in sx["files"]]
    return sorted(out)


def serve(elfs):
    class Handler(tb.QuietHandler):
        def do_GET(self):
            path = self.path.split("?")[0]
            if not path.startswith("/__gen_elfs/"):
                return super().do_GET()
            p = (elfs / path[len("/__gen_elfs/"):]).resolve()
            if elfs.resolve() not in p.parents or not p.is_file():
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(p.stat().st_size))
            self.end_headers()
            with p.open("rb") as f:
                shutil.copyfileobj(f, self.wfile, 1 << 20)
    return tb.serve(handler_class=Handler)


def check(bname, res, ci_local, problems):
    tag = f"{bname} {res['name']}"
    got = {p: (h, n, raw) for p, h, n, raw in res["files"]}
    ci = {p: h for p, h, _, _ in res["ci"]}
    if set(got) != set(ci):
        problems.append(f"{tag}: files made {len(got)}, CI's {len(ci)}; only made: {sorted(set(got) - set(ci))[:5]}, "
                        f"only CI's: {sorted(set(ci) - set(got))[:5]}")
    diff = [p for p in sorted(set(got) & set(ci)) if got[p][0] != ci[p]]
    if diff:
        problems.append(f"{tag}: {len(diff)} files differ from CI's: {diff[:8]}")
    if ci_local is not None:
        mismatch = [p for p, h in ci.items() if ci_local.get(p) != h]
        if mismatch:
            problems.append(f"{tag}: the page's normalisation of CI's files is not this script's: {mismatch[:5]}")
    if res.get("pastEnd"):
        problems.append(f"{tag}: gen-src's past-the-end lines: {res['pastEnd'][:3]}")
    gz = [p for p in got if p.endswith(".gz")]
    raw_same = sum(1 for p in gz if p in ci and got[p][2] == res_raw(res, p))
    print(f"  {'SAME' if not diff and set(got) == set(ci) else 'DIFFERENT'} {res['name']}: {len(got)} files "
          f"({sum(n for _, n, _ in got.values())} B; {len(gz)} gzipped, {raw_same} of them with CI's very bytes); "
          f"Worker {res['ms']['plain']:.0f} ms (symbols/disasm) + {res['ms']['dwarf']:.0f} ms (dwarf) + {res['ms']['src']:.0f} ms (src); "
          f"times {res['times']}", flush=True)


def res_raw(res, path):
    return next((raw for p, _, _, raw in res["ci"] if p == path), None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--elfs", required=True, help="directory of <name>.plain.elf and <name>.elf")
    ap.add_argument("--browsers", default="chromium")
    ap.add_argument("--only", help="comma-separated kernel names")
    ap.add_argument("--base", help="test this deployed site instead of serving site/")
    ap.add_argument("--json", help="write the results here")
    ap.add_argument("--timeout", type=float, default=1800)
    args = ap.parse_args()
    elfs = pathlib.Path(args.elfs)
    kernels = [k for k in KERNELS if not args.only or k[0] in args.only.split(",")]
    missing = [f for k in kernels for f in (k[2], k[3]) if not (elfs / f).is_file()]
    if missing:
        sys.exit(f"missing in {elfs}: {missing}")
    if args.base:
        big = [k[0] for k in kernels if max((elfs / k[2]).stat().st_size, (elfs / k[3]).stat().st_size) > 64 << 20]
        kernels = [k for k in kernels if k[0] not in big]
        print(f"--base: kernels with an ELF above 64 MiB left out: {big}")
    base = args.base.rstrip("/") if args.base else None
    httpd = None if base else serve(elfs)
    base = base or f"http://127.0.0.1:{httpd.server_address[1]}"
    ci_files, ci_local = {}, {}
    for k in kernels:
        if args.base:
            ci_files[k[0]] = ci_list_remote(base, k[0])
            ci_local[k[0]] = None
        else:
            kdir = SITE / "kernels" / k[0]
            ci_files[k[0]] = ci_list(kdir)
            ci_local[k[0]] = {p: hashlib.sha256(norm(p, (kdir / p).read_bytes())).hexdigest() for p in ci_files[k[0]]}
    jscov.install("gen-worker")
    problems, results = [], {}
    with sync_playwright() as p:
        for bname in args.browsers.split(","):
            bt = getattr(p, bname)
            # Chromium itself, not the headless shell (test_clang_worker.py: its renderer crashes on a wasm trap).
            browser = bt.launch(channel="chromium") if bname == "chromium" else RAW_LAUNCH(bt)
            ctx = browser.new_context(service_workers="block")
            page = ctx.new_page()
            page.set_default_timeout(args.timeout * 1000)

            def elf_route(route):
                n = route.request.url.split("/__gen_elfs/")[1].split("?")[0]
                p = (elfs / n).resolve()
                if elfs.resolve() not in p.parents or not p.is_file():
                    return route.fulfill(status=404)
                route.fulfill(path=str(p), content_type="application/octet-stream")
            if args.base:
                page.route("**/__gen_elfs/**", elf_route)
            page.goto(f"{base}/index.html?kernel=hello")
            page.wait_for_function("typeof ClangWorker === 'function' && typeof kfetch === 'function'")
            results[bname] = []
            rss = PeakRss()
            t0 = time.monotonic()
            for name, elf, plain, debug, trim, disasm in kernels:
                k = {"name": name, "elf": elf, "plain": plain, "debug": debug, "trim": trim, "disasm": disasm, "ciFiles": ci_files[name]}
                try:
                    res = page.evaluate(RUN_JS, {"k": k, "elfs": f"{base}/__gen_elfs/", "ci": f"{base}/kernels/{name}/"})
                except Exception as e:  # noqa: BLE001 (any page failure is a test failure)
                    problems.append(f"{bname} {name}: {e}")
                    continue
                results[bname].append(res)
                if name == "hello":
                    print(f"{bname}: {res['ua']}", flush=True)
                    if "not a 32-bit little-endian ELF" not in res.get("notElf", ""):
                        problems.append(f"{bname}: a file that is not an ELF: {res.get('notElf')!r}")
                    if "truncated" not in res.get("half", ""):
                        problems.append(f"{bname}: an ELF cut to half: {res.get('half')!r}")
                check(bname, res, ci_local[name], problems)
            wall = time.monotonic() - t0
            peak = rss.done()
            print(f"{bname}: {wall:.1f} s for {len(kernels)} kernels, peak RSS of the browser {peak >> 20} MiB", flush=True)
            # The comparison tells outputs apart: hello's symbols.json is not hello-os's.
            by = {r["name"]: {p: h for p, h, _, _ in r["files"]} for r in results[bname]}
            cis = {r["name"]: {p: h for p, h, _, _ in r["ci"]} for r in results[bname]}
            if "hello" in by and "hello-os" in cis and by["hello"].get("symbols.json") == cis["hello-os"].get("symbols.json"):
                problems.append(f"{bname}: hello's symbols.json equals hello-os's")
            ctx.close()
            browser.close()
    if httpd:
        httpd.shutdown()
    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(results, indent=1))
    if problems:
        print("\n".join(problems), file=sys.stderr)
        print("FAIL", file=sys.stderr)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
