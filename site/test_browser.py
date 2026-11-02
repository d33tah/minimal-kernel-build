#!/usr/bin/env python3
"""Boot kernels on the page in headless Chromium and check it end to end.

Serves site/ with python's http.server on a free port and opens
index.html?kernel=NAME&mem=BYTES for each case. Per case: the VGA screen
shows "Hello, World!" then "Still alive"; the boot counters count both
lines; the page scan; the Page tab listing (instructions and hex rows,
one symbol index, survives scans, tab kept in the URL hash across a
reload, all-zero page collapsed); no horizontal scroll at 390-1280 px;
a snapshot round trip. Then per kernel, started paused at the reset
vector: Step 10, Run to 0x100000, 1k/s slow motion. Then a 390 px touch
case for the "?" popovers and a stale-cache case (two deploys with
max-age=600). See README.md, Testing.

Usage:
  site/test_browser.py                      all of the above
  site/test_browser.py hello:1048576 minified-linux:18874368
  site/test_browser.py --probe hello:655360 report only, never fail

Needs: pip install playwright && playwright install chromium
"""

import argparse
import functools
import http.server
import json
import os
import pathlib
import re
import shutil
import subprocess
import socketserver
import sys
import tempfile
import threading
import time

from playwright.sync_api import sync_playwright

SITE = pathlib.Path(__file__).resolve().parent
HELLO = "Hello, World!"
ALIVE = "Still alive"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class CachingHandler(QuietHandler):
    """Assets may be cached for 10 minutes (GitHub Pages sends max-age=600);
    index.html is never cached, so a reload always gets the newest one and a
    cached older app.js would be mixed with it unless the URLs differ."""
    def end_headers(self):
        html = self.path.split("?")[0].endswith((".html", "/"))
        # no-store, not no-cache: http.server answers If-Modified-Since with
        # one-second resolution, so two deploys within a second would 304.
        self.send_header("Cache-Control", "no-store" if html else "max-age=600")
        super().end_headers()


def serve(directory=SITE, handler_class=QuietHandler):
    handler = functools.partial(handler_class, directory=str(directory))
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def kernel_entries():
    return {k["name"]: k for k in json.loads((SITE / "kernels.json").read_text())["kernels"]}


def default_cases():
    names = kernel_entries()
    # hello boots with the page's default (1k/s from the kernel entry, about
    # 0.2 s); Linux would take ~15 minutes that way, so it runs with
    # ?speed=full (whole boot counted in the background, then the JIT).
    cases = [("hello", None, None)]
    if "minified-linux" in names:
        cases.append(("minified-linux", None, "full"))
        cases.append(("minified-linux", 18 * 1024 * 1024, "full"))
        # The smallest RAM it boots with in v86 (measured: 2176 KiB fails).
        cases.append(("minified-linux", 2180 * 1024, "full"))
    return cases


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


def listing_counts(page):
    return (int(page.get_attribute("#listing", "data-insns") or 0),
            int(page.get_attribute("#listing", "data-hexrows") or 0))


def no_hscroll(page, sel):
    return page.eval_on_selector(sel, "e => e.scrollWidth <= e.clientWidth + 1")


def counters(page):
    """Rows of the boot counter table: {milestone: {steps, entry, unique, kernel}}."""
    out = {}
    for row in page.locator("#counters tbody tr").all():
        d = {k: row.get_attribute("data-" + k) for k in ("steps", "entry", "unique", "kernel")}
        out[row.get_attribute("data-milestone")] = {k: int(v) if v not in (None, "") else None for k, v in d.items()}
    return out


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
        for t in ("page", "memory", "debugger", "graph", "snapshots", "about"):
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


def regs(page):
    return {e.get_attribute("data-reg"): e.inner_text() for e in page.locator("#regs div").all()}


