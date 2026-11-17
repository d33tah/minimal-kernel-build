"""Browser test suite framegold (test_browser.py --suites)."""

import hashlib
import json
import os
import time

from tests.browser.common import report, run_to_wait, show_tab, SITE, wait_for
from tests.browser.reverse import rev_step_to


# ---- framegold: the Calls recordings against a golden (P1b) -----------------------
# The stepper's call frames and everything the Calls tab derives from them
# (call tree, the top graph, timeline layout rows, summary, depth, task
# names, callers/callees, frame counts) on a fixed stepped run: Run to the
# kernel entry, step to A, step back and branch there (B: the recordings
# past it are dropped, Stepper.branch), step on (C), and for the minified
# Linux on past its task switch (D). Runs on the virtual clock repeat
# exactly (the minified Linux from the reset vector, Debian with the JIT to
# startup_32). Each part is a hash compared with
# site/testdata/frames-golden.json: how the frames are stored must not
# change one number of what is shown. The golden belongs to the kernel it
# was made with (bzImage SHA-256): a new kernel needs a new golden, written
# with MKB_FRAMEGOLD_WRITE=1 from code whose Calls output is trusted.
FRAMEGOLD = SITE / "testdata" / "frames-golden.json"
# kernel: (URL params, Run to, then counts from there: A, back from A to B,
# on from B to C, D or 0). The minified Linux's D is past its one task
# switch, just before its end. Debian's A has
# millions of frames, tens of tasks and hundreds of switches and
# continuations, some of them dropped by the branch.
FRAMEGOLD_CASES = {
    "minified-linux": ("fastforward=0&revk=20000", "0x100000", 300000, 100000, 80000, 722000),
    "debian-6.1-686": ("", "startup_32", 75000000, 5000000, 3000000, 0),
}
FRAMEGOLD_JS = """async () => {
  const st = MKB.stepper, n = st.fr.n;
  const hex = (b) => Array.from(new Uint8Array(b), (x) => x.toString(16).padStart(2, '0')).join('');
  const sha = async (o) => hex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(JSON.stringify(o))));
  // Two 32-bit FNV-1a lanes over the low and high 32 bits of integers.
  const lanes = () => [0x811c9dc5, 0x2f6b9a1d];
  const mix = (a, x) => { const lo = x >>> 0, hi = Math.floor(x / 4294967296) | 0;
    a[0] = Math.imul(a[0] ^ lo, 16777619) >>> 0; a[0] = Math.imul(a[0] ^ hi, 16777619) >>> 0;
    a[1] = Math.imul(a[1] ^ hi, 16777619) >>> 0; a[1] = Math.imul(a[1] ^ lo, 2246822519) >>> 0; };
  const fin = (a) => a[0].toString(16).padStart(8, '0') + a[1].toString(16).padStart(8, '0');
  const COLS = ['start', 'end', 'node', 'depth', 'parent', 'irq', 'cont', 'child', 'esp', 'task', 'from'];
  const col = (k) => [st.fr.start(k), st.fr.end(k), st.fr.node(k), st.fr.depth(k), st.fr.parent(k), st.fr.irq(k), st.fr.cont(k), st.fr.child(k),
                      st.fr.esp(k), st.fr.task(k), st.fr.from(k)];
  const h = COLS.map(lanes), bad = [];
  for (let k = 0; k < n; k++) {
    const v = col(k);
    for (let c = 0; c < v.length; c++) {
      const x = v[c];
      if (typeof x !== 'number' || !Number.isInteger(x)) { if (bad.length < 5) bad.push([k, COLS[c], String(x)]); continue; }
      mix(h[c], x);
    }
  }
  const out = { steps: st.steps, frames: n, contFrames: st.contFrames, tasks: st.taskCount, task: st.task,
    switches: st.swStep.length, bad, cols: {}, frameBytes: st.fr.bytes(),
    pastEnd: COLS.filter((c) => st.fr[c](n) !== undefined || st.fr[c](-1) !== undefined) };
  COLS.forEach((c, i) => { out.cols[c] = fin(h[i]); });
  out.state = await sha({ stack: st.stack, lanes: [...st.lanes], lane: st.lane, lastTop: st.lastTop, open: st.allOpen(),
    suspended: [...st.suspended], sw: [st.swStep, st.swTask, st.swIrq], taskOf: [...st.taskOf], taskRegion: st.taskRegion });
  const saved = [MKB.G.fold, MKB.G.chains];
  let rows = 0;
  for (const fold of [false, true]) {
    MKB.G.fold = fold;
    MKB.G.taskNames = null; MKB.G.taskInfo = null; MKB.G.depthKey = -1;
    const d = MKB.graphData();
    out['tree_' + fold] = await sha(d.call_tree);
    out['graph_' + fold] = await sha([d.steps, d.entry_step, d.nodes, d.edges]);
    for (const chains of [false, true]) {
      MKB.G.chains = chains; MKB.G.layout = null;
      const L = MKB.timelineLayout(st), a = lanes(), deepest = MKB.layoutDeepest(st, L);
      for (let k = 0; k < n; k++) {
        mix(a, L.row[k]); mix(a, L.isHead[k] ? 1 : 0); mix(a, deepest[k]);
        if (L.row[k] + 1 > rows) rows = L.row[k] + 1;
      }
      out['layout_' + fold + '_' + chains] = fin(a) + ':' + await sha([...L.chain]);
    }
    MKB.drawTop();
    out['top_' + fold] = await sha([MKB.G.boxes.map((b) => b[0]), document.getElementById('graph_canvas').dataset.edges]);
    out['summary_' + fold] = await sha([MKB.timelineSummary(), MKB.timelineDepth(st), MKB.taskGuesses(st)]);
  }
  [MKB.G.fold, MKB.G.chains] = saved; MKB.G.layout = null; MKB.G.taskNames = null; MKB.G.taskInfo = null; MKB.G.depthKey = -1;
  out.rows = rows;
  const top = st.nodeOrder.slice().sort((a, b) => st.nodeSteps[b] - st.nodeSteps[a] || a - b).slice(0, 12);
  out.sandwich = await sha(top.map((x) => MKB.sandwich(st, x)));
  const counts = [];
  for (let k = 0; k < n; k += Math.max(1, Math.floor(n / 5000))) counts.push(MKB.frameCounts(st, k));
  for (const k of st.allOpen()) counts.push(MKB.frameCounts(st, k));
  out.counts = await sha(counts);
  return out; }"""
