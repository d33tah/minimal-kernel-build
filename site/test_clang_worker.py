#!/usr/bin/env python3
"""The page's toolchain end to end (plan E0c): clang.wasm in the page's
Worker (clang.js -> clangworker.js -> wasi.js) compiles CI's objects again
from the inputs CI's build read, and the bytes must equal CI's.

The fixtures come from check-wasm-objects.mjs --own-wasi --fixture (the
kernels and debian-clang jobs): per object the compile's argv, its
working directory and every file it read, and the SHA-256 of the build's
object (or of the native clang's rerun of the same argv). The page loads
them with fetch (Playwright serves /__clang_fixtures/ from --fixtures), feeds
them to a ClangWorker, runs the argv and hashes what came out. Then, on the
first fixture: another -O level and an edited source must give other bytes
(the check tells objects apart and the compile reads the fed file); ld.lld,
llvm-objcopy, llvm-ar and llvm-nm on the object; the API's errors (a run
before load, a 404 module, terminate with a request pending).

Per browser and fixture it reports the times (module fetch and compile,
each run, instance creation), the wasm memory at clang's exit and the peak
RSS of the browser's processes; --json writes it all.

Usage:
  site/test_clang_worker.py --fixtures DIR [--browsers chromium,firefox,webkit]
  site/test_clang_worker.py --fixtures DIR --base https://d33.pl/minimal-kernel-build
  site/test_clang_worker.py --fixtures DIR --browsers firefox --firefox-profile OUT.json

--firefox-profile: Firefox runs with the Gecko profiler from startup
(MOZ_PROFILER_STARTUP, every thread, JS frames) and writes the profile at
exit; the script prints the functions with the most samples (self and
total) on the Worker's thread.

Needs: pip install playwright && playwright install chromium (firefox, webkit)
"""

import argparse
import collections
import json
import os
import pathlib
import re
import sys
import threading
import time

from playwright.sync_api import BrowserType, sync_playwright

import jscov
import test_browser as tb

SITE = tb.SITE
RAW_LAUNCH = BrowserType.launch  # before jscov.install wraps it (Chromium only)

