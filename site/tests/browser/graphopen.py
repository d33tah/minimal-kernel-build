"""Browser test suite graphopen (test_browser.py --suites)."""

import json
import time

from tests.browser.common import (
    graph_box_xy, kernel_entries, nav_at, nav_press, nav_settle, report, run_to_wait, show_tab, shown_tab, SITE,
    wait_for,
)


# ---- graphopen: a double click in the Calls chart opens the function (G1) --
# The operator: "dwuklik na flame chart niech otwiera daną funkcję w
# przeglądarce kodu". In the Calls tab's timeline (icicle, and with Flame)
# and in Top 30, a double click on a function's box, a double tap, or Enter
# on the canvas with a box selected, opens that function: the Page tab at its
# first instruction with its symbol header in view, and the Source panel at
# its file:line (DWARF). One history entry: Back returns to the Calls tab
# with the same window, scroll, selection, focus and orientation. A single
# click still selects; boxes with no symbol and the empty area do nothing.
GRAPHOPEN_POINTS = {"hello": ("hello_main", 40, "boot/hello.c"),
                    "minified-linux": ("start_kernel", 3000, "init/main.c")}

GRAPH_STATE_JS = """() => ({ tab: MKB.tab, view: MKB.G.view, x0: MKB.G.x0, x1: MKB.G.x1, yOff: MKB.G.yOff, xOff: MKB.G.xOff, selected: MKB.G.selected,
    focus: MKB.G.focus, flame: MKB.G.flame, transpose: MKB.G.transpose, node: document.getElementById('graph_info').dataset.node || '' })"""


def graph_empty_xy(page):
    """A point in the timeline's drawing area that no box, merged column or
    task switch mark covers (below the deepest row)."""
    return page.evaluate("""() => { const c = document.getElementById('graph_canvas').getBoundingClientRect();
        let low = MKB.G.areaTop || 40; for (const r of MKB.G.rects) low = Math.max(low, r[2] + r[4]);
        const y = Math.min(c.height - 30, low + 40);
        return { x: c.left + c.width * 0.5, y: c.top + y }; }""")


def graph_double(page, xy, touch):
    if touch:
        page.touchscreen.tap(xy["x"], xy["y"])
        time.sleep(0.08)
        page.touchscreen.tap(xy["x"], xy["y"])
    else:
        page.mouse.dblclick(xy["x"], xy["y"])


