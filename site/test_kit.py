#!/usr/bin/env python3
"""The build kits in the page (plan E3): every kernel the site rebuilds
(hello, hello-os, minified-linux, minified-linux-os) is built again in
Chromium, Firefox and WebKit from its kit (site/kits/, gen-kit.py in the
kernels job) by kit.js with clang.wasm and the kit's host tools in the
page's Worker (clang.js), and must come out as CI's bytes:

  link    CI's prebuilt -g objects, then the kit's steps (archives, the
          LTO and final links, objcopy, sed, cat-size, mkpiggy,
          tools/build): every step's output, vmlinux (hello: kernel.elf)
          and the bzImage are CI's bytes
  all     every object compiled from the kit's sources and headers first:
          each object is CI's, and the products are again
  edit    hello only: "Hello, World!" in boot/hello.c becomes
          "Hello, Kit!!!" (compile "changed"): one object compiled, another
          bzImage, with the new string in it

The bzImage of the -g build is the one the page boots (the plain build's)
except for the bytes gen-kit.py recorded (the GNU build-id note, a hash
over the -g ELF): checked against site/kernels/<name>/bzImage. With --boot
the bzImages the browsers wrote boot in qemu (Hello, World! and Still
alive on the screen; the edited hello: Hello, Kit!!!), and every
browser's bytes must be the first browser's.

Also: kitSed in the page on site/testdata/sed-cases.json against GNU sed's
output; a blob that answers 503 twice and then its bytes (fetched again)
and one that always answers 503; the errors (a missing kit, another
format, a blob with other bytes, a failing step or compile, a module the kit lacks, an edit of a
file the kit lacks) and the steps clang.wasm cannot run alone (a .S
compile: clang -### and its jobs) on synthetic kits.

Usage:
  site/test_kit.py [--kits DIR] [--browsers chromium,firefox,webkit] [--out DIR] [--boot] [--json OUT]
  site/test_kit.py --base https://d33.pl/minimal-kernel-build [--browsers ...]

Needs: pip install playwright && playwright install chromium firefox webkit;
qemu-system-i386 and qemu-system-x86_64 for --boot.
"""

import argparse
import base64
import gzip
import hashlib
import json
import pathlib
import subprocess
import sys
import time
import urllib.request

from playwright.sync_api import BrowserType, sync_playwright

import jscov
import test_browser as tb
from test_clang_worker import PeakRss

SITE = tb.SITE
RAW_LAUNCH = BrowserType.launch  # before jscov.install wraps it (Chromium only)
KITS = ["hello", "hello-os", "minified-linux", "minified-linux-os"]
EDIT = {"path": "boot/hello.c", "from": "Hello, World!", "to": "Hello, Kit!!!"}

RUN_JS = """async ({ name, keep, edit }) => {
  const b64 = (u) => { let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };
  const src = MKB.kitSource('kits/');
  const k = await src.kit(name);
  const go = async (opts) => {
    const cc = new MKB.ClangWorker();
    try {
      const l = await cc.load();
      const seen = [];
      const r = await MKB.kitBuild(cc, k, src, Object.assign({ keep, onstep: (e) => seen.push(e.kind) }, opts));
      r.loadMs = l.ms;
      r.events = seen.length;
      r.keep = r.files.map(([p, b]) => [p, b64(b)]);
      delete r.files;
      return r;
    } finally {
      cc.terminate();
    }
  };
  const res = { ua: navigator.userAgent };
  res.link = await go({});
  res.all = await go({ compile: 'all' });
  if (edit) {
    const f = k.files.find((x) => x[0] === edit.path);
    const text = new TextDecoder().decode(await src.get(f[1]));
    res.edit = await go({ compile: 'changed', verify: false, edits: { [edit.path]: text.replace(edit.from, edit.to) } });
    res.editUsers = MKB.kitUsers(k).get(edit.path);
  }
  return res;
}"""

