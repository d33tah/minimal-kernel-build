"""Browser test suite boot (test_browser.py --suites)."""

import json
import pathlib
import time

from tests.browser.common import (
    ALIVE, counters, gate_on_screen, HELLO, kernel_entries, no_hscroll, open_page, page_center, show_canvas, show_tab,
    SITE, wait_for,
)


def listing_counts(page):
    return (int(page.get_attribute("#listing", "data-insns") or 0),
            int(page.get_attribute("#listing", "data-hexrows") or 0))


def check_counters(page, problems, timeout):
    """Wait until the counted boot is done; both lines must have counts."""
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("jit", "paused"), timeout)
    c = counters(page)
    for m in (HELLO, ALIVE):
        row = c.get(m) or {}
        if not (row.get("steps") or 0) > 0 or not (row.get("entry") or 0) > 0 \
                or not (row.get("unique") or 0) > 0 or not (row.get("kernel") or 0) > 0:
            problems.append(f"no counts for {m!r}: {row}")
    h, a = c.get(HELLO, {}), c.get(ALIVE, {})
    if h.get("steps") and a.get("steps") and not a["steps"] >= h["steps"]:
        problems.append("Still alive counted before Hello")
    c["ms"] = int(page.get_attribute("#count_info", "data-ms") or 0)
    return c


def text_section_end_page(name):
    entry = kernel_entries().get(name, {})
    if "symbols" not in entry:
        return None
    syms = json.loads((SITE / entry["symbols"]).read_text())
    text = [s for s in syms["sections"] if s["class"] == "text"]
    if not text:
        return None
    return (text[0]["paddr"] + text[0]["size"]) // 4096