def graphopen_opened(page, name, fn, paddr, src_file, how, problems, r, entries0):
    """After a double click / tap / Enter: the Page tab at fn, Source panel
    at its line, one more history entry."""
    tag = f"{name} {fn} ({how})"
    if not wait_for(lambda: shown_tab(page) == "page", 8):
        problems.append(f"{tag}: the Page tab did not open (tab {shown_tab(page)!r})")
        return False
    nav_settle(page)
    got = page.evaluate("""([pa, fn]) => { const box = document.getElementById('listing').getBoundingClientRect();
        // On screen: inside the listing's box, below the sticky top bar and
        // above the Source panel where it is a sheet over the screen (phone).
        const sp0 = document.getElementById('src_panel'), pr = sp0.hidden ? null : sp0.getBoundingClientRect();
        const lowest = pr && pr.left <= 0 && pr.top > 0 ? pr.top : innerHeight;
        const topBar = document.querySelector('.top').getBoundingClientRect().bottom;
        const vis = (e) => { if (!e) return false; const b = e.getBoundingClientRect();
            return b.height > 0 && b.top >= box.top - 1 && b.bottom <= box.bottom + 1 && b.top >= topBar - 1 && b.bottom <= lowest + 1 && b.left >= 0 && b.left < innerWidth; };
        const ins = [...document.querySelectorAll('#listing .ins[data-addr="' + pa + '"]')];
        const hdr = document.querySelector('#listing .lbl[data-sym="' + fn + '"][data-addr="' + pa + '"]');
        const sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
        const here = sb.querySelector('.here');
        const hv = here && (() => { const a = here.getBoundingClientRect(), b = sb.getBoundingClientRect(); return a.top >= b.top - 1 && a.bottom <= b.bottom + 1; })();
        return { page: MKB.selectedPage, ins: ins.length, insVisible: ins.some(vis), hdr: !!hdr, hdrVisible: vis(hdr),
                 panel: !sp.hidden, file: sb.dataset.file || '', state: sb.dataset.state || '', title: document.getElementById('src_title').textContent,
                 hereVisible: !!hv, hereLine: here ? Number(here.dataset.line) : 0,
                 hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }""", [paddr, fn])
    if got["state"] != "ok":
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 20)
        time.sleep(0.5)
        got.update(page.evaluate("""() => { const sb = document.getElementById('src_body'), here = sb.querySelector('.here');
            const a = here && here.getBoundingClientRect(), b = sb.getBoundingClientRect();
            return { file: sb.dataset.file || '', state: sb.dataset.state || '', title: document.getElementById('src_title').textContent,
                     hereVisible: !!here && a.top >= b.top - 1 && a.bottom <= b.bottom + 1, hereLine: here ? Number(here.dataset.line) : 0 }; }"""))
    r[tag] = got
    if got["page"] != paddr // 4096 or not got["ins"]:
        problems.append(f"{tag}: the listing is not at {fn}'s first instruction {paddr:#x}: {got}")
    elif not got["insVisible"] or not got["hdr"] or not got["hdrVisible"]:
        problems.append(f"{tag}: {fn}'s header and first instruction are not both in view: {got}")
    if not got["panel"] or got["file"] != src_file or got["state"] != "ok" or not got["title"].startswith(fn + ",") or not got["hereVisible"]:
        problems.append(f"{tag}: the Source panel is not at {fn} in {src_file}: {got}")
    if got.get("hscroll"):
        problems.append(f"{tag}: horizontal scroll")
    at = nav_at(page)
    if at[0] != entries0[0] + 1 or at[1] != entries0[0] + 2:
        problems.append(f"{tag}: one history entry expected: before {entries0}, after {at}")
    return True


def graphopen_back(page, name, fn, before, how, touch, problems):
    """Back (the menu's button or Alt+Left): the Calls tab exactly as left."""
    tag = f"{name} {fn}: Back ({how})"
    if touch:
        nav_press(page, "#nav_back", "tap")
    elif how == "alt":
        page.focus("#runbadge_toggle")
        page.keyboard.press("Alt+ArrowLeft")
    else:
        page.go_back()
    if not wait_for(lambda: shown_tab(page) == "calls", 8):
        problems.append(f"{tag}: did not return to the Calls tab ({shown_tab(page)!r})")
        return
    time.sleep(0.6)
    if page.evaluate("!document.getElementById('src_panel').hidden"):
        problems.append(f"{tag}: the Source panel stays open")
    now = page.evaluate(GRAPH_STATE_JS)
    diff = {k: (before[k], now[k]) for k in before
            if (abs(before[k] - now[k]) > max(1e-6 * abs(before[k]), 0.5) if isinstance(before[k], float) else before[k] != now[k])}
    if diff:
        problems.append(f"{tag}: the Calls tab is not as it was: {diff}")


# B2: on a slow host one double click or double tap opened the function
# twice (two history entries). The canvas's dblclick opened it and the timer
# of the pointerup double-press detection opened it again: the guard against
# a second open counted from the start of the open, and the open itself takes
# longer than that window when the CPU is slow. Two ways to be slow: the open
# takes 900 ms (a wrapper busy-waits after it, in real time), and the CPU is
# throttled 10x through CDP.
# The wrapper also counts the opens: at 1280 px the second open of the same
# place adds no history entry, but it is still a second jump.
GRAPHOPEN_SLOW_JS = """(ms) => { if (!window.__openFunction0) window.__openFunction0 = MKB.openFunction;
    window.__opens = 0;
    MKB.openFunction = (...a) => { __opens++; __openFunction0(...a); const t = MKB.realNow(); while (MKB.realNow() - t < ms); }; }"""


