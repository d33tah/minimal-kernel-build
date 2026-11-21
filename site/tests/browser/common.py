"""Helpers the browser suites share (test_browser.py)."""

import functools
import http.server
import json
import os
import pathlib
import re
import subprocess
import socketserver
import tempfile
import threading
import time
import traceback

from playwright.sync_api import Error as PlaywrightError


SITE = pathlib.Path(__file__).resolve().parent.parent.parent
HELLO = "Hello, World!"
ALIVE = "Still alive"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingTCPServer):
    """Concurrent like a real host. One thread with the default listen
    backlog of 5 dropped connections when the service worker's precache
    opened many at once; Linux retries a dropped SYN after 1, 3, 7, 15 s,
    so the page's own v86.wasm fetch waited ~25 s and stale_cache timed
    out in CI run 42689108273."""
    daemon_threads = True
    request_queue_size = 128


def serve(directory=SITE, handler_class=QuietHandler):
    handler = functools.partial(handler_class, directory=str(directory))
    httpd = Server(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def kernel_entries():
    return {k["name"]: k for k in json.loads((SITE / "kernels.json").read_text())["kernels"]}


def gate_on_screen(page):
    text = page.inner_text("#screen_container")
    h = text.find(HELLO)
    return h >= 0 and text.find(ALIVE, h + len(HELLO)) >= 0


def wait_for(cond, timeout, step=0.25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if cond():
            return True
        time.sleep(step)
    return cond()


def page_center(page, p):
    """Viewport-relative point of page p's square in the grid canvas."""
    show_tab(page, "memory")
    cols = int(page.get_attribute("#pages", "data-cols"))
    cell = int(page.get_attribute("#pages", "data-cell"))
    width = int(page.eval_on_selector("#pages", "e => e.width"))
    page.locator("#pages").scroll_into_view_if_needed()
    for _ in range(2):
        box = page.locator("#pages").bounding_box()
        scale = box["width"] / width
        x = box["x"] + ((p % cols) * cell + cell / 2) * scale
        y = box["y"] + ((p // cols) * cell + cell / 2) * scale
        # The sticky top bar covers the top of the viewport: keep the cell below it.
        top = page.evaluate("document.querySelector('.top').getBoundingClientRect().bottom")
        if top + 10 < y < page.viewport_size["height"] - 10:
            break
        page.evaluate(f"window.scrollBy(0, {y - top - 80})")
    return x, y


def show_tab(page, name):
    if page.get_attribute(f"#tab_{name}", "aria-selected") != "true":
        page.click(f"#tab_{name}")


def shown_tab(page):
    return page.get_attribute(".tabbar [role=tab][aria-selected=true]", "data-tab")


def dbg_shown_once(page):
    """The Debugger tab's elements exist only once it has been shown (the
    panel is mounted the first time it is shown and stays): if it has not
    been shown since the page loaded, show it as a user would and go back
    to the tab shown before."""
    if page.locator("#dbg_status").count():
        return
    back = shown_tab(page)
    # (Right after a load the tab bar may not answer yet: click again.)
    end = time.monotonic() + 30
    while not page.locator("#dbg_status").count() and time.monotonic() < end:
        show_tab(page, "debugger")
        try:
            page.wait_for_selector("#dbg_status", state="attached", timeout=2000)
        except PlaywrightError:
            pass
    if back and back != "debugger":
        show_tab(page, back)


def machine_do(page, method, sel, *args, **kw):
    """Use a run control (U5: in the Machine tab) from any tab as a user
    would: open the Machine tab, use it, go back to the tab shown before."""
    back = shown_tab(page)
    show_tab(page, "machine")
    try:
        return getattr(page, method)(sel, *args, **kw)
    finally:
        if back and back != "machine":
            show_tab(page, back)


def open_page(page, p):
    """Click page p in the grid; the Page tab must open with its listing."""
    x, y = page_center(page, p)
    page.mouse.move(x, y)
    # The sticky top bar can change height between measuring and clicking:
    # re-measure until the hover line names the page.
    for _ in range(5):
        if f"page {p * 4096:#x}," in page.inner_text("#hover_info"):
            break
        time.sleep(0.2)
        x, y = page_center(page, p)
        page.mouse.move(x, y)
    page.mouse.click(x, y)
    return wait_for(lambda: page.get_attribute("#listing", "data-page") == str(p)
                    and page.get_attribute("#tab_page", "aria-selected") == "true", 5)


def no_hscroll(page, sel):
    return page.eval_on_selector(sel, "e => e.scrollWidth <= e.clientWidth + 1")


def counters(page):
    """Rows of the boot counter table: {milestone: {steps, entry, unique, kernel}}."""
    out = {}
    for row in page.locator("#counters tbody tr").all():
        d = {k: row.get_attribute("data-" + k) for k in ("steps", "entry", "unique", "kernel")}
        out[row.get_attribute("data-milestone")] = {k: int(v) if v not in (None, "") else None for k, v in d.items()}
    return out


def show_canvas(page):
    """Scroll so the top of the graph canvas sits just below the sticky top bar."""
    page.evaluate("""() => { const top = document.querySelector('.top').getBoundingClientRect().bottom;
        const c = document.getElementById('graph_status').getBoundingClientRect().top;
        window.scrollBy(0, c - top - 4); }""")
    time.sleep(0.2)


def import_inputs():
    """System.map and vmlinux of the minified Linux: from the workflow's
    kernels artifact (kernels/import-test/), else made from ../minified."""
    d = SITE / "kernels" / "import-test"
    if (d / "System.map").exists() and (d / "vmlinux").exists():
        return d / "System.map", d / "vmlinux"
    elf = SITE.parent / "minified" / "vmlinux"
    if not elf.exists():
        return None
    tmp = pathlib.Path(tempfile.mkdtemp())
    (tmp / "System.map").write_text(subprocess.run(["llvm-nm", "-n", str(elf)], capture_output=True, text=True,
                                                   check=True).stdout)
    return tmp / "System.map", elf


PHONE = dict(viewport={"width": 390, "height": 844}, device_scale_factor=3, is_mobile=True, has_touch=True)


def debug_elf(name):
    """The -g ELF of a kernel for llvm-symbolizer: $MKB_DEBUG_ELF_DIR/<name>.elf
    (the workflow puts the minified Linux, hello and Debian's -dbg vmlinux there)."""
    d = os.environ.get("MKB_DEBUG_ELF_DIR")
    p = pathlib.Path(d) / f"{name}.elf" if d else None
    return p if p and p.exists() else None


def frames_agree(got, want):
    """Frames from the page ({func, file, line}, innermost first) against
    llvm-symbolizer's (func, file, line); its paths may carry the build
    directory in front of the page's tree-relative ones."""
    g = [(f["func"], f["file"] if f["line"] else "", f["line"]) for f in got]
    w = list(want)
    if len(w) == 1 and len(g) <= 1 and (not g or g[0][0] is None):
        # No DWARF function (assembly): the page names the ELF symbol instead.
        w = [(None, w[0][1], w[0][2])]
        g = g or [(None, "", 0)]
    return len(g) == len(w) and all(gf == wf and gl == wl and (wp == gp or wp.endswith("/" + gp))
                                    for (gf, gp, gl), (wf, wp, wl) in zip(g, w))


def symbolize_frames(elf, vaddrs):
    from tests.dwarf.common import symbolize
    return symbolize(str(elf), vaddrs, [])


def source_rows(page):
    """EIP, state and frames of the Debugger's source rows, once loaded."""
    dbg_shown_once(page)
    def get():
        return page.evaluate("""() => { const e = document.getElementById('cur_src');
            return e ? [e.dataset.state, e.dataset.frames] : [null, '[]']; }""")
    wait_for(lambda: get()[0] not in (None, "loading"), 30)
    state, frames = get()
    return int(page.get_attribute("#regs", "data-phys") or -1), state, json.loads(frames or "[]")


# Instructions stepped between the samples of dwarf_check: uneven, so the
# samples land in different functions and inlined code.
DWARF_STEPS = (0, 37, 101, 263, 541, 1009, 2003, 3001, 4007, 5003, 7001, 9001)


def source_texts(page):
    """The C text of each source row (frame) of the Debugger, "" where none, once the files loaded."""
    dbg_shown_once(page)
    get = lambda: page.evaluate("""() => [...document.querySelectorAll('#cur_src .frame')].map(f => {
        const t = f.querySelector('.srctext'); return t ? t.textContent.trim() : ''; })""")
    wait_for(lambda: "reading " not in (page.inner_text("#cur_src") if page.locator("#cur_src").count() else "") and
             not any("reading " in (b or "") for b in page.eval_on_selector_all("#cur_src button", "els => els.map(e => e.title)")), 20)
    return get()


def text_check(page, name, samples, texts, entry_fn, problems, r):
    """The C text shown with the source rows is the kernel's: the entry line
    of entry_fn or one of the ten before it (the first row can be the "{" or
    the first statement after a comment) names it, and the call line of an inlined function mostly names that
    function (macros and multi-line calls do not; judged from 3 call lines on: in clang 22.1.0's -O0
    minified Linux 260 of 459 inlined call sites name the callee, so one or two can all be macros)."""
    rows = sum(1 for t in texts for x in t if x)
    calls = [(t[k], fr[k - 1]["func"]) for (_, st, fr), t in zip(samples, texts) if st == "ok"
             for k in range(1, min(len(fr), len(t))) if fr[k]["line"]]
    named = sum(1 for text, fn in calls if fn and fn in text)
    r["text_rows"] = rows
    r["call_lines_naming_callee"] = f"{named}/{len(calls)}"
    f0 = samples[0][2][0] if samples and samples[0][2] else None
    r["entry_text"] = page.evaluate("(f) => [...Array(11).keys()].map((k) => MKB.srcText(f.file, f.line - 10 + k))", f0) if f0 else None
    if not r["entry_text"] or not texts[0] or texts[0][0] != (r["entry_text"][-1] or "").strip() or \
            not any(entry_fn + "(" in (t or "") for t in r["entry_text"]):
        problems.append(f"{name}: the C line at the entry of {entry_fn} is {r['entry_text']!r}")
    if len(calls) >= 3 and named * 2 < len(calls):
        problems.append(f"{name}: call lines name the inlined function in only {named} of {len(calls)}")


def dwarf_check(page, name, offset, problems, r, steps=DWARF_STEPS, entry_fn=None):
    """At EIP and after each Step: the Debugger's source rows (file:line,
    inlined-at chain) equal llvm-symbolizer --inlines on the kernel's -g ELF;
    the call stack names frames with file:line in their tooltips. With
    entry_fn (the C source is there): text_check on the rows' C text."""
    dbg_shown_once(page)
    samples, texts = [], []
    for n in steps:
        if n:
            before = page.evaluate("MKB.stepper.steps")
            page.fill("#dbg_n", str(n))
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("MKB.stepper.steps") - before >= n and
                     page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        samples.append(source_rows(page))
        if entry_fn:
            texts.append(source_texts(page))
    if entry_fn:
        text_check(page, name, samples, texts, entry_fn, problems, r)
    ok = [x for x in samples if x[1] == "ok"]
    r["dwarf_samples"] = len(samples)
    r["dwarf_ok"] = len(ok)
    r["dwarf_inlined"] = sum(1 for x in ok if len(x[2]) > 1)
    r["dwarf_first"] = [f"{f['func']} {f['file']}:{f['line']}" for f in samples[0][2]]
    if len(ok) < len(samples) // 2:
        problems.append(f"{name}: source rows answered at only {len(ok)} of {len(samples)} EIPs: "
                        f"{[(hex(p), s) for p, s, _ in samples]}")
    elf = debug_elf(name)
    if elf:
        want = symbolize_frames(elf, [p + offset for p, _, _ in samples])
        bad = [(hex(p), fr, w) for (p, st, fr), w in zip(samples, want) if st == "ok" and not frames_agree(fr, w)]
        # "none" must mean llvm-symbolizer knows no line either.
        bad += [(hex(p), st, w) for (p, st, fr), w in zip(samples, want)
                if st == "none" and any(x[2] for x in w)]
        r["dwarf_vs_llvm"] = f"{len(samples) - len(bad)}/{len(samples)} agree"
        if bad:
            problems.append(f"{name}: source rows differ from llvm-symbolizer --inlines: {bad[:3]}")
    elif os.environ.get("CI"):
        problems.append(f"{name}: no -g ELF in $MKB_DEBUG_ELF_DIR to check the source rows against")
    cs = int(page.get_attribute("#call_stack", "data-count") or 0)
    r["call_stack"] = cs
    first = page.locator("#call_stack button").first
    r["call_stack_title"] = first.get_attribute("title") if cs else None
    if cs < 1 or not re.search(r"\.[chS]:\d+|no line information", r["call_stack_title"] or ""):
        problems.append(f"{name}: call stack without file:line tooltips: {cs} frames, {r['call_stack_title']!r}")
    # Calls tab: a frame's hover text has where it was called from.
    t = page.evaluate("""() => { const st = MKB.G.stepper; const k = st.stack.length ? st.stack[st.stack.length - 1] : -1;
        return k >= 0 ? MKB.frameTitle(st, k) : null; }""")
    r["frame_title"] = t
    if not t or "called from 0x" not in t or not re.search(r"\.[chS]:\d+", t):
        problems.append(f"{name}: flame frame tooltip without DWARF locations: {t!r}")
    return samples


def hover_byte(page, pa, click=False):
    """Point at (or tap) byte pa of the Page listing's hex rows; #var_info's path."""
    # The listing may be redrawn (and scrolled) between finding the row and
    # the pointer event: try again until #var_info names this byte.
    for _ in range(4):
        path = hover_once(page, pa, click)
        if page.get_attribute("#var_info", "data-addr") == str(pa):
            return path
    return None


def hover_once(page, pa, click):
    xy = page.evaluate("""(pa) => { for (const l of document.querySelectorAll('#listing .hex')) {
        if (pa < +l.dataset.addr || pa >= +l.dataset.end) continue;
        l.scrollIntoView({ block: 'center' });
        const r = l.getBoundingClientRect(), cw = r.width / l.textContent.length, i = pa - +l.dataset.row, per = +l.dataset.per;
        return [r.left + (9 + i * 3 + (per === 16 && i >= 8 ? 1 : 0) + 0.5) * cw, r.top + r.height / 2]; }
        return null; }""", pa)
    if not xy:
        return None
    (page.mouse.click if click else page.mouse.move)(*xy)
    wait_for(lambda: page.get_attribute("#var_info", "data-addr") == str(pa) and
             page.get_attribute("#var_info", "data-state") != "loading", 3)
    return page.get_attribute("#var_info", "data-path")

# The stack pages of a pause (8 KiB stacks around ESP and every frame's CFA)
# read through the page tables, with the registers: what gdb needs to unwind.
UNWIND_DUMP_JS = """() => {
    const cpu = MKB.stepper.cpu, r = MKB.stepper.regs(), fr = JSON.parse(document.getElementById('call_stack').dataset.frames);
    const want = new Set();
    for (const a of [r.esp, ...fr.map((f) => f.cfa).filter((x) => x !== undefined && x !== null)])
        for (let p = ((a >>> 13) << 13) - 8192; p < ((a >>> 13) << 13) + 16384; p += 4096) want.add(p >>> 0);
    const pages = [];
    for (const va of [...want].sort((a, b) => a - b)) {
        const w = MKB.pageWalk(cpu, va);
        if (w.phys === null || w.phys + 4096 > cpu.mem8.length) continue;
        let s = '';
        for (const b of cpu.mem8.subarray(w.phys, w.phys + 4096)) s += String.fromCharCode(b);
        pages.push([va, btoa(s)]);
    }
    return { regs: r, pages, frames: fr, unwind: document.getElementById('call_stack').dataset.unwind,
             why: document.getElementById('call_stack').dataset.why };
}"""


# The Debugger's "Parameters and locals" with every frame open: per frame
# its inlined calls and function, each variable's state ("ok", "partial",
# "out"), the unsigned value of a scalar (raw) and the text shown.
LOCALS_DUMP_JS = """() => [...document.querySelectorAll('#locals details.lframe')].map((d) => ({
    n: +d.dataset.n, pc: +d.dataset.pc, state: d.dataset.state || '',
    scopes: [...d.querySelectorAll('.lscope')].map((s) => ({ func: s.dataset.func, inlined: !!s.dataset.inlined,
        vars: [...s.querySelectorAll(':scope > .lvar')].map((v) => ({ name: v.dataset.name, kind: v.dataset.kind,
            state: v.dataset.state, raw: v.dataset.raw === undefined ? null : v.dataset.raw, value: v.dataset.value,
            loc: (v.querySelector('.lloc') || {}).textContent || '' })) })) }))"""


def locals_open_all(page, timeout=30):
    """Open every frame of the Parameters and locals section and wait until
    each one has its variables (or says why not)."""
    dbg_shown_once(page)
    page.evaluate("document.querySelectorAll('#locals details.lframe').forEach((d) => { d.open = true; })")
    wait_for(lambda: page.evaluate("[...document.querySelectorAll('#locals details.lframe')].every((d) => "
                                   "d.dataset.filled || (d.dataset.state && d.dataset.state !== 'loading'))"), timeout)
    return page.evaluate(LOCALS_DUMP_JS)


def run_to_wait(page, bps, timeout=240):
    dbg_shown_once(page)
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                  int(page.get_attribute("#regs", "data-phys") or -1) in bps, timeout)
    return ok, int(page.get_attribute("#regs", "data-phys") or -1), page.inner_text("#dbg_status")


def cov_rows(page, kind=None):
    """The Coverage tree's rows: [{kind, path, lines, linesRun, ins, insRun}] (run None when unknown)."""
    rows = page.eval_on_selector_all("#cov_tree .covrow[data-kind]", "es => es.map(e => ({...e.dataset}))")
    out = []
    for x in rows:
        if kind and x["kind"] != kind:
            continue
        out.append({"kind": x["kind"], "path": x["path"], "lines": int(x["lines"]), "ins": int(x["ins"]),
                    "linesRun": int(x["linesRun"]) if x["linesRun"] != "" else None,
                    "insRun": int(x["insRun"]) if x["insRun"] != "" else None})
    return out


def cov_open_all(page):
    """Open every directory and file of the Coverage tree."""
    page.evaluate("""() => {
        const T = MKB.covTree(MKB.DBG.files);
        const walk = (d) => { MKB.COV.open.add(d.path); d.dirs.forEach(walk); };
        walk(T.root);
        MKB.DBG.files.files.forEach(([p]) => MKB.COV.open.add(p));
        MKB.covRender(); }""")


def report(label, problems, r=None, **extra):
    """One JSON line per check (flushed: the CI log shows when each ran); True if it failed."""
    print(json.dumps({label: problems or "ok", **extra, **{k: str(v) for k, v in (r or {}).items()}}), flush=True)
    return bool(problems)


def debian_names(ctx):
    return [n for n in ctx.entries if n.startswith("debian") and "initrd" not in ctx.entries[n]]


# ---- marks in the asm listings: the page's popover (T1) --------------------

# The popover's state: shown, its text and where it is.
TIP_STATE_JS = """() => { const p = document.getElementById('tip_pop'), r = p.getBoundingClientRect();
  return { shown: !p.hidden, text: p.textContent, l: r.left, r: r.right, t: r.top, b: r.bottom, w: innerWidth, h: innerHeight }; }"""


def tip_wait(page, shown, timeout=3):
    wait_for(lambda: page.evaluate(TIP_STATE_JS)["shown"] == shown, timeout, 0.05)
    return page.evaluate(TIP_STATE_JS)


def tip_tap_away(page):
    """Tap the page's left margin (nothing there acts), off the popover and
    away from any control: a phone's touch adjustment moves a tap a few px
    off a control onto it (since H1 the header is shorter and a "?" of the
    Page tab sat next to the middle of the left margin)."""
    y = page.evaluate("""() => { const vh = innerHeight, pop = document.getElementById('tip_pop');
        const act = 'button, a, input, select, textarea, label, summary, [data-tip], .tip, [tabindex], #tip_pop, #runbadge, header';
        const near = (y) => { for (let dx = 0; dx <= 48; dx += 6) for (let dy = -24; dy <= 24; dy += 6) {
            const e = document.elementFromPoint(3 + dx, y + dy); if (!e || e.closest(act)) return true; } return false; };
        for (let k = 0; k < vh / 2; k += 6) for (const y of [vh / 2 + k, vh / 2 - k])
            if (y > 30 && y < vh - 30 && !near(y)) return Math.round(y);
        return Math.round(vh / 2); }""")
    page.touchscreen.tap(3, y)


def tip_fits(st, where, problems):
    if st["l"] < 0 or st["r"] > st["w"] + 0.5 or st["t"] < 0 or st["b"] > st["h"] + 0.5:
        problems.append(f"{where}: the popover is off screen: {st}")


def nav_at(page):
    """(the entry shown, entries); (0, 0) on a page without the history."""
    return tuple(page.evaluate("""() => { const e = document.getElementById('nav_hist');
        return e ? [Number(e.dataset.at || 0), Number(e.dataset.count || 0)] : [0, 0]; }"""))


def nav_settle(page):
    """A jump's listing is drawn again on the next frame (its width): wait
    for that, a little longer than NAV_SCROLL_MS."""
    time.sleep(1.3)


def nav_menu(page, how="click"):
    """Open the menu toggle's menu (H1: Back and Forward are in it)."""
    t = page.locator("#runbadge_toggle")
    if t.count() and t.get_attribute("aria-expanded") != "true":
        t.tap() if how == "tap" else t.click()
        wait_for(lambda: t.get_attribute("aria-expanded") == "true", 3)


def nav_press(page, sel, how="click"):
    """Back or Forward as a user does: open the menu, click or tap it."""
    nav_menu(page, how)
    page.tap(sel) if how == "tap" else page.click(sel)


def graph_box_xy(page, fn, view="timeline"):
    """Viewport point inside the widest drawn box of function fn (timeline
    frame or Top 30 node), the canvas scrolled below the sticky top bar."""
    js = """([fn, view]) => { const st = MKB.G.stepper, c = document.getElementById('graph_canvas').getBoundingClientRect();
        let best = null;
        if (view === 'timeline') {
            for (const [k, x, y, w, h] of MKB.G.rects)
                if (st.nodeName(st.fr.node(k)) === fn && (!best || w > best[3])) best = [k, x, y, w, h];
        } else
            for (const [n, x, y, w, h] of MKB.G.boxes) if (st.nodeName(n) === fn) best = [n, x, y, w, h];
        if (!best) return null;
        const [k, x, y, w, h] = best;
        const cx = Math.max(x, 0) + Math.min(w, c.width - Math.max(x, 0)) / 2;
        return { k, x: c.left + cx, y: c.top + y + h / 2, top: document.querySelector('.top').getBoundingClientRect().bottom, vh: innerHeight }; }"""
    for _ in range(3):
        b = page.evaluate(js, [fn, view])
        if not b:
            return None
        if b["top"] + 8 < b["y"] < b["vh"] - 60:
            return b
        page.evaluate(f"window.scrollBy(0, {b['y'] - b['top'] - 120})")
        time.sleep(0.3)
    return b

SNAP_STATE_JS = """() => { const rows = [...document.querySelectorAll('#snapshots tbody tr[data-id]')];
  const shown = (e) => !!e && !e.hidden && e.getClientRects().length > 0;
  return { rows: rows.map((tr) => ({ id: tr.dataset.id, name: (tr.querySelector('.snapname') || {}).textContent || '',
      kernel: tr.dataset.kernel || '', where: tr.dataset.where || '', stored: Number(tr.dataset.stored || 0),
      raw: Number(tr.dataset.raw || 0), restore: !!tr.querySelector('button.restore'), sw: !!tr.querySelector('button.snapswitch'),
      text: tr.innerText.replace(/\\s+/g, ' ') })),
    store: (document.getElementById('snap_store') || {}).textContent || '', status: document.getElementById('snap_status').textContent,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, storeShown: shown(document.getElementById('snap_store')) }; }"""


def snap_rows(page):
    return page.evaluate(SNAP_STATE_JS)


def snap_wait_rows(page, n, timeout=30):
    wait_for(lambda: len(snap_rows(page)["rows"]) == n, timeout)
    return snap_rows(page)


def snap_click(page, sel, phone):
    # The table is drawn again when the kernel's hash or the storage
    # estimate arrives: a button can be replaced between finding and tapping.
    for i in range(3):
        try:
            loc = page.locator(sel).first
            loc.scroll_into_view_if_needed(timeout=10000)
            loc.tap(timeout=10000) if phone else loc.click(timeout=10000)
            return
        except PlaywrightError:
            if i == 2:
                raise
            time.sleep(0.5)


def snap_save(page, n, phone):
    show_tab(page, "snapshots")
    snap_click(page, "#snap_save", phone)
    st = snap_wait_rows(page, n)
    wait_for(lambda: all(r["kernel"] for r in snap_rows(page)["rows"]), 15)
    return st


def snapdiff_guard(fn, *args):
    """A step that times out (or finds nothing to look at) is a failure of
    that case, not of the suite."""
    try:
        return fn(*args)
    except (PlaywrightError, IndexError, KeyError, TypeError) as e:
        # The step that failed: its line here (a timeout's first line names no selector).
        tb = [f for f in traceback.extract_tb(e.__traceback__) if os.path.dirname(f.filename) == os.path.dirname(__file__) and f.name != "snapdiff_guard"]
        at = f" at {os.path.basename(tb[-1].filename)}:{tb[-1].lineno} ({tb[-1].line})" if tb else ""
        return [f"{fn.__name__}: {type(e).__name__}{at}: {(str(e).splitlines() or [''])[0][:300]}"], {}

BM_STATE_JS = """() => { const rows = [...document.querySelectorAll('#bm_list .bmrow')];
  return { rows: rows.map((e) => ({ id: e.dataset.id, kind: e.dataset.kind, group: e.dataset.group,
      name: (e.querySelector('.bmgo') || {}).textContent || '', what: (e.querySelector('.bmwhat') || {}).textContent || '',
      link: (e.querySelector('a.bmlink') || {}).href || '', sw: !!e.querySelector('.bmswitch'), shown: e.getClientRects().length > 0 })),
    count: Number((document.getElementById('bm_list') || {dataset: {}}).dataset.count || 0),
    store: (() => { try { return JSON.parse(localStorage.getItem('mkb-bookmarks') || 'null'); } catch (e) { return 'error ' + e; } })(),
    status: (document.getElementById('bm_status') || {}).textContent || '',
    list: typeof window.MKB?.BM === 'undefined' ? null : MKB.BM.list.map((b) => ({ ...b })) }; }"""

BM_DIALOG_JS = """() => { const d = document.getElementById('bm_dialog'); if (!d || d.hidden) return null;
  const b = d.getBoundingClientRect(), n = document.getElementById('bm_name');
  return { name: n.value, what: document.getElementById('bm_what').textContent, kind: d.dataset.kind || '',
    focus: document.activeElement === n, box: [b.left, b.top, b.right, b.bottom], view: [innerWidth, innerHeight] }; }"""


def bm_state(page):
    return page.evaluate(BM_STATE_JS)


def bm_open_list(page, phone):
    """The menu toggle's menu and its bookmarks list, open."""
    nav_menu(page, "tap" if phone else "click")
    if not page.locator("#bm_open").count():
        return False
    if page.get_attribute("#bm_open", "aria-expanded") != "true":
        page.tap("#bm_open") if phone else page.click("#bm_open")
    return wait_for(lambda: page.is_visible("#bm_list"), 3)


def bm_close_menu(page):
    if page.get_attribute("#runbadge_toggle", "aria-expanded") == "true":
        page.click("#runbadge_toggle")
        wait_for(lambda: page.get_attribute("#runbadge_toggle", "aria-expanded") == "false", 3)


def bm_dialog_save(page, problems, tag, kind, name=None, key="Enter", want_default=None, then=None):
    """The add dialog is open for kind: check its default, type name (then
    do then(), e.g. tick a box), save."""
    if not wait_for(lambda: page.evaluate(BM_DIALOG_JS) is not None, 5):
        problems.append(f"{tag}: no bookmark dialog opened ({kind})")
        return None
    d = page.evaluate(BM_DIALOG_JS)
    if d["kind"] != kind:
        problems.append(f"{tag}: the dialog is for a {d['kind']!r} bookmark, want {kind!r}: {d}")
    if not d["name"].strip():
        problems.append(f"{tag}: the dialog has no default name ({kind}): {d}")
    if want_default and want_default not in d["name"]:
        problems.append(f"{tag}: the default name {d['name']!r} lacks {want_default!r} ({kind})")
    b, v = d["box"], d["view"]
    if b[0] < -1 or b[1] < -1 or b[2] > v[0] + 1 or b[3] > v[1] + 1:
        problems.append(f"{tag}: the dialog {b} is outside the viewport {v}")
    if not d["focus"]:
        problems.append(f"{tag}: the name field does not have the focus ({kind})")
    n0 = len(bm_state(page)["list"] or [])
    if then:
        then()
    if name is not None:
        page.fill("#bm_name", name)
    if key == "Enter":
        page.press("#bm_name", "Enter")
    else:
        page.click("#bm_save")
    if not wait_for(lambda: page.evaluate(BM_DIALOG_JS) is None and len(bm_state(page)["list"] or []) == n0 + 1, 5):
        problems.append(f"{tag}: saving the {kind} bookmark did not add it: {bm_state(page)['list']}")
        return None
    return (bm_state(page)["list"] or [None])[-1] | {"dialog": d}


def bm_tap(page, sel, phone):
    page.tap(sel) if phone else page.click(sel)


# ---- jitdet: deterministic full JIT speed on the virtual clock (J1) --------
# D1 (2026-11-10): with the clock taken from v86's instruction counter, the
# only remaining JIT nondeterminism is when compiled code goes live (v86
# instantiates each block asynchronously, so on wall time), which moves the
# ends of main_loop's slices and so where timer interrupts land. With the
# virtual clock, full speed runs v86's JIT from the page, one main_loop at a
# time, and compiled code goes live only between two of them. The test:
# Debian from the reset vector to its end twice (the root-fs panic's last
# line at about 2.6e9, where the page stops exactly since J3; until J3 the
# screen was polled, so the test stopped at a cap of 2.5e9; no init script
# fixes Date or crypto: the page's RTC and RDRAND follow the virtual clock
# and a seed, J2) ends with the same
# count, EIP, chunk-chain hash and RAM; hello and the minified Linux at full
# speed (fast-forward to the entry, then the JIT) end in the same state twice;
# the speed up to v86's count JITDET_CAP is at least JITDET_MIN_RATIO of v86's
# own loop on real time (clock "real", kept as the non-deterministic option).
JITDET_CAP = 2_500_000_000
# The speed: a 10 ms watcher times the run from Continue to v86's count cap
# (or to an earlier stop: on real time Debian can reach its panic before the
# cap). stop: also stop there (on real time: Pause), else the virtual-clock
# run goes on to the page's own stop at the panic's last line.
JITDET_WATCH_JS = """([cap, stop]) => { const st = MKB.stepper, w = window.__jd = { t0: MKB.realNow(), c0: MKB.ffCounter(st), cap, at: null, c: null, done: false };
  const iv = setInterval(() => { if (MKB.stepper !== st) { clearInterval(iv); return; }
    const c = MKB.ffCounter(st), stopped = MKB.runState === 'paused' || MKB.runState === 'off';
    if (!w.ran && MKB.runState === 'jit') Object.assign(w, { ran: true, t0: MKB.realNow(), c0: c });
    if (w.ran && !w.at && (c >= cap || stopped)) { w.at = MKB.realNow(); w.c = c; if (stop && MKB.runState === 'jit') MKB.pause(); }
    if (w.at && (stop || stopped)) { w.done = true; clearInterval(iv); } }, 10); }"""


# ---- replay: record and replay guest input (J4) -------------------------------
# On the virtual clock (J1, J2, J3) a run repeats exactly until someone types:
# the keyboard reached the PS/2 controller at a moment of wall time. Now a key
# typed on the virtual clock waits for the next boundary between two JIT
# slices or two stepper slices and goes in there, logged with the count and
# the virtual time. A run (boot settings, J2's start date and seed, the log,
# where it ended) is downloadable, saved with snapshots and position
# bookmarks, and Replay boots the same kernel and puts the keys in at the
# same counts: the same count, EIP, RAM and screen at the end.
REPLAY_STATE_JS = """async () => { MKB.flushScreen(); const st = MKB.stepper, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const I = typeof window.MKB?.INPUT === 'object' ? MKB.INPUT : null, keys = I ? I.log.filter((e) => e.keys) : [];
  const rs = document.getElementById('run_status');
  return { steps: st.steps, counter: MKB.ffCounter(st), eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    clock: MKB.CLOCK.now(), screen: Array.from(document.getElementById('screen_text').children, (e) => e.textContent.replace(/\\s+$/, '')),
    entries: I ? I.log.length : null, keyEntries: keys.length, scancodes: keys.reduce((n, e) => n + e.keys.length, 0),
    firstKeyAt: keys.length ? keys[0].at : null, modes: I ? I.log.filter((e) => e.mode).map((e) => e.mode) : null,
    pending: I ? I.pending.length : null, taint: I ? I.taint : null, prompt: st.prompt ? st.prompt.steps : null, runState: MKB.runState,
    replay: I && I.replay ? { done: I.replay.done, ok: I.replay.ok === undefined ? null : I.replay.ok, late: I.replay.late.length } : null,
    status: rs ? rs.textContent : null, verdict: rs ? rs.dataset.replay || null : null }; }"""


def replay_state(page):
    return page.evaluate(REPLAY_STATE_JS)


def replay_wait(page, timeout):
    """Until a replay has finished and the machine is paused there."""
    return wait_for(lambda: page.evaluate("typeof window.MKB?.INPUT === 'object' && !!MKB.INPUT.replay && MKB.INPUT.replay.done && MKB.runState === 'paused'"),
                    timeout, 1)


def rev_click(page, sel, phone, timeout=120):
    """Click a reverse button; wait for its result. (wall ms, the page's ms, status, state)."""
    dbg_shown_once(page)
    page.evaluate("(() => { const s = document.getElementById('dbg_status'); delete s.dataset.rev; delete s.dataset.revMs; })()")
    loc = page.locator(sel)
    loc.scroll_into_view_if_needed()
    t0 = time.monotonic()
    loc.tap() if phone else loc.click()
    wait_for(lambda: page.get_attribute("#dbg_status", "data-rev") in ("done", "error", "none"), timeout, 0.05)
    wall = round((time.monotonic() - t0) * 1000)
    st = page.get_attribute("#dbg_status", "data-rev")
    ms = page.get_attribute("#dbg_status", "data-rev-ms")
    return wall, int(ms) if ms else None, page.inner_text("#dbg_status"), st


def mev_page(browser, scheme="light", phone=False):
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    return ctx, page, errs

SRCEDIT_STATE_JS = """() => { const p = document.getElementById('src_panel'), ta = document.getElementById('ed_text'), g = document.getElementById('ed_gutter');
  const shown = (e) => !!e && !e.hidden && e.getClientRects().length > 0, b = document.getElementById('src_edit');
  const r = (e) => { const x = e.getBoundingClientRect(); return [Math.round(x.left), Math.round(x.top), Math.round(x.right), Math.round(x.bottom)]; };
  return { editing: p.classList.contains('editing'), open: !p.hidden, value: ta.value, gutterLines: g.textContent.trim().split('\\n').length,
    gutterTop: Math.round(g.scrollTop), taTop: Math.round(ta.scrollTop), state: document.getElementById('ed_state').textContent,
    pressed: b.getAttribute('aria-pressed'), editDisabled: b.disabled, editTitle: b.title, editShown: shown(b),
    taShown: shown(ta), bodyShown: shown(document.getElementById('src_body')), pdiffShown: shown(document.getElementById('ed_pdiff')),
    pdiff: document.getElementById('ed_pdiff').textContent, barShown: shown(document.getElementById('ed_bar')),
    active: document.activeElement ? document.activeElement.id : '',
    list: [...document.querySelectorAll('#ed_list li[data-path]')].map((li) => ({ path: li.dataset.path, add: Number(li.dataset.add), del: Number(li.dataset.del) })),
    count: document.getElementById('ed_count').textContent, status: document.getElementById('ed_status').textContent,
    diff: document.getElementById('ed_diff').textContent, diffShown: shown(document.getElementById('ed_diff')),
    download: !document.getElementById('ed_download').disabled, hscroll: document.documentElement.scrollWidth > innerWidth + 1,
    ta: r(ta), gutter: r(g), panel: r(p), vw: innerWidth, vh: innerHeight }; }"""


def srcedit_state(page):
    return page.evaluate(SRCEDIT_STATE_JS)


def srcedit_press(page, sel, phone):
    # The list of edited files is drawn again when the DWARF or the source
    # arrives: a button can be replaced between finding and pressing it.
    for i in range(3):
        try:
            loc = page.locator(sel).first
            loc.scroll_into_view_if_needed(timeout=10000)
            loc.tap(timeout=10000) if phone else loc.click(timeout=10000)
            return
        except PlaywrightError:
            if i == 2:
                raise
            time.sleep(0.5)


def srcedit_ready(page, name):
    """The overlay read from storage and the kernel shown (paths() are the shown kernel's)."""
    page.wait_for_function("typeof window.MKB?.srcOverlay !== 'undefined' && MKB.srcOverlay.ready", timeout=60000)
    page.evaluate("MKB.srcOverlay.ready")
    page.wait_for_function("(n) => MKB.srcKernelKey() === n", arg=name, timeout=60000)


def srcedit_overlay(page):
    return page.evaluate("""async () => { await MKB.srcOverlay.ready; const paths = MKB.srcOverlay.paths(), out = {};
      for (const p of paths) out[p] = [await MKB.srcOverlay.read(p), await MKB.srcOverlay.original(p)];
      return { paths, files: out }; }""")
