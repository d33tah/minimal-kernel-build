"""Browser test suite tasks, tasks_minified (test_browser.py --suites)."""

import json
import math
import os
import re
import shutil
import subprocess
import time

import jscov
from tests.browser.common import (
    bm_dialog_save, debian_names, debug_elf, graph_box_xy, hover_byte, kernel_entries, nav_at, nav_settle, open_page,
    report, show_canvas, show_tab, shown_tab, SITE, wait_for,
)


TASKS_JS = """() => { const st = MKB.stepper, s0 = st.sinceEntry, n = st.fr.n, name = (k) => st.nodeName(st.fr.node(k));
    const iv = [], bySym = {}, perTask = {}; let roots = 0, cont = 0;
    for (let k = 0; k < n; k++) if (st.fr.parent(k) < 0 && st.fr.start(k) >= s0) {
        iv.push([st.fr.start(k), st.fr.end(k) < 0 ? st.steps : st.fr.end(k)]);
        if (st.fr.cont(k)) { cont++; continue; }
        if (st.fr.irq(k)) continue;
        roots++; bySym[name(k)] = (bySym[name(k)] || 0) + 1; perTask[st.fr.task(k)] = (perTask[st.fr.task(k)] || 0) + 1; }
    iv.sort((a, b) => a[0] - b[0]); let last = s0, overlap = 0;
    for (const [a, b] of iv) { if (a < last) overlap += Math.min(b, last) - a; if (b > last) last = b; }
    const te = []; for (let k = 0; k < n; k++) if (name(k) === '__trace_early_add_event_dirs') {
        let under = false; for (let p = st.fr.parent(k); p >= 0; p = st.fr.parent(p)) if (name(p) === 'event_trace_init') under = true;
        te.push(under); }
    return { tasks: st.taskCount, roots, contRoots: cont, overlap, maxRootsPerTask: Math.max(0, ...Object.values(perTask)),
        sched: ['__schedule', '__switch_to', '__switch_to_asm', '__cond_resched', 'schedule'].filter((x) => bySym[x]),
        traceEarly: te.length, traceEarlyOrphans: te.filter((u) => !u).length,
        switches: st.swStep.length, visibleSwitches: st.swStep.filter((t, i) => !(MKB.G.fold && st.swIrq[i])).length,
        markers: Number(document.getElementById('graph_canvas').dataset.switchMarkers),
        labels: Number(document.getElementById('graph_canvas').dataset.switchLabels),
        rows: MKB.G.layout.row.reduce((a, b) => Math.max(a, b), 0) + 1, maxDepth: (() => { let m = 0; for (let k = 0; k < n; k++) m = Math.max(m, st.fr.depth(k)); return m; })() }; }"""


# Covered pixel columns per timeline row: the whole run at once vs the
# same run as 8 windows at 8x zoom, folded back to the coarse columns.
COVER_JS = """() => { const st = MKB.G.stepper, s0 = MKB.runStart(), s1 = st.steps, span = s1 - s0;
    const saved = [MKB.G.x0, MKB.G.x1, MKB.G.userMoved, MKB.G.follow]; MKB.G.follow = false; MKB.G.userMoved = true;
    MKB.G.x0 = s0; MKB.G.x1 = s1; MKB.drawTimeline();
    const one = new Map(); let W = 0;
    for (const [r, c] of MKB.G.cover) { W = c.length; one.set(r, c.reduce((a, b) => a + b, 0)); }
    const coarse = new Map();
    for (let i = 0; i < 8; i++) {
        MKB.G.x0 = s0 + span * i / 8; MKB.G.x1 = s0 + span * (i + 1) / 8; MKB.drawTimeline();
        for (const [r, c] of MKB.G.cover) {
            if (!coarse.has(r)) coarse.set(r, new Uint8Array(W));
            const m = coarse.get(r);
            for (let col = 0; col < c.length - 1; col++) if (c[col]) m[Math.min(W - 1, Math.floor((i * (c.length - 1) + col) / 8))] = 1; } }
    [MKB.G.x0, MKB.G.x1, MKB.G.userMoved, MKB.G.follow] = saved; MKB.G.dirty = true; MKB.drawTimeline();
    const rows = [];
    for (const r of new Set([...one.keys(), ...coarse.keys()])) {
        const a = one.get(r) || 0, b = coarse.has(r) ? coarse.get(r).reduce((x, y) => x + y, 0) : 0;
        rows.push([r, a, b]); }
    rows.sort((x, y) => x[0] - y[0]);
    return { width: W, rows }; }"""


def coverage_problems(page, name):
    """No timeline row loses covered pixels when zoomed out."""
    cov = page.evaluate(COVER_JS)
    bad = [(r, a, b) for r, a, b in cov["rows"] if abs(a - b) > max(3, 0.03 * max(a, b))]
    return ([f"{name}: rows whose zoomed-out coverage differs from 8x zoom (row, 1x, 8x): {bad[:8]}"] if bad else []), cov