FRAMEGOLD_SUMMARY = ("steps", "frames", "contFrames", "tasks", "switches", "rows")
# Stepper.fr.bytes() per frame with a million frames or more: the columns
# (FR_BYTES in stepper.js) plus the unfilled part of the last chunk (plain
# JS arrays of the same fields take about 60, and twice that while growing).
FRAMEGOLD_BYTES_PER_FRAME = 36


def framegold_kernel(browser, base, name, case):
    """(problems, the digests at A, B, C and D (see FRAMEGOLD_CASES))."""
    params, first, to, back, more, last = case
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    problems, d = [], {}
    try:
        page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10&{params}#debugger", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
            return [f"{name}: did not start paused"], d
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator && !!MKB.symbols"), 60)
        t0 = time.monotonic()
        show_tab(page, "debugger")
        page.fill("#dbg_addr", first)
        page.click("#dbg_runto")
        want_at = int(first, 16) if first.startswith("0x") else page.evaluate(f"MKB.symbols.symbols.find((s) => s.name === '{first}' && s.paddr >= 0x100000).paddr")
        ok, at, status = run_to_wait(page, {want_at}, 600)
        if not ok:
            return [f"{name}: Run to {first} stopped at {at:#x}: {status[:200]}"], d
        s0 = page.evaluate("MKB.stepper.steps")
        show_tab(page, "calls")

        def at(w, target):
            if rev_step_to(page, s0 + target) != s0 + target:
                problems.append(f"{name}: could not step to {target} past {first} for {w} (at {page.evaluate('MKB.stepper.steps') - s0})")
                return False
            page.evaluate("MKB.afterStop()")
            d[w] = page.evaluate(FRAMEGOLD_JS)
            return True

        if not at("A", to):
            return problems, d
        page.evaluate(f"MKB.revStepBack({back})")
        if not wait_for(lambda: page.evaluate(f"!MKB.REV.busy && MKB.stepper.steps === {s0 + to - back}"), 120, 0.1):
            return [f"{name}: Step back {back} from {to} past {first} ended at {page.evaluate('MKB.stepper.steps') - s0}: "
                    f"{page.inner_text('#dbg_status')[:300]!r} {errors[:3]}"], d
        page.evaluate("MKB.revBranch(MKB.stepper, 'framegold'); MKB.afterStop()")
        d["B"] = page.evaluate(FRAMEGOLD_JS)
        if not at("C", to - back + more) or (last and not at("D", last)):
            return problems, d
        d["seconds"] = round(time.monotonic() - t0, 1)
        problems += [f"{name}: page error {e}" for e in errors]
        return problems, d
    finally:
        page.close()