def debugger_case(browser, base, name, mem, shots):
    """Start paused at reset: Step 10, Run to 0x100000, slow motion."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1" + (f"&mem={mem}" if mem else "") + "#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        return ["did not start paused: " + page.inner_text("#dbg_status")], r
    before = regs(page)
    r["reset_eip"] = before.get("EIP")
    page.fill("#dbg_n", "10")
    page.click("#dbg_stepn")
    wait_for(lambda: "Stepped" in page.inner_text("#dbg_status"), 5)
    after = regs(page)
    r["step10_eip"] = after.get("EIP")
    if after.get("EIP") == before.get("EIP"):
        problems.append("Step 10 did not change EIP")
    if page.locator("#regs div.chg").count() == 0:
        problems.append("register panel marks no changed register after Step 10")
    now = counters(page).get("now", {})
    if now.get("steps") != 10:
        problems.append(f"Step 10 counted {now.get('steps')} instructions")

    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(0x100000), 60):
        problems.append("Run to 0x100000 did not stop there: " + page.inner_text("#dbg_status"))
    r["runto_status"] = page.inner_text("#dbg_status")
    r["runto_steps"] = counters(page).get("now", {}).get("steps")
    if page.locator("#context .cur").count() != 1:
        problems.append("Debugger tab shows no current instruction")
    page.screenshot(path=str(shots / f"tab-debugger-{name}-1100.png"))

    # hello is done 159 instructions after the entry: watch it at 10/s.
    rate, least = ("10", 10) if name == "hello" else ("1000", 200)
    page.select_option("#dbg_speed", rate)
    show_tab(page, "memory")
    page.click("#dbg_continue")
    start = counters(page).get("now", {}).get("steps") or 0
    time.sleep(2.0)
    end = counters(page).get("now", {}).get("steps") or 0
    r["slow_steps_2s"] = end - start
    r["flashes"] = int(page.get_attribute("#pages", "data-flashes") or 0)
    if not end - start >= least:
        problems.append(f"{rate}/s slow motion advanced only {end - start} instructions in 2 s")
    if r["flashes"] < 1:
        problems.append("the grid flashed no written page during slow motion")
    page.screenshot(path=str(shots / f"slow-{name}-1100.png"))
    page.set_viewport_size({"width": 390, "height": 900})
    show_tab(page, "debugger")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"tab-debugger-{name}-390.png"))
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("debugger tab scrolls horizontally at 390 px")
    if page.is_enabled("#dbg_pause"):
        page.click("#dbg_pause")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def symbol_case(browser, base, name, shots):
    """Debugger tab code window and symbol history, from start_kernel."""
    problems, r = [], {}
    entry = kernel_entries().get(name, {})
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next((s for s in syms if s["name"] == "start_kernel"), None)
    if not sk:
        return ["no start_kernel in symbols.json"], r
    page = browser.new_page(viewport={"width": 390, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        return ["did not start paused"], r
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 60):
        return ["Run to start_kernel did not stop there: " + page.inner_text("#dbg_status")], r
    offset = sk["vaddr"] - sk["paddr"]
    for i in range(20):
        page.fill("#dbg_n", "50")
        page.click("#dbg_stepn")
        time.sleep(0.15)
        phys = int(page.get_attribute("#regs", "data-phys"))
        eip = int(page.get_attribute("#regs", "data-eip"))
        cur_line = page.get_attribute("#context", "data-cur")
        lo = page.get_attribute("#cur_sym", "data-lo")
        hi = page.get_attribute("#cur_sym", "data-hi")
        cur = page.get_attribute("#cur_sym", "data-name")
        prev = page.get_attribute("#prev_sym", "data-name")
        if i == 0:
            r["after_step50"] = {"eip": hex(eip), "phys": hex(phys), "current": cur, "previous": prev}
            if cur_line != str(phys) or eip - offset != phys:
                problems.append(f"code window line {cur_line} is not EIP {eip:#x} (phys {phys:#x})")
            if not lo or not (int(lo) <= phys < int(hi)):
                problems.append(f"current symbol {cur} [{lo}, {hi}) does not contain EIP {phys:#x}")
        if prev and cur and prev != cur and page.locator("#context .ins.cur").count() == 1:
            r.update(steps=(i + 1) * 50, current=cur, previous=prev,
                     path=page.inner_text("#sym_info .crumbs"))
            break
    else:
        problems.append(f"no previous symbol different from the current one after 1000 steps: {cur}/{prev}")
    page.screenshot(path=str(shots / f"debugger-symbols-{name}-390.png"))
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("debugger tab scrolls horizontally at 390 px")
    page.locator("#prev_sym button").click()
    if page.get_attribute("#tab_page", "aria-selected") != "true":
        problems.append("clicking the previous symbol did not open the Page tab")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def linux_slow_case(browser, base, shots):
    """Default load of Linux: entry marker, 1k/s, counter grows, ETA, symbol."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return ["no entry marker: " + page.inner_text("#entry_info")], r
    r["entry_marker"] = page.inner_text("#entry_info")
    r["speed"] = page.eval_on_selector("#dbg_speed", "e => e.value")
    if r["speed"] != "1000":
        problems.append("speed is not 1k/s after the entry: " + r["speed"])
    time.sleep(0.5)
    a = (counters(page).get("now") or {}).get("entry") or 0
    time.sleep(3)
    b = (counters(page).get("now") or {}).get("entry") or 0
    r["grew_3s"] = b - a
    if not 1500 <= b - a <= 4500:
        problems.append(f"from-entry counter grew by {b - a} in 3 s at 1k/s")
    r["state"] = page.inner_text("#dbg_state")
    r["eta"] = page.get_attribute("#count_info", "data-eta")
    if " in " not in r["state"]:
        problems.append("top bar does not name the current symbol: " + r["state"])
    if not r["eta"]:
        problems.append("no ETA: " + page.inner_text("#count_text"))
    page.screenshot(path=str(shots / "linux-slow-1100.png"))
    # The choice is remembered: pick full speed, reload, it stays full.
    page.select_option("#dbg_speed", "full")
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("stepped", "jit"), 30)
    if page.eval_on_selector("#dbg_speed", "e => e.value") != "full":
        problems.append("the speed choice was not remembered across a reload")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def source_check(page, name, problems, shots, tag):
    """C lines in the listing of the page with the kernel's main function; coverage panel."""
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    fn = "start_kernel" if name != "hello" else "hello_main"
    sym = next(s for s in syms if s["name"] == fn)
    r = {}
    if not open_page(page, sym["paddr"] // 4096):
        problems.append(f"could not open the page of {fn}")
        return r
    r["c_lines"] = page.locator("#listing .src").count()
    if r["c_lines"] <= 0:
        problems.append(f"no C lines in the listing of the page of {fn}")
        return r
    want = "main.c" if name != "hello" else "hello.c"
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


def show_canvas(page):
    """Scroll so the top of the graph canvas sits just below the sticky top bar."""
    page.evaluate("""() => { const top = document.querySelector('.top').getBoundingClientRect().bottom;
        const c = document.getElementById('graph_status').getBoundingClientRect().top;
        window.scrollBy(0, c - top - 4); }""")
    time.sleep(0.2)


def timeline_views(page, problems, shots, tag, r):
    """Focus, sandwich, search, depth cap, transposed and flame views."""
    k = page.evaluate("""() => { const st = G.stepper; let best = -1, w = -1;
        for (let k = 0; k < st.fStart.length; k++) if (st.nodeName(st.fNode[k]) === 'start_kernel') {
            const e = (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]) - st.fStart[k]; if (e > w) { w = e; best = k; } }
        return best; }""")
    page.evaluate(f"G.selected = {k}; renderGraphInfo()")
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
    page.fill("#graph_search", "delay")
    time.sleep(0.4)
    r["search_matches"] = int(page.get_attribute("#graph_canvas", "data-matches") or 0)
    if r["search_matches"] < 1:
        problems.append("search 'delay' highlights nothing in the timeline")
    page.fill("#graph_search", "")
    page.locator("#graph_crumbs button").first.click()  # back to the root
    time.sleep(0.3)
    if page.get_attribute("#graph_canvas", "data-focus"):
        problems.append("the root crumb did not leave the focus")
    page.evaluate("document.getElementById('graph_depth').value = 3; document.getElementById('graph_depth').dispatchEvent(new Event('input'))")
    time.sleep(0.3)
    if page.evaluate("G.rects.some(([k]) => G.layout.row[k] > 3)"):
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
    """Graph tab: flame timeline, call tree, top-30 graph, exports, tap."""
    show_tab(page, "graph")
    time.sleep(0.4)
    r = {"nodes": int(page.get_attribute("#graph_status", "data-nodes") or 0),
         "edges": int(page.get_attribute("#graph_status", "data-edges") or 0),
         "frames": int(page.get_attribute("#graph_status", "data-frames") or 0)}
    if r["nodes"] < 2 or r["edges"] < 1 or r["frames"] < 1:
        problems.append(f"graph recorded {r}")
    from_entry = (counters(page).get("now") or {}).get("entry")
    summ = page.evaluate("timelineSummary()")
    r["timeline_total"], r["timeline_root_sum"] = summ["total"], summ["rootSum"]
    r["top_frames"] = [(f["name"], f["depth"], f["incl"]) for f in summ["top"][:6]]
    # Code without a symbol (the relocation stub) has no frame, so the
    # outermost frames cover at most the run; the call tree root is the run.
    r["max_depth"], r["p99_depth"] = summ["maxDepth"], summ["p99Depth"]
    if summ["total"] != from_entry or not 0 < summ["rootSum"]:
        problems.append(f"timeline total {summ['total']} / outermost frames {summ['rootSum']} vs from-entry count {from_entry}")
    if name != "hello":
        names = [f["name"] for f in summ["top"]]
        if "start_kernel" not in names[:3]:
            problems.append(f"start_kernel is not among the top frames: {names[:6]}")
        ki = page.evaluate("""() => { const st = G.stepper; let incl = 0;
            for (let k = 0; k < st.fStart.length; k++) if (st.nodeName(st.fNode[k]) === 'kernel_init') incl += (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]) - st.fStart[k];
            return incl; }""")
        r["kernel_init_incl"] = ki
        if not 0 < ki < summ["total"]:
            problems.append(f"kernel_init inclusive count {ki} not plausible")
    if int(page.get_attribute("#graph_canvas", "data-labels") or 0) < 1:
        problems.append("the timeline labels no box")
    page.screenshot(path=str(shots / f"graph-timeline-{tag}-1100.png"))
    # Tap the widest box at depth 0.
    xy = page.evaluate("""() => { const st = G.stepper, c = document.getElementById('graph_canvas').getBoundingClientRect();
        let best = -1, w = -1;
        for (let k = 0; k < st.fStart.length; k++) if (st.fDepth[k] === 0 && !hiddenFrame(st, k)) {
            const e = (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]) - Math.max(st.fStart[k], runStart()); if (e > w) { w = e; best = k; } }
        const a = Math.max(st.fStart[best], G.x0), b = Math.min(st.fEnd[best] < 0 ? st.steps : st.fEnd[best], G.x1);
        return [c.left + ((a + b) / 2 - G.x0) / (G.x1 - G.x0) * c.width, c.top + 26 + 6 + 8 - G.yOff, st.nodeName(st.fNode[best])]; }""")
    page.mouse.click(xy[0], xy[1])
    if not wait_for(lambda: page.get_attribute("#graph_info", "data-node") == xy[2], 3):
        problems.append(f"tapping the {xy[2]} box did not select it: {page.get_attribute('#graph_info', 'data-node')}")
    if name != "hello":
        timeline_views(page, problems, shots, tag, r)
    # Call tree.
    page.click("#gview_tree")
    wait_for(lambda: page.get_attribute("#graph_tree", "data-root-incl"), 3)
    r["tree_root"] = int(page.get_attribute("#graph_tree", "data-root-incl") or -1)
    if r["tree_root"] != from_entry:
        problems.append(f"call tree root {r['tree_root']} != from-entry count {from_entry}")
    r["tree_rows"] = page.locator("#graph_tree .trow").count()
    page.screenshot(path=str(shots / f"graph-tree-{tag}-1100.png"))
    page.fill("#graph_search", "start_kernel" if name != "hello" else "hello_main")
    time.sleep(0.3)
    if not page.locator("#graph_tree .trow[data-name='" + ("start_kernel" if name != "hello" else "hello_main") + "']").count():
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
    if name != "hello":
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


def determinism_case(browser, base):
    """Virtual clock: the same counts at different stepping speeds and on reload."""
    problems, runs = [], {}

    def run(q, timeout):
        page = browser.new_page()
        page.goto(f"{base}/index.html?{q}")
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), timeout, 0.5)
        c = counters(page)
        out = {k: (v.get("entry"), v.get("steps")) for k, v in c.items() if k != "now"}
        out["irq0"] = page.get_attribute("#vclock", "data-irq0")
        out["virtual"] = page.get_attribute("#vclock", "data-virtual")
        page.close()
        return out

    for q, t in (("kernel=hello", 60), ("kernel=hello&speed=full", 60),
                 ("kernel=minified-linux&speed=full", 120), ("kernel=minified-linux&speed=full", 120),
                 ("kernel=minified-linux&speed=1000000", 180)):
        runs.setdefault(q.split("&")[0], []).append((q, run(q, t)))
    for k, rs in runs.items():
        first = rs[0][1]
        if first.get("virtual") != "true":
            problems.append(f"{rs[0][0]}: clock not virtual")
        for q, r in rs[1:]:
            if r != first:
                problems.append(f"{k}: {rs[0][0]} {first} but {q} {r}")
    return problems, {k: rs[0][1] for k, rs in runs.items()}


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


