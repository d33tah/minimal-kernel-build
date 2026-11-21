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
import collections
import gzip
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import threading
import time
import urllib.request

from playwright.sync_api import BrowserType, sync_playwright

import jscov
from tests.browser import common as tb
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

ERRORS_JS = """async ({ sed, sedErrors, tampered, raw, flaky, grep }) => {
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
  // An object's post step (Debian's recordmcount; here llvm-objcopy of
  // clang.wasm on the object), and grep steps (mksysmap's) on a file and
  // on the previous step's output.
  const r4 = await MKB.kitBuild(cc, kit({ files: [['x.S', hs, ns, 0]], objects: [Object.assign(obj('x.S'),
    { post: [{ tool: 'clang', argv: ['llvm-objcopy', '--add-section', '.mkb=x.S', 'x.o'] }] })] }), mem, { compile: 'all' });
  res.post = { size: r4.objects[0].size, plain: r2.objects[0].size };
  res.postBad = await err(() => MKB.kitBuild(cc, kit({ files: [['x.S', hs, ns, 0]], objects: [Object.assign(obj('x.S'),
    { post: [{ tool: 'clang', argv: ['llvm-objcopy', '--no-such-option', 'x.o'] }] })] }), mem, { compile: 'all' }));
  const [hg, ng] = await put('c1 T _text\\nc2 t .Lx\\nc3 a f.c\\nc4 T start\\n');
  const k3 = kit({ files: [['m.txt', hg, ng, 0]], steps: [
    step({ tool: 'grep', argv: ['grep', '-v', '-e', ' [aNUw] ', '-e', ' \\\\.L', 'm.txt'], inputs: ['m.txt'], stdout: { file: 'g1.txt' } }),
    step({ tool: 'sed', argv: ['sed', '-n', 'p', 'm.txt'], inputs: ['m.txt'], stdout: { var: 'v1' } }),
    step({ tool: 'grep', argv: ['grep', '-E', 'T (_text|start)'], stdin: 'v1', stdout: { file: 'g2.txt' } }),
  ], products: [['g1.txt', '', 0], ['g2.txt', '', 0]] });
  await MKB.kitBuild(cc, k3, mem, { keep: ['g1.txt', 'g2.txt'] }).then((r) => {
    res.grep = r.files.map(([, b]) => new TextDecoder().decode(b));
  });
  res.grepCases = grep.map(([args, input]) => { try { return MKB.kitLatin1(MKB.kitGrep(args, MKB.kitBytes(input))); } catch (e) { return 'error: ' + e.message; } });
  res.grepBad = await err(async () => MKB.kitGrep(['-c', 'x'], MKB.kitBytes('a')));
  res.grepNone = await err(async () => MKB.kitGrep(['-v'], MKB.kitBytes('a')));
  res.asmBad = await err(() => MKB.kitBuild(cc, kit({ files: [['y.S', hb, nb, 0]], objects: [obj('y.S')] }), mem, { compile: 'all' }));
  res.asmMissing = await err(() => MKB.kitBuild(cc, kit({ files: [['x.S', hs, ns, 0]], objects: [Object.assign(obj('z.S'), { deps: [] })] }), mem, { compile: 'all' }));
  res.stepFails = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'clang', argv: ['ld.lld', '-o', 'v', 'missing.o'] })] }), mem, {}));
  res.noModule = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'kallsyms', argv: ['scripts/kallsyms'] })] }), mem, {}));
  res.badModule = await err(() => MKB.kitBuild(cc, kit({ modules: { kallsyms: { sha256: 'x', size: 1 } }, steps: [step({ tool: 'kallsyms', argv: ['scripts/kallsyms'] })] }), mem, {}));
  res.notInKit = await err(() => MKB.kitBuild(cc, k1, mem, { edits: { 'b.c': 'int x;' } }));
  res.badCompile = await err(() => MKB.kitBuild(cc, k1, mem, { compile: 'some' }));
  res.noFile = await err(() => MKB.kitBuild(cc, kit({ steps: [step({ tool: 'cat', argv: ['cat', 'nothing.txt'] })] }), mem, {}));
  // A generator (E9: the minified Linux's initramfs): gen_init_cpio's input
  // lines made into the archive, run when every object is compiled; a line
  // it cannot take and a file it cannot read fail as the C tool does.
  const [hc, nc] = await put('abc');
  const gk = (list) => kit({ files: [['c.txt', hc, nc, 0]], gen: [step({ tool: 'cpio', argv: ['usr/gen_init_cpio'], list, inputs: ['c.txt'],
    outputs: [['out.cpio', '']] })], products: [['out.cpio', '', 0]] });
  const r5 = await MKB.kitBuild(cc, gk(['file /x c.txt 644 0 0', 'dir /dev 755 0 0', 'nod /dev/console 600 0 0 c 5 1']), mem, { compile: 'all' });
  res.cpio = r5.gen.map((g) => [g.out, g.sha256, g.size]).concat(r5.products.map((p) => [p[0], p[1], p[2]]));
  res.cpioNotRun = (await MKB.kitBuild(cc, Object.assign(gk(['file /x c.txt 644 0 0']), { products: [] }), mem, {})).gen.length;
  res.cpioBad = await err(() => MKB.kitBuild(cc, gk(['slink /a b 777 0 0']), mem, { compile: 'all' }));
  res.cpioMissing = await err(() => MKB.kitBuild(cc, gk(['file /x none.txt 644 0 0']), mem, { compile: 'all' }));
  cc.terminate();
  return res;
}"""