# Task names from task_struct (D6): the page's names against gdb's member
# offsets of struct task_struct applied to guest RAM by the test itself.
TASK_PAGE_JS = """() => { const st = MKB.stepper, info = MKB.taskInfo(st);
    return { cur: st.task, region: st.taskRegion.slice(), alive: info.map((x, t) => t === st.task || st.taskOf.get(st.taskRegion[t]) === t),
             tasks: info.map((x) => ({ name: x.name, how: x.how, guess: !!x.guess, comm: x.comm === undefined ? null : x.comm,
                                       pid: x.pid === undefined ? null : x.pid, va: x.va === undefined ? null : x.va })),
             off: MKB.varsOffset(), types: MKB.dbgTypes().state }; }"""


def tasks_late_types(browser, base, shots, name, r):
    """The variables (types.json) held back until the end of a counted run:
    no task switch could read current_task, yet once they arrive the tasks
    are named from their task_struct (the running one through
    current_task, the boot task as init_task) and task_names_check holds
    as on a normal run. While they are held a byte of init_task in the Page
    listing says they are being read, and it names init_task's member once
    they are in."""
    problems = []
    held = []
    # A browser without service workers (their fetches would not pass the page's routes).
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.add_init_script("delete Navigator.prototype.serviceWorker")
    page.route(re.compile(r"/dwarf/types\.json(\?|$)"), lambda route: held.append(route))
    page.goto(f"{base}/index.html?kernel={name}&speed=full#calls")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 300, 1):
        page.close()
        return [f"{name} with the variables held back: no end of the run"]
    T = json.loads((SITE / kernel_entries()[name]["dwarf"] / "types.json").read_text())
    it = next(v for v in T["vars"] if v[0] == "init_task")
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    pa = it[1] + 16
    hover_byte(page, pa)
    loading = [page.get_attribute("#var_info", "data-state"), page.inner_text("#var_info")]
    for x in held:
        x.continue_()
    page.route(re.compile(r"/dwarf/types\.json(\?|$)"), lambda route: route.continue_())
    wait_for(lambda: page.get_attribute("#var_info", "data-state") == "ok", 30)
    after = [page.get_attribute("#var_info", "data-state"), page.get_attribute("#var_info", "data-path")]
    r["late_types_hover"] = [loading, after, len(held)]
    if not held or loading[0] != "loading" or not loading[1].startswith(f"{pa:#x}: ") or after[0] != "ok" or not (after[1] or "").startswith("init_task."):
        problems.append(f"{name}: a byte of init_task while the variables are held back {loading}, then {after}")
    late = shots / "late-types"
    late.mkdir(exist_ok=True)
    rl = {}
    show_tab(page, "calls")
    problems += [p + " (variables held back)" for p in task_names_check(page, name, late, rl)]
    r["late_types_tasks"] = [rl.get("task_names"), rl.get("tasks_checked")]
    page.close()
    return problems


def gdb_task_layout(elf):
    """gdb's offsets of task_struct's comm, pid, stack, tasks (None if it has
    no such member) and the addresses of current_task, __per_cpu_offset
    (None on a UP kernel) and init_task; None without gdb."""
    keys = ["comm", "pid", "stack", "tasks", "current_task", "per_cpu_offset", "init_task"]
    exprs = ["(int)&((struct task_struct *)0)->" + m for m in keys[:4]] + \
        ["(unsigned)&current_task", "(unsigned)&__per_cpu_offset", "(unsigned)&init_task"]
    if not elf or not shutil.which("gdb"):
        return None
    args = ["gdb", "-nx", "-batch"]
    for k, e in zip(keys, exprs):
        # A failed expression prints only its tag on stdout (the error goes to stderr).
        args += ["-ex", f"echo @{k}=", "-ex", f"output {e}", "-ex", "echo \\n"]
    out = subprocess.run(args + [str(elf)], capture_output=True, text=True).stdout
    res = dict.fromkeys(keys)
    for m in re.finditer(r"@(\w+)=(-?\d+)?", out):
        res[m.group(1)] = int(m.group(2)) if m.group(2) else None
    return res


def guest_bytes(page, pa, n):
    return bytes(page.evaluate("([pa, n]) => Array.from(MKB.emulator.read_memory(pa, n))", [pa, n]))


def guest_task(page, lay, off, va):
    """comm, pid, stack region of the task_struct at va, read with gdb's offsets."""
    pa = va - off
    comm = guest_bytes(page, pa + lay["comm"], 16).split(b"\0")[0].decode("latin-1")
    pid = int.from_bytes(guest_bytes(page, pa + lay["pid"], 4), "little", signed=True)
    reg = int.from_bytes(guest_bytes(page, pa + lay["stack"], 4), "little") >> 13 if lay["stack"] is not None else -1
    return {"comm": comm, "pid": pid, "region": reg, "va": va}