def import_case(browser, base, shots):
    """Imported System.map / vmlinux give the same symbols as symbols.json."""
    problems, r = [], {}
    files = import_inputs()
    if not files:
        return ["no System.map/vmlinux to import"], r
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")

    def at_start_kernel(path):
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        if path:
            page.set_input_files("#symfile", str(path))
            wait_for(lambda: page.get_attribute("#sym_status", "data-state") in ("imported", "error"), 10)
            r[path.name + "_status"] = page.inner_text("#sym_status")
            if page.get_attribute("#sym_status", "data-state") != "imported":
                problems.append(f"{path.name} not imported: {page.inner_text('#sym_status')}")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("stepped", "paused", "jit"), 30)
        page.fill("#dbg_addr", hex(sk["vaddr"]))
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                      and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 60)
        cur = page.get_attribute("#cur_sym", "data-name") if ok else None
        listing_note = None
        if path:
            show_tab(page, "page")
            listing_note = page.inner_text("#dump_info")
            page.set_viewport_size({"width": 390, "height": 900})
            for t in ("memory", "page", "debugger"):
                show_tab(page, t)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{path.name}: page scrolls horizontally at 390 px on the {t} tab")
            page.screenshot(path=str(shots / f"import-{path.name}-390.png"))
        page.close()
        if errors:
            problems.append("page errors: " + "; ".join(errors))
        return cur, listing_note

    base_cur, _ = at_start_kernel(None)
    r["symbols.json"] = base_cur
    for path in files:
        cur, note = at_start_kernel(path)
        r[path.name] = cur
        if cur != base_cur or cur != "start_kernel":
            problems.append(f"{path.name}: current symbol at start_kernel is {cur!r}, symbols.json gives {base_cur!r}")
        # No disasm.json for imported symbols: the listing decodes guest memory.
        if not note or "decoded live" not in note:
            problems.append(f"{path.name}: the listing is not decoded live: {note!r}")
        # A counted full-speed boot with the imported symbols: the timeline has start_kernel, panic is found.
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
        page.evaluate("window.oldStepper = stepper")
        page.set_input_files("#symfile", str(path))
        wait_for(lambda: page.evaluate("stepper && stepper !== window.oldStepper && !!stepper.end"), 60)
        sk_frame = page.evaluate("""() => { const st = G.stepper; let best = null;
            for (let k = 0; k < st.fStart.length; k++) if (st.nodeName(st.fNode[k]) === 'start_kernel') {
                const incl = (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]) - st.fStart[k];
                if (!best || incl > best.incl) best = { incl, depth: st.fDepth[k] }; }
            return best && { ...best, total: st.steps - runStart() }; }""")
        r[path.name + "_start_kernel"] = sk_frame
        if not sk_frame or sk_frame["incl"] < sk_frame["total"] / 2:
            problems.append(f"{path.name}: start_kernel missing or small in the timeline: {sk_frame}")
        if page.get_attribute("#end_banner", "data-kind") != "panic":
            problems.append(f"{path.name}: panic not detected with imported symbols")
        page.close()
    # A malformed file: a clear error, the previous symbols stay.
    page = browser.new_page(viewport={"width": 390, "height": 900})
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    before = page.evaluate("symbols.symbols.length")
    bad = pathlib.Path(tempfile.mkdtemp()) / "notes.txt"
    bad.write_text("this is not a symbol map\nat all\nreally\n")
    page.set_input_files("#symfile", str(bad))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "error", 5)
    r["malformed"] = page.inner_text("#sym_status")
    if page.get_attribute("#sym_status", "data-state") != "error" or "previous symbols stay" not in r["malformed"]:
        problems.append("malformed file: no clear error: " + r["malformed"])
    if page.evaluate("symbols.symbols.length") != before or page.evaluate("!!symbols.imported"):
        problems.append("malformed file replaced the symbols")
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("the error message makes the page scroll horizontally at 390 px")
    page.screenshot(path=str(shots / "import-malformed-390.png"))
    # Debian's /boot/System.map-<version> placeholder: say where the real map is.
    deb = bad.parent / "System.map-5.10.0-30-686"
    deb.write_text("ffffffffffffffff B The real System.map is in the linux-image-<version>-dbg package\n")
    page.set_input_files("#symfile", str(deb))
    wait_for(lambda: "placeholder" in page.inner_text("#sym_status"), 5)
    r["debian_placeholder"] = page.inner_text("#sym_status")
    if "-dbg" not in r["debian_placeholder"] or page.evaluate("!!symbols.imported"):
        problems.append("Debian placeholder System.map: no pointer to the -dbg package: " + r["debian_placeholder"])
    page.close()
    return problems, r