RUN_JS = """async ({ names, repeat }) => {
  const b64 = (s) => { const t = atob(s), u = new Uint8Array(t.length); for (let i = 0; i < t.length; i++) u[i] = t.charCodeAt(i); return u; };
  const hex = async (b) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', b))].map((x) => x.toString(16).padStart(2, '0')).join('');
  const res = { ua: navigator.userAgent, jobs: [] };
  // A Worker that does not start fails its requests (it once hung them).
  const bad = new ClangWorker('no-such-worker.js');
  try { await bad.list(); res.badWorker = 'resolved'; } catch (e) { res.badWorker = e.message; }
  bad.terminate();
  const cc = new ClangWorker();
  try { await cc.run(['clang', '--version']); res.unloaded = 'resolved'; } catch (e) { res.unloaded = e.message; }
  try { await cc.load('clang/no-such.wasm'); res.missing = 'resolved'; } catch (e) { res.missing = e.message; }
  // Times are the Worker's (the page's performance.now() is clock.js's virtual clock).
  res.load = await cc.load();
  res.loadMs = res.load.ms;
  const v = await cc.run(['clang', '--version']);
  res.version = v.stdout.split('\\n')[0];
  res.versionMs = v.ms;
  let first = null;
  for (const n of names) {
    const f = await (await fetch('__clang_fixtures/' + n)).json();
    const w = await cc.write(f.files.map(([p, d]) => [p, b64(d)]));
    const out = f.cwd + '/' + f.obj;
    const job = { name: n, files: f.files.length, bytes: (await cc.list()).bytes, writeMs: w.ms, want: f.sha256, runs: [] };
    for (let i = 0; i < repeat; i++) {
      await cc.remove([out]);
      const r = await cc.run(f.args, { cwd: f.cwd, outputs: [out] });
      const o = r.files[0][1];
      job.runs.push({ code: r.code, ms: r.ms, instantiateMs: r.instantiateMs, mem: r.mem,
                      size: o ? o.length : null, sha256: o ? await hex(o) : null, stderr: r.stderr.slice(0, 2000) });
    }
    if (!first) {
      first = f;
      // Another -O level, then an edited source: other bytes.
      const other = f.args.map((a) => /^-O[0-9sz]?$/.test(a) ? (a === '-O0' || a === '-Os' ? '-O2' : '-Os') : a);
      let r = await cc.run(other, { cwd: f.cwd, outputs: [out] });
      job.otherOpt = { code: r.code, sha256: r.files[0][1] ? await hex(r.files[0][1]) : null };
      const src = f.cwd + '/' + f.args.findLast((a) => a.endsWith('.c'));
      const text = (await cc.read([src])).files[0][1];
      const add = new TextEncoder().encode('\\nvoid mkb_e0c(void);\\nvoid mkb_e0c(void) {}\\n');
      const edited = new Uint8Array(text.length + add.length);
      edited.set(text);
      edited.set(add, text.length);
      await cc.write([[src, edited]]);
      r = await cc.run(f.args, { cwd: f.cwd, outputs: [out] });
      job.edited = { code: r.code, sha256: r.files[0][1] ? await hex(r.files[0][1]) : null, stderr: r.stderr };
      await cc.write([[src, text]]);
      r = await cc.run(f.args, { cwd: f.cwd, outputs: [out] });
      job.restored = { code: r.code, sha256: r.files[0][1] ? await hex(r.files[0][1]) : null };
      // The other tools of the multicall module on the object.
      const d = f.cwd;
      const tools = [
        ['ld.lld', '-m', 'elf_i386', '-r', '-o', 'e0c/r.o', f.obj],
        // To a file it fails in clang.wasm under Node's WASI too ("Function
        // not implemented", from the module's libc); to stdout it works.
        ['llvm-objcopy', '-O', 'binary', f.obj, '-'],
        ['llvm-ar', 'rcs', 'e0c/lib.a', f.obj],
        ['llvm-nm', 'e0c/lib.a'],
        ['clang', '-c', '-o', 'e0c/x.o', 'e0c/missing.c'],
      ];
      await cc.write([[d + '/e0c/.keep', '']]);
      job.tools = [];
      for (const a of tools) {
        r = await cc.run(a, { cwd: d });
        job.tools.push({ args: a.join(' '), code: r.code, bytes: r.stdoutBytes.length, out: (r.stdout + r.stderr).slice(0, 2000) });
      }
      const outs = (await cc.read(['r.o', 'lib.a'].map((x) => d + '/e0c/' + x))).files;
      job.toolFiles = outs.map(([p, b]) => [p.slice(d.length + 1), b ? b.length : null]);
      job.toolList = (await cc.list()).files.filter((x) => x[0].startsWith(d + '/e0c/')).map((x) => x[0].slice(d.length + 1));
    }
    job.removed = (await cc.remove(f.files.map((x) => x[0]))).removed;
    res.jobs.push(job);
  }
  const pending = cc.run(['clang', '--version']);
  cc.terminate();
  try { await pending; res.terminated = 'resolved'; } catch (e) { res.terminated = e.message; }
  return res;
}"""


def tree_rss(root):
    """RSS in bytes of root's descendant processes (not root)."""
    kids = collections.defaultdict(list)
    for d in pathlib.Path("/proc").iterdir():
        if d.name.isdigit():
            try:
                stat = (d / "stat").read_text()
                kids[int(stat[stat.rindex(")") + 2:].split()[1])].append(int(d.name))
            except (OSError, ValueError):
                pass
    total, todo = 0, list(kids[root])
    while todo:
        pid = todo.pop()
        todo += kids[pid]
        try:
            for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
                if line.startswith("VmRSS:"):
                    total += int(line.split()[1]) * 1024
        except OSError:
            pass
    return total


class PeakRss:
    def __init__(self):
        self.peak, self.stop = 0, threading.Event()
        self.t = threading.Thread(target=self.loop, daemon=True)
        self.t.start()

    def loop(self):
        while not self.stop.is_set():
            self.peak = max(self.peak, tree_rss(os.getpid()))
            self.stop.wait(0.2)

    def done(self):
        self.stop.set()
        self.t.join()
        return self.peak