def task_names_check(page, name, shots, r):
    """Each task the stepper saw is named "comm (pid N)" from its
    task_struct where the page can read it; the running one, every live
    one and the task list (when the kernel has one) read with gdb's
    offsets agree; the Debugger's task row and the call tree's task rows
    show the same names."""
    problems = []
    if not wait_for(lambda: page.evaluate("MKB.dbgTypes().state") == "ok", 60):
        return [f"{name}: the types did not load"]
    p = page.evaluate(TASK_PAGE_JS)
    tasks = p["tasks"]
    r["task_names"] = [t["name"] for t in tasks]
    named = [t for t in tasks if not t["guess"]]
    r["tasks_named"] = f"{len(named)}/{len(tasks)}"
    if len(named) < 2:
        problems.append(f"{name}: only {len(named)} of {len(tasks)} tasks named from task_struct: {tasks[:4]}")
    for t in tasks:
        if t["guess"] and "not read: " not in t["how"]:
            problems.append(f"{name}: a guessed name does not say why: {t}")
    lay = gdb_task_layout(debug_elf(name))
    if lay is None:
        if os.environ.get("CI"):
            problems.append(f"{name}: no gdb or -g ELF to check the task names against")
        return problems
    off = p["off"]
    cur_addr = lay["current_task"]
    if lay["per_cpu_offset"] is not None:
        cur_addr += int.from_bytes(guest_bytes(page, lay["per_cpu_offset"] - off, 4), "little")
    cur = guest_task(page, lay, off, int.from_bytes(guest_bytes(page, cur_addr - off, 4), "little"))
    r["current"] = f"{cur['comm']} (pid {cur['pid']})"
    truth = {}
    if lay["tasks"] is not None:
        head = lay["init_task"] + lay["tasks"]
        at = head
        t0 = guest_task(page, lay, off, lay["init_task"])
        truth[t0["region"]] = t0
        for _ in range(4096):
            at = int.from_bytes(guest_bytes(page, at - off, 4), "little")
            if at == head:
                break
            t = guest_task(page, lay, off, at - lay["tasks"])
            truth[t["region"]] = t
        r["task_list"] = len(truth)
    checked = 0
    for i, t in enumerate(tasks):
        if t["guess"] or not p["alive"][i]:
            continue
        want = cur if i == p["cur"] and (cur["region"] == p["region"][i] or p["region"][i] < 0) else \
            truth.get(p["region"][i]) or guest_task(page, lay, off, t["va"])
        if (t["comm"], t["pid"]) != (want["comm"], want["pid"]):
            problems.append(f"{name}: task {i} is {t['name']} on the page, {want['comm']} (pid {want['pid']}) in RAM by gdb's offsets ({t['how']})")
        else:
            checked += 1
    for reg, w in truth.items():
        i = next((i for i in range(len(tasks)) if p["alive"][i] and p["region"][i] == reg), None)
        if i is not None and tasks[i]["guess"]:
            problems.append(f"{name}: task {i} ({tasks[i]['name']}) is in the task list as {w['comm']} (pid {w['pid']}) but the page guessed")
    r["tasks_checked"] = checked
    if not checked:
        problems.append(f"{name}: no live named task to check")
    # The Debugger's task row is the running task.
    page.click("#tab_debugger")
    wait_for(lambda: page.get_attribute("#cur_task", "data-state") == "ok", 10)
    row = page.evaluate("(() => { const d = document.getElementById('cur_task'); return d ? { ...d.dataset, text: d.textContent } : null; })()")
    if not row or row.get("state") != "ok" or (row.get("comm"), int(row.get("pid", -99))) != (cur["comm"], cur["pid"]):
        problems.append(f"{name}: Debugger task row {row}, current_task by gdb's offsets {cur}")
    for scheme, width in (("light", 1100), ("dark", 1100), ("dark", 390)):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": width, "height": 1000})
        time.sleep(0.5)
        page.locator("#sym_info").screenshot(path=str(shots / f"tasks-debugger-{name}-{scheme}-{width}.png"))
        if width == 390 and page.evaluate("document.documentElement.scrollWidth > innerWidth"):
            problems.append(f"{name}: horizontal scroll at 390 px in the Debugger")
    page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1100, "height": 1000})
    # Call tree: one row per task under the root, named like the timeline.
    page.click("#tab_calls")
    page.click("#gview_tree")
    wait_for(lambda: page.locator("#graph_tree .trow[data-task]").count() > 0, 5)
    rows = page.evaluate("[...document.querySelectorAll('#graph_tree .trow[data-task]')].map((r) => [+r.dataset.task, r.dataset.name])")
    r["tree_tasks"] = len(rows)
    if len(rows) < 2 or any(n != f"task {t}: {tasks[t]['name']}" for t, n in rows):
        problems.append(f"{name}: call tree task rows {rows[:6]}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        page.screenshot(path=str(shots / f"tasks-tree-{name}-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    return problems


# The Calls tab's controls on a finished run (tasks_case, minified Linux):
# every fillText of the canvas is recorded, so the checks read what was
# drawn, not the page's own bookkeeping.
GRAPH_SPY_JS = """() => { if (window.__ft) return; window.__ft = [];
    const f = CanvasRenderingContext2D.prototype.fillText;
    CanvasRenderingContext2D.prototype.fillText = function (t, x, y, ...a) {
        if (this.canvas.id === 'graph_canvas') __ft.push([String(t), x, y]); return f.call(this, t, x, y, ...a); };
    const u = URL.createObjectURL; window.__blobs = [];
    URL.createObjectURL = (b) => { const h = u(b); __blobs.push(h); return h; }; }"""
# Redraw now; the texts drawn.
GRAPH_DRAW_JS = "() => { __ft = []; MKB.G.dirty = true; MKB.graphTick(); return __ft; }"


def graph_canvas_xy(page, x, y):
    """Viewport point of canvas point (x, y)."""
    b = page.locator("#graph_canvas").bounding_box()
    return b["x"] + x, b["y"] + y


def graph_controls(page, name, shots, r):
    """Clicks on a task switch's label and on merged sub-pixel calls, Fit,
    the wheel and a two-finger pinch (not in Top 30), a cancelled touch,
    drags in the icicle (sticky labels of the rows scrolled away) and
    transposed (time downwards) views, the depth cap's "+N" marks, "hide
    interrupts", "chains", Enter and arrow keys, the info's Page tab
    button, a call tree toggle and a search with more than 2,000 rows, two
    quick presses that open a function (and swallow the click after it), a
    bookmark on a Top 30 box, and the export's blob URL revoked after 10 s."""
    problems = []
    page.evaluate(GRAPH_SPY_JS)
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]}
    show_canvas(page)
    with page.expect_download():
        page.click("#graph_json")
    blob_at = time.monotonic()
    blob = page.evaluate("__blobs[__blobs.length - 1]")
    texts = page.evaluate(GRAPH_DRAW_JS)

    # A task switch's label: the info names the incoming task, as drawn.
    sw = page.evaluate("""() => { const st = MKB.G.stepper, h = MKB.G.switchHits[0]; if (!h) return null;
        return { i: h[0], lx: h[1], x: h[1] + h[3] / 2, y: h[2] + h[4] / 2, task: st.swTask[h[0]], at: st.swStep[h[0]] - st.sinceEntry }; }""")
    if not sw:
        problems.append(f"{name}: no task switch label drawn")
    else:
        label = next((t for t, x, y in texts if abs(x - sw["lx"] - 2) < 0.01 and abs(y - sw["y"]) < 0.01), None)
        page.mouse.click(*graph_canvas_xy(page, sw["x"], sw["y"]))
        time.sleep(0.3)
        info, swname = page.inner_text("#graph_info"), page.get_attribute("#graph_info", "data-switch")
        r["switch info"] = info[:120]
        if not label or swname != label or f"Task switch {sw['at']:,} instructions from the entry, to task {sw['task']}: {label}" not in info:
            problems.append(f"{name}: clicking the task switch label {label!r} ({sw}) shows {info[:160]!r} (data-switch {swname!r})")
        time.sleep(0.7)  # the next press starts afresh

    # Merged calls narrower than a pixel: a click zooms into their range.
    m = page.evaluate("""() => { const st = MKB.G.stepper, L = MKB.G.layout;
        for (const h of MKB.G.mergedHits) { const [x, y, w, hh, n, t0, t1] = h; const cx = x + w / 2, cy = y + hh / 2;
            if (n < 2 || MKB.timelineFrameAt(cx, cy) >= 0 || MKB.G.switchHits.some(([, a, b, c, d]) => cx >= a - 2 && cx <= a + c + 2 && cy >= b - 2 && cy <= b + d + 2)) continue;
            return { cx, cy, n, t0, t1, row: Math.round((y - MKB.G.areaTop + MKB.G.yOff) / 17) }; }
        return null; }""")
    if not m:
        problems.append(f"{name}: no merged column of 2 or more calls to click")
    else:
        page.mouse.click(*graph_canvas_xy(page, m["cx"], m["cy"]))
        time.sleep(0.4)
        x0, x1 = page.evaluate("[MKB.G.x0, MKB.G.x1]")
        pad = (m["t1"] - m["t0"]) * 0.05
        info = page.inner_text("#graph_info")
        boxes = page.evaluate("""([t0, t1, row]) => MKB.G.rects.filter(([k]) => MKB.G.layout.row[k] === row &&
            MKB.G.stepper.fr.start(k) >= t0 && MKB.G.stepper.fr.start(k) < t1).length""", [m["t0"], m["t1"], m["row"]])
        r["merged"] = [m["n"], m["row"], boxes, info[:80]]
        if abs(x0 - (m["t0"] - pad)) > 1e-6 * x1 or abs(x1 - (m["t1"] + pad)) > 1e-6 * x1 or \
                not info.startswith(f"{m['n']:,} calls in this range") or "zoomed in" not in info or boxes < 1:
            problems.append(f"{name}: clicking {m['n']} merged calls [{m['t0']}, {m['t1']}] gave the window [{x0}, {x1}] "
                            f"(want +-{pad:.1f}), {boxes} boxes in row {m['row']}, info {info[:120]!r}")
        # Fit: the whole run again.
        page.click("#graph_fit")
        time.sleep(0.3)
        got = page.evaluate("[MKB.G.x0, MKB.G.x1, MKB.G.stepper.sinceEntry, MKB.G.stepper.steps]")
        if got[0] != got[2] or got[1] != got[3]:
            problems.append(f"{name}: Fit shows [{got[0]}, {got[1]}], the run is [{got[2]}, {got[3]}]")

    # The wheel zooms about the pointer; it does nothing in Top 30.
    w = page.evaluate("document.getElementById('graph_canvas').clientWidth")
    px = round(w * 0.3)
    x0, x1 = page.evaluate("[MKB.G.x0, MKB.G.x1]")
    page.mouse.move(*graph_canvas_xy(page, px, 200))
    page.mouse.wheel(0, -500)
    time.sleep(0.4)
    a0, a1 = page.evaluate("[MKB.G.x0, MKB.G.x1]")
    span = (x1 - x0) / math.e
    t = x0 + px / w * (x1 - x0)
    r["wheel"] = [round(x1 - x0), round(a1 - a0)]
    if abs(a1 - a0 - span) > 1e-6 * span or abs(a0 - (t - px / w * span)) > 1e-6 * span:
        problems.append(f"{name}: a wheel step of -500 at x={px} zoomed [{x0}, {x1}] to [{a0}, {a1}], want span {span:.0f} about {t:.0f}")
    page.click("#gview_top")
    time.sleep(0.3)
    page.mouse.move(*graph_canvas_xy(page, px, 100))
    page.mouse.wheel(0, -500)
    time.sleep(0.2)
    if page.evaluate("[MKB.G.x0, MKB.G.x1]") != [a0, a1]:
        problems.append(f"{name}: the wheel over Top 30 changed the timeline window")
    page.click("#gview_timeline")
    page.click("#graph_fit")
    time.sleep(0.3)

    # Two fingers 100 px apart moved to 300 px: a third of the span.
    # In Top 30 a pinch does nothing to the timeline.
    cdp = page.context.new_cdp_session(page)
    cx, cy = graph_canvas_xy(page, w / 2, 200)
    tp = lambda i, d: {"x": cx + (d if i == 2 else -d), "y": cy, "id": i}

    def pinch():
        cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [tp(1, 50), tp(2, 50)]})
        for i in range(1, 6):
            cdp.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [tp(1, 50 + 20 * i), tp(2, 50 + 20 * i)]})
        cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
        time.sleep(0.3)
    x0, x1 = page.evaluate("[MKB.G.x0, MKB.G.x1]")
    for v in ("top", "timeline"):
        page.click("#gview_" + v)
        time.sleep(0.3)
        pinch()
    a0, a1 = page.evaluate("[MKB.G.x0, MKB.G.x1]")
    r["pinch"] = round((x1 - x0) / (a1 - a0), 3)
    if abs(r["pinch"] - 3) > 0.01:
        problems.append(f"{name}: a pinch from 100 to 300 px in Top 30, then in the timeline, zoomed by {r['pinch']}, want 3 (once)")
    # A cancelled touch leaves no pointer behind.
    cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [tp(1, 0)]})
    held = page.evaluate("MKB.G.pointers.size")
    cdp.send("Input.dispatchTouchEvent", {"type": "touchCancel", "touchPoints": []})
    time.sleep(0.2)
    after = page.evaluate("[MKB.G.pointers.size, MKB.G.press === null]")
    if held != 1 or after != [0, True]:
        problems.append(f"{name}: a touch held {held} pointers, after its cancel {after}")
    cdp.detach()
    page.click("#graph_fit")
    time.sleep(0.3)

    # Icicle drag upwards by 60 px: the rows scrolled away keep the names
    # of the calls over the middle of the window on top.
    cx, cy = graph_canvas_xy(page, w / 2, 300)
    page.mouse.move(cx, cy)
    page.mouse.down()
    page.mouse.move(cx, cy - 60, steps=4)
    page.mouse.up()
    time.sleep(0.3)
    texts = page.evaluate(GRAPH_DRAW_JS)
    want = page.evaluate("""() => { const st = MKB.G.stepper, L = MKB.G.layout, tm = (MKB.G.x0 + MKB.G.x1) / 2;
        for (let k = 0; k < st.fr.n; k++) if (L.isHead[k] && L.row[k] === 0 && st.fr.start(k) <= tm && MKB.fEnd(st, k) > tm)
            return st.nodeName(st.fr.node(k)); return null; }""")
    sticky = [t for t, x, y in texts if t.startswith("↑ ")]
    r["sticky"] = [page.evaluate("MKB.G.yOff"), want, sticky[:1]]
    if page.evaluate("MKB.G.yOff") != 60 or not want or not sticky or not sticky[0].startswith("↑ " + want):
        problems.append(f"{name}: a 60 px drag upwards: yOff {page.evaluate('MKB.G.yOff')}, sticky labels {sticky[:2]}, want the top row's {want!r} first")
    page.screenshot(path=str(shots / f"graph-sticky-{name}-1100.png"))
    page.click("#graph_fit")

    # Transposed, depth cap 3: "+N" past the cap; a drag moves the time
    # window by the dragged share of the height and scrolls the columns.
    page.click("#graph_transpose")
    page.evaluate("document.getElementById('graph_depth').value = 3; document.getElementById('graph_depth').dispatchEvent(new Event('input'))")
    time.sleep(0.3)
    texts = page.evaluate(GRAPH_DRAW_JS)
    deeper = [t for t, x, y in texts if re.fullmatch(r"\+\d+", t)]
    rows = page.evaluate("Math.max(...MKB.G.rects.map(([k]) => MKB.G.layout.row[k]))")
    r["transposed +N"] = [len(deeper), rows]
    if not deeper or rows > 3:
        problems.append(f"{name}: transposed with depth 3: {len(deeper)} '+N' marks, rows up to {rows}")
    page.evaluate("document.getElementById('graph_depth').value = 0; document.getElementById('graph_depth').dispatchEvent(new Event('input'))")
    x0, x1, xoff, ch = page.evaluate("[MKB.G.x0, MKB.G.x1, MKB.G.xOff, document.getElementById('graph_canvas').clientHeight]")
    cx, cy = graph_canvas_xy(page, w / 2, 200)
    page.mouse.move(cx, cy)
    page.mouse.down()
    page.mouse.move(cx - 50, cy + 80, steps=4)
    page.mouse.up()
    time.sleep(0.3)
    a0, a1, aoff = page.evaluate("[MKB.G.x0, MKB.G.x1, MKB.G.xOff]")
    dt = 80 / (ch - 26 - 24) * (x1 - x0)
    r["transposed drag"] = [round(x0 - a0), round(dt), aoff]
    if abs((x0 - a0) - dt) > 1e-6 * (x1 - x0) or abs(a1 - a0 - (x1 - x0)) > 1e-6 * (x1 - x0) or aoff != xoff + 50:
        problems.append(f"{name}: a transposed drag of (-50, +80) px moved [{x0}, {x1}] to [{a0}, {a1}] (want -{dt:.0f}), xOff {xoff} -> {aoff}")
    page.screenshot(path=str(shots / f"graph-transposed-drag-{name}-1100.png"))
    page.click("#graph_fit")
    page.click("#graph_transpose")
    time.sleep(0.3)

    # "hide interrupts" off: the interrupt frames are drawn; the selection goes.
    page.evaluate("MKB.G.selected = 0; MKB.renderGraphInfo()")
    n_on = page.evaluate("MKB.G.rects.filter(([k]) => MKB.G.stepper.fr.irq(k)).length")
    page.uncheck("#graph_fold")
    time.sleep(0.3)
    n_off, sel = page.evaluate("[MKB.G.rects.filter(([k]) => MKB.G.stepper.fr.irq(k)).length, MKB.G.selected]")
    page.check("#graph_fold")
    time.sleep(0.3)
    n_back = page.evaluate("MKB.G.rects.filter(([k]) => MKB.G.stepper.fr.irq(k)).length")
    r["irq boxes"] = [n_on, n_off, n_back]
    if n_on or not n_off or n_back or sel != -1:
        problems.append(f"{name}: interrupt boxes with hide interrupts on/off/on {r['irq boxes']}, selection after the toggle {sel}")

    # "chains": single-call chains drawn as one box "a › b" only when ticked.
    texts = page.evaluate(GRAPH_DRAW_JS)
    on = sum(" › " in t for t, x, y in texts)
    page.uncheck("#graph_chains")
    time.sleep(0.3)
    off = sum(" › " in t for t, x, y in page.evaluate(GRAPH_DRAW_JS))
    page.check("#graph_chains")
    r["chain labels"] = [on, off]
    if not on or off:
        problems.append(f"{name}: labels with ' › ' with chains on {on}, off {off}")

    # Arrows: with nothing selected, the widest box of the top row; Right
    # the next box of its row to the right; Up from the top row stays;
    # Ctrl+Left is not a move.
    page.evaluate("MKB.G.selected = -1; MKB.renderGraphInfo(); MKB.G.dirty = true; MKB.graphTick()")
    rects = page.evaluate("MKB.G.rects.map(([k, x, y, w, h]) => [k, x, w, MKB.G.layout.row[k]])")
    top = min(x[3] for x in rects)
    first = max((x for x in rects if x[3] == top), key=lambda x: x[2])
    right = min((x for x in rects if x[3] == top and x[1] > first[1]), key=lambda x: x[1] - first[1], default=None)
    page.focus("#graph_canvas")
    # Enter with nothing selected opens nothing.
    entries0 = nav_at(page)
    page.keyboard.press("Enter")
    time.sleep(0.5)
    if shown_tab(page) != "calls" or nav_at(page) != entries0:
        problems.append(f"{name}: Enter on the canvas with nothing selected opened {shown_tab(page)!r}, history {entries0} -> {nav_at(page)}")
    seq = []
    for key in ("ArrowRight", "ArrowRight", "ArrowUp", "Control+ArrowLeft"):
        page.keyboard.press(key)
        time.sleep(0.15)
        seq.append(page.evaluate("MKB.G.selected"))
    want = [first[0], right[0] if right else first[0]]
    want += [want[1], want[1]]
    r["arrows"] = [seq, want]
    if seq != want:
        problems.append(f"{name}: arrows Right, Right, Up, Ctrl+Left selected {seq}, want {want}")

    # The info's "Page tab" button: the listing at the function's entry.
    xy = graph_box_xy(page, "start_kernel")
    page.mouse.click(xy["x"], xy["y"])
    time.sleep(0.7)
    page.locator("#graph_info").get_by_role("button", name="Page tab", exact=True).first.click()
    ok = wait_for(lambda: shown_tab(page) == "page", 5)
    nav_settle(page)
    got = page.evaluate("MKB.selectedPage")
    r["page button"] = [ok, got]
    if not ok or got != syms["start_kernel"]["paddr"] // 4096:
        problems.append(f"{name}: the info's Page tab button showed tab {shown_tab(page)!r}, page {got}, want {syms['start_kernel']['paddr'] // 4096}")
    page.go_back()
    wait_for(lambda: shown_tab(page) == "calls", 5)
    time.sleep(0.6)

    # Two presses 15 px apart (no dblclick): the second opens the function,
    # and a click right after the open does not reach the Page tab.
    xy = graph_box_xy(page, "start_kernel")
    # Clicks as the document sees them first (ms since the open) and those
    # that reach it: swallowed exactly when within 500 ms of the open (on a
    # slow host the click after the open may come later than that).
    page.evaluate("""() => { window.__cap = []; window.__bub = [];
        const dt = () => (MKB.G.openedAt ? Math.round(MKB.realNow() - MKB.G.openedAt) : null);
        // The bubbling listener records the time its capture saw (CI run
        // 43724195461: 823 ms at capture, 824 ms after the handlers ran).
        document.addEventListener('click', (e) => { e.mkbDt = dt(); __cap.push(e.mkbDt); }, true);
        document.addEventListener('click', (e) => __bub.push(e.mkbDt)); }""")
    entries0 = nav_at(page)
    page.mouse.click(xy["x"], xy["y"])
    page.mouse.click(xy["x"] + 15, xy["y"])
    ok = wait_for(lambda: page.evaluate("MKB.tab") == "page", 5, 0.02)
    page.mouse.click(5, 300)
    time.sleep(0.7)
    page.mouse.click(5, 300)
    cap, bub = page.evaluate("[__cap, __bub]")
    r["two presses"] = [ok, cap, bub, entries0, nav_at(page)]
    want = [t for t in cap if t is None or t >= 500]
    if not ok or page.evaluate("MKB.selectedPage") != syms["start_kernel"]["paddr"] // 4096 or len(cap) != 4 or \
            cap[:2] != [None, None] or cap[2] is None or bub != want:
        problems.append(f"{name}: two presses 15 px apart on start_kernel: Page tab {ok}; clicks (ms after the open) {cap}, "
                        f"reaching the page {bub}, want {want} (within 500 ms of the open swallowed)")
    page.go_back()
    wait_for(lambda: shown_tab(page) == "calls", 5)
    time.sleep(0.6)

    # Call tree: a row's toggle opens and closes its callees.
    page.click("#gview_tree")
    time.sleep(0.4)
    rows = "#graph_tree .trow:not(.thead)"
    rows0 = page.locator(rows).count()
    i = page.evaluate("""(sel) => [...document.querySelectorAll(sel)].findIndex((r) => {
        const t = r.querySelector('.ttog'); return !t.disabled && t.textContent === '\u25b8'; })""", rows)
    tname = page.locator(rows).nth(i).get_attribute("data-name")
    page.locator(rows).nth(i).locator(".ttog").click()
    rows1 = page.locator(rows).count()
    page.locator(rows).nth(i).locator(".ttog").click()
    rows2 = page.locator(rows).count()
    r["tree toggle"] = [tname, rows0, rows1, rows2]
    if not rows1 > rows0 or rows2 != rows0:
        problems.append(f"{name}: the call tree toggle of {tname}: rows {rows0} -> {rows1} -> {rows2}")
    # A search matching more than 2,000 call paths shows 2,001 rows and says so.
    page.fill("#graph_search", "_")
    time.sleep(0.5)
    many = [page.locator(rows).count(), page.evaluate("document.getElementById('graph_tree').lastChild.textContent")]
    page.fill("#graph_search", "")
    r["tree search _"] = many
    if many[0] != 2001 or many[1] != "(more rows not shown: search to narrow)":
        problems.append(f"{name}: the call tree searched for '_': {many}, want 2001 rows and the note")

    # Top 30: a right click on a box bookmarks its function; its box gets a ★.
    page.click("#gview_top")
    time.sleep(0.5)
    xy = graph_box_xy(page, "start_kernel", "top")
    page.mouse.click(xy["x"], xy["y"], button="right")
    b = bm_dialog_save(page, problems, f"{name} Top 30", "fn", want_default="start_kernel")
    time.sleep(0.5)
    texts = page.evaluate(GRAPH_DRAW_JS)
    marks = page.get_attribute("#graph_canvas", "data-bmarks")
    r["top bookmark"] = [b and b["t"], marks]
    if not b or b["t"] != {"pa": syms["start_kernel"]["paddr"], "name": "start_kernel"} or marks != "1" or \
            not any(t.startswith("★ start_kernel") for t, x, y in texts):
        problems.append(f"{name}: a Top 30 right click bookmarked {b and b['t']}, {marks} boxes marked")
    page.screenshot(path=str(shots / f"graph-top-bookmark-{name}-1100.png"))
    page.click("#gview_timeline")

    # The JSON export's blob URL is revoked 10 s after the download.
    time.sleep(max(0, 10.5 - (time.monotonic() - blob_at)))
    gone = page.evaluate("(h) => fetch(h).then(() => false, () => true)", blob)
    if not gone:
        problems.append(f"{name}: the JSON export's blob URL still loads 10 s later")
    return problems