def initrd_case(browser, base, shots):
    """An initrd reaches guest RAM at 64 MiB; RAM is raised; the map shows it."""
    problems, r = [], {}
    tmp = pathlib.Path(tempfile.mkdtemp())
    cpio = tmp / "scratch.cpio"
    # A newc cpio with one file and the trailer, built here.
    def entry(name, data, mode):
        n = name.encode() + b"\0"
        hdr = "070701" + "".join(f"{v:08x}" for v in (1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(n), 0))
        out = hdr.encode() + n
        out += b"\0" * (-len(out) % 4) + data
        return out + b"\0" * (-len(out) % 4)
    body = entry("init", b"#!/bin/sh\necho initrd-marker-1234\n", 0o100755) + entry("TRAILER!!!", b"", 0)
    cpio.write_bytes(body + b"\0" * (-len(body) % 512))
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("window.oldStepper = stepper")
    page.set_input_files("#initrd_file", str(cpio))
    # boot() resumes at the default speed; wait for the new machine.
    wait_for(lambda: page.evaluate("!!stepper && stepper !== window.oldStepper"), 60)
    page.click("#dbg_pause") if page.is_enabled("#dbg_pause") else None
    r["ram"] = int(page.eval_on_selector("#memory", "e => e.value"))
    if r["ram"] <= 64 << 20:
        problems.append(f"RAM not raised for the initrd: {r['ram']}")
    head = page.evaluate("Array.from(emulator.read_memory(64 << 20, 16))")
    if bytes(head) != cpio.read_bytes()[:16]:
        problems.append(f"initrd bytes not at 64 MiB in guest RAM: {bytes(head)!r}")
    r["note"] = page.inner_text("#initrd_status")
    if "ignore a bootloader initrd" not in r["note"]:
        problems.append("no note that our kernels ignore a bootloader initrd: " + r["note"])
    show_tab(page, "memory")
    if "initrd" not in page.inner_text("#map_all"):
        problems.append("the memory map does not show the initrd")
    if not page.evaluate("pageClass.includes('r-initrd')"):
        problems.append("no grid page is classed as initrd")
    # Too little RAM: the header check says so.
    page.select_option("#memory", str(32 << 20))
    r["problem"] = page.inner_text("#problems")
    if "64 MiB" not in r["problem"]:
        problems.append("no RAM error for the initrd at 32 MB: " + r["problem"])
    page.select_option("#memory", str(r["ram"]))
    page.set_viewport_size({"width": 390, "height": 900})
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("initrd controls make the page scroll horizontally at 390 px")
    page.screenshot(path=str(shots / "initrd-390.png"))
    page.click("#initrd_clear")
    if page.is_visible("#initrd_clear") or page.inner_text("#initrd_status"):
        problems.append("no initrd did not clear it")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def third_party_case(browser, base, shots, folder):
    """Optional (MKB_THIRD_PARTY=dir with vmlinuz + initramfs): a distro
    kernel with its initrd prints its boot log on the VGA console."""
    folder = pathlib.Path(folder)
    kernel = next(iter(sorted(folder.glob("vmlinuz*"))), None)
    rd = next(iter(sorted(folder.glob("initramfs*"))), None)
    if not kernel or not rd:
        return [f"no vmlinuz*/initramfs* in {folder}"], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0&cmdline=console%3Dtty0")
    wait_for(lambda: gate_on_screen(page), 60)
    page.set_input_files("#initrd_file", str(rd))
    page.set_input_files("#file", str(kernel))
    t0 = time.monotonic()
    ok = wait_for(lambda: "] " in page.inner_text("#screen_container") and "Linux" in page.inner_text("#screen_container")
                  or "[    " in page.inner_text("#screen_container"), 180, 1)
    r = {"kernel": kernel.name, "initrd": rd.name, "seconds": round(time.monotonic() - t0),
         "ram": page.eval_on_selector("#memory", "e => e.value"),
         "screen": page.inner_text("#screen_container").splitlines()[:3]}
    page.screenshot(path=str(shots / "third-party.png"))
    page.close()
    return ([] if ok else ["no kernel log on the screen"]), r