def suite_framegold(ctx):
    write = os.environ.get("MKB_FRAMEGOLD_WRITE") == "1"
    gold = json.loads(FRAMEGOLD.read_text()) if FRAMEGOLD.exists() else {}
    failed = False
    for name, case in FRAMEGOLD_CASES.items():
        if name not in ctx.entries:
            continue
        sha = hashlib.sha256((SITE / ctx.entries[name]["path"]).read_bytes()).hexdigest()
        problems, d = framegold_kernel(ctx.browser, ctx.base, name, case)
        parts = [w for w in "ABCD" if w in d]
        # Memory of the frames (not in the golden: storage may change, the numbers may not).
        per = {w: round(d[w].pop("frameBytes") / max(1, d[w]["frames"]), 1) for w in parts}
        past = {w: d[w].pop("pastEnd") for w in parts}
        r = {"seconds": d.get("seconds"), "bytes per frame": per, **{w: {k: d[w][k] for k in FRAMEGOLD_SUMMARY} for w in parts}}
        if not problems:
            for w in parts:
                if d[w]["bad"]:
                    problems.append(f"{name} {w}: frame values that are not integers: {d[w]['bad']}")
                if past[w]:
                    problems.append(f"{name} {w}: reads past the last frame or before the first give values (want undefined): {past[w]}")
                if d[w]["frames"] >= 1000000 and per[w] > FRAMEGOLD_BYTES_PER_FRAME:
                    problems.append(f"{name} {w}: {per[w]} bytes per frame for {d[w]['frames']} frames, want <= {FRAMEGOLD_BYTES_PER_FRAME}")
            if not d["A"]["frames"] > d["B"]["frames"] < d["C"]["frames"]:
                problems.append(f"{name}: frames A {d['A']['frames']}, B {d['B']['frames']}, C {d['C']['frames']}: "
                                "the branch dropped none or the run recorded none after it")
        if write and not problems:
            gold[name] = {"bzImage_sha256": sha, "case": list(case), **{w: d[w] for w in parts}}
        elif not problems:
            g = gold.get(name)
            if not g:
                problems.append(f"{name}: no golden in {FRAMEGOLD.name} (write it with MKB_FRAMEGOLD_WRITE=1)")
            elif g["bzImage_sha256"] != sha or g["case"] != list(case):
                problems.append(f"{name}: the golden was made with bzImage {g['bzImage_sha256'][:12]} and case {g['case']}, this is "
                                f"{sha[:12]} and {list(case)}: write a new golden (MKB_FRAMEGOLD_WRITE=1) from code whose Calls output is trusted")
            else:
                for w in parts:
                    diff = {k: (d[w].get(k), v) for k, v in g[w].items() if d[w].get(k) != v}
                    if diff:
                        problems.append(f"{name} {w}: differs from the golden (got, want): {json.dumps(diff)[:1500]}")
        failed |= report("framegold", problems, r, kernel=name)
    if write:
        FRAMEGOLD.write_text(json.dumps(gold, indent=1, sort_keys=True) + "\n")
    if "minified-linux" in ctx.entries:
        failed |= report("framegold_snapshot", *framegold_snapshot(ctx.browser, ctx.base))
    return failed


# Every recorded frame after a snapshot restore: an end at or after its
# start, children counts not negative.
FRAMES_SANE_JS = """() => { const fr = MKB.stepper.fr, bad = [];
  for (let k = 0; k < fr.n; k++) {
    const s = fr.start(k), e = fr.end(k), c = fr.child(k);
    if ((e !== -1 && e < s) || c < 0) { if (bad.length < 5) bad.push([k, MKB.stepper.nodeName(fr.node(k)), s, e, c]); bad.n = (bad.n || 0) + 1; }
  }
  return { frames: fr.n, steps: MKB.stepper.steps, bad, nBad: bad.n || 0 }; }"""


def framegold_snapshot(browser, base):
    """The minified Linux stepped past its entry, a snapshot saved, stepped on
    (frames open at the later count), the snapshot restored: the count goes
    back, and the frames open then must not end before they started (an end
    before the start gave negative inclusive and children counts in Calls)."""
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    problems, r = [], {}
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux&paused=1&mips=10&fastforward=0#debugger", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
            return ["did not start paused"], r
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator && !!MKB.symbols"), 60)
        page.fill("#dbg_addr", "0x100000")
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {0x100000}, 600)
        if not ok:
            return [f"Run to 0x100000 stopped at {at:#x}: {status[:200]}"], r
        s0 = page.evaluate("MKB.stepper.steps")
        rev_step_to(page, s0 + 100000)
        page.evaluate("MKB.saveSnapshot()")
        if not wait_for(lambda: page.evaluate("MKB.snapshots.length > 0"), 60):
            return [f"no snapshot saved: {page.inner_text('#snap_status')[:200]!r}"], r
        rev_step_to(page, s0 + 300000)
        r["before"] = page.evaluate(FRAMES_SANE_JS)
        r["open before"] = page.evaluate("MKB.stepper.allOpen().length")
        page.evaluate("MKB.restoreSnapshot(MKB.snapshots[MKB.snapshots.length - 1])")
        wait_for(lambda: page.evaluate("MKB.stepper.steps < %d" % (s0 + 300000)), 30)
        page.evaluate("MKB.pauseAll()")
        # On from the snapshot: stepped (the open calls unwind) and a stretch of the JIT (markGap).
        rev_step_to(page, page.evaluate("MKB.stepper.steps") + 20000)
        page.evaluate("MKB.stepper.markGap()")
        r["after"] = page.evaluate(FRAMES_SANE_JS)
        if not r["open before"]:
            problems.append("no frame open before the restore (test setup)")
        if r["after"]["steps"] >= s0 + 300000:
            problems.append(f"the restore did not take the count back: {r['after']['steps']}")
        if r["after"]["nBad"]:
            problems.append(f"{r['after']['nBad']} frames end before their start or have negative children counts after "
                            f"the snapshot restore: {r['after']['bad']} ([frame, function, start, end, children])")
        problems += [f"page error {e}" for e in errors]
        return problems, r
    finally:
        page.close()
