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
before load, a 404 module, a Worker that does not start, terminate with a
request pending). Then testdata/wasi-probe.c (built with wasi-sdk into
DIR/wasi-probe.wasm) in a ClangWorker of its own: the WASI calls of files,
directories, stdin and clocks, with the errors programs check for.

Per browser and fixture it reports the times (module fetch and compile,
each run, instance creation), the wasm memory at clang's exit and the peak
RSS of the browser's processes; --json writes it all.

Usage:
  site/test_clang_worker.py --fixtures DIR [--browsers chromium,firefox,webkit]
  site/test_clang_worker.py --fixtures DIR --base https://d33.pl/minimal-kernel-build
  site/test_clang_worker.py --fixtures DIR --browsers firefox --firefox-profile OUT.json
  site/test_clang_worker.py --fixtures DIR --browsers firefox --no-automation

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
import subprocess
import sys
import tempfile
import threading
import time

from playwright.sync_api import BrowserType, sync_playwright

import jscov
import test_browser as tb

SITE = tb.SITE
RAW_LAUNCH = BrowserType.launch  # before jscov.install wraps it (Chromium only)

RUN_JS = """async ({ names, repeat, probe }) => {
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
        job.tools.push({ args: a.join(' '), code: r.code, bytes: r.stdoutBytes.length, out: (r.stdout + r.stderr).slice(0, 2000),
                         codeSymbols: (r.stdout.match(/^[0-9a-f]+ [Tt] \\w/gm) || []).length });
      }
      const outs = (await cc.read(['r.o', 'lib.a'].map((x) => d + '/e0c/' + x))).files;
      job.toolFiles = outs.map(([p, b]) => [p.slice(d.length + 1), b ? b.length : null]);
      job.toolList = (await cc.list()).files.filter((x) => x[0].startsWith(d + '/e0c/')).map((x) => x[0].slice(d.length + 1));
    }
    job.removed = (await cc.remove(f.files.map((x) => x[0]))).removed;
    res.jobs.push(job);
  }
  if (probe) {
    // wasi-probe.c (wasi-sdk) on the same layer, in a Worker of its own.
    const pw = new ClangWorker();
    await pw.load('__clang_fixtures/wasi-probe.wasm');
    const r = await pw.run(['wasi-probe'], { cwd: '/probe', stdin: 'probe\\n' });
    res.probe = { code: r.code, stdout: r.stdout, stderr: r.stderr, ms: r.ms };
    pw.terminate();
  }
  const pending = cc.run(['clang', '--version']);
  cc.terminate();
  try { await pending; res.terminated = 'resolved'; } catch (e) { res.terminated = e.message; }
  return res;
}"""


AUTO_HTML = """<!doctype html>
<meta charset="utf-8">
<title>clang.wasm without automation</title>
<script>const withV = (u) => u;</script>
<script src="clang.js"></script>
<script>
(async () => {
  let res;
  try { res = await (%s)(%s); } catch (e) { res = { error: String(e), jobs: [] }; }
  await fetch("__clang_result", { method: "POST", body: JSON.stringify(res) });
  window.close();  // the last window: Firefox quits (and writes a profile)
})();
</script>
"""


def serve(fdir, auto):
    """site/ plus /__clang_fixtures/NAME from fdir, /__clang_auto.html (runs
    RUN_JS with auto["args"] by itself) and POST /__clang_result (sets
    auto["result"], auto["done"])."""
    class Handler(tb.QuietHandler):
        def do_GET(self):
            path = self.path.split("?")[0]
            if path.startswith("/__clang_fixtures/"):
                name = path.rsplit("/", 1)[1]
                body, ctype = (fdir / name).read_bytes(), "application/wasm" if name.endswith(".wasm") else "application/json"
            elif path == "/__clang_auto.html":
                body, ctype = (AUTO_HTML % (RUN_JS, json.dumps(auto["args"]))).encode(), "text/html"
            else:
                return super().do_GET()
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            auto["result"] = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            self.send_response(204)
            self.end_headers()
            auto["done"].set()
    return tb.serve(handler_class=Handler)