ERRORS_JS = """async ({ sed, sedErrors, tampered, raw, flaky }) => {
  const res = {};
  const err = async (f) => { try { await f(); return 'resolved'; } catch (e) { return e.message; } };
  const src = MKB.kitSource('kits/');
  res.missingKit = await err(() => src.kit('no-such-kit'));
  res.format = await err(() => src.kit('__format2'));
  res.tampered = await err(() => src.get(tampered));
  res.raw = (await src.get(raw)).length;
  try { res.flaky = (await src.get(flaky)).length; } catch (e) { res.flaky = e.message; }
  res.down = await err(() => src.get('always-503'));
  res.sed = sed.map(([, args, input]) => { try { return MKB.kitLatin1(MKB.kitSed(args, MKB.kitBytes(input))); } catch (e) { return 'error: ' + e.message; } });
  res.sedErrors = sedErrors.map(([args]) => { try { MKB.kitSed(args, MKB.kitBytes('a\\n')); return 'resolved'; } catch (e) { return e.message; } });

  // Synthetic kits on blobs in memory.
  const enc = new TextEncoder();
  const blobs = {};
  const put = async (text) => { const b = enc.encode(text); const h = await MKB.kitHash(b); blobs[h] = b; return [h, b.length]; };
  const mem = { get: async (h) => blobs[h].slice(), moduleUrl: () => 'kits/no-such-module.wasm' };
  const kit = (over) => Object.assign({ format: 1, name: 'synthetic', dir: '/s', files: [], objects: [], steps: [], modules: {}, products: [] }, over);
  const step = (over) => Object.assign({ inputs: [], outputs: [], fresh: [], mkdir: [] }, over);
  const cc = new MKB.ClangWorker();
  await cc.load();
  // sed on a file argument into a variable, cat of stdin into a file,
  // cat-size of two files.
  const [ha, na] = await put('x\\nq\\n');
  const want = enc.encode('x\\nq\\ny\\n\\x06\\x00\\x00\\x00');
  const k1 = kit({ files: [['a.txt', ha, na, 0]], steps: [
    step({ tool: 'sed', argv: ['sed', '-n', 's/x/y/p', 'a.txt'], inputs: ['a.txt'], stdout: { var: 'v0' } }),
    step({ tool: 'cat', argv: ['cat'], stdin: 'v0', stdout: { file: 'out/b.txt' }, outputs: [['out/b.txt', await MKB.kitHash(enc.encode('y\\n'))]], mkdir: ['out'] }),
    step({ tool: 'cat-size', argv: ['cat', 'a.txt', 'out/b.txt'], stdout: { file: 'c.bin' }, inputs: ['a.txt'], fresh: ['c.bin'] }),
  ], products: [['c.bin', await MKB.kitHash(want), want.length]] });
  const r1 = await MKB.kitBuild(cc, k1, mem, {});
  res.glue = { products: r1.products, steps: r1.steps.map((s) => s.outputs || null) };
  // A .S object (clang -### and its two jobs), compiled; one that does
  // not assemble and one whose source is missing fail with clang's message.
  const [hs, ns] = await put('.text\\nnop\\n');
  const [hb, nb] = await put('.text\\nnot_an_instruction\\n');
  const obj = (src) => ({ out: 'x.o', argv: ['clang', '--target=i386-linux-gnu', '-c', '-o', 'x.o', src], deps: [0], sha256: '', size: 0 });
  const r2 = await MKB.kitBuild(cc, kit({ files: [['x.S', hs, ns, 0]], objects: [obj('x.S')] }), mem, { compile: 'all' });
  res.asm = { size: r2.objects[0].size, tmp: (await cc.list()).files.filter((f) => f[0].startsWith('/s/.kit-tmp/')).length };
  res.asmBad = await err(() => MKB.kitBuild(cc, kit({ files: [['y.S', hb, nb, 0]], objects: [obj('y.S')] }), mem, { compile: 'all' }));
  res.asmMissing = await err(() => MKB.kitBuild(cc, kit({ files: [['x.S', hs, ns, 0]], objects: [Object.assign(obj('z.S'), { deps: [] })] }), mem, { compile: 'all' }));
  res.stepFails = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'clang', argv: ['ld.lld', '-o', 'v', 'missing.o'] })] }), mem, {}));
  res.noModule = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'kallsyms', argv: ['scripts/kallsyms'] })] }), mem, {}));
  res.badModule = await err(() => MKB.kitBuild(cc, kit({ modules: { kallsyms: { sha256: 'x', size: 1 } }, steps: [step({ tool: 'kallsyms', argv: ['scripts/kallsyms'] })] }), mem, {}));
  res.notInKit = await err(() => MKB.kitBuild(cc, k1, mem, { edits: { 'b.c': 'int x;' } }));
  res.badCompile = await err(() => MKB.kitBuild(cc, k1, mem, { compile: 'some' }));
  res.noFile = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'cat', argv: ['cat', 'nothing.txt'] })] }), mem, {}));
  cc.terminate();
  return res;
}"""