def panic_text_case(browser, base, shots):
    """'---[ end Kernel panic - not syncing: ... ]---' on the screen stops the machine."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return ["no kernel entry"], r
    r["build_info"] = page.inner_text("#build_info")
    if not r["build_info"]:
        problems.append("no build line in the header")
    # Write the line through the VGA MMIO handlers, as the guest would, on a
    # row past the first screen (vgacon scrolls through the 32 KiB window).
    page.evaluate("""() => { const cpu = emulator.v86.cpu;
        const t = "---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---";
        for (let i = 0; i < t.length; i++) { cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i, t.charCodeAt(i)); cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i + 1, 7); } }""")
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text"
                  and page.get_attribute("#dbg_state", "data-state") == "paused", 3, 0.1)
    r["banner"] = page.inner_text("#end_banner")
    if not ok or "VFS: Unable to mount root fs on unknown-block(0,0)" not in r["banner"]:
        problems.append("panic text did not stop the machine: " + r["banner"] + " / " + page.get_attribute("#dbg_state", "data-state"))
    page.screenshot(path=str(shots / "panic-text.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def distro_panic_case(browser, base, cache):
    """Optional ($MKB_DISTRO_CACHE from test_distro_symbols.py): Debian's 686
    kernel without an initrd panics; the page stops itself."""
    kernel = next(iter(sorted(pathlib.Path(cache).glob("boot/vmlinuz-*"))), None)
    if not kernel:
        return [f"no boot/vmlinuz-* in {cache}"], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0")
    wait_for(lambda: gate_on_screen(page), 60)
    page.fill("#cmdline", "console=tty0")
    t0 = time.monotonic()
    page.set_input_files("#file", str(kernel))
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text", 300, 1)
    r = {"seconds": round(time.monotonic() - t0), "banner": page.inner_text("#end_banner"),
         "state": page.get_attribute("#dbg_state", "data-state")}
    page.close()
    bad = [] if ok and r["state"] == "paused" and "Unable to mount root fs" in r["banner"] else [f"distro panic not caught: {r}"]
    return bad, r


def follow_case(browser, base, shots):
    """Slow motion, transposed timeline on a phone: "follow" keeps the
    running frame's column on screen; the cmdline default is shown."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 390, "height": 900})
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=100000#graph")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return ["no kernel entry"], r
    r["cmdline"] = page.input_value("#cmdline")
    if r["cmdline"] != "nokaslr cryptomgr.notests":
        problems.append(f"cmdline default is {r['cmdline']!r}")
    page.click("#graph_transpose")
    seen, rows = [], []
    for _ in range(12):
        time.sleep(0.5)
        v = page.get_attribute("#graph_canvas", "data-cur-visible")
        if v is not None:
            seen.append(v)
            rows.append(int(page.get_attribute("#graph_canvas", "data-cur-row") or 0))
    r["visible"], r["rows"] = seen, rows
    if not seen or any(v != "true" for v in seen):
        problems.append(f"the running frame left the view while following: {seen} rows {rows}")
    if not rows or max(rows) < 3:
        problems.append(f"stack never deep enough to need scrolling: rows {rows}")
    page.screenshot(path=str(shots / "follow-transposed-390.png"))
    page.close()
    # A bzImage from disk starts with the default command line too.
    page = browser.new_page(viewport={"width": 1100, "height": 900})
    page.goto(f"{base}/index.html?kernel=hello")
    wait_for(lambda: gate_on_screen(page), 60)
    page.set_input_files("#file", str(SITE / "kernels" / "hello" / "bzImage"))
    time.sleep(1)
    r["disk_cmdline"] = page.input_value("#cmdline")
    if r["disk_cmdline"] != "nokaslr cryptomgr.notests":
        problems.append(f"disk bzImage cmdline is {r['disk_cmdline']!r}")
    page.close()
    return problems, r