def run_direct(executable, base, env, prefs, timeout, auto):
    """Firefox started by itself (no Playwright and its Juggler agent),
    headless with a fresh profile, on the auto page; the result comes back
    as a POST, then the page closes its window and Firefox quits."""
    auto["done"].clear()
    with tempfile.TemporaryDirectory() as prof:
        prefs = dict(prefs, **{"dom.allow_scripts_to_close_windows": True})
        (pathlib.Path(prof) / "user.js").write_text("".join(f"user_pref({json.dumps(k)}, {json.dumps(v)});\n" for k, v in prefs.items()))
        proc = subprocess.Popen([executable, "-headless", "-no-remote", "-profile", prof, f"{base}/__clang_auto.html"],
                                env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        ok = auto["done"].wait(timeout)
        try:
            proc.wait(60)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    return auto["result"] if ok else {"error": f"no result in {timeout:.0f} s", "jobs": []}


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


def profile_summary(path, top=20):
    """Where the Worker's time went in a Gecko profile (the format
    MOZ_PROFILER_SHUTDOWN writes): samples of the thread that ran
    clangworker.js, idle ones (waiting for a message) left out, by leaf
    (clang's own wasm code, wasi.js, other JS, native code; Gecko's native
    frames are not symbolicated here) and the functions with the most
    samples; and the busy samples of the JS helper threads (wasm tier-up
    compiles run there)."""
    prof = json.loads(pathlib.Path(path).read_text())

    def threads(p):
        yield from p.get("threads", [])
        for sub in p.get("processes", []):
            yield from threads(sub)

    kinds, names, helpers, idle, n = collections.Counter(), collections.Counter(), collections.Counter(), 0, 0
    for t in threads(prof):
        strings = t["stringTable"]
        st, ft, sm = t["stackTable"], t["frameTable"], t["samples"]
        sp, sf, fl, ss = st["schema"]["prefix"], st["schema"]["frame"], ft["schema"]["location"], sm["schema"]["stack"]

        def stack(k):
            out = []
            while k is not None:
                out.append(strings[ft["data"][st["data"][k][sf]][fl]])
                k = st["data"][k][sp]
            return out
        mine = "Worker" in t.get("name", "") and any("clangworker.js" in x for x in strings)
        for smp in sm["data"]:
            k = smp[ss]
            if k is None:
                continue
            frames = stack(k)
            if any("WaitForWorkerEvents" in f or "Wait" in f.split(" ")[0] and "Event" in f for f in frames[:6]):
                if mine:
                    idle += 1
                continue
            if not mine:
                if "Helper" in t.get("name", ""):
                    helpers[t["name"]] += 1
                continue
            n += 1
            leaf = frames[0]
            js = next((f for f in frames if "wasi.js" in f or "clangworker.js" in f), None)
            kinds["clang (wasm)" if leaf.startswith("wasm-function") else
                  "wasi.js" if "wasi.js" in leaf else
                  "other JS" if ".js" in leaf else
                  "native under wasi.js" if js and "wasi.js" in js and frames.index(js) < next((i for i, f in enumerate(frames) if f.startswith("wasm-function")), 1 << 30) else
                  "native"] += 1
            for f in set(frames):
                if not f.startswith("0x") and not f.startswith("wasm-function"):
                    names[f] += 1
    print(f"firefox profile {path}: {n} busy samples of the clangworker.js thread ({idle} idle); "
          f"busy JS helper samples: {dict(helpers)}")
    for k, c in kinds.most_common():
        print(f"    {100 * c / max(n, 1):5.1f}%  {k}")
    print("  named frames on the stack (total):")
    for name, c in names.most_common(top):
        print(f"    {100 * c / max(n, 1):5.1f}%  {name[:150]}")
    return {"busy": n, "idle": idle, "helpers": dict(helpers), "leaf": kinds.most_common(), "names": names.most_common(top)}


def check_probe(res, problems, tag):
    """wasi-probe.c's output: every check() of the source "ok", exit code 3."""
    want = (SITE / "testdata" / "wasi-probe.c").read_text().count('check("')
    p = res.get("probe") or {}
    out = p.get("stdout", "").splitlines()
    oks = [x for x in out if x.startswith("ok ")]
    fails = [x for x in out if x.startswith("FAIL ")]
    print(f"  wasi-probe: exit {p.get('code')}, {len(oks)} of {want} checks ok, {p.get('ms', 0):.0f} ms", flush=True)
    if p.get("code") != 3 or len(oks) != want or fails or out[-1:] != ["PROBE OK"] or p.get("stderr") != "probe stderr\n":
        problems.append(f"{tag}: wasi-probe: exit {p.get('code')}, {len(oks)} of {want} ok, {fails}, stderr {p.get('stderr')!r}")


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
            if not job["tools"][3]["codeSymbols"]:
                problems.append(f"{tag} {job['name']}: llvm-nm lists no code symbol: {job['tools'][3]}")
            if job["tools"][1]["bytes"] == 0:
                problems.append(f"{tag} {job['name']}: llvm-objcopy -O binary wrote nothing to stdout")
            if any(size is None or size == 0 for _, size in job["toolFiles"]):
                problems.append(f"{tag} {job['name']}: tool outputs: {job['toolFiles']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fixtures", required=True, help="directory of check-wasm-objects.mjs --fixture files")
    ap.add_argument("--only", help="comma-separated fixture names (default: all)")
    ap.add_argument("--no-probe", action="store_true", help="the fixtures have no wasi-probe.wasm: skip it")
    ap.add_argument("--browsers", default="chromium")
    ap.add_argument("--repeat", type=int, default=2, help="runs per fixture (the first includes tier-up)")
    ap.add_argument("--base", help="test this deployed site instead of serving site/")
    ap.add_argument("--json", help="write the results here")
    ap.add_argument("--firefox-profile", help="Gecko profile of the Firefox run, written here")
    ap.add_argument("--firefox-pref", action="append", default=[], help="NAME=true|false|NUMBER for Firefox (e.g. javascript.options.wasm_baselinejit=false)")
    ap.add_argument("--timeout", type=float, default=900)
    ap.add_argument("--no-automation", action="store_true",
                    help="Firefox only: start it without Playwright, on a page that runs the same checks by itself")
    ap.add_argument("--firefox-binary", help="with --no-automation: this Firefox instead of Playwright's")
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
    probe = (fdir / "wasi-probe.wasm").exists()
    if not probe and not args.no_probe:
        sys.exit(f"no {fdir}/wasi-probe.wasm (build site/testdata/wasi-probe.c, or --no-probe)")
    auto = {"args": {"names": names, "repeat": args.repeat, "probe": probe}, "done": threading.Event(), "result": None}
    httpd = None if args.base else serve(fdir, auto)
    base = args.base.rstrip("/") if args.base else f"http://127.0.0.1:{httpd.server_address[1]}"
    if args.no_automation and (args.base or args.browsers != "firefox"):
        sys.exit("--no-automation: Firefox only, on the local site")
    jscov.install("clang-worker")
    problems, results = [], {}
    with sync_playwright() as p:
        for bname in args.browsers.split(","):
            bt = getattr(p, bname)
            kw = {}
            if bname == "firefox" and args.firefox_pref:
                kw["firefox_user_prefs"] = {k: json.loads(v) for k, v in (x.split("=", 1) for x in args.firefox_pref)}
            if bname == "firefox" and args.firefox_profile:
                prof = str(pathlib.Path(args.firefox_profile).resolve())
                kw["env"] = dict(os.environ, MOZ_PROFILER_STARTUP="1", MOZ_PROFILER_SHUTDOWN=prof,
                                 MOZ_PROFILER_STARTUP_ENTRIES="16777216", MOZ_PROFILER_STARTUP_INTERVAL="1",
                                 MOZ_PROFILER_STARTUP_FEATURES="js,stackwalk,cpu",
                                 MOZ_PROFILER_STARTUP_FILTERS="GeckoMain,DOM Worker,Helper")
            if args.no_automation:
                rss = PeakRss()
                t0 = time.monotonic()
                res = run_direct(args.firefox_binary or bt.executable_path, base, kw.get("env"), kw.get("firefox_user_prefs", {}), args.timeout, auto)
                if res.get("error"):
                    problems.append(f"{bname}: {res['error']}")
            else:
                browser = bt.launch(**kw) if bname == "chromium" else RAW_LAUNCH(bt, **kw)
                ctx = browser.new_context(service_workers="block")
                page = ctx.new_page()
                page.set_default_timeout(args.timeout * 1000)

                def fixture(route):
                    n = route.request.url.split("/__clang_fixtures/")[1]
                    route.fulfill(path=str(fdir / n), content_type="application/wasm" if n.endswith(".wasm") else "application/json")
                page.route("**/__clang_fixtures/*", fixture)
                page.goto(f"{base}/index.html?kernel=hello")
                page.wait_for_function("typeof ClangWorker === 'function'")
                rss = PeakRss()
                t0 = time.monotonic()
                try:
                    res = page.evaluate(RUN_JS, auto["args"])
                except Exception as e:  # noqa: BLE001 (any page failure is a test failure)
                    problems.append(f"{bname}: {e}")
                    res = None
            wall = time.monotonic() - t0
            res = res or {"jobs": []}
            res["wall_s"], res["peak_rss"] = round(wall, 1), rss.done()
            results[bname] = res
            print(f"{bname}: {res.get('ua')}\n  {res.get('version')}; module fetch+compile {res.get('loadMs', 0):.0f} ms "
                  f"({(res.get('load') or {}).get('bytes', 0)} B); clang --version {res.get('versionMs', 0):.0f} ms; "
                  f"peak RSS of the browser {res['peak_rss'] >> 20} MiB; {wall:.1f} s", flush=True)
            for job in res["jobs"]:
                runs = ", ".join(f"{r['ms']:.0f} ms" for r in job["runs"])
                ok = all(r["sha256"] == job["want"] for r in job["runs"])
                print(f"  {'SAME' if ok else 'DIFFERENT'} {job['name']}: {job['files']} files {job['bytes']} B "
                      f"(write {job['writeMs']:.0f} ms); runs {runs}; instance {job['runs'][0]['instantiateMs']:.0f} ms; "
                      f"wasm memory {job['runs'][0]['mem'] >> 20} MiB", flush=True)
            check(res, fixtures, problems, bname)
            if probe:
                check_probe(res, problems, bname)
            if not args.no_automation:
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