def profile_summary(path, top=25):
    """Functions with the most samples on the DOM Worker threads of a Gecko
    profile (the format MOZ_PROFILER_SHUTDOWN writes)."""
    prof = json.loads(pathlib.Path(path).read_text())
    selfc, totalc, n = collections.Counter(), collections.Counter(), 0

    def threads(p):
        for t in p.get("threads", []):
            yield t
        for sub in p.get("processes", []):
            yield from threads(sub)

    for t in threads(prof):
        if "Worker" not in t.get("name", ""):
            continue
        strings = t["stringTable"]
        st, ft, fr = t["stackTable"], t["frameTable"], t["samples"]
        sp, sf = st["schema"]["prefix"], st["schema"]["frame"]
        fl = ft["schema"]["location"]
        ss = fr["schema"]["stack"]
        for s in fr["data"]:
            k = s[ss]
            if k is None:
                continue
            n += 1
            seen, leaf = set(), True
            while k is not None:
                name = strings[ft["data"][st["data"][k][sf]][fl]]
                if leaf:
                    selfc[name] += 1
                    leaf = False
                if name not in seen:
                    totalc[name] += 1
                    seen.add(name)
                k = st["data"][k][sp]
    print(f"firefox profile {path}: {n} samples on Worker threads")
    for title, c in (("self", selfc), ("total", totalc)):
        print(f"  top by {title}:")
        for name, k in c.most_common(top):
            print(f"    {100 * k / max(n, 1):5.1f}%  {name[:150]}")
    return {"samples": n, "self": selfc.most_common(top), "total": totalc.most_common(top)}