def b64(s):
    return base64.b64decode(s)


def check_errors(bname, res, sed_want, problems):
    want = {
        "missingKit": "HTTP 404", "format": "format 2, this page reads 1", "tampered": "its bytes have SHA-256",
        "asmBad": "x.o: clang exited with 1", "asmMissing": "no such file", "stepFails": "exited with 1",
        "noModule": "no module kallsyms", "badModule": "HTTP 404", "notInKit": "b.c is not a file of the kit",
        "badCompile": "compile is false", "noFile": "nothing.txt: no such file",
        "down": "always-503.gz: HTTP 503",
    }
    for k, w in want.items():
        if w not in str(res.get(k)):
            problems.append(f"{bname}: {k}: {res.get(k)!r}, wanted {w!r}")
    if res.get("raw") != sed_want["raw"]:
        problems.append(f"{bname}: a blob served unpacked: {res.get('raw')} bytes, wanted {sed_want['raw']}")
    # GitHub Pages answered 503 to one of the parallel blob fetches in the
    # live check of E3: the page tries again.
    if res.get("flaky") != sed_want["flaky"]:
        problems.append(f"{bname}: a blob answered 503 twice, then its bytes: {res.get('flaky')!r}, wanted {sed_want['flaky']} bytes")
    bad = [(c[0], got, w) for c, got, w in zip(sed_want["cases"], res["sed"], sed_want["out"]) if got != w]
    if bad:
        problems.append(f"{bname}: kitSed differs from GNU sed: {bad}")
    bad = [(a, got, w) for (a, w), got in zip(sed_want["errors"], res["sedErrors"]) if w not in got]
    if bad:
        problems.append(f"{bname}: kitSed must refuse: {bad}")
    g = res["glue"]
    if g["products"][0][1] != g["products"][0][3] or g["steps"][1][0][1] != g["steps"][1][0][2]:
        problems.append(f"{bname}: sed/cat/cat-size kit: {g}")
    if not res["asm"]["size"] or res["asm"]["tmp"]:
        problems.append(f"{bname}: a .S compile: {res['asm']} (an object, no temporary file left)")
    print(f"  errors and synthetic kits: {len(want)} refusals, {len(res['sed'])} sed scripts, .S object "
          f"{res['asm']['size']} B", flush=True)