def graphopen_slow(page, ctx, name, fn, paddr, src_file, phone, problems, r):
    """A double click / tap while the host is slow: still one history entry."""
    cdp = ctx.new_cdp_session(page)
    for slow in ("open takes 900 ms", "CPU 10x slower"):
        how = ("double tap" if phone else "double click") + ", " + slow
        xy = graph_box_xy(page, fn)
        if not xy:
            problems.append(f"{name} {fn}: no box drawn ({how})")
            continue
        time.sleep(0.7)  # the next press starts afresh
        before = page.evaluate(GRAPH_STATE_JS)
        entries0 = nav_at(page)
        page.evaluate(GRAPHOPEN_SLOW_JS, 900 if slow.startswith("open") else 0)
        if slow.startswith("CPU"):
            cdp.send("Emulation.setCPUThrottlingRate", {"rate": 10})
        graph_double(page, xy, phone)
        time.sleep(4)  # a second open would land within this
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 1})
        opens = page.evaluate("__opens")
        page.evaluate("() => { MKB.openFunction = __openFunction0; }")  # an expression's function value would be called
        r[f"slow {how}"] = [entries0, nav_at(page), opens]
        if opens != 1:
            problems.append(f"{name} {fn} ({how}): opened {opens} times, once expected")
        if graphopen_opened(page, name, fn, paddr, src_file, how, problems, r, entries0):
            graphopen_back(page, name, fn, before, "button" if phone else "browser", phone, problems)
    cdp.detach()