def memory_lab(page, name, has_symbols, problems, shots, tag):
    """Grid, Page tab listing, tabs in the URL, widths, snapshots."""
    r = {}
    kpage = 0x100000 // 4096
    for _ in range(5):
        x, y = page_center(page, kpage)
        page.mouse.move(x, y)
        r["hover"] = page.inner_text("#hover_info")
        if "page 0x100000," in r["hover"]:
            break
        time.sleep(0.2)
    if "page 0x100000" not in r["hover"]:
        problems.append("hover line does not name the page: " + r["hover"])
    if has_symbols and "more symbols" not in r["hover"] and "startup_32" not in r["hover"]:
        problems.append("hover line names no symbol: " + r["hover"])
    if not open_page(page, kpage):
        problems.append("clicking the 1 MiB page did not open the Page tab")
        return r
    if "#page/0x100000" not in page.url:
        problems.append("Page tab not in the URL hash: " + page.url)
    insns, rows = listing_counts(page)
    r["listing_1mib"] = [insns, rows]
    if int(page.get_attribute("#listing", "data-nonzero")) <= 0:
        problems.append("the 1 MiB page is all zero")
    if has_symbols:
        if insns <= 0:
            problems.append("no instruction lines in the 1 MiB page")
        r["symbol_index"] = int(page.get_attribute("#symbol_index", "data-count") or 0)
        if r["symbol_index"] <= 0 or page.locator("#listing .lbl").count() <= 0:
            problems.append("no symbol index or no symbol headers in the listing")
        if page.locator("#page_symbols").count():
            problems.append("a second symbol table is still on the page")
        page.locator("#symbol_index button").first.click()
    # A page with both instructions and hex rows: the end of .text.
    tpage = text_section_end_page(name)
    if tpage is not None and open_page(page, tpage):
        r["listing_text_end"] = listing_counts(page)
        if not (r["listing_text_end"][0] > 0 and r["listing_text_end"][1] > 0):
            problems.append(f"page {tpage:#x}: listing lacks instructions or hex rows {r['listing_text_end']}")
    open_page(page, kpage)

    reads = int(page.get_attribute("#listing", "data-reads"))
    page.check("#live")
    time.sleep(1.6)
    page.uncheck("#live")
    r["live_reads"] = int(page.get_attribute("#listing", "data-reads")) - reads
    # Both kernels halt after the gate lines, so "live" only re-reads while
    # the VM is in the JIT loop or stepping; a halted VM may re-read 0 times.

    # Scans keep the instruction lines.
    show_tab(page, "memory")
    page.click("#scan")
    page.click("#scan")
    r["changed_pages"] = page.get_attribute("#footprint", "data-changed")
    if r["changed_pages"] in (None, ""):
        problems.append("second scan did not report changed pages")
    page.check("#autoscan")
    show_tab(page, "page")
    time.sleep(2.2)
    show_tab(page, "memory")
    page.uncheck("#autoscan")
    show_tab(page, "page")
    insns2, _ = listing_counts(page)
    if has_symbols and insns2 <= 0:
        problems.append("scans lost the instruction lines")

    # Tab state survives a reload.
    page.reload()
    if not wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true", 10):
        problems.append("reload did not keep the Page tab")
    wait_for(lambda: gate_on_screen(page), 60)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("jit", "paused"), 60)
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(kpage), 10)

    # All-zero last page: one row and "*".
    last = int(page.get_attribute("#pages", "data-pages")) - 1
    open_page(page, last)
    lines = page.inner_text("#listing").splitlines()
    r["zero_page_lines"] = len(lines)
    if len(lines) > 3 or "*" not in lines:
        problems.append("all-zero page not collapsed: %d lines" % len(lines))

    open_page(page, kpage)
    widths = {}
    for w in (390, 800, 1000, 1280):
        page.set_viewport_size({"width": w, "height": 900})
        time.sleep(0.4)
        for t in ("page", "memory", "debugger", "calls", "snapshots", "settings", "about"):
            show_tab(page, t)
            if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                problems.append(f"page scrolls horizontally at {w} px on the {t} tab")
        show_tab(page, "page")
        time.sleep(0.2)
        widths[w] = no_hscroll(page, "#listing")
        if not widths[w]:
            problems.append(f"listing scrolls horizontally at {w} px")
        if w in (390, 1280):
            page.screenshot(path=str(shots / f"tab-page-{tag}-{w}.png"))
    r["listing_fits"] = widths
    page.set_viewport_size({"width": 1100, "height": 1000})

    show_tab(page, "snapshots")
    page.click("#snap_save")
    if not wait_for(lambda: page.locator("#snapshots tbody tr").count() >= 1, 30):
        problems.append("Save snapshot added no row: " + page.inner_text("#snap_status"))
        return r
    row = page.locator("#snapshots tbody tr").first
    r["snapshot_raw"] = int(row.get_attribute("data-raw"))
    r["snapshot_gzip"] = int(row.get_attribute("data-stored"))
    r["snapshot_ratio"] = row.locator("td.ratio").inner_text()
    if not r["snapshot_gzip"] < r["snapshot_raw"]:
        problems.append("compressed snapshot is not smaller than the raw state")
    if ":" not in r["snapshot_ratio"]:
        problems.append("snapshot ratio not displayed: " + r["snapshot_ratio"])
    # Blank the text screen's DOM; only the restore can bring the lines back.
    page.eval_on_selector("#screen_text", "e => { for (const c of e.children) c.textContent = ''; }")
    if gate_on_screen(page):
        problems.append("could not blank the screen before the restore")
    row.locator("button.restore").click()
    if not wait_for(lambda: page.get_attribute("#snap_status", "data-restored") is not None, 30):
        problems.append("restore did not finish: " + page.inner_text("#snap_status"))
        return r
    r["restored_gate"] = wait_for(lambda: gate_on_screen(page), 15)
    if not r["restored_gate"]:
        problems.append("gate lines not on the screen after restoring the snapshot")
    return r