def check(res, fixtures, problems, tag):
    if "not loaded" not in res.get("unloaded", ""):
        problems.append(f"{tag}: a run before load: {res.get('unloaded')!r}, wanted 'clang.wasm is not loaded'")
    if not res.get("badWorker", "").startswith("the Worker failed"):
        problems.append(f"{tag}: a request to a Worker that did not start: {res.get('badWorker')!r}")
    if "404" not in res.get("missing", ""):
        problems.append(f"{tag}: load of a missing module: {res.get('missing')!r}, wanted HTTP 404")
    if res.get("terminated") != "terminated":
        problems.append(f"{tag}: a request pending at terminate: {res.get('terminated')!r}")
    if not res.get("version", "").startswith("clang version 22.1.0"):
        problems.append(f"{tag}: clang --version: {res.get('version')!r}")
    for job in res["jobs"]:
        want = fixtures[job["name"]]["sha256"]
        for i, r in enumerate(job["runs"]):
            if r["code"] != 0 or r["sha256"] != want:
                problems.append(f"{tag} {job['name']} run {i + 1}: exit {r['code']}, {r['size']} B {r['sha256']}, CI's {want}; {r['stderr']}")
        if job["removed"] != job["files"]:
            problems.append(f"{tag} {job['name']}: removed {job['removed']} of {job['files']} files")
        if "otherOpt" in job:
            for k in ("otherOpt", "edited"):
                if job[k]["code"] != 0 or job[k]["sha256"] in (None, want):
                    problems.append(f"{tag} {job['name']}: {k} must compile to other bytes: {job[k]}")
            if job["restored"]["sha256"] != want:
                problems.append(f"{tag} {job['name']}: the source written back must give CI's bytes again: {job['restored']}")
            codes = [t["code"] for t in job["tools"]]
            if codes[:4] != [0, 0, 0, 0] or codes[4] == 0 or "no such file" not in job["tools"][4]["out"]:
                problems.append(f"{tag} {job['name']}: tools: {job['tools']}")
            if not re.search(r"^[0-9a-f]+ [Tt] \w", job["tools"][3]["out"], re.M):
                problems.append(f"{tag} {job['name']}: llvm-nm lists no code symbol: {job['tools'][3]}")
            if job["tools"][1]["bytes"] == 0:
                problems.append(f"{tag} {job['name']}: llvm-objcopy -O binary wrote nothing to stdout")
            if any(size is None or size == 0 for _, size in job["toolFiles"]):
                problems.append(f"{tag} {job['name']}: tool outputs: {job['toolFiles']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True, help="directory of check-wasm-objects.mjs --fixture files")
    ap.add_argument("--only", help="comma-separated fixture names (default: all)")
    ap.add_argument("--browsers", default="chromium")
    ap.add_argument("--repeat", type=int, default=2, help="runs per fixture (the first includes tier-up)")
    ap.add_argument("--base", help="test this deployed site instead of serving site/")
    ap.add_argument("--json", help="write the results here")
    ap.add_argument("--firefox-profile", help="Gecko profile of the Firefox run, written here")
    ap.add_argument("--timeout", type=float, default=900)
    args = ap.parse_args()

    fdir = pathlib.Path(args.fixtures)
    names = sorted(p.name for p in fdir.glob("*.json"))
    if args.only:
        names = [n for n in names if any(n.startswith(o) for o in args.only.split(","))]
    if not names:
        sys.exit(f"no fixtures in {fdir}")
    # Small ones first: the edit and tool checks run on the first.
    names.sort(key=lambda n: (fdir / n).stat().st_size)
    fixtures = {n: json.loads((fdir / n).read_text()) for n in names}
    httpd = None if args.base else tb.serve()
    base = args.base.rstrip("/") if args.base else f"http://127.0.0.1:{httpd.server_address[1]}"
    jscov.install("clang-worker")
    problems, results = [], {}
    with sync_playwright() as p:
        for bname in args.browsers.split(","):
            bt = getattr(p, bname)
            kw = {}
            if bname == "firefox" and args.firefox_profile:
                prof = str(pathlib.Path(args.firefox_profile).resolve())
                kw["env"] = dict(os.environ, MOZ_PROFILER_STARTUP="1", MOZ_PROFILER_SHUTDOWN=prof,
                                 MOZ_PROFILER_STARTUP_ENTRIES="16777216", MOZ_PROFILER_STARTUP_INTERVAL="1",
                                 MOZ_PROFILER_STARTUP_FEATURES="js,stackwalk,cpu",
                                 MOZ_PROFILER_STARTUP_FILTERS="GeckoMain,DOM Worker")
            browser = bt.launch(**kw) if bname == "chromium" else RAW_LAUNCH(bt, **kw)
            ctx = browser.new_context(service_workers="block")
            page = ctx.new_page()
            page.set_default_timeout(args.timeout * 1000)

            def fixture(route):
                n = route.request.url.split("/__clang_fixtures/")[1]
                route.fulfill(path=str(fdir / n), content_type="application/json")
            page.route("**/__clang_fixtures/*", fixture)
            page.goto(f"{base}/index.html?kernel=hello")
            page.wait_for_function("typeof ClangWorker === 'function'")
            rss = PeakRss()
            t0 = time.monotonic()
            try:
                res = page.evaluate(RUN_JS, {"names": names, "repeat": args.repeat})
            except Exception as e:  # noqa: BLE001 (any page failure is a test failure)
                problems.append(f"{bname}: {e}")
                res = None
            wall = time.monotonic() - t0
            res = res or {"jobs": []}
            res["wall_s"], res["peak_rss"] = round(wall, 1), rss.done()
            results[bname] = res
            check(res, fixtures, problems, bname)
            print(f"{bname}: {res.get('ua')}\n  {res.get('version')}; module fetch+compile {res.get('loadMs', 0):.0f} ms "
                  f"({(res.get('load') or {}).get('bytes', 0)} B); clang --version {res.get('versionMs', 0):.0f} ms; "
                  f"peak RSS of the browser {res['peak_rss'] >> 20} MiB; {wall:.1f} s", flush=True)
            for job in res["jobs"]:
                runs = ", ".join(f"{r['ms']:.0f} ms" for r in job["runs"])
                ok = all(r["sha256"] == job["want"] for r in job["runs"])
                print(f"  {'SAME' if ok else 'DIFFERENT'} {job['name']}: {job['files']} files {job['bytes']} B "
                      f"(write {job['writeMs']:.0f} ms); runs {runs}; instance {job['runs'][0]['instantiateMs']:.0f} ms; "
                      f"wasm memory {job['runs'][0]['mem'] >> 20} MiB", flush=True)
            ctx.close()
            browser.close()
            if bname == "firefox" and args.firefox_profile:
                for _ in range(60):
                    if pathlib.Path(args.firefox_profile).exists():
                        break
                    time.sleep(1)
                res["profile"] = profile_summary(args.firefox_profile)
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