def check_kit(bname, name, k, res, shipped, problems, out):
    tag = f"{bname} {name}"
    want = {p: h for p, h, _ in k["products"]}
    for run in ("link", "all"):
        r = res[run]
        prods = {p: h for p, h, _, _ in r["products"]}
        if prods != want:
            problems.append(f"{tag} {run}: products {prods}, CI's {want}")
        bad = [(i, o) for i, s in enumerate(r["steps"]) for o in s.get("outputs") or [] if o[1] != o[2]]
        if bad:
            problems.append(f"{tag} {run}: step outputs differ from CI's: {bad[:3]}")
        if len(r["steps"]) != len(k["steps"]):
            problems.append(f"{tag} {run}: {len(r['steps'])} of {len(k['steps'])} steps ran")
    objs = res["all"]["objects"]
    diff = [o["out"] for o in objs if o["sha256"] != o["want"]]
    if len(objs) != len(k["objects"]) or diff:
        problems.append(f"{tag}: {len(objs)} of {len(k['objects'])} objects compiled, different: {diff[:5]}")
    if res["link"]["objects"]:
        problems.append(f"{tag} link: compiled {len(res['link']['objects'])} objects")
    img_path = k["shipped"]["path"]
    img = dict((p, b64(b)) for p, b in res["link"]["keep"])[img_path]
    if hashlib.sha256(img).hexdigest() != want[img_path]:
        problems.append(f"{tag}: the kept bzImage is not CI's")
    (out / f"{bname}-{name}.bzImage").write_bytes(img)
    # The -g bzImage against the one the page boots.
    sh = k["shipped"]
    ranges = []
    if len(shipped) != len(img):
        problems.append(f"{tag}: bzImage {len(img)} B, the page's {len(shipped)} B")
    else:
        for i in range(len(img)):
            if img[i] != shipped[i]:
                if ranges and ranges[-1][0] + ranges[-1][1] == i:
                    ranges[-1][1] += 1
                else:
                    ranges.append([i, 1])
    if hashlib.sha256(shipped).hexdigest() != sh["sha256"] or ranges != sh["differs"]:
        problems.append(f"{tag}: the page's bzImage differs at {ranges[:4]}, the kit says {sh['differs']}")
    if sum(n for _, n in ranges) > 32:
        problems.append(f"{tag}: {sum(n for _, n in ranges)} bytes differ from the page's bzImage (more than a build-id)")
    a, l = res["all"], res["link"]
    verdict = "DIFFERENT" if any(p.startswith(tag) for p in problems) else "SAME"
    nd = sum(n for _, n in ranges)
    print(f"  {verdict} {name}: link {l['fetched']} blobs ({l['fetchedBytes']} B) in {l['fetchMs']:.0f} ms, {len(l['steps'])} steps "
          f"{l['linkMs']:.0f} ms; all: {a['fetched']} blobs ({a['fetchedBytes']} B) {a['fetchMs']:.0f} ms, {len(objs)} objects "
          f"{a['compileMs']:.0f} ms, link {a['linkMs']:.0f} ms, total {a['ms']:.0f} ms; bzImage {len(img)} B, "
          + (f"{nd} bytes (the build-id) differ from the page's" if nd else "the page's bytes")
          + f"; clang.wasm load {l['loadMs']:.0f} ms", flush=True)
    if "edit" in res:
        e = res["edit"]
        eimg = dict((p, b64(b)) for p, b in e["keep"])[img_path]
        (out / f"{bname}-{name}-edited.bzImage").write_bytes(eimg)
        ok = len(e["objects"]) == 1 == len(res["editUsers"]) and e["objects"][0]["sha256"] != e["objects"][0]["want"] \
            and all(h != w for _, h, _, w in e["products"]) and EDIT["to"].encode() in eimg and EDIT["from"].encode() not in eimg
        if not ok:
            problems.append(f"{tag} edit: objects {[(o['out'], o['sha256'][:12], o['want'][:12]) for o in e['objects']]}, "
                            f"products {e['products']}, new string in bzImage {EDIT['to'].encode() in eimg}")
        print(f"  edit {EDIT['path']}: {len(e['objects'])} object compiled ({e['compileMs']:.0f} ms), link {e['linkMs']:.0f} ms, "
              f"bzImage {len(eimg)} B with {EDIT['to']!r}", flush=True)