def source_check(page, name, problems, shots, tag):
    """C lines in the listing of the page with the kernel's main function; coverage panel."""
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    fn = "start_kernel" if not name.startswith("hello") else "hello_main"
    sym = next(s for s in syms if s["name"] == fn)
    r = {}
    if not open_page(page, sym["paddr"] // 4096):
        problems.append(f"could not open the page of {fn}")
        return r
    r["c_lines"] = page.locator("#listing .src").count()
    if r["c_lines"] <= 0:
        problems.append(f"no C lines in the listing of the page of {fn}")
        return r
    want = "main.c" if not name.startswith("hello") else "hello.c"
    line = page.locator("#listing .src", has_text=want).first
    if not line.count():
        problems.append(f"no {want} line on the page of {fn}")
        return r
    line.click()
    if not wait_for(lambda: page.is_visible("#src_panel"), 3):
        problems.append("tapping a C line opened no source panel")
        return r
    r["panel"] = page.inner_text("#src_title")
    r["executed"] = int(page.get_attribute("#src_body", "data-executed") or 0)
    r["code"] = int(page.get_attribute("#src_body", "data-code") or 0)
    if r["executed"] < 1:
        problems.append(f"coverage panel marks no executed line: {r}")
    page.screenshot(path=str(shots / f"source-{tag}.png"))
    page.click("#src_close")
    page.uncheck("#show_src")
    if page.locator("#listing .src").count():
        problems.append("C source toggle did not hide the C lines")
    page.check("#show_src")
    return r


def timeline_pan(page, problems, r):
    """A drag on the timeline moves its time window by the dragged share of
    it (the span stays) and stops following the run; a drag in the minimap
    centres the window where the pointer is."""
    show_canvas(page)
    if not page.is_checked("#graph_follow"):
        page.check("#graph_follow")
    box = page.locator("#graph_canvas").bounding_box()
    x0, x1, w = page.evaluate("[MKB.G.x0, MKB.G.x1, document.getElementById('graph_canvas').clientWidth]")
    cx, cy = box["x"] + box["width"] / 2, box["y"] + 120
    page.mouse.move(cx, cy)
    page.mouse.down()
    page.mouse.move(cx - 200, cy, steps=5)
    page.mouse.up()
    a0, a1, follow = page.evaluate("[MKB.G.x0, MKB.G.x1, MKB.G.follow || document.getElementById('graph_follow').checked]")
    want = 200 / w * (x1 - x0)
    r["pan"] = [round(a0 - x0), round(want), follow]
    if abs((a0 - x0) - want) > 0.01 * (x1 - x0) or abs((a1 - a0) - (x1 - x0)) > 1e-6 * (x1 - x0) or follow:
        problems.append(f"a 200 px drag on the timeline moved [{x0}, {x1}] to [{a0}, {a1}] (want +{want:.0f}, the same span), "
                        f"still following {follow}")
    y = box["y"] + 10
    page.mouse.move(box["x"] + box["width"] * 0.25, y)
    page.mouse.down()
    page.mouse.move(box["x"] + box["width"] * 0.5, y, steps=5)
    page.mouse.up()
    mid, want_mid, full = page.evaluate("""() => { const t0 = MKB.runStart(), t1 = Math.max(t0 + 1, MKB.recTo(MKB.G.stepper));
        return [(MKB.G.x0 + MKB.G.x1) / 2, t0 + (t1 - t0) / 2, t1 - t0]; }""")
    r["minimap"] = [round(mid), round(want_mid)]
    if abs(mid - want_mid) > 0.01 * full:
        problems.append(f"a drag to the middle of the minimap centred the window at {mid:.0f}, want {want_mid:.0f}")


def timeline_views(page, problems, shots, tag, r):
    """Focus, sandwich, search, depth cap, transposed and flame views."""
    timeline_pan(page, problems, r)
    k = page.evaluate("""() => { const st = MKB.G.stepper; let best = -1, w = -1;
        for (let k = 0; k < st.fr.n; k++) if (st.nodeName(st.fr.node(k)) === 'start_kernel') {
            const e = (st.fr.end(k) < 0 ? st.steps : st.fr.end(k)) - st.fr.start(k); if (e > w) { w = e; best = k; } }
        return best; }""")
    page.evaluate(f"MKB.G.selected = {k}; MKB.renderGraphInfo()")
    r["sandwich"] = [page.get_attribute("#graph_info", "data-callers"), page.get_attribute("#graph_info", "data-callees")]
    if not int(r["sandwich"][1] or 0) > 0:
        problems.append(f"sandwich of start_kernel lists no callees: {r['sandwich']}")
    page.click("#graph_focus")
    time.sleep(0.4)
    r["focus"] = page.get_attribute("#graph_canvas", "data-focus")
    if r["focus"] != "start_kernel" or not page.is_visible("#graph_crumbs"):
        problems.append(f"focus on start_kernel: {r['focus']!r}, crumbs visible {page.is_visible('#graph_crumbs')}")
    show_canvas(page)
    page.screenshot(path=str(shots / f"graph-focus-{tag}-1100.png"))
    page.fill("#graph_search", "init")
    time.sleep(0.4)
    r["search_matches"] = int(page.get_attribute("#graph_canvas", "data-matches") or 0)
    if r["search_matches"] < 1:
        problems.append("search 'init' highlights nothing in the timeline")
    page.fill("#graph_search", "")
    page.locator("#graph_crumbs button").first.click()  # back to the root
    time.sleep(0.3)
    if page.get_attribute("#graph_canvas", "data-focus"):
        problems.append("the root crumb did not leave the focus")
    page.evaluate("document.getElementById('graph_depth').value = 3; document.getElementById('graph_depth').dispatchEvent(new Event('input'))")
    time.sleep(0.3)
    if page.evaluate("MKB.G.rects.some(([k]) => MKB.G.layout.row[k] > 3)"):
        problems.append("the depth cap draws deeper rows")
    page.evaluate("document.getElementById('graph_depth').value = 0; document.getElementById('graph_depth').dispatchEvent(new Event('input'))")
    for b in ("graph_transpose", "graph_flame"):
        page.click("#" + b)
        time.sleep(0.4)
        drawn = int(page.get_attribute("#graph_canvas", "data-drawn") or 0)
        labels = int(page.get_attribute("#graph_canvas", "data-labels") or 0)
        r[b] = [drawn, labels]
        if drawn < 10 or labels < 1:
            problems.append(f"{b}: {drawn} boxes, {labels} labels")
        show_canvas(page)
        page.screenshot(path=str(shots / f"{b}-{tag}-1100.png"))
        page.click("#" + b)


def graph_check(page, name, problems, shots, tag):
    """Calls tab: flame timeline, call tree, top-30 graph, exports, tap."""
    show_tab(page, "calls")
    time.sleep(0.4)
    r = {"nodes": int(page.get_attribute("#graph_status", "data-nodes") or 0),
         "edges": int(page.get_attribute("#graph_status", "data-edges") or 0),
         "frames": int(page.get_attribute("#graph_status", "data-frames") or 0)}
    if r["nodes"] < 2 or r["edges"] < 1 or r["frames"] < 1:
        problems.append(f"graph recorded {r}")
    from_entry = (counters(page).get("now") or {}).get("entry")
    summ = page.evaluate("MKB.timelineSummary()")
    r["timeline_total"], r["timeline_root_sum"] = summ["total"], summ["rootSum"]
    r["top_frames"] = [(f["name"], f["depth"], f["incl"]) for f in summ["top"][:6]]
    # Code without a symbol (the relocation stub) has no frame, so the
    # outermost frames cover at most the run; the call tree root is the run.
    r["max_depth"], r["p99_depth"] = summ["maxDepth"], summ["p99Depth"]
    if summ["total"] != from_entry or not 0 < summ["rootSum"]:
        problems.append(f"timeline total {summ['total']} / outermost frames {summ['rootSum']} vs from-entry count {from_entry}")
    if not name.startswith("hello"):
        names = [f["name"] for f in summ["top"]]
        if "start_kernel" not in names[:6]:
            problems.append(f"start_kernel is not among the top frames: {names[:6]}")
        ki = page.evaluate("""() => { const st = MKB.G.stepper; let incl = 0;
            for (let k = 0; k < st.fr.n; k++) if (st.nodeName(st.fr.node(k)) === 'kernel_init') incl += (st.fr.end(k) < 0 ? st.steps : st.fr.end(k)) - st.fr.start(k);
            return incl; }""")
        r["kernel_init_incl"] = ki
        if not 0 < ki < summ["total"]:
            problems.append(f"kernel_init inclusive count {ki} not plausible")
    if int(page.get_attribute("#graph_canvas", "data-labels") or 0) < 1:
        problems.append("the timeline labels no box")
    page.screenshot(path=str(shots / f"graph-timeline-{tag}-1100.png"))
    # Tap the widest box at depth 0.
    xy = page.evaluate("""() => { const st = MKB.G.stepper, c = document.getElementById('graph_canvas').getBoundingClientRect();
        let best = -1, w = -1;
        for (let k = 0; k < st.fr.n; k++) if (st.fr.depth(k) === 0 && !MKB.hiddenFrame(st, k)) {
            const e = (st.fr.end(k) < 0 ? st.steps : st.fr.end(k)) - Math.max(st.fr.start(k), MKB.runStart()); if (e > w) { w = e; best = k; } }
        const a = Math.max(st.fr.start(best), MKB.G.x0), b = Math.min(st.fr.end(best) < 0 ? st.steps : st.fr.end(best), MKB.G.x1);
        return [c.left + ((a + b) / 2 - MKB.G.x0) / (MKB.G.x1 - MKB.G.x0) * c.width, c.top + MKB.G.areaTop + MKB.timelineLayout(st).row[best] * 17 + 8 - MKB.G.yOff, st.nodeName(st.fr.node(best))]; }""")
    page.mouse.click(xy[0], xy[1])
    if not wait_for(lambda: page.get_attribute("#graph_info", "data-node") == xy[2], 3):
        problems.append(f"tapping the {xy[2]} box did not select it: {page.get_attribute('#graph_info', 'data-node')}")
    if not name.startswith("hello"):
        timeline_views(page, problems, shots, tag, r)
    # Call tree.
    page.click("#gview_tree")
    wait_for(lambda: page.get_attribute("#graph_tree", "data-root-incl"), 3)
    r["tree_root"] = int(page.get_attribute("#graph_tree", "data-root-incl") or -1)
    if r["tree_root"] != from_entry:
        problems.append(f"call tree root {r['tree_root']} != from-entry count {from_entry}")
    r["tree_rows"] = page.locator("#graph_tree .trow").count()
    page.screenshot(path=str(shots / f"graph-tree-{tag}-1100.png"))
    page.fill("#graph_search", "start_kernel" if not name.startswith("hello") else "hello_main")
    time.sleep(0.3)
    if not page.locator("#graph_tree .trow[data-name='" + ("start_kernel" if not name.startswith("hello") else "hello_main") + "']").count():
        problems.append("call tree search found nothing")
    page.fill("#graph_search", "")
    # Top 30.
    page.click("#gview_top")
    time.sleep(0.3)
    r["top_nodes"] = int(page.get_attribute("#graph_canvas", "data-nodes") or 0)
    if not 1 <= r["top_nodes"] <= 30:
        problems.append(f"top graph has {r['top_nodes']} nodes")
    page.screenshot(path=str(shots / f"graph-top-{tag}-1100.png"), full_page=True)
    with page.expect_download() as dl:
        page.click("#graph_json")
    data = json.loads(pathlib.Path(dl.value.path()).read_text())
    if data["call_tree"]["inclusive"] != from_entry or len(data["edges"]) != r["edges"]:
        problems.append("JSON export does not match the page")
    with page.expect_download() as dl:
        page.click("#graph_dot")
    if not pathlib.Path(dl.value.path()).read_text().startswith("digraph"):
        problems.append("DOT export is not a graph")
    if not name.startswith("hello"):
        node = next((n for n in data["nodes"] if n["name"] == "start_kernel"), None)
        if not node or not any(e["to"] == node["id"] and e["from_name"] == "i386_start_kernel" and e["calls"]
                               for e in data["edges"]):
            problems.append("no call edge i386_start_kernel -> start_kernel")
    # 390 px: the timeline is readable and nothing scrolls sideways.
    page.set_viewport_size({"width": 390, "height": 900})
    for v in ("timeline", "tree", "top"):
        page.click("#gview_" + v)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"graph-{v}-{tag}-390.png"))
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"graph {v} view scrolls horizontally at 390 px")
    page.click("#gview_timeline")
    page.set_viewport_size({"width": 1100, "height": 1000})
    return r