def debian_case(browser, base, shots, name):
    """The Debian sample: its defaults, the end-of-panic auto-stop with the
    message on the screen, and Run to start_kernel naming start_kernel."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("jit", "stepped"), 60)
    r["cmdline"] = page.input_value("#cmdline")
    r["ram"] = page.eval_on_selector("#memory", "e => e.value")
    r["label"] = page.eval_on_selector("#kernel", "e => e.selectedOptions[0].text")
    if r["cmdline"] != entry.get("cmdline") or str(entry.get("ram")) != r["ram"]:
        problems.append(f"Debian defaults not applied: {r}")
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text", 300, 1)
    r["seconds_to_panic"] = round(time.monotonic() - t0)
    r["banner"] = page.inner_text("#end_banner")
    screen = page.inner_text("#screen_container")
    if not ok or "Unable to mount root fs" not in r["banner"] or "end Kernel panic" not in screen:
        problems.append(f"no end-of-panic stop: {r['banner']!r}")
    page.screenshot(path=str(shots / "debian-panic.png"))
    # No disasm.json: the listing at start_kernel is decoded live.
    syms0 = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk0 = next(x for x in syms0 if x["name"] == "start_kernel")
    open_page(page, sk0["paddr"] // 4096)
    wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 15)
    open_page(page, sk0["paddr"] // 4096)
    got = listing_insns(page, sk0["paddr"], sk0["paddr"] + 64)
    r["listing_source"] = page.get_attribute("#listing", "data-source")
    r["start_kernel_insns"] = list(got.values())[:3]
    if r["listing_source"] != "live" or not got:
        problems.append(f"no live instructions at start_kernel: {r['listing_source']}")
    vmlinux = os.environ.get("MKB_DEBIAN_VMLINUX")
    if vmlinux and got:
        # The first 5 bytes are the ftrace call the kernel patches to a NOP
        # at boot, so compare from the second instruction on.
        dump = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={sk0['vaddr']:#x}",
                               f"--stop-address={sk0['vaddr'] + 32:#x}", vmlinux], capture_output=True, text=True).stdout
        want = [norm_insn(l.split(":", 1)[1]) for l in dump.splitlines() if re.match(r"^\s*[0-9a-f]+:", l)]
        live = [got[a] for a in sorted(got)]
        r["objdump"], r["live"] = want[1:3], live[1:3]
        if want[1:3] != live[1:3]:
            problems.append(f"live decode {live[1:3]} != llvm-objdump {want[1:3]}")
    page.close()
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    t0 = time.monotonic()
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 900, 2)
    r["runto_seconds"] = round(time.monotonic() - t0)
    r["runto_symbol"] = page.get_attribute("#cur_sym", "data-name")
    r["context_source"] = page.get_attribute("#context", "data-source")
    if r["context_source"] != "live" or page.locator("#context .ins.cur").count() != 1:
        problems.append(f"Debugger code window at EIP: source {r['context_source']!r}, boxed lines {page.locator('#context .ins.cur').count()}")
    if not ok or r["runto_symbol"] != "start_kernel":
        problems.append(f"Run to start_kernel: {r['runto_symbol']!r} ({page.inner_text('#dbg_status')})")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def norm_insn(text):
    """Instruction text without addresses' symbol names, comments and spacing."""
    text = re.sub(r"<[^>]*>|#.*$", "", text.lower())
    text = re.sub(r"\s+", " ", text).strip().replace(", ", ",")
    # Numbers modulo formatting: -0x3fef0834 and 0xc010f7cc, 9 and 0x9 are equal.
    return re.sub(r"-?(?:0x[0-9a-f]+|\b\d+\b)", lambda m: hex(int(m.group(0), 0) & 0xffffffff), text)


def listing_insns(page, lo, hi):
    """{paddr: normalized instruction} of the Page listing's lines in [lo, hi)."""
    out = {}
    for addr, text in page.evaluate("""() => [...document.querySelectorAll('#listing .ins')].map(e =>
            [Number(e.dataset.addr), e.textContent])"""):
        if lo <= addr < hi:
            # "addr bytes... insn [symbol+off]": drop the address, bytes and symbol+offset
            body = re.sub(r"^[0-9a-f]{8} +(?:[0-9a-f]{2})+\u2026? *", "", text.strip())
            body = re.sub(r" [A-Za-z_][\w.]*\+0x[0-9a-f]+$", "", body)
            out[addr] = norm_insn(body)
    return out