def graphopen_run(browser, base, shots, name, scheme, phone):
    fn, steps, src_file = GRAPHOPEN_POINTS[name]
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]}
    sym = syms[fn]
    problems, r = [], {}
    errors = []
    width = 390 if phone else 1280
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True) if phone else \
        browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    tagw = f"{name} {width} {scheme}"
    if not phone:
        # At the reset vector the timeline has no box: the arrow keys select nothing.
        show_tab(page, "calls")
        page.focus("#graph_canvas")
        page.keyboard.press("ArrowRight")
        time.sleep(0.3)
        empty = page.evaluate("[MKB.G.rects.length, MKB.G.selected, MKB.tab]")
        r["arrow at reset"] = empty
        if empty != [0, -1, "calls"]:
            problems.append(f"{tagw}: ArrowRight on the empty timeline at the reset vector: [boxes, selected, tab] {empty}")
        show_tab(page, "debugger")
    page.fill("#dbg_addr", hex(sym["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {sym["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)
    show_tab(page, "calls")
    if not wait_for(lambda: page.evaluate("MKB.G.rects.length > 0"), 20):
        ctx.close()
        return [f"{name}: the Calls timeline drew no box"], r

    def select(xy):
        page.touchscreen.tap(xy["x"], xy["y"]) if phone else page.mouse.click(xy["x"], xy["y"])

    orientations = [False] if phone else [False, True]
    for flame in orientations:
        if page.evaluate("MKB.G.flame") != flame:
            page.click("#graph_flame")
            time.sleep(0.5)
        how = ("double tap" if phone else "double click") + (" flame" if flame else "")
        xy = graph_box_xy(page, fn)
        if not xy:
            problems.append(f"{tagw}: no {fn} box drawn ({how})")
            continue
        # A single click (tap) selects and stays in the Calls tab, as before.
        focus0 = page.evaluate("MKB.G.focus")
        select(xy)
        if not wait_for(lambda: page.get_attribute("#graph_info", "data-node") == fn, 3):
            problems.append(f"{tagw}: a single {'tap' if phone else 'click'} did not select {fn}: {page.get_attribute('#graph_info', 'data-node')}")
        time.sleep(0.7)  # longer than a double click: the next press starts afresh
        if shown_tab(page) != "calls" or page.evaluate("MKB.G.focus") != focus0:
            problems.append(f"{tagw}: a single {'tap' if phone else 'click'} left the Calls tab or re-rooted it ({shown_tab(page)})")
        if not phone:
            page.mouse.move(xy["x"], xy["y"] + 1)
            time.sleep(0.2)
            title = page.get_attribute("#graph_canvas", "title") or ""
            r[f"title {tagw}"] = title[-90:]
            if "double-click" not in title.lower():
                problems.append(f"{tagw}: the box's hover text does not mention the double click: {title[-120:]!r}")
        info = page.inner_text("#graph_info")
        if "double" not in info.lower():
            problems.append(f"{tagw}: the selected box's info does not mention the double click/tap: {info[:160]!r}")
        before = page.evaluate(GRAPH_STATE_JS)
        entries0 = nav_at(page)
        graph_double(page, xy, phone)
        if graphopen_opened(page, name, fn, sym["paddr"], src_file, how, problems, r, entries0):
            if not flame:
                for s in ("light", "dark"):
                    page.emulate_media(color_scheme=s)
                    time.sleep(0.3)
                    page.screenshot(path=str(shots / f"graphopen-{name}-{width}-{s}-page.png"))
                page.emulate_media(color_scheme=scheme)
            graphopen_back(page, name, fn, before, "button" if phone else ("alt" if flame else "browser"), phone, problems)
            if not flame:
                for s in ("light", "dark"):
                    page.emulate_media(color_scheme=s)
                    time.sleep(0.3)
                    page.screenshot(path=str(shots / f"graphopen-{name}-{width}-{s}-back.png"))
                page.emulate_media(color_scheme=scheme)
    if page.evaluate("MKB.G.flame"):
        page.click("#graph_flame")
        time.sleep(0.4)
    graphopen_slow(page, ctx, name, fn, sym["paddr"], src_file, phone, problems, r)

    # Enter on the canvas opens the selected box; the arrows move the selection.
    xy = graph_box_xy(page, fn)
    if xy and not phone:
        select(xy)
        wait_for(lambda: page.get_attribute("#graph_info", "data-node") == fn, 3)
        time.sleep(0.7)
        before = page.evaluate(GRAPH_STATE_JS)
        entries0 = nav_at(page)
        page.focus("#graph_canvas")
        page.keyboard.press("Enter")
        if graphopen_opened(page, name, fn, sym["paddr"], src_file, "Enter", problems, r, entries0):
            graphopen_back(page, name, fn, before, "browser", False, problems)
        # Arrows: Down to a callee, Up back to fn.
        page.focus("#graph_canvas")
        page.keyboard.press("ArrowDown")
        time.sleep(0.3)
        down = page.evaluate("[MKB.G.selected >= 0 ? MKB.G.stepper.fr.parent(MKB.G.selected) : -2, MKB.G.selected]")
        page.keyboard.press("ArrowUp")
        time.sleep(0.3)
        up = page.evaluate("MKB.G.selected")
        r[f"arrows {tagw}"] = [before["selected"], down, up]
        if down[0] != before["selected"] or up != before["selected"]:
            problems.append(f"{tagw}: ArrowDown/ArrowUp on the canvas do not move to a callee and back: {r[f'arrows {tagw}']}")

    # The empty area: nothing opens, no history entry.
    e = graph_empty_xy(page)
    entries0 = nav_at(page)
    graph_double(page, e, phone)
    time.sleep(1)
    if shown_tab(page) != "calls" or nav_at(page) != entries0:
        problems.append(f"{tagw}: a double {'tap' if phone else 'click'} on the empty area opened {shown_tab(page)!r} / {nav_at(page)} vs {entries0}")

    # Top 30: a node's box opens its function too.
    if not phone and name != "hello":
        page.click("#gview_top")
        time.sleep(0.6)
        xy = graph_box_xy(page, fn, "top")
        if not xy:
            problems.append(f"{tagw}: Top 30 has no {fn} box")
        else:
            select(xy)
            time.sleep(0.7)
            before = page.evaluate(GRAPH_STATE_JS)
            entries0 = nav_at(page)
            graph_double(page, xy, False)
            if graphopen_opened(page, name, fn, sym["paddr"], src_file, "Top 30", problems, r, entries0):
                graphopen_back(page, name, fn, before, "alt", False, problems)
        page.click("#gview_timeline")
    if errors:
        problems.append(f"{tagw}: page errors {errors}")
    ctx.close()
    return problems, r


# DWARF line shards (dwarf/<hex>.bin) wait for window.mkbRelease().
GRAPHOPEN_HOLD_JS = """(() => { const f = window.fetch; let release; const held = new Promise((r) => { release = r; });
    window.mkbRelease = release; window.mkbHeld = 0;
    window.fetch = async (u, ...a) => { if (/\\/dwarf\\/[0-9a-f]+\\.bin/.test(String(u))) { window.mkbHeld++; await held; }
        return f(u, ...a); }; })()"""


# The listing's line of physical address pa is inside the listing's box.
GRAPHOPEN_IN_VIEW_JS = """(pa) => { const box = document.getElementById('listing').getBoundingClientRect();
    const e = document.querySelector('#listing .ins[data-addr="' + pa + '"]'); if (!e) return false;
    const b = e.getBoundingClientRect(); return b.height > 0 && b.top >= box.top - 1 && b.bottom <= box.bottom + 1; }"""


def graphopen_loading(browser, base, shots):
    """hello, its DWARF line shard held back: a double click on hello_main's
    box opens the Page tab at once and the Source panel when the shard
    arrives (the open waits for it), with the function's first
    instruction still in view in the listing; a second open whose user goes to
    another tab before the shard arrives opens no Source panel then."""
    problems, r = [], {}
    fn, steps, src_file = GRAPHOPEN_POINTS["hello"]
    sym = next(s for s in json.loads((SITE / kernel_entries()["hello"]["symbols"]).read_text())["symbols"] if s["name"] == fn)
    errors = []
    for leave in (False, True):
        ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
        ctx.add_init_script(GRAPHOPEN_HOLD_JS)
        page = ctx.new_page()
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        try:
            page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
            page.fill("#dbg_addr", fn)
            page.click("#dbg_runto")
            ok, at, status = run_to_wait(page, {sym["paddr"]}, 120)
            page.fill("#dbg_n", str(steps))
            page.click("#dbg_stepn")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
            show_tab(page, "calls")
            xy = wait_for(lambda: page.evaluate("MKB.G.rects.length > 0") and graph_box_xy(page, fn), 20) and graph_box_xy(page, fn)
            if not ok or not xy:
                problems.append(f"shard held: Run to {fn} {ok} ({status!r}), its box {xy}")
                continue
            graph_double(page, xy, False)
            opened = wait_for(lambda: shown_tab(page) == "page", 8)
            time.sleep(1)
            before = [opened, page.evaluate("mkbHeld > 0"), page.is_visible("#src_panel")]
            if leave:
                show_tab(page, "debugger")
            page.evaluate("mkbRelease()")
            wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 5 if leave else 20)
            time.sleep(0.5)
            after = [page.is_visible("#src_panel"), page.get_attribute("#src_body", "data-file"), (page.text_content("#src_title") or "")[:40],
                     page.evaluate(GRAPHOPEN_IN_VIEW_JS, sym["paddr"])]
            tag = "shard held, then " + ("another tab" if leave else "it arrives")
            r[tag] = [before, after]
            if before != [True, True, False]:
                problems.append(f"{tag}: before the shard [Page tab, shard asked for, Source panel] {before}, want [True, True, False]")
            if not leave and (not after[0] or after[1] != src_file or not after[2].startswith(fn + ",") or not after[3]):
                problems.append(f"{tag}: [Source panel, its file, title, {fn}'s first instruction in view in the listing] {after}, "
                                f"want {fn} in {src_file} and in view")
            if leave and after[0]:
                problems.append(f"{tag}: the Source panel opened in the Debugger tab after the user left: {after}")
            if not leave:
                page.screenshot(path=str(shots / "graphopen-loading-1280.png"))
        finally:
            ctx.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def graphopen_case(browser, base, shots, linux):
    out = [report("graphopen loading", *graphopen_loading(browser, base, shots))]
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if linux:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = graphopen_run(browser, base, shots, name, scheme, phone)
        out.append(report("graphopen", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_graphopen(ctx):
    return graphopen_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)