def boot(out, browsers, problems):
    first = browsers[0]
    for name in KITS + ["hello-edited"]:
        stem = name if name != "hello-edited" else "hello"
        files = [out / f"{b}-{stem}{'-edited' if name == 'hello-edited' else ''}.bzImage" for b in browsers]
        files = [f for f in files if f.exists()]
        if not files:
            problems.append(f"boot {name}: no bzImage written")
            continue
        if len({f.read_bytes() for f in files}) != 1:
            problems.append(f"boot {name}: the browsers wrote different bzImages")
        qemu = "qemu-system-i386" if stem.startswith("hello") else "qemu-system-x86_64"
        expect = ["Hello, Kit!!!"] if name == "hello-edited" else ["Hello, World!", "Still alive"]
        t = time.monotonic()
        r = subprocess.run([sys.executable, str(SITE / "qemu_screen_check.py"), f"--qemu={qemu}", f"--kernel={files[0]}",
                            "--mem=18M"] + [f"--expect={x}" for x in expect], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        print(f"  boot {files[0].name} ({first}'s, {len(files)} browsers' bytes equal) in {qemu}: "
              f"{'ok' if r.returncode == 0 else 'FAILED'} {expect} in {time.monotonic() - t:.0f} s", flush=True)
        if r.returncode:
            print(r.stdout[-3000:])
            problems.append(f"boot {name}: {expect} not on the screen")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kits", default=str(SITE / "kits"), help="the kits (served as kits/ next to the page)")
    ap.add_argument("--browsers", default="chromium")
    ap.add_argument("--only", help="comma-separated kit names")
    ap.add_argument("--base", help="test this deployed site instead of serving site/")
    ap.add_argument("--out", default="kit-images", help="the bzImages the browsers wrote go here")
    ap.add_argument("--boot", action="store_true", help="boot the bzImages in qemu")
    ap.add_argument("--json", help="write the results here")
    ap.add_argument("--timeout", type=float, default=1800)
    args = ap.parse_args()
    names = [n for n in KITS if not args.only or n in args.only.split(",")]
    base = args.base.rstrip("/") if args.base else None
    get = (lambda p: urllib.request.urlopen(f"{base}/{p}", timeout=120).read()) if base else \
        (lambda p: (SITE / p).read_bytes() if p.startswith("kernels/") else (pathlib.Path(args.kits) / p[len("kits/"):]).read_bytes())
    kits = {n: json.loads(get(f"kits/{n}.json")) for n in names}
    shipped = {n: get(f"kernels/{n}/bzImage") for n in names}
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    # Blobs the error checks serve otherwise: hello's first file with other
    # bytes, its second unpacked, its third after two 503 answers.
    hk = kits.get("hello") or json.loads(get("kits/hello.json"))
    tampered, raw, flaky = hk["files"][0][1], hk["files"][1][1], hk["files"][2][1]
    raw_bytes = gzip.decompress(get(f"kits/blobs/{raw}.gz"))
    flaky_gz = get(f"kits/blobs/{flaky}.gz")
    sed = json.loads((SITE / "testdata" / "sed-cases.json").read_text())
    cases = sed["cases"]
    sed_out = [subprocess.run(["sed"] + a, input=i.encode("latin-1"), stdout=subprocess.PIPE, check=True).stdout.decode("latin-1")
               for _, a, i in cases]
    sed_want = {"cases": cases, "out": sed_out, "errors": sed["errors"], "raw": len(raw_bytes),
                "flaky": len(gzip.decompress(flaky_gz))}
    httpd = None
    if not base:
        if pathlib.Path(args.kits).resolve() != (SITE / "kits").resolve():
            sys.exit("--kits: serve them as site/kits (the page fetches kits/)")
        httpd = tb.serve()
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
    jscov.install("kit")
    problems, results = [], {}
    browsers = args.browsers.split(",")
    with sync_playwright() as p:
        for bname in browsers:
            bt = getattr(p, bname)
            # Chromium itself, not the headless shell (test_clang_worker.py: its renderer crashes on a wasm trap).
            browser = bt.launch(channel="chromium") if bname == "chromium" else RAW_LAUNCH(bt)
            ctx = browser.new_context(service_workers="block")
            page = ctx.new_page()
            page.set_default_timeout(args.timeout * 1000)
            page.route("**/kits/__format2.json", lambda route: route.fulfill(json={"format": 2}))
            page.route(f"**/kits/blobs/{tampered}.gz", lambda route: route.fulfill(body=gzip.compress(b"other bytes")))
            page.route(f"**/kits/blobs/{raw}.gz", lambda route: route.fulfill(body=raw_bytes))
            answers = iter([503, 503])
            page.route(f"**/kits/blobs/{flaky}.gz",
                       lambda route: route.fulfill(status=next(answers, 200), body=flaky_gz))
            page.route("**/kits/blobs/always-503.gz", lambda route: route.fulfill(status=503, body="busy"))
            page.goto(f"{base}/index.html?kernel=hello")
            page.wait_for_function("typeof window.MKB?.ClangWorker === 'function' && typeof window.MKB?.kitBuild === 'function'")
            rss = PeakRss()
            t0 = time.monotonic()
            results[bname] = {}
            try:
                errs = page.evaluate(ERRORS_JS, {"sed": cases, "sedErrors": sed["errors"], "tampered": tampered, "raw": raw, "flaky": flaky})
                print(f"{bname}: {page.evaluate('navigator.userAgent')}", flush=True)
                check_errors(bname, errs, sed_want, problems)
                results[bname]["errors"] = errs
            except Exception as e:  # noqa: BLE001 (any page failure is a test failure)
                problems.append(f"{bname} errors: {e}")
            page.unroute(f"**/kits/blobs/{tampered}.gz")
            page.unroute(f"**/kits/blobs/{raw}.gz")
            page.unroute(f"**/kits/blobs/{flaky}.gz")
            for n in names:
                try:
                    res = page.evaluate(RUN_JS, {"name": n, "keep": [kits[n]["shipped"]["path"]], "edit": EDIT if n == "hello" else None})
                except Exception as e:  # noqa: BLE001
                    problems.append(f"{bname} {n}: {e}")
                    continue
                check_kit(bname, n, kits[n], res, shipped[n], problems, out)
                for r in ("link", "all", "edit"):
                    if r in res:
                        res[r].pop("keep", None)
                results[bname][n] = res
            print(f"{bname}: {time.monotonic() - t0:.1f} s, peak RSS of the browser {rss.done() >> 20} MiB", flush=True)
            ctx.close()
            browser.close()
    if httpd:
        httpd.shutdown()
    if args.boot:
        boot(out, browsers, problems)
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