def livedecode_case(browser, base, shots):
    """Live decoding (iced-x86) agrees with disasm.json on start_kernel."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
    open_page(page, sk["paddr"] // 4096)
    hi = min(sk["paddr"] + sk["size"], (sk["paddr"] // 4096 + 1) * 4096)
    built = listing_insns(page, sk["paddr"], hi)
    page.check("#live_decode")
    wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
    live = listing_insns(page, sk["paddr"], hi)
    same = [a for a in built if live.get(a) == built[a]]
    r["build"], r["live"], r["same"] = len(built), len(live), len(same)
    r["differ"] = [(hex(a), built[a], live.get(a)) for a in built if live.get(a) != built[a]][:5]
    if not built or len(same) < 0.95 * len(built):
        problems.append(f"live decode disagrees with disasm.json on start_kernel: {r}")
    page.screenshot(path=str(shots / "live-decode.png"))
    page.close()
    return problems, r


def touch_case(browser, base, shots):
    """390 px phone with touch: tapping a "?" shows its text, tapping elsewhere hides it."""
    problems = []
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=hello#memory")
    wait_for(lambda: gate_on_screen(page), 60)
    if not page.evaluate("matchMedia('(pointer: coarse)').matches"):
        problems.append("touch emulation does not report pointer: coarse")
    tips = page.locator(".tip")
    if tips.count() < 10:
        problems.append(f"only {tips.count()} ? markers")
    if page.locator(".tip[title]").count():
        problems.append("a ? marker still relies on a title attribute")
    size = page.evaluate("""() => { const s = getComputedStyle(document.querySelector('.tip'), '::after');
        return [parseFloat(s.width), parseFloat(s.height)]; }""")
    if min(size) < 32:
        problems.append(f"? tap target is {size} px")
    for sel in ("#panel_memory .tip", "#count_info .tip"):
        tip = page.locator(sel).first
        tip.scroll_into_view_if_needed()
        tip.tap()
        if not wait_for(lambda: page.is_visible("#tip_pop"), 2):
            problems.append(f"tapping {sel} showed no popover")
            continue
        text = page.inner_text("#tip_pop")
        if len(text) < 20:
            problems.append(f"popover of {sel} is empty: {text!r}")
        page.screenshot(path=str(shots / "touch-tip-390.png"))
        page.tap("h1")
        if not wait_for(lambda: not page.is_visible("#tip_pop"), 2):
            problems.append(f"tapping elsewhere did not close the popover of {sel}")
    # Inside a label the ? must not open the control (file chooser).
    choosers = []
    page.on("filechooser", lambda fc: choosers.append(fc))
    page.locator("label:has(#file) .tip").tap()
    time.sleep(1)
    if choosers:
        problems.append("tapping the From disk ? opened the file chooser")
    if not page.is_visible("#tip_pop"):
        problems.append("the From disk ? showed no popover")
    page.keyboard.press("Escape")
    if page.is_visible("#tip_pop"):
        problems.append("Esc did not close the popover")
    # Every tab fits on screen (the bar wraps) and is a 32 px target.
    show_tab(page, "memory")
    for b in page.locator(".tabbar button").all():
        box = b.bounding_box()
        if box["x"] < 0 or box["x"] + box["width"] > 390 + 0.5 or box["height"] < 32:
            problems.append(f"tab {b.inner_text()!r} at {box}")
    page.locator(".tabbar button").last.tap()
    if not page.is_visible("#panel_about"):
        problems.append("tapping the last tab did not show its panel")
    # A touch swipe outside the graph scrolls the page.
    show_tab(page, "memory")
    page.evaluate("window.scrollTo(0, 0)")
    cdp = ctx.new_cdp_session(page)
    cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": 200, "y": 700}]})
    for i in range(1, 13):
        cdp.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": 200, "y": 700 - 400 * i / 12}]})
        time.sleep(0.016)
    cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
    time.sleep(0.5)
    if page.evaluate("window.scrollY") < 100:
        problems.append(f"a swipe scrolled the page by {page.evaluate('window.scrollY')} px")
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    ctx.close()
    return problems


STAMPED = ("style.css", "clock.js", "app.js", "stepper.js", "graph.js", "source.js", "symimport.js", "livedis.js", "v86/libv86.js")


def stamp(html, v):
    """What the workflow's sed does to index.html: asset?v=<commit>."""
    for f in STAMPED:
        # The workflow has stamped index.html already when this runs in CI.
        html = re.sub(r'(href|src)="%s(\?v=[^"]*)?"' % re.escape(f), r'\1="%s?v=%s"' % (f, v), html)
    return html