def tasks_case(browser, base, shots, name):
    """Task switches on a full counted run: a resumed task continues its
    suspended calls (no scheduler code as a root, no orphaned calls), one
    root per task, and the roots never overlap; tasks are named from their
    task_struct (task_names_check)."""
    problems = []
    # Under coverage (site/jscov.py) a Debian run never ended (its stepping
    # with V8's block counters is about 20 times slower: no end in 1,800 s):
    # the page's coverage starts at the end of the run, so the checks below
    # (task naming, vartasks.js) are measured on every run. The page then
    # runs on the Machine tab until the end: code the Calls tab would run
    # (and compile) while the machine runs is compiled after the coverage
    # starts, with its block counters. The minified Linux's run is short
    # enough to be measured whole.
    held = name != "minified-linux"
    tab = "" if held and jscov.COLLECTORS else "#calls"
    if held:
        jscov.hold()
    try:
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto(f"{base}/index.html?kernel={name}&speed=full{tab}")
        ended = wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 900, 2)
    finally:
        if held:
            jscov.release()
    if not ended:
        return ["no end of the run"], {}
    if tab != "#calls":
        show_tab(page, "calls")
    time.sleep(2)
    show_canvas(page)
    time.sleep(1)
    r = page.evaluate(TASKS_JS)
    page.screenshot(path=str(shots / f"graph-tasks-{name}.png"))
    cp, cov = coverage_problems(page, name)
    problems += cp
    r["coverage_rows"] = len(cov["rows"])
    r["coverage_1x_8x"] = [row[1:] for row in cov["rows"][:6]]
    if name == "minified-linux":
        problems += graph_controls(page, name, shots, r)
    problems += task_names_check(page, name, shots, r)
    page.close()
    if name == "minified-linux":
        problems += tasks_late_types(browser, base, shots, name, r)
    if r["tasks"] < 2:
        problems.append(f"fewer than 2 tasks: {r}")
    # One set of rows for all tasks (no per-task bands).
    if r["rows"] > r["maxDepth"] + 1:
        problems.append(f"{r['rows']} timeline rows for call depth {r['maxDepth']}")
    if r["markers"] != r["visibleSwitches"] or r["switches"] < r["tasks"] - 1 or r["labels"] < 1:
        problems.append(f"switch markers {r['markers']} / labels {r['labels']} for {r['visibleSwitches']} visible of {r['switches']} switches")
    if r["sched"]:
        problems.append(f"scheduler code as a root: {r['sched']}")
    if r["overlap"]:
        problems.append(f"roots overlap by {r['overlap']} instructions")
    if r["maxRootsPerTask"] > 1:
        problems.append(f"a task has {r['maxRootsPerTask']} roots")
    if r["traceEarlyOrphans"]:
        problems.append(f"{r['traceEarlyOrphans']} of {r['traceEarly']} __trace_early_add_event_dirs calls not under event_trace_init")
    return problems, r


def suite_tasks(ctx):
    failed = False
    for name in debian_names(ctx):
        failed |= report("tasks_case", *tasks_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed


def suite_tasks_minified(ctx):
    return report("tasks_case", *tasks_case(ctx.browser, ctx.base, ctx.shots, "minified-linux"), kernel="minified-linux")