# kitGrep in the page against GNU grep: scripts/mksysmap's filter (the
# Debian kit runs it) and the other options.
GREP_SYMS = ("c1000000 T _text\nc1000010 t .Ltmp1\n00000000 a foo.c\nc1000020 T __crc_bar\nc1000030 W weak\n"
             "c1000040 T $x\nc1000050 r __kstrtab_baz\nc1000070 t L0\nc1000080 T start_kernel\nno newline")
GREP_CASES = [[a, GREP_SYMS] for a in (
    ["-v", "-e", " [aNUw] ", "-e", " \\$", "-e", " \\.L", "-e", " __crc_", "-e", " __kstrtab_", "-e", " __kstrtabns_", "-e", " L0$"],
    ["-v", "T"], ["-E", "T (_text|start)"], ["-F", "$x"], ["-ve", "kstrtab"], ["zzz"])]


def gnu_grep(args, text):
    return subprocess.run(["grep"] + args, input=text.encode("latin-1"), stdout=subprocess.PIPE).stdout.decode("latin-1")


def b64(s):
    return base64.b64decode(s)


def cpio_bytes(lines, files):
    """gen-kit.py's newc() (the archive usr/gen_init_cpio writes) of lines
    over files {path: bytes}."""
    import importlib.util
    import tempfile
    spec = importlib.util.spec_from_file_location("gen_kit", SITE / "gen-kit.py")
    gk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gk)
    with tempfile.TemporaryDirectory() as td:
        for p, b in files.items():
            (pathlib.Path(td) / p).write_bytes(b)
        return gk.newc(lines, pathlib.Path(td))