def stale_cache_case(browser, timeout):
    """Deploy A, load it, deploy B over it, reload with A in the HTTP cache.

    The server sends max-age=600 like GitHub Pages. Deploy B's app.js sets a
    marker; after the reload the page must run B's app.js (not A's from the
    cache), boot, and raise no errors.
    """
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        for d in ("kernels", "v86"):
            (root / d).symlink_to(SITE / d)
        for f in ("kernels.json", "style.css") + tuple(SITE.glob("*.js")):
            f = pathlib.Path(f)
            shutil.copy(SITE / f.name, root / f.name)
        html = (SITE / "index.html").read_text()
        app = (SITE / "app.js").read_text()

        def deploy(v):
            (root / "index.html").write_text(stamp(html, v))
            (root / "app.js").write_text(app + f'\nwindow.deployMarker = "{v}";\n')

        deploy("A")
        httpd = serve(root, CachingHandler)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        ctx = browser.new_context()
        page = ctx.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(base + "/index.html?kernel=hello")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append("deploy A did not boot")
        deploy("B")
        page.reload()
        marker = page.evaluate("window.deployMarker")
        stamp_v = page.evaluate("new URL(document.querySelector('script[src*=\"app.js\"]').src).searchParams.get('v')")
        if marker != "B" or stamp_v != "B":
            problems.append(f"after the reload index.html is deploy {stamp_v} but app.js is deploy {marker}")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append("deploy B did not boot after the reload")
        if errors:
            problems.append("page errors: " + "; ".join(errors))
        ctx.close()
        httpd.shutdown()
    return problems


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
        want = "halt" if name == "hello" else "panic"
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
        if "sources" in kernel_entries().get(name, {}):
            count["source"] = source_check(page, name, lab_problems, shots, tag)
        if speed is None and name == "hello" and (count.get(HELLO) or {}).get("entry") != 86:
            lab_problems.append(f"hello: {count.get(HELLO)} instructions from the entry to Hello, not 86")
        page.screenshot(path=str(shots / f"top-{tag}-1100.png"))
    show_tab(page, "memory")
    if not page.is_disabled("#scan"):
        page.click("#scan")
        nonzero = int(page.get_attribute("#footprint", "data-nonzero"))
        total = int(page.get_attribute("#footprint", "data-total"))
        if ok:
            lab = memory_lab(page, name, has_symbols, lab_problems, shots, tag)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*", help="NAME or NAME:MEMBYTES")
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--probe", action="store_true", help="report, do not assert")
    ap.add_argument("--shots", default=str(SITE / "test-screenshots"))
    args = ap.parse_args()

    cases = []
    for c in args.cases:
        name, mem, speed = (c.split(":") + ["", ""])[:3]
        cases.append((name, int(mem) if mem else None, speed or None))
    cases = cases or default_cases()
    shots = pathlib.Path(args.shots)
    shots.mkdir(parents=True, exist_ok=True)

    httpd = serve()
    base = f"http://127.0.0.1:{httpd.server_address[1]}"
    failed = False
    entries = kernel_entries()
    with sync_playwright() as p:
        browser = p.chromium.launch()
        at2mb = {}
        for name, mem, speed in cases:
            has_symbols = "symbols" in entries.get(name, {})
            r = run_case(browser, base, name, mem, args.timeout, shots, has_symbols, speed)
            print(json.dumps(r))
            good = (r["ok"] and r["nonzero_pages"] and 0 < r["nonzero_pages"] < r["total_pages"]
                    and not r["lab_problems"] and not r["page_errors"])
            if not good:
                failed = True
            if mem is None:
                at2mb[name] = r["counters"]
        if not args.cases:
            h = (at2mb.get("hello") or {}).get(HELLO) or {}
            k = (at2mb.get("minified-linux") or {}).get(HELLO) or {}
            if k and not (k.get("entry") or 0) > (h.get("entry") or 0):
                print("FAIL: Linux needs no more instructions than hello", h, k)
                failed = True
            if "minified-linux" in at2mb:
                lp, lr = linux_slow_case(browser, base, shots)
                print(json.dumps({"linux_slow": lp or "ok", **lr}))
                if lp:
                    failed = True
                sp, sr = symbol_case(browser, base, "minified-linux", shots)
                print(json.dumps({"symbols": sp or "ok", **sr}))
                if sp:
                    failed = True
            for name in at2mb:
                dp, dr = debugger_case(browser, base, name, None, shots)
                print(json.dumps({"debugger": name, "problems": dp, **dr}))
                if dp:
                    failed = True
        if not args.cases and "minified-linux" in entries:
            ip, ir = import_case(browser, base, shots)
            print(json.dumps({"import": ip or "ok", **{k: str(v) for k, v in ir.items()}}))
            if ip:
                failed = True
            np_, nr = initrd_case(browser, base, shots)
            print(json.dumps({"initrd": np_ or "ok", **{k: str(v) for k, v in nr.items()}}))
            if np_:
                failed = True
            if os.environ.get("MKB_THIRD_PARTY"):
                tp, tr = third_party_case(browser, base, shots, os.environ["MKB_THIRD_PARTY"])
                print(json.dumps({"third_party": tp or "ok", **{k: str(v) for k, v in tr.items()}}))
                if tp:
                    failed = True
            pp, pr = panic_text_case(browser, base, shots)
            print(json.dumps({"panic_text": pp or "ok", **pr}))
            if pp:
                failed = True
            if os.environ.get("MKB_DISTRO_CACHE"):
                xp, xr = distro_panic_case(browser, base, os.environ["MKB_DISTRO_CACHE"])
                print(json.dumps({"distro_panic": xp or "ok", **xr}))
                if xp:
                    failed = True
            dp, dr = determinism_case(browser, base)
            print(json.dumps({"determinism": dp or "ok", **{k: str(v) for k, v in dr.items()}}))
            if dp:
                failed = True
        if not args.cases and "minified-linux" in entries:
            fp, fr = follow_case(browser, base, shots)
            print(json.dumps({"follow": fp or "ok", **{k: str(v) for k, v in fr.items()}}))
            if fp:
                failed = True
        for name in [n for n in entries if n.startswith("debian")] if not args.cases else []:
            bp, br = debian_case(browser, base, shots, name)
            print(json.dumps({"debian": bp or "ok", **{k: str(v) for k, v in br.items()}}))
            if bp:
                failed = True
        if not args.cases and "minified-linux" in entries:
            lp, lr = livedecode_case(browser, base, shots)
            print(json.dumps({"live_decode": lp or "ok", **{k: str(v) for k, v in lr.items()}}))
            if lp:
                failed = True
        if not args.cases:
            touch = touch_case(browser, base, shots)
            print(json.dumps({"touch": touch or "ok"}))
            if touch:
                failed = True
            stale = stale_cache_case(browser, args.timeout)
            print(json.dumps({"stale_cache": stale or "ok"}))
            if stale:
                failed = True
        browser.close()
    httpd.shutdown()
    if failed and not args.probe:
        print("FAIL", file=sys.stderr)
        return 1
    print("done" if args.probe else "PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