# Instructions from the kernel entry to "Hello, World!": the -O0 hello
# (stores and reloads every variable, calls put_line) and the -Os one.
HELLO_ENTRY = {"hello": 234, "hello-os": 86}


def run_case(browser, base, name, mem, timeout, shots, has_symbols, speed=None):
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel={name}" + (f"&mem={mem}" if mem else "") + (f"&speed={speed}" if speed else "")
    page.goto(url)
    start = time.monotonic()
    ok = wait_for(lambda: gate_on_screen(page), timeout, 0.5)
    text = page.inner_text("#screen_container")
    elapsed = time.monotonic() - start
    ram = int(page.eval_on_selector("#memory", "e => e.value"))
    status = page.inner_text("#status")
    problems = page.inner_text("#problems")
    nonzero = total = None
    lab, lab_problems, count = {}, [], {}
    tag = f"{name}-{ram // 1024}k"
    if ok:
        count = check_counters(page, lab_problems, timeout)
        count["speed"] = page.eval_on_selector("#dbg_speed", "e => e.value")
        count["entry_marker"] = page.get_attribute("#entry_info", "data-steps")
        if speed is None:
            # Default: stepped to the entry, then 1k/s, counted live.
            if not count["entry_marker"]:
                lab_problems.append("no 'entered the kernel' marker: " + page.inner_text("#entry_info"))
            if count["speed"] != "1000":
                lab_problems.append("speed did not switch to 1k/s at the entry: " + count["speed"])
        # The end of the kernel: Linux panics after init exits (exit(0)),
        # hello halts with interrupts off.
        count["end"] = page.get_attribute("#end_banner", "data-kind")
        count["exit_code"] = page.get_attribute("#end_banner", "data-code")
        count["end_text"] = page.inner_text("#end_banner") if count["end"] else ""
        want = "halt" if name.startswith("hello") else "panic"
        if want == "panic":
            rows = counters(page)
            pe = (rows.get("panic_entry") or {}).get("entry")
            ee = (rows.get("end") or {}).get("entry")
            count["panic_entry_vs_end"] = [pe, ee]
            if not pe or not ee or not pe <= ee:
                lab_problems.append(f"panic() entry milestone missing or after the end: {pe} / {ee}")
        if count["end"] != want:
            lab_problems.append(f"end banner {count['end']!r}, want {want!r}")
        if want == "panic" and (count["exit_code"] != "0" or "exited with code 0" not in count["end_text"]):
            lab_problems.append("panic banner does not say init exited with code 0: " + count["end_text"])
        count["graph"] = graph_check(page, name, lab_problems, shots, tag)
        if "src" in kernel_entries().get(name, {}):
            count["source"] = source_check(page, name, lab_problems, shots, tag)
        if speed is None and name in HELLO_ENTRY and (count.get(HELLO) or {}).get("entry") != HELLO_ENTRY[name]:
            lab_problems.append(f"{name}: {count.get(HELLO)} instructions from the entry to Hello, not {HELLO_ENTRY[name]}")
        page.screenshot(path=str(shots / f"top-{tag}-1100.png"))
    show_tab(page, "memory")
    if not page.is_disabled("#scan"):
        page.click("#scan")
        nonzero = int(page.get_attribute("#footprint", "data-nonzero"))
        total = int(page.get_attribute("#footprint", "data-total"))
        if ok:
            lab = memory_lab(page, name, has_symbols, lab_problems, shots, tag)
    show_tab(page, "machine")
    page.locator("#screen_container").screenshot(path=str(shots / f"screen-{tag}.png"))
    page.screenshot(path=str(shots / f"page-{tag}.png"), full_page=True)
    page.close()
    return {
        "kernel": name, "ram_kib": ram // 1024, "ok": ok,
        "seconds": round(elapsed, 1), "nonzero_pages": nonzero,
        "total_pages": total, "status": status, "problems": problems,
        "page_errors": errors, "counters": count,
        "screen_head": "\n".join(text.splitlines()[:4]) if not ok else "",
        "lab": lab, "lab_problems": lab_problems,
    }


def suite_boot(ctx):
    """The boot cases (default_cases or the command line's), then Linux vs hello."""
    failed = False
    at2mb = {}
    for name, mem, speed in ctx.cases:
        has_symbols = "symbols" in ctx.entries.get(name, {})
        r = run_case(ctx.browser, ctx.base, name, mem, ctx.args.timeout, ctx.shots, has_symbols, speed)
        print(json.dumps(r), flush=True)
        good = (r["ok"] and r["nonzero_pages"] and 0 < r["nonzero_pages"] < r["total_pages"]
                and not r["lab_problems"] and not r["page_errors"])
        if not good:
            failed = True
        if mem is None:
            at2mb[name] = r["counters"]
    h = (at2mb.get("hello") or {}).get(HELLO) or {}
    k = (at2mb.get("minified-linux") or {}).get(HELLO) or {}
    if k and not (k.get("entry") or 0) > (h.get("entry") or 0):
        print("FAIL: Linux needs no more instructions than hello", h, k)
        failed = True
    return failed