def check_errors(bname, res, sed_want, problems):
    want = {
        "missingKit": "HTTP 404", "format": "format 2, this page reads 1", "tampered": "its bytes have SHA-256",
        "asmBad": "x.o: clang exited with 1", "asmMissing": "no such file", "stepFails": "exited with 1",
        "noModule": "no module kallsyms", "badModule": "HTTP 404", "notInKit": "b.c is not a file of the kit",
        "badCompile": "compile is false", "noFile": "nothing.txt: no such file",
        "down": "always-503.gz: HTTP 503", "postBad": "x.o: llvm-objcopy exited with 1",
        "cpioBad": "out.cpio: gen_init_cpio exited with 1: gen_init_cpio: unsupported line: slink",
        "cpioMissing": "out.cpio: gen_init_cpio exited with 1: gen_init_cpio: cannot read: none.txt",
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
    bad = [(c[0], got) for c, got in zip(GREP_CASES, res["grepCases"]) if got != gnu_grep(*c)]
    if bad:
        problems.append(f"{bname}: kitGrep differs from GNU grep: {bad}")
    if "unsupported option -c" not in res["grepBad"] or "no pattern" not in res["grepNone"]:
        problems.append(f"{bname}: kitGrep must refuse -c and a missing pattern: {res['grepBad']!r}, {res['grepNone']!r}")
    if res["grep"] != ["c1 T _text\nc4 T start\n", "c1 T _text\nc4 T start\n"]:
        problems.append(f"{bname}: grep steps (a file, the previous step's output): {res['grep']}")
    if not res["post"]["size"] > res["post"]["plain"]:
        problems.append(f"{bname}: an object's post step did not rewrite it: {res['post']}")
    want = cpio_bytes(["file /x c.txt 644 0 0", "dir /dev 755 0 0", "nod /dev/console 600 0 0 c 5 1"], {"c.txt": b"abc"})
    w = [hashlib.sha256(want).hexdigest(), len(want)]
    if res["cpio"] != [["out.cpio"] + w, ["out.cpio"] + w] or res["cpioNotRun"] != 0:
        problems.append(f"{bname}: a cpio generator: {res['cpio']} (ran {res['cpioNotRun']} times without compiling), gen-kit.py's newc(): {w}")
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


# The editable Debian (plan E6): too large to compile every object, so link
# only (CI's bytes), an edit of init/main.c (one object, recordmcount after
# it, a string on the screen) and a header edit (exactly its users), with
# the time and the memory: the largest wasm memory of a run and the
# browser's RSS.
DEBIAN = "debian-6.1-686-clang"
DEBIAN_EDIT = {"path": "init/main.c", "from": "Run %s as init process", "to": "Run %s as init (edited in the page)"}

DEBIAN_JS = """async ({ name, edit, header }) => {
  const b64 = (u) => { let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); };
  const src = MKB.kitSource('kits/');
  const k = await src.kit(name);
  const img = k.shipped.path;
  const go = async (opts) => {
    const cc = new MKB.ClangWorker();
    try {
      const l = await cc.load();
      const r = await MKB.kitBuild(cc, k, src, Object.assign({ verify: false, keep: [img] }, opts));
      r.loadMs = l.ms;
      r.keep = r.files.map(([p, b]) => [p, b64(b)]);
      delete r.files;
      return r;
    } finally {
      cc.terminate();
    }
  };
  const text = async (p) => new TextDecoder().decode(await src.get(k.files.find((x) => x[0] === p)[1]));
  const res = {};
  res.link = await go({});
  res.edit = await go({ compile: 'changed', edits: { [edit.path]: (await text(edit.path)).replace(edit.from, edit.to) } });
  res.header = await go({ compile: 'changed', edits: { [header]: (await text(header)) + '\\n/* edited in the page */\\n' } });
  res.headerUsers = MKB.kitUsers(k).get(header);
  return res;
}"""


class PeakPss:
    """Every 0.2 s the PSS of this process's descendants (the browser's
    processes; shared pages counted once, unlike RSS): the largest sum and
    the largest single process (the renderer, its Worker included)."""

    def __init__(self):
        self.sum = self.one = 0
        self.stop = threading.Event()
        self.t = threading.Thread(target=self.loop, daemon=True)
        self.t.start()

    def loop(self):
        while not self.stop.is_set():
            kids = collections.defaultdict(list)
            for d in pathlib.Path("/proc").iterdir():
                if d.name.isdigit():
                    try:
                        st = (d / "stat").read_text()
                        kids[int(st[st.rindex(")") + 2:].split()[1])].append(int(d.name))
                    except (OSError, ValueError):
                        pass
            total, todo = 0, list(kids[os.getpid()])
            while todo:
                pid = todo.pop()
                todo += kids[pid]
                try:
                    pss = next(int(x.split()[1]) for x in pathlib.Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines()
                               if x.startswith("Pss:")) << 10
                except (OSError, StopIteration, ValueError):
                    continue
                total += pss
                self.one = max(self.one, pss)
            self.sum = max(self.sum, total)
            self.stop.wait(0.2)

    def done(self):
        self.stop.set()
        self.t.join()
        return self


def debian(args, base, out):
    """The Debian kit in each browser (DEBIAN_JS); returns the problems."""
    get = (lambda p: urllib.request.urlopen(f"{base}/{p}", timeout=120).read()) if args.base else \
        (lambda p: (pathlib.Path(args.kits) / p[len("kits/"):]).read_bytes())
    k = json.loads(get(f"kits/{DEBIAN}.json"))
    users = {}
    for o in k["objects"]:
        for d in o["deps"]:
            users.setdefault(k["files"][d][0], []).append(o["out"])
    # A header a few objects include (the dependency map at work).
    header = min((p for p, u in users.items() if p.endswith(".h") and p.startswith("include/linux/") and 2 <= len(u) <= 6),
                 key=lambda p: (len(users[p]), p))
    want = {p: h for p, h, _ in k["products"]}
    problems = []
    with sync_playwright() as p:
        for bname in args.browsers.split(","):
            bt = getattr(p, bname)
            browser = bt.launch(channel="chromium") if bname == "chromium" else RAW_LAUNCH(bt)
            ctx = browser.new_context(service_workers="block")
            page = ctx.new_page()
            page.set_default_timeout(args.timeout * 1000)
            page.goto(f"{base}/index.html?kernel=hello")
            page.wait_for_function("typeof window.MKB?.kitBuild === 'function'")
            time.sleep(2)
            idle = PeakPss()
            time.sleep(1)
            idle.done()
            rss, pss = PeakRss(), PeakPss()
            try:
                res = page.evaluate(DEBIAN_JS, {"name": DEBIAN, "edit": DEBIAN_EDIT, "header": header})
            except Exception as e:  # noqa: BLE001 (any page failure is a test failure)
                problems.append(f"{bname} {DEBIAN}: {e}")
                rss.done()
                pss.done()
                browser.close()
                continue
            peak = rss.done()
            pss.done()
            ua = page.evaluate("navigator.userAgent")
            browser.close()
            link, edit, hdr = res["link"], res["edit"], res["header"]
            prods = {pp: h for pp, h, _, _ in link["products"]}
            if prods != want:
                problems.append(f"{bname}: link only: products {prods}, CI's {want}")
            if link["objects"]:
                problems.append(f"{bname}: link only compiled {len(link['objects'])} objects")
            img = dict((pp, b64(b)) for pp, b in edit["keep"])[k["shipped"]["path"]]
            (out / f"{bname}-{DEBIAN}-edited.bzImage").write_bytes(img)
            eo = edit["objects"]
            if [o["out"] for o in eo] != ["init/main.o"] or eo[0]["sha256"] == eo[0]["wantg"] or \
                    any(h == w for _, h, _, w in edit["products"]):
                problems.append(f"{bname}: edit of init/main.c: {[(o['out'], o['sha256'][:12]) for o in eo]}, {edit['products']}")
            got = sorted(o["out"] for o in hdr["objects"])
            if got != sorted(res["headerUsers"]) or got != sorted(users[header]):
                problems.append(f"{bname}: header edit of {header} compiled {got}, its users {sorted(users[header])}")
            s = lambda ms: f"{ms / 1000:.1f} s"
            print(f"{bname}: {ua}\n  link only: {link['fetched']} blobs ({link['fetchedBytes'] >> 20} MiB) fetched in {s(link['fetchMs'])}, "
                  f"{len(link['steps'])} steps in {s(link['linkMs'])}, total {s(link['ms'])}, products "
                  f"{'SAME' if prods == want else 'DIFFERENT'}; largest wasm memory {link['wasmPeak'] >> 20} MiB\n"
                  f"  edit of init/main.c: {len(eo)} object compiled in {s(edit['compileMs'])}, link {s(edit['linkMs'])}, "
                  f"total {s(edit['ms'])}; largest wasm memory {edit['wasmPeak'] >> 20} MiB\n"
                  f"  header {header}: {len(got)} objects compiled ({', '.join(got)}) in {s(hdr['compileMs'])}, total {s(hdr['ms'])}\n"
                  f"  memory of the browser: PSS summed {pss.sum >> 20} MiB at most (the page idle before: {idle.sum >> 20} MiB), "
                  f"largest process {pss.one >> 20} MiB (idle {idle.one >> 20} MiB); RSS summed {peak >> 20} MiB; "
                  f"clang.wasm load {link['loadMs']:.0f} ms", flush=True)
    return problems


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
    ap.add_argument("--debian", action="store_true", help="only the editable Debian's kit (DEBIAN_JS)")
    args = ap.parse_args()
    names = [n for n in KITS if not args.only or n in args.only.split(",")]
    base = args.base.rstrip("/") if args.base else None
    if args.debian:
        out = pathlib.Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        httpd = None
        if not base:
            httpd = tb.serve()
            base = f"http://127.0.0.1:{httpd.server_address[1]}"
        jscov.install("kit-debian")
        problems = debian(args, base, out)
        if httpd:
            httpd.shutdown()
        if args.boot:
            img = out / f"{args.browsers.split(',')[0]}-{DEBIAN}-edited.bzImage"
            initrd = SITE / "kernels" / DEBIAN / "initrd.img"
            want = DEBIAN_EDIT["to"].replace("%s", "/init")
            r = subprocess.run([sys.executable, str(SITE / "qemu_screen_check.py"), "--qemu=qemu-system-i386", f"--kernel={img}",
                                "--mem=128M", f"--initrd={initrd}", "--append=nokaslr console=tty0 cryptomgr.notests break",
                                "--timeout=900", f"--expect={want}", "--expect=(initramfs)"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            print(f"  boot of the edited bzImage in qemu: {'ok' if r.returncode == 0 else 'FAILED'} ({want!r}, then (initramfs))", flush=True)
            if r.returncode:
                print(r.stdout[-3000:])
                problems.append(f"boot {DEBIAN} edited: {want!r} and (initramfs) not on the screen")
        if problems:
            print("\n".join(problems), file=sys.stderr)
            print("FAIL", file=sys.stderr)
            return 1
        print("PASS")
        return 0
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
                errs = page.evaluate(ERRORS_JS, {"sed": cases, "sedErrors": sed["errors"], "tampered": tampered, "raw": raw, "flaky": flaky, "grep": GREP_CASES})
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
