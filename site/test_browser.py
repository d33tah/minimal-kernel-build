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
case for the "?" popovers, a coverage case (never-executed
instructions tinted in the Page listing) and a stale-cache case (two deploys with
max-age=600). See README.md, Testing.

Usage:
  site/test_browser.py                      all of the above
  site/test_browser.py --suites debian,dwarf  some suites (SUITES below; CI runs them in parallel jobs)
  site/test_browser.py hello:1048576 minified-linux:18874368
  site/test_browser.py --probe hello:655360 report only, never fail

Needs: pip install playwright && playwright install chromium
"""

import argparse
import functools
import gzip
import http.server
import json
import os
import pathlib
import re
import shutil
import subprocess
import socketserver
import sys
import tarfile
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
    r["real_speed"] = int(page.get_attribute("#vclock", "data-speed") or 0)
    if not 500 <= r["real_speed"] <= 2000:
        problems.append(f"measured speed at 1k/s is {r['real_speed']}/s")
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
        if "start_kernel" not in names[:6]:
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
        return [c.left + ((a + b) / 2 - G.x0) / (G.x1 - G.x0) * c.width, c.top + G.areaTop + timelineLayout(st).row[best] * 17 + 8 - G.yOff, st.nodeName(st.fNode[best])]; }""")
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
                 ("kernel=minified-linux&speed=full&fastforward=0", 120),
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
    if "ignores a bootloader initrd" not in r["note"]:
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
    seen, rows, scrolled = [], [], 0
    for _ in range(12):
        time.sleep(0.5)
        v = page.get_attribute("#graph_canvas", "data-cur-visible")
        if v is not None:
            seen.append(v)
            rows.append(int(page.get_attribute("#graph_canvas", "data-cur-row") or 0))
            scrolled = max(scrolled, page.evaluate("G.xOff"))
    r["visible"], r["rows"], r["scrolled_px"] = seen, rows, scrolled
    if not seen or any(v != "true" for v in seen):
        problems.append(f"the running frame left the view while following: {seen} rows {rows}")
    # At 390 px, three 120 px columns fit; a call in column 2 or deeper is
    # outside the band and must have scrolled the view.
    if not rows or max(rows) < 2 or scrolled <= 0:
        problems.append(f"following never had to scroll: rows {rows}, scrolled {scrolled}px")
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


TASKS_JS = """() => { const st = stepper, s0 = st.sinceEntry, n = st.fStart.length, name = (k) => st.nodeName(st.fNode[k]);
    const iv = [], bySym = {}, perTask = {}; let roots = 0, cont = 0;
    for (let k = 0; k < n; k++) if (st.fParent[k] < 0 && st.fStart[k] >= s0) {
        iv.push([st.fStart[k], st.fEnd[k] < 0 ? st.steps : st.fEnd[k]]);
        if (st.fCont[k]) { cont++; continue; }
        if (st.fIrq[k]) continue;
        roots++; bySym[name(k)] = (bySym[name(k)] || 0) + 1; perTask[st.fTask[k]] = (perTask[st.fTask[k]] || 0) + 1; }
    iv.sort((a, b) => a[0] - b[0]); let last = s0, overlap = 0;
    for (const [a, b] of iv) { if (a < last) overlap += Math.min(b, last) - a; if (b > last) last = b; }
    const te = []; for (let k = 0; k < n; k++) if (name(k) === '__trace_early_add_event_dirs') {
        let under = false; for (let p = st.fParent[k]; p >= 0; p = st.fParent[p]) if (name(p) === 'event_trace_init') under = true;
        te.push(under); }
    return { tasks: st.taskCount, roots, contRoots: cont, overlap, maxRootsPerTask: Math.max(0, ...Object.values(perTask)),
        sched: ['__schedule', '__switch_to', '__switch_to_asm', '__cond_resched', 'schedule'].filter((x) => bySym[x]),
        traceEarly: te.length, traceEarlyOrphans: te.filter((u) => !u).length,
        switches: st.swStep.length, visibleSwitches: st.swStep.filter((t, i) => !(G.fold && st.swIrq[i])).length,
        markers: Number(document.getElementById('graph_canvas').dataset.switchMarkers),
        labels: Number(document.getElementById('graph_canvas').dataset.switchLabels),
        rows: G.layout.row.reduce((a, b) => Math.max(a, b), 0) + 1, maxDepth: st.fDepth.reduce((a, b) => Math.max(a, b), 0) }; }"""


# Covered pixel columns per timeline row: the whole run at once vs the
# same run as 8 windows at 8x zoom, folded back to the coarse columns.
COVER_JS = """() => { const st = G.stepper, s0 = runStart(), s1 = st.steps, span = s1 - s0;
    const saved = [G.x0, G.x1, G.userMoved, G.follow]; G.follow = false; G.userMoved = true;
    G.x0 = s0; G.x1 = s1; drawTimeline();
    const one = new Map(); let W = 0;
    for (const [r, c] of G.cover) { W = c.length; one.set(r, c.reduce((a, b) => a + b, 0)); }
    const coarse = new Map();
    for (let i = 0; i < 8; i++) {
        G.x0 = s0 + span * i / 8; G.x1 = s0 + span * (i + 1) / 8; drawTimeline();
        for (const [r, c] of G.cover) {
            if (!coarse.has(r)) coarse.set(r, new Uint8Array(W));
            const m = coarse.get(r);
            for (let col = 0; col < c.length - 1; col++) if (c[col]) m[Math.min(W - 1, Math.floor((i * (c.length - 1) + col) / 8))] = 1; } }
    [G.x0, G.x1, G.userMoved, G.follow] = saved; G.dirty = true; drawTimeline();
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
TASK_PAGE_JS = """() => { const st = stepper, info = taskInfo(st);
    return { cur: st.task, region: st.taskRegion.slice(), alive: info.map((x, t) => t === st.task || st.taskOf.get(st.taskRegion[t]) === t),
             tasks: info.map((x) => ({ name: x.name, how: x.how, guess: !!x.guess, comm: x.comm === undefined ? null : x.comm,
                                       pid: x.pid === undefined ? null : x.pid, va: x.va === undefined ? null : x.va })),
             off: varsOffset(), types: dbgTypes().state }; }"""


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
    return bytes(page.evaluate("([pa, n]) => Array.from(emulator.read_memory(pa, n))", [pa, n]))


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
    if not wait_for(lambda: page.evaluate("dbgTypes().state") == "ok", 60):
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
    page.click("#tab_graph")
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


def tasks_case(browser, base, shots, name):
    """Task switches on a full counted run: a resumed task continues its
    suspended calls (no scheduler code as a root, no orphaned calls), one
    root per task, and the roots never overlap; tasks are named from their
    task_struct (task_names_check)."""
    problems = []
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel={name}&speed=full#graph")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 900, 2):
        return ["no end of the run"], {}
    time.sleep(2)
    show_canvas(page)
    time.sleep(1)
    r = page.evaluate(TASKS_JS)
    page.screenshot(path=str(shots / f"graph-tasks-{name}.png"))
    cp, cov = coverage_problems(page, name)
    problems += cp
    r["coverage_rows"] = len(cov["rows"])
    r["coverage_1x_8x"] = [row[1:] for row in cov["rows"][:6]]
    problems += task_names_check(page, name, shots, r)
    page.close()
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


def initramfs_case(browser, base, shots, name):
    """A kernel shipped with its initrd (the Debian sample with Debian's
    initramfs) boots to the "(initramfs)" BusyBox shell, which takes
    keyboard input; an initrd for another kernel gets a warning."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0")
    ok = wait_for(lambda: "(initramfs)" in page.inner_text("#screen_container"), 300, 1)
    r["seconds_to_shell"] = round(time.monotonic() - t0)
    r["note"] = page.inner_text("#initrd_status")
    r["ram"] = page.eval_on_selector("#memory", "e => e.value")
    if not ok:
        problems.append("no (initramfs) shell: " + page.inner_text("#screen_container")[-300:])
    elif "matches the kernel" not in r["note"]:
        problems.append("the initrd note does not say it matches: " + r["note"])
    if ok:
        # The prompt pauses the VM: overlay with the reason, a counter row.
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        r["state"] = page.get_attribute("#dbg_state", "data-state")
        r["overlay"] = page.inner_text("#vm_overlay") if page.is_visible("#vm_overlay") else None
        r["prompt_row"] = page.get_attribute("#counters tr[data-milestone='prompt']", "data-steps") \
            if page.locator("#counters tr[data-milestone='prompt']").count() else None
        if r["state"] != "paused" or not r["overlay"] or "VM paused" not in r["overlay"] or \
                "(initramfs) prompt" not in r["overlay"] or not page.is_visible("#vm_continue"):
            problems.append(f"no pause with the overlay at the prompt: {r['state']} {r['overlay']!r}")
        if not r["prompt_row"]:
            problems.append("no (initramfs) prompt row in the counters")
        page.set_viewport_size({"width": 390, "height": 900})
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        time.sleep(0.5)
        page.screenshot(path=str(shots / f"initramfs-paused-{name}-390.png"))
        page.set_viewport_size({"width": 1100, "height": 1000})
        page.click("#vm_continue")
        if not wait_for(lambda: not page.is_visible("#vm_overlay"), 10):
            problems.append("the overlay stays after Continue")
        page.click("#screen_container")
        time.sleep(0.5)
        # Unshifted characters only: Playwright's type() sends no Shift.
        page.keyboard.type("uname -m; echo hi")
        page.keyboard.press("Enter")
        lines = lambda: [l.strip() for l in page.inner_text("#screen_container").splitlines()]
        if not wait_for(lambda: "hi" in lines(), 60):
            problems.append("the shell did not run a typed command")
        r["uname"] = [l for l in lines() if l in ("i686", "i586", "i386")]
    page.screenshot(path=str(shots / f"initramfs-{name}.png"))
    # An initrd for another kernel: gzip'd newc cpio with lib/modules of
    # 6.12 amd64 and a 64-bit ELF /bin/busybox, named like Debian's.
    def entry(fname, data, mode):
        n = fname.encode() + b"\0"
        hdr = "070701" + "".join(f"{v:08x}" for v in (1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(n), 0))
        out = hdr.encode() + n
        out += b"\0" * (-len(out) % 4) + data
        return out + b"\0" * (-len(out) % 4)
    body = (entry("lib/modules/6.12.111+deb13-amd64", b"", 0o40755) +
            entry("bin/busybox", b"\x7fELF\x02\x01\x01" + b"\0" * 57, 0o100755) + entry("TRAILER!!!", b"", 0))
    tmp = pathlib.Path(tempfile.mkdtemp()) / "initrd.img-6.12.111+deb13-amd64"
    tmp.write_bytes(gzip.compress(body))
    page.set_input_files("#initrd_file", str(tmp))
    wait_for(lambda: "Warning" in page.inner_text("#initrd_status"), 20)
    r["mismatch_note"] = page.inner_text("#initrd_status")
    if not all(w in r["mismatch_note"] for w in ("Warning", "6.12.111+deb13-amd64", "64-bit", "6.1.0-50-686", "cannot run")):
        problems.append("no mismatch warning: " + r["mismatch_note"])
    if errors:
        problems.append(f"page errors: {errors}")
    page.close()
    return problems, r


def cpu_state_case(browser, base, shots, name):
    """The Debugger's machine-state sections at the end of a boot: CR4,
    GDTR, IDTR and TR set by the kernel, the GDT's kernel code segment
    (base 0, limit 4 GiB), IDT gate 0x80 at entry_INT80_32, the master
    PIC's IMR equal to the kernel's cached_irq_mask, PIT channel 0 at
    HZ=250 (reload 4773), and the translate box mapping start_kernel's
    virtual address to its physical one; no horizontal scroll at 390 px."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&speed=full#debugger")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 300, 1):
        return ["no end of the boot"], r
    page.evaluate("document.querySelectorAll('#cpu_state details').forEach((d) => d.open = true)")
    time.sleep(0.5)
    row = lambda sec, reg: page.inner_text(f"#cpu_{sec} [data-reg='{reg}']") \
        if page.locator(f"#cpu_{sec} [data-reg='{reg}']").count() else ""
    for sec, reg in (("control", "CR4"), ("tables", "GDTR"), ("tables", "IDTR"), ("tables", "TR")):
        r[reg] = row(sec, reg)
    if r["CR4"].split()[1:2] == ["0x00000000"]:
        problems.append("CR4 is 0: " + r["CR4"])
    for reg in ("GDTR", "IDTR", "TR"):
        if "base 0x00000000" in r[reg] or not r[reg]:
            problems.append(f"{reg} not set: {r[reg]!r}")
    page.click("#view_gdt")
    gdt = page.inner_text("#cpu_table")
    r["gdt_0x60"] = next((l for l in gdt.splitlines() if l.startswith("0x0060")), "")
    if not all(w in r["gdt_0x60"] for w in ("code", "base 0x00000000", "limit 0xffffffff", "32-bit")):
        problems.append("GDT entry 0x60 is not the flat 32-bit kernel code segment: " + r["gdt_0x60"])
    page.click("#view_idt")
    r["idt_0x80"] = next((l for l in page.inner_text("#cpu_table").splitlines() if l.startswith("0x80")), "")
    if "entry_INT80_32" not in r["idt_0x80"]:
        problems.append("IDT gate 0x80 does not point at entry_INT80_32: " + r["idt_0x80"])
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    cim = next((x for x in syms if x["name"] == "cached_irq_mask"), None)
    r["pic"] = row("irq", "PIC master")
    m = re.search(r"IMR 0x([0-9a-f]{2})", r["pic"])
    if cim and m:
        cached = page.evaluate(f"stepper.cpu.mem8[{cim['paddr']}]")
        r["imr"], r["cached_master_mask"] = m.group(1), f"{cached:02x}"
        if int(m.group(1), 16) != cached:
            problems.append(f"PIC IMR 0x{m.group(1)} != the kernel's cached_irq_mask 0x{cached:02x}")
    else:
        problems.append(f"no PIC IMR or cached_irq_mask: {r['pic']!r}")
    r["pit"] = row("irq", "PIT ch0")
    if "reload 4773 " not in r["pit"]:
        problems.append("PIT channel 0 is not at HZ=250: " + r["pit"])
    sk = next(x for x in syms if x["name"] == "start_kernel")
    page.fill("#xlate_in", hex(sk["vaddr"]))
    page.click("#xlate_go")
    r["translate"] = page.inner_text("#xlate_out").splitlines()[-2:]
    if f"physical 0x{sk['paddr']:08x}" not in page.inner_text("#xlate_out"):
        problems.append(f"translate {hex(sk['vaddr'])} did not give {hex(sk['paddr'])}: {r['translate']}")
    page.screenshot(path=str(shots / f"cpu-state-{name}-1280.png"), full_page=True)
    page.set_viewport_size({"width": 390, "height": 900})
    time.sleep(0.5)
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("horizontal scroll at 390 px with the machine state open")
    page.eval_on_selector("#cpu_state", "e => e.scrollIntoView()")
    page.screenshot(path=str(shots / f"cpu-state-{name}-390.png"))
    if errors:
        problems.append(f"page errors: {errors}")
    page.close()
    return problems, r


def runto_case(browser, base, shots):
    """Run to rest_init on the minified Linux stops there with no fault,
    with and without fast-forward. The fast Run to used to write its HLT
    breakpoint into the bzImage payload still lying at rest_init's
    physical address (the minified Linux's vmlinux runs where its bzImage
    is loaded), which ended halted in exc_page_fault after 72,644
    instructions."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    ri = next(x for x in syms if x["name"] == "rest_init")
    for ff in ("", "&fastforward=0"):
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.goto(f"{base}/index.html?kernel=minified-linux&paused=1{ff}#debugger")
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
            problems.append(f"did not start paused ({ff or 'fast'})")
            page.close()
            continue
        page.fill("#dbg_addr", hex(ri["vaddr"]))
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                      and page.get_attribute("#regs", "data-phys") == str(ri["paddr"]), 180)
        status = page.inner_text("#dbg_status")
        r[ff or "fast"] = status
        if not ok or "Halted" in status:
            problems.append(f"Run to rest_init ({ff or 'fast'}): {status}")
        page.close()
    return problems, r


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
    from test_dwarf import symbolize
    return symbolize(str(elf), vaddrs, [])


def source_rows(page):
    """EIP, state and frames of the Debugger's source rows, once loaded."""
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
    get = lambda: page.evaluate("""() => [...document.querySelectorAll('#cur_src .frame')].map(f => {
        const t = f.querySelector('.srctext'); return t ? t.textContent.trim() : ''; })""")
    wait_for(lambda: "reading " not in (page.inner_text("#cur_src") if page.locator("#cur_src").count() else "") and
             not any("reading " in (b or "") for b in page.eval_on_selector_all("#cur_src button", "els => els.map(e => e.title)")), 20)
    return get()


def text_check(page, name, samples, texts, entry_fn, problems, r):
    """The C text shown with the source rows is the kernel's: the entry line
    of entry_fn or one of the ten before it (the first row can be the "{" or
    the first statement after a comment) names it, and the call line of an inlined function mostly names that
    function (macros and multi-line calls do not)."""
    rows = sum(1 for t in texts for x in t if x)
    calls = [(t[k], fr[k - 1]["func"]) for (_, st, fr), t in zip(samples, texts) if st == "ok"
             for k in range(1, min(len(fr), len(t))) if fr[k]["line"]]
    named = sum(1 for text, fn in calls if fn and fn in text)
    r["text_rows"] = rows
    r["call_lines_naming_callee"] = f"{named}/{len(calls)}"
    f0 = samples[0][2][0] if samples and samples[0][2] else None
    r["entry_text"] = page.evaluate("(f) => [...Array(11).keys()].map((k) => srcText(f.file, f.line - 10 + k))", f0) if f0 else None
    if not r["entry_text"] or not texts[0] or texts[0][0] != (r["entry_text"][-1] or "").strip() or \
            not any(entry_fn + "(" in (t or "") for t in r["entry_text"]):
        problems.append(f"{name}: the C line at the entry of {entry_fn} is {r['entry_text']!r}")
    if calls and named * 2 < len(calls):
        problems.append(f"{name}: call lines name the inlined function in only {named} of {len(calls)}")


def dwarf_check(page, name, offset, problems, r, steps=DWARF_STEPS, entry_fn=None):
    """At EIP and after each Step: the Debugger's source rows (file:line,
    inlined-at chain) equal llvm-symbolizer --inlines on the kernel's -g ELF;
    the call stack names frames with file:line in their tooltips. With
    entry_fn (the C source is there): text_check on the rows' C text."""
    samples, texts = [], []
    for n in steps:
        if n:
            before = page.evaluate("stepper.steps")
            page.fill("#dbg_n", str(n))
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("stepper.steps") - before >= n and
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
    # Graph tab: a frame's hover text has where it was called from.
    t = page.evaluate("""() => { const st = G.stepper; const k = st.stack.length ? st.stack[st.stack.length - 1] : -1;
        return k >= 0 ? frameTitle(st, k) : null; }""")
    r["frame_title"] = t
    if not t or "called from 0x" not in t or not re.search(r"\.[chS]:\d+", t):
        problems.append(f"{name}: flame frame tooltip without DWARF locations: {t!r}")
    return samples


# Per view: [selector of the element holding a C location, needs C text too].
SRC_COLOUR_VIEWS = {
    "Page listing": ("#listing .src:not(.nol)", True),
    "Debugger listing": ("#context .src:not(.nol)", True),
    "Debugger source row": ("#cur_src .frame", True),
    "Source panel": ("#src_body > span", True),
    "Debugger inlined-into row": ("#src_colour_probe .frame.inl", False),
    "call stack": ("#call_stack", False),
    "Graph detail": ("#graph_src", False),
}

# The colours of a view's first .srcloc and .srctext, and the opaque
# background behind each (semi-transparent ancestors composited over it).
SRC_COLOURS_JS = """([sel, text]) => {
    const rgba = (c) => { const m = c.match(/[\\d.]+/g).map(Number); return [m[0], m[1], m[2], m.length > 3 ? m[3] : 1]; };
    const bg = (el) => { const layers = [];
        for (let e = el; e; e = e.parentElement) { const c = rgba(getComputedStyle(e).backgroundColor);
            if (c[3] > 0) { layers.push(c); if (c[3] >= 1) break; } }
        if (!layers.length || layers[layers.length - 1][3] < 1) layers.push(rgba(getComputedStyle(document.body).backgroundColor));
        let out = layers.pop().slice(0, 3);
        while (layers.length) { const [r, g, b, a] = layers.pop(); out = [r * a + out[0] * (1 - a), g * a + out[1] * (1 - a), b * a + out[2] * (1 - a)]; }
        return out; };
    const box = [...document.querySelectorAll(sel)].find((e) => e.querySelector('.srcloc') && (!text || e.querySelector('.srctext')));
    if (!box) return null;
    const one = (cls) => { const e = box.querySelector(cls); if (!e) return null; const c = rgba(getComputedStyle(e).color);
        let o = 1; for (let x = e; x; x = x.parentElement) o *= Number(getComputedStyle(x).opacity);
        return { colour: c.slice(0, 3), alpha: c[3] * o, bg: bg(e), text: e.textContent.trim().slice(0, 40) }; };
    return { loc: one('.srcloc'), text: one('.srctext') };
}"""


def contrast(fg, bg, alpha=1):
    """WCAG contrast ratio of fg (alpha over bg) against bg, both [r, g, b] 0-255."""
    fg = [f * alpha + b * (1 - alpha) for f, b in zip(fg, bg)]

    def lum(c):
        c = [x / 255 for x in c]
        c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    hi, lo = sorted((lum(fg), lum(bg)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def src_colour_check(page, name, phys, inl_phys, problems, r, shots):
    """Every view showing C lines colours the location (.srcloc) and the C
    text (.srctext) differently, with the same pair everywhere, each at
    least 4.5:1 against its background, in light and dark. inl_phys: an
    address in inlined code, whose Debugger source rows are rendered into
    a probe box to check the "inlined into" rows too."""
    seen = {}
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 1280, "height": 1000})
        got = {}
        show_tab(page, "memory")
        open_page(page, phys // 4096)
        wait_for(lambda: page.locator("#listing .srcloc").count() > 0, 15)
        got["Page listing"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Page listing"]))
        show_tab(page, "debugger")
        wait_for(lambda: page.locator("#cur_src .srctext").count() > 0, 15)
        for view in ("Debugger listing", "Debugger source row", "call stack"):
            got[view] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS[view]))
        if inl_phys is not None:
            page.evaluate("""(pa) => { const box = document.createElement('div'); box.id = 'src_colour_probe';
                document.getElementById('sym_info').append(box); renderSourceRows(box, pa, null); }""", inl_phys)
            wait_for(lambda: page.evaluate("srcText(...JSON.parse(document.querySelector('#src_colour_probe #cur_src').dataset.frames)"
                                           ".slice(-1).map(f => [f.file, f.line])[0]) !== null"), 15)
            page.evaluate("""(pa) => { const box = document.getElementById('src_colour_probe'); box.textContent = '';
                renderSourceRows(box, pa, null); }""", inl_phys)
            got["Debugger inlined-into row"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Debugger inlined-into row"]))
            page.evaluate("document.getElementById('src_colour_probe').remove()")
        page.evaluate("(pa) => openSource(pa)", phys)
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 20)
        got["Source panel"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Source panel"]))
        page.screenshot(path=str(shots / f"srccolour-{name}-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"srccolour-{name}-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
        page.click("#src_close")
        show_tab(page, "graph")
        page.evaluate("""() => { const st = G.stepper; G.view = 'timeline';
            G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fStart.length - 1; renderGraphInfo(); }""")
        got["Graph detail"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Graph detail"]))
        show_tab(page, "debugger")
        for view, (_, text) in SRC_COLOUR_VIEWS.items():
            if view == "Debugger inlined-into row" and inl_phys is None:
                continue
            g = got.get(view)
            if not g or not g["loc"] or (text and not g["text"]):
                problems.append(f"{name} {scheme}: {view} has no .srcloc/.srctext pair: {g}")
                continue
            seen.setdefault((scheme, "loc"), set()).add(tuple(g["loc"]["colour"]))
            for part in ("loc", "text") if text else ("loc",):
                c = g[part]
                ratio = contrast(c["colour"], c["bg"], c["alpha"])
                r[f"contrast {scheme} {view} {part}"] = round(ratio, 2)
                if ratio < 4.5:
                    problems.append(f"{name} {scheme}: {view} {part} contrast {ratio:.2f} < 4.5: {c}")
            if text:
                seen.setdefault((scheme, "text"), set()).add(tuple(g["text"]["colour"]))
                if g["loc"]["colour"] == g["text"]["colour"]:
                    problems.append(f"{name} {scheme}: {view} location and C text have the same colour {g['loc']['colour']}")
    page.emulate_media(color_scheme="light")
    for key, colours in seen.items():
        r[f"colour {key[0]} {key[1]}"] = sorted(colours)
        if len(colours) != 1:
            problems.append(f"{name} {key[0]}: .src{key[1]} differs between views: {sorted(colours)}")
    if seen.get(("light", "loc")) == seen.get(("dark", "loc")) or seen.get(("light", "text")) == seen.get(("dark", "text")):
        problems.append(f"{name}: the C line colours do not change with the theme: {seen}")


def dwarf_case(browser, base, shots, name):
    """file:line and inline chains from the shipped DWARF shards: Debugger
    source rows and call stack against llvm-symbolizer, C lines and "no
    line information" in the listings, screenshots light/dark at 1280/390."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    fn = "start_kernel" if name != "hello" else "hello_main"
    sk = next(s for s in syms if s["name"] == fn)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return [f"Run to {fn} did not stop there"], r
    steps = DWARF_STEPS if name != "hello" else (0, 3, 7, 11, 19)
    samples = dwarf_check(page, name, sk["vaddr"] - sk["paddr"], problems, r, steps, fn)
    if name != "hello" and not r.get("dwarf_inlined"):
        problems.append(f"{name}: no sample EIP was in inlined code: {r}")
    page.screenshot(path=str(shots / f"dwarf-debugger-{name}-1280-light.png"))
    # Graph tab: the detail of the innermost open frame names its call site and entry with file:line.
    show_tab(page, "graph")
    page.evaluate("""() => { const st = G.stepper; G.view = 'timeline';
        G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fStart.length - 1; renderGraphInfo(); }""")
    wait_for(lambda: "reading DWARF" not in (page.evaluate("(document.getElementById('graph_src') || {}).innerText") or ""), 15)
    r["graph_detail"] = page.evaluate("(document.getElementById('graph_src') || {}).innerText")
    if not r["graph_detail"] or not ("called from 0x" in r["graph_detail"] and re.search(r"\.[chS]:\d+", r["graph_detail"])):
        problems.append(f"{name}: Graph frame detail without DWARF locations: {r['graph_detail']!r}")
    page.screenshot(path=str(shots / f"dwarf-graph-{name}-1280-light.png"), full_page=True)
    # The listing of the page holding the last EIP: C lines with titles.
    phys = samples[-1][0]
    show_tab(page, "memory")
    open_page(page, phys // 4096)
    wait_for(lambda: page.locator("#listing .src:not(.dim)").count() > 0, 15)
    r["listing_c_lines"] = page.locator("#listing .src:not(.nol):not(.dim)").count()
    r["listing_no_line"] = page.locator("#listing .src.nol").count()
    titles = page.eval_on_selector_all("#listing .src", "els => els.map(e => e.title)")
    r["listing_inlined_titles"] = sum(1 for t in titles if "inlined into" in t)
    if r["listing_c_lines"] < 1:
        problems.append(f"{name}: no C lines in the listing of page {phys // 4096:#x}")
    page.screenshot(path=str(shots / f"dwarf-listing-{name}-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"dwarf-listing-{name}-1280-dark.png"))
    show_tab(page, "debugger")
    page.screenshot(path=str(shots / f"dwarf-debugger-{name}-1280-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        show_tab(page, "debugger")
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"dwarf-debugger-{name}-390-{scheme}.png"), full_page=True)
    page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    src_colour_check(page, name, phys, next((p for p, _, fr in samples if len(fr) > 1), None), problems, r, shots)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def import_source_check(page, problems, r, shots):
    """At start_kernel of an imported vmlinux: file:line only until its source is picked; a
    .tar.gz is refused with a reason; a pax .tar, a GNU .tar and a folder
    (each holding the tree one level down) give the text of the lines,
    equal to the files' own; the panel opens."""
    src = SITE / kernel_entries()["minified-linux"]["src"]
    r["src_status_before"] = page.inner_text("#src_status")
    if not page.is_visible("#src_pick") or "file:line only" not in r["src_status_before"] or any(source_texts(page)):
        problems.append(f"import without source: picker hidden or text shown: {r['src_status_before']!r}")
    with tempfile.TemporaryDirectory() as tmp:
        tree = pathlib.Path(tmp) / "linux-min"
        for gz in src.rglob("*.gz"):
            out = tree / gz.relative_to(src).with_suffix("")
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(gzip.decompress(gz.read_bytes()))
        # A path over 100 bytes: a pax "path" record, a GNU "L" entry.
        (tree / ("long-" * 25)).mkdir()
        (tree / ("long-" * 25) / "name.c").write_text("int x;\n")
        tars = {}
        for fmt, name in ((tarfile.PAX_FORMAT, "pax.tar"), (tarfile.GNU_FORMAT, "gnu.tar")):
            tars[name] = pathlib.Path(tmp) / name
            with tarfile.open(tars[name], "w", format=fmt) as t:
                t.add(tree, arcname="linux-min")
        tgz = pathlib.Path(tmp) / "src.tar.gz"
        tgz.write_bytes(gzip.compress(tars["pax.tar"].read_bytes()))
        page.set_input_files("#src_tar", str(tgz))
        wait_for(lambda: "compressed" in page.inner_text("#src_status"), 10)
        r["src_status_tgz"] = page.inner_text("#src_status")
        if "compressed" not in r["src_status_tgz"]:
            problems.append(f"a .tar.gz source was not refused with a reason: {r['src_status_tgz']!r}")
        picks = [("pax.tar", "#src_tar", tars["pax.tar"]), ("gnu.tar", "#src_tar", tars["gnu.tar"]), ("folder", "#src_dir", tree)]
        for label, sel, path in picks:
            page.set_input_files(sel, str(path))
            wait_for(lambda: page.get_attribute("#src_status", "data-state") == "picked", 30)
            _, state, frames = source_rows(page)
            texts = source_texts(page)
            want = []
            for f in frames:
                p = tree / f["file"] if f["file"] else None
                lines = p.read_text(errors="replace").split("\n") if p and p.is_file() else []
                want.append(lines[f["line"] - 1].strip() if 0 < f["line"] <= len(lines) else "")
            r[f"src_{label}"] = f"{page.inner_text('#src_status')}: {sum(1 for t in texts if t)} rows with text"
            if state != "ok" or not any(texts) or texts != want:
                problems.append(f"source picked as {label}: rows {texts} != files {want}")
        page.locator("#cur_src .frame button").first.click()
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 10)
        r["src_panel"] = page.inner_text("#src_title")
        if page.get_attribute("#src_body", "data-state") != "ok" or "the folder linux-min" not in page.inner_text("#src_info"):
            problems.append(f"source panel of a picked folder: {r['src_panel']!r} {page.inner_text('#src_info')!r}")
        page.screenshot(path=str(shots / "source-import-1280.png"))
        page.click("#src_close")


def dwarf_import_case(browser, base, shots):
    """An imported unstripped vmlinux: its DWARF is read in a Worker one
    compile unit at a time (clang forms: the minified Linux's -g vmlinux;
    GCC forms: Debian's -dbg vmlinux), matches llvm-symbolizer, survives a
    reload from IndexedDB; a vmlinux without DWARF says so."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel=minified-linux&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    # The import reboots the kernel: start that boot paused too (?paused=1 holds for the first boot only).
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported" and
             "compile units" in page.inner_text("#sym_status"), 30)
    r["status"] = page.inner_text("#sym_status")
    if page.get_attribute("#sym_status", "data-dwarf") != "import" or "compile units" not in r["status"]:
        problems.append(f"imported -g vmlinux: DWARF not read: {r['status']!r}")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        problems.append("imported -g vmlinux: Run to start_kernel did not stop there")
    import_source_check(page, problems, r, shots)
    samples = dwarf_check(page, "minified-linux", sk["vaddr"] - sk["paddr"], problems, r, DWARF_STEPS[:6])
    r["imported_first"] = r.get("dwarf_first")
    page.screenshot(path=str(shots / "dwarf-import-1280.png"))
    # Reload: the symbols come back from localStorage, the decoded units from IndexedDB.
    page.goto("about:blank")
    page.goto(url)
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
    r["reload_status"] = page.inner_text("#sym_status")
    got = page.evaluate("""async (pas) => { const out = [];
        for (const pa of pas) { let f = dbgFrames(pa);
            for (let i = 0; i < 50 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = dbgFrames(pa); }
            out.push(f); }
        return out; }""", [p for p, _, _ in samples])
    same = sum(1 for (p, st, fr), g in zip(samples, got) if g["state"] == st and (g.get("frames") or []) == fr)
    r["after_reload"] = f"{same}/{len(samples)} EIPs answer as before the reload"
    if page.get_attribute("#sym_status", "data-dwarf") != "cached" or same != len(samples):
        problems.append(f"after a reload: {r['after_reload']}, status {r['reload_status']!r}")
    page.click("#sym_forget")
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") != "imported", 30)
    page.close()
    # A vmlinux without DWARF (the plain build): said so, symbols still work.
    plain = (import_inputs() or [None, None])[1]
    if plain:
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(url)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.set_input_files("#symfile", str(plain))
        wait_for(lambda: "no DWARF" in page.inner_text("#sym_status") or "compile units" in page.inner_text("#sym_status"), 30)
        r["plain_status"] = page.inner_text("#sym_status")
        if "no DWARF" not in r["plain_status"]:
            problems.append(f"vmlinux without DWARF: not said so: {r['plain_status']!r}")
        page.click("#sym_forget")
        page.close()
    # Debian's -dbg vmlinux (GCC forms, 300 MB): only the parts needed are read.
    deb = debug_elf("debian-6.1-686")
    if deb:
        dsyms = json.loads((SITE / kernel_entries()["debian-6.1-686"]["symbols"]).read_text())["symbols"]
        rnd = __import__("random").Random(5)
        fns = [s for s in dsyms if s["type"] in "tT" and s["size"] > 16]
        pas = sorted(s["paddr"] + rnd.randrange(s["size"]) for s in rnd.sample(fns, 40))
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel=debian-6.1-686&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        t0 = time.monotonic()
        page.set_input_files("#symfile", str(deb))
        wait_for(lambda: "compile units" in page.inner_text("#sym_status") or
                 page.get_attribute("#sym_status", "data-state") == "error", 120)
        r["debian_import_seconds"] = round(time.monotonic() - t0, 1)
        r["debian_status"] = page.inner_text("#sym_status")
        got = page.evaluate("""async (pas) => { const out = [];
            for (const pa of pas) { let f = dbgFrames(pa);
                for (let i = 0; i < 300 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = dbgFrames(pa); }
                out.push(f); }
            return out; }""", pas)
        want = symbolize_frames(deb, [p + 0xC0000000 for p in pas])
        bad = [(hex(p), g, w) for p, g, w in zip(pas, got, want) if g["state"] != "ok" or not frames_agree(g["frames"], w)]
        r["debian_import"] = f"{len(pas) - len(bad)}/{len(pas)} agree with llvm-symbolizer"
        if bad:
            problems.append(f"Debian vmlinux import: {bad[:3]}")
        page.click("#sym_forget")
        page.close()
    elif os.environ.get("CI"):
        problems.append("no debian-6.1-686.elf in $MKB_DEBUG_ELF_DIR")
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def watch(page, expr):
    """Add a watch; its row's text once the types have loaded ("" if not shown)."""
    wait_for(lambda: page.get_attribute("#watches", "data-state") == "ok", 120)
    page.fill("#watch_expr", expr)
    page.click("#watch_add")
    sel = f'#watches .watch[data-expr="{expr}"]'
    wait_for(lambda: page.locator(sel).count() > 0, 10)
    return page.inner_text(sel).replace("×", "").strip() if page.locator(sel).count() else ""


def gdb_values(elf, exprs):
    """gdb's print of each expression from the ELF's own data (the values at
    boot, before the kernel changes them), or None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    args = ["gdb", "-nx", "-batch"]
    for e in exprs:
        args += ["-ex", f"print {e}"]
    out = subprocess.run(args + [str(elf)], capture_output=True, text=True).stdout
    return [re.sub(r"^\$\d+ = ", "", l) for l in out.splitlines() if re.match(r"^\$\d+ = ", l)]


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


# Watches whose values at start_kernel are the ELF's own data (gdb prints them from the file).
TYPES_WATCHES = {
    "minified-linux": ["init_task.comm", "jiffies_64", "init_task.pid", "current_task"],
    "debian-6.1-686": ["init_task.comm", "jiffies_64", "init_uts_ns.name.release", "linux_banner", "system_state"],
}


def types_case(browser, base, shots, name):
    """Variables of the shipped DWARF (types.json): at start_kernel the
    Variables panel prints watches equal to gdb's print of the ELF's data,
    opens structs, follows a pointer to the Page tab; in the Page listing
    a pointed-at byte names variable + member path, which gdb evaluates to
    an lvalue holding that byte; data symbols' label lines carry the type."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    T = json.loads((SITE / entry["dwarf"] / "types.json").read_text())
    off = sk["vaddr"] - sk["paddr"]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 1):
        page.close()
        return [f"{name}: Run to start_kernel did not stop there"], r
    r["status"] = page.inner_text("#watch_status") if wait_for(
        lambda: page.get_attribute("#watches", "data-state") == "ok", 120) else page.inner_text("#watch_status")
    if f"{len(T['vars'])} variables" not in r["status"]:
        problems.append(f"{name}: Variables status {r['status']!r}")
    exprs = TYPES_WATCHES[name]
    rows = {e: watch(page, e) for e in exprs}
    r["watches"] = rows
    elf = debug_elf(name)
    want = gdb_values(elf, exprs)
    if want is None and os.environ.get("CI"):
        problems.append(f"{name}: no gdb or -g ELF to check the watches against")
    for e, w in zip(exprs, want or []):
        got = rows[e]
        # gdb: "swapper\000..." / 4294892296 / SYSTEM_BOOTING / (struct task_struct *) 0xc... <init_task>
        g = re.sub(r"(\\000)+(\"?)$", r"\2", re.sub(r", '\\000' <repeats \d+ times>$", "", w))
        g = re.sub(r"^\([^)]*\) ", "", g)
        # A string: its first characters; else the first word (number, enumerator, pointer).
        key = g[g.index('"'):][:30] if '"' in g else g.split(" ")[0]
        if key not in got:
            problems.append(f"{name}: watch {e} shows {got!r}, gdb prints {w!r}")
    # A struct opens with its members; a pointer opens the Page tab at its target.
    page.locator('#watches .watch[data-expr="init_task.comm"] .wdel').click()
    watch(page, "init_task")
    page.locator('#watches .watch[data-expr="init_task"] summary').first.click()
    wait_for(lambda: page.locator('#watches .watch[data-expr="init_task"] .wkids').count() > 0, 10)
    r["init_task_members"] = page.locator('#watches .watch[data-expr="init_task"] .wkids > * .wname').count()
    kids = page.inner_text('#watches .watch[data-expr="init_task"] .wkids')
    if ".comm" not in kids or '"swapper"' not in kids:
        problems.append(f"{name}: init_task opened without .comm = \"swapper\": {kids[:300]!r}")
    page.screenshot(path=str(shots / f"vars-debugger-{name}-1280-light.png"), full_page=True)
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"vars-debugger-{name}-1280-dark.png"), full_page=True)
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger with variables scrolls horizontally at 390 px ({scheme})")
        page.locator("#watches").scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"vars-debugger-{name}-390-{scheme}.png"))
    page.set_viewport_size({"width": 1280, "height": 1000})
    it = next(v for v in T["vars"] if v[0] == "init_task")
    ptr = page.locator('#watches .watch[data-expr="init_task"] .wptr').first
    r["pointer"] = ptr.inner_text() if ptr.count() else None
    if ptr.count():
        target = int(ptr.inner_text().split()[0], 16) - off
        ptr.click()
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(target // 4096), 15)
        if page.get_attribute("#listing", "data-page") != str(target // 4096):
            problems.append(f"{name}: tapping pointer {r['pointer']!r} did not open page {target // 4096:#x}")
    else:
        problems.append(f"{name}: init_task has no pointer member to follow")
    # The Page listing: the label line of init_task and bytes inside variables.
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    wait_for(lambda: page.locator('#listing .lbl[data-sym="init_task"][data-type]').count() > 0, 30)
    r["label_type"] = page.get_attribute('#listing .lbl[data-sym="init_task"]', "data-type")
    if r["label_type"] != "struct task_struct":
        problems.append(f"{name}: init_task's label line has type {r['label_type']!r}")
    rnd = __import__("random").Random(7)
    near = [v for v in T["vars"] if v[3] > 0 and abs(v[1] - it[1]) < 3 * 4096 and not v[5] & 4]
    picks = [it[1] + 16 + it[3] // 3] + [v[1] + rnd.randrange(v[3]) for v in rnd.sample(near, min(24, len(near)))]
    got = []
    for pa in picks:
        if pa // 4096 != int(page.get_attribute("#listing", "data-page")):
            open_page(page, pa // 4096)
        # Repeated rows are collapsed to "*": their bytes have no line to point at.
        if page.evaluate("(pa) => [...document.querySelectorAll('#listing .hex')].some((l) => pa >= +l.dataset.addr && pa < +l.dataset.end)", pa):
            got.append((pa, hover_byte(page, pa), page.inner_text("#var_info")))
    r["hover_first"] = got[0][2] if got else None
    r["hovered"] = f"{len(got)} of {len(picks)} (the others in collapsed rows)"
    if len(got) < 10:
        problems.append(f"{name}: only {len(got)} bytes to point at in the listing")
    named = [(pa, p) for pa, p, _ in got if p]
    if len(named) < len(got):
        problems.append(f"{name}: bytes inside variables named no variable: {[(hex(a), t) for a, p, t in got if not p][:3]}")
    if elf and shutil.which("gdb"):
        from test_dwarf import gdb_eval
        def start(pa, var):
            return next(v[1] for v in T["vars"] if v[0] == var and v[1] <= pa < v[1] + max(v[3], 1))
        res = gdb_eval(str(elf), [[re.match(r"\w+", p)[0], "", p, start(pa, re.match(r"\w+", p)[0]) + off] for pa, p in named])
        bad = [(hex(pa), p, g) for (pa, p), g in zip(named, res) if g[0] in ("error", None) or not g[0] <= pa + off < g[0] + max(g[1], 1)]
        r["hover_vs_gdb"] = f"{len(named) - len(bad)}/{len(named)} paths hold the byte in gdb"
        if bad:
            problems.append(f"{name}: hovered paths gdb places elsewhere: {bad[:3]}")
    # A tap pins the description and offers to watch the path.
    if named and named[0][0] // 4096 != int(page.get_attribute("#listing", "data-page")):
        open_page(page, named[0][0] // 4096)
    path = hover_byte(page, named[0][0], click=True) if named else None
    # Just below the sticky header and tabs.
    page.evaluate("""() => { const tabs = document.querySelector('[role=tablist]').getBoundingClientRect().bottom;
        window.scrollBy(0, document.getElementById('var_info').getBoundingClientRect().top - tabs - 40); }""")
    page.screenshot(path=str(shots / f"vars-page-{name}-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"vars-page-{name}-1280-dark.png"))
    page.emulate_media(color_scheme="light")
    if page.locator("#var_watch").count():
        page.click("#var_watch")
        wait_for(lambda: page.locator(f'#watches .watch[data-expr="{path}"]').count() > 0, 10)
    r["tap_watch"] = page.inner_text(f'#watches .watch[data-expr="{path}"]') if page.locator(f'#watches .watch[data-expr="{path}"]').count() else None
    if not r["tap_watch"] or page.get_attribute(f'#watches .watch[data-expr="{path}"]', "data-state") != "ok":
        problems.append(f"{name}: tapping a byte did not offer a working watch of {path!r}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def types_import_case(browser, base, shots):
    """Variables of an imported unstripped vmlinux, read by a second Worker in
    one pass over every compile unit: the same data as the shipped types.json
    (clang forms: the minified Linux; GCC forms: Debian's -dbg vmlinux), kept
    in IndexedDB across a reload."""
    problems, r = [], {}
    same_js = """async (url) => { const t = await (await fetch(url)).text();
        const T = DBG.types; return t === JSON.stringify({ format: T.format, types: T.types.slice(0, T.types.length - (T.sub ? T.sub.size : 0)),
            vars: T.vars, sections: T.sections }); }"""
    for name, size in (("minified-linux", None), ("debian-6.1-686", None)):
        elf = debug_elf(name)
        if not elf:
            if os.environ.get("CI"):
                problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
            continue
        entry = kernel_entries()[name]
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        url = f"{base}/index.html?kernel={name}&paused=1#debugger"
        page.goto(url)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.evaluate("startPaused = true")
        page.set_input_files("#symfile", str(elf))
        wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 120)
        t0 = time.monotonic()
        show_tab(page, "debugger")
        wait_for(lambda: page.get_attribute("#watches", "data-state") in ("ok", "none"), 300, 0.5)
        r[f"{name}_seconds"] = round(time.monotonic() - t0, 1)
        r[f"{name}_status"] = page.inner_text("#watch_status")
        if page.get_attribute("#watches", "data-state") != "ok":
            problems.append(f"{name} import: variables not read: {r[f'{name}_status']!r}")
            page.close()
            continue
        if not page.evaluate(same_js, entry["dwarf"] + "types.json"):
            problems.append(f"{name} import: the Worker's variables differ from the shipped types.json")
        # Paused at the reset vector: the values are not the kernel's yet, and the panel says so.
        if page.get_attribute("#watches", "data-started") != "no" or "has not run yet" not in r[f"{name}_status"]:
            problems.append(f"{name} import: before the kernel runs the Variables do not say so: {r[f'{name}_status']!r}")
        if name == "minified-linux":
            sk = next(s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"] if s["name"] == "start_kernel")
            page.fill("#dbg_addr", hex(sk["vaddr"]))
            page.click("#dbg_runto")
            wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120)
            r[f"{name}_comm"] = watch(page, "init_task.comm")
            if '"swapper"' not in r[f"{name}_comm"] or page.get_attribute("#watches", "data-started") == "no":
                problems.append(f"{name} import: init_task.comm at start_kernel shows {r[f'{name}_comm']!r}")
            page.screenshot(path=str(shots / "vars-import-1280.png"), full_page=True)
            page.goto("about:blank")
            page.goto(url)
            wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
            show_tab(page, "debugger")
            wait_for(lambda: page.get_attribute("#watches", "data-state") == "ok", 30)
            r["reload_status"] = page.inner_text("#watch_status")
            if page.get_attribute("#watches", "data-state") != "ok" or not page.evaluate(same_js, entry["dwarf"] + "types.json"):
                problems.append(f"after a reload the imported variables are gone: {r['reload_status']!r}")
        page.click("#sym_forget")
        page.close()
        if errors:
            problems.append(f"{name} import page errors: " + "; ".join(errors))
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
    st32 = next(s for s in syms if s["name"] == "startup_32")
    # Default boot: fast-forward (JIT) through BIOS, setup and the XZ
    # decompressor, stop exactly at startup_32, then 1/s.
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=1#debugger")
    ok = wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5)
    r["ff_seconds"] = round(time.monotonic() - t0, 1)
    page.click("#dbg_pause")
    info = page.evaluate("ffInfo && { stop: ffInfo.stopPhys, steps: ffInfo.steps, ms: Math.round(ffInfo.ms) }")
    r["ff"] = info
    if not ok or not info or info["stop"] != st32["paddr"] or r["ff_seconds"] > 120:
        problems.append(f"fast-forward to startup_32 {st32['paddr']:#x}: {info}, {r['ff_seconds']} s")
    s0 = page.evaluate("stepper.steps")
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 0.5)
    r["entry_to_start_kernel"] = page.evaluate("stepper.steps") - s0
    if not ok or r["entry_to_start_kernel"] != 62512:
        problems.append(f"startup_32 -> start_kernel took {r['entry_to_start_kernel']} instructions, not 62,512")
    page.close()
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    src_files = {}  # source files fetched: path -> bytes
    page.on("response", lambda resp: "/src/" in resp.url and resp.url.split("?")[0].endswith(".gz") and
            src_files.__setitem__(resp.url.split("/src/", 1)[1].split("?")[0], int(resp.headers.get("content-length") or 0)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    t0 = time.monotonic()
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 1)
    r["runto_seconds"] = round(time.monotonic() - t0)
    r["runto_symbol"] = page.get_attribute("#cur_sym", "data-name")
    r["context_source"] = page.get_attribute("#context", "data-source")
    if r["context_source"] != "live" or page.locator("#context .ins.cur").count() != 1:
        problems.append(f"Debugger code window at EIP: source {r['context_source']!r}, boxed lines {page.locator('#context .ins.cur').count()}")
    if not ok or r["runto_symbol"] != "start_kernel":
        problems.append(f"Run to start_kernel: {r['runto_symbol']!r} ({page.inner_text('#dbg_status')})")
    elif "dwarf" in entry:
        dwarf_check(page, name, sk["vaddr"] - sk["paddr"], problems, r, entry_fn="start_kernel" if "src" in entry else None)
        page.screenshot(path=str(shots / "dwarf-debugger-debian-1280.png"))
        open_page(page, sk["paddr"] // 4096)
        wait_for(lambda: page.locator("#listing .src:not(.dim)").count() > 0, 15)
        r["listing_c_lines"] = page.locator("#listing .src:not(.nol):not(.dim)").count()
        main = page.locator("#listing .src", has_text="main.c").first
        r["listing_main_c"] = main.inner_text() if main.count() else None
        if not r["listing_main_c"]:
            problems.append("Debian: no init/main.c line in the listing of start_kernel")
        page.screenshot(path=str(shots / "dwarf-listing-debian-1280.png"))
        if "src" in entry:
            debian_source_panel(page, page.locator(f'#listing .src[data-addr="{sk["paddr"]}"]'), src_files, problems, r, shots)
        debian_covtree(page, entry, bins, problems, r, shots)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def debian_source_panel(page, line, src_files, problems, r, shots):
    """Debian's C source: the listing's C line at start_kernel's entry shows
    init/main.c's text, tapping it opens start_kernel with lines that ran; only the files of lines shown
    were fetched; screenshots light/dark at 1100 and 390 px."""
    wait_for(lambda: line.count() and not line.locator(".dim").count(), 20)
    r["listing_main_c_text"] = line.inner_text() if line.count() else None
    if not r["listing_main_c_text"] or "main.c:" not in r["listing_main_c_text"] or line.locator(".dim").count():
        problems.append(f"Debian: the init/main.c line of the listing has no text: {r['listing_main_c_text']!r}")
    line.click()
    wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 30)
    r["panel"] = page.inner_text("#src_title")
    r["panel_code"] = int(page.get_attribute("#src_body", "data-code") or 0)
    r["panel_executed"] = int(page.get_attribute("#src_body", "data-executed") or 0)
    r["panel_info"] = page.inner_text("#src_info")
    if not r["panel"].startswith("start_kernel — init/main.c:") or not 0 < r["panel_executed"] < r["panel_code"]:
        problems.append(f"Debian source panel: {r['panel']!r}, {r['panel_executed']} of {r['panel_code']} lines executed")
    if "Debian's source package" not in r["panel_info"]:
        problems.append(f"Debian source panel does not say where the source comes from: {r['panel_info']!r}")
    r["src_files_fetched"] = len(src_files)
    r["src_bytes_fetched"] = sum(src_files.values())
    if not src_files or len(src_files) > 100:
        problems.append(f"Debian: {len(src_files)} source files fetched: {sorted(src_files)[:10]}")
    page.screenshot(path=str(shots / "source-debian-1100-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / "source-debian-1100-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"Debian source panel: the page scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"source-debian-390-{scheme}.png"))
    page.set_viewport_size({"width": 1100, "height": 1000})


def debian_covtree(page, entry, bins, problems, r, shots):
    """Debian's Coverage tab at start_kernel: the whole tree from files.json
    without reading every shard (only those with code that ran), totals
    equal files.json, init/main.c has lines that ran."""
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    shards = json.loads((SITE / entry["dwarf"] / "index.json").read_text())["shards"]
    page.click("#src_close")
    show_tab(page, "coverage")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok" and not page.inner_text("#cov_status").startswith("Reading"), 60)
    page.fill("#cov_filter", "init/main.c")
    rows = cov_rows(page)
    root = next((x for x in rows if x["kind"] == "root"), None)
    main = next((x for x in rows if x["kind"] == "file" and x["path"] == "init/main.c"), None)
    r["covtree_root"], r["covtree_main"], r["covtree_status"] = root, main, page.inner_text("#cov_status")
    r["covtree_shards_fetched"] = [len(bins), len(shards)]
    if not root or root["lines"] != sum(f[1] for f in F["files"]) or root["linesRun"] is None:
        problems.append(f"Debian coverage tree root {root} (files.json: {sum(f[1] for f in F['files'])} lines)")
    if not main or not main["linesRun"] or main["lines"] != F["files"][[f[0] for f in F["files"]].index("init/main.c")][1]:
        problems.append(f"Debian coverage tree init/main.c: {main}")
    if len(bins) >= len(shards) / 2:
        problems.append(f"Debian coverage tree fetched {len(bins)} of {len(shards)} shards")
    page.evaluate("document.getElementById('panel_coverage').scrollIntoView()")
    page.screenshot(path=str(shots / "covtree-debian-1100.png"))
    page.fill("#cov_filter", "")


# Where the unwind suite pauses: (kernel, function to Run to, Steps after it).
# Deep boot code (CFI only for the minified Linux, CFI + EBP chain for
# Debian's GCC build) and timer interrupts (entry code's pt_regs).
UNWIND_POINTS = [
    ("minified-linux", "memblock_add_range", (0, 9)),
    ("minified-linux", "tty_write", (0, 5)),
    ("debian-6.1-686", "timer_interrupt", (0, 7)),
]

# The stack pages of a pause (8 KiB stacks around ESP and every frame's CFA)
# read through the page tables, with the registers: what gdb needs to unwind.
UNWIND_DUMP_JS = """() => {
    const cpu = stepper.cpu, r = stepper.regs(), fr = JSON.parse(document.getElementById('call_stack').dataset.frames);
    const want = new Set();
    for (const a of [r.esp, ...fr.map((f) => f.cfa).filter((x) => x !== undefined && x !== null)])
        for (let p = ((a >>> 13) << 13) - 8192; p < ((a >>> 13) << 13) + 16384; p += 4096) want.add(p >>> 0);
    const pages = [];
    for (const va of [...want].sort((a, b) => a - b)) {
        const w = pageWalk(cpu, va);
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
    page.evaluate("document.querySelectorAll('#locals details.lframe').forEach((d) => { d.open = true; })")
    wait_for(lambda: page.evaluate("[...document.querySelectorAll('#locals details.lframe')].every((d) => "
                                   "d.dataset.filled || (d.dataset.state && d.dataset.state !== 'loading'))"), timeout)
    return page.evaluate(LOCALS_DUMP_JS)


# Runs inside gdb on a core: for each frame (inlined calls are frames of
# their own) its function and the value of every parameter and local of the
# blocks around its pc: the unsigned integer of a scalar, "out" (optimized
# out), "agg" (no scalar) or "err:..." (memory not in the core).
GDB_LOCALS = r"""
import gdb, json
def scalar(v):
    if v.is_optimized_out:
        return "out"
    t = v.type.strip_typedefs()
    if t.code not in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_CHAR):
        return "agg"
    return str(int(v) & ((1 << (8 * t.sizeof)) - 1))
def enumerator(s):
    t = s.type.strip_typedefs() if s.type is not None else None
    return t is not None and t.code == gdb.TYPE_CODE_ENUM and any(x.name == s.name for x in t.fields())
out, f = [], gdb.newest_frame()
while f is not None and len(out) < NFRAMES:
    fn = f.function()
    fr = {"func": fn.name if fn else None, "inlined": f.type() == gdb.INLINE_FRAME, "pc": int(f.pc()), "vars": []}
    try:
        b = f.block()
    except RuntimeError:
        b = None
    while b is not None:
        for s in b:
            if not (s.is_argument or s.is_variable or (s.is_constant and not enumerator(s))) or s.addr_class == gdb.SYMBOL_LOC_LABEL:
                continue
            if s.type is not None and str(s.type) == "__CORE_ADDR":
                continue
            try:
                val = scalar(s.value(f))
            except gdb.error as e:
                val = "out" if "optimized out" in str(e) else "err:" + str(e)
            fr["vars"].append([s.name, val])
        if b.function is not None:
            break
        b = b.superblock
    out.append(fr)
    try:
        f = f.older()
    except gdb.error:
        break
json.dump(out, open(DST, "w"))
"""


def gdb_locals(elf, regs, pages, nframes):
    """gdb's frames (inlined ones included) with their variables' values,
    from a core of the registers and stack pages; None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "locals.py", "locals.json"))
        write_core(core, regs, pages)
        pathlib.Path(py).write_text(f"DST, NFRAMES = {dst!r}, {nframes}\n" + GDB_LOCALS)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=300)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else []


def locals_compare(elf, locs, udump):
    """The page's parameters and locals (locs: LOCALS_DUMP_JS) against
    gdb's on a core of the same registers and stack pages (udump:
    UNWIND_DUMP_JS), over the frames from the CPU's registers down to the
    first one not found by CFI (gdb unwinds those the same way, see
    unwind_compare). Each inlined call and function must have the same
    variables; a scalar both read must be equal, and one gdb finds
    optimized out the page must not show. Returns (values compared, values
    only gdb has, problems) or None without gdb."""
    fr = udump["frames"]
    n = 1
    while n < len(fr) and fr[n]["method"] == "cfi":
        n += 1
    # A frame without a DWARF function (assembly) is one gdb frame without variables.
    page = [sc for d in locs[:n] for sc in (d["scopes"] or [{"func": None, "vars": []}])]
    regs = {**udump["regs"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b}
    want = gdb_locals(elf, regs, udump["pages"], len(page))
    if want is None:
        return None
    same, more, bad = 0, 0, []
    for i, sc in enumerate(page):
        if i >= len(want):
            bad.append(f"frame {i} ({sc['func']}): gdb has no such frame")
            break
        g = want[i]
        if g["func"] != sc["func"]:
            bad.append(f"frame {i}: page {sc['func']}, gdb {g['func']}")
            continue
        if sorted(v["name"] for v in sc["vars"]) != sorted(x[0] for x in g["vars"]):
            bad.append(f"frame {i} ({sc['func']}): variables page {sorted(v['name'] for v in sc['vars'])} gdb {sorted(x[0] for x in g['vars'])}")
            continue
        gv = {}
        for name, val in g["vars"]:
            gv.setdefault(name, []).append(val)
        pv = {}
        for v in sc["vars"]:
            pv.setdefault(v["name"], []).append(v)
        for name, mine in pv.items():
            # Same-named variables of nested blocks: compare as sets of values.
            theirs = gv[name]
            for v in mine:
                nums = [x for x in theirs if x not in ("out", "agg") and not x.startswith("err:")]
                if v["raw"] is not None:
                    if v["raw"] in nums:
                        same += 1
                    elif any(x.startswith("err:") or x == "agg" for x in theirs) and not nums:
                        pass  # gdb could not read it from the core (memory outside the stack pages)
                    elif all(x == "out" for x in theirs):
                        bad.append(f"frame {i} ({sc['func']}): {name} = {v['raw']} on the page, optimized out for gdb")
                    else:
                        bad.append(f"frame {i} ({sc['func']}): {name} = {v['raw']} ({v['loc']}) on the page, gdb {theirs}")
                elif v["state"] == "out" and nums and len(mine) == 1:
                    more += 1
    return same, more, bad


def locals_check(page, elf, dump, r, tag):
    """The Parameters and locals section at a pause: one frame per unwound
    frame, the innermost with variables, every variable either with a
    value and its location or saying why not, and with the -g ELF the
    values equal gdb's on a core of the same registers and stack."""
    problems = []
    locs = locals_open_all(page)
    unwound = [f for f in dump["frames"] if not f.get("user") and f["method"] != "recorded"]
    if len(locs) != len(unwound):
        problems.append(f"{len(locs)} frames in Parameters and locals for {len(unwound)} unwound frames")
    nv = sum(len(sc["vars"]) for d in locs for sc in d["scopes"])
    shown = sum(1 for d in locs for sc in d["scopes"] for v in sc["vars"] if v["state"] != "out")
    r[tag + " locals"] = f"{len(locs)} frames, {nv} variables, {shown} with a value; frame 0: " + "; ".join(
        f"{sc['func']}: " + ", ".join(f"{v['name']}={v['value']}" for v in sc["vars"]) for sc in (locs[0]["scopes"] if locs else []))
    if not locs or locs[0]["state"] != "ok" or not any(d["scopes"] for d in locs):
        problems.append(f"no parameters and locals in any frame: {locs[:2]}")
    for d in locs:
        for sc in d["scopes"]:
            for v in sc["vars"]:
                if not v["loc"].strip() or (v["state"] == "out" and v["value"]):
                    problems.append(f"{sc['func']}.{v['name']}: no location or reason shown ({v})")
    if elf:
        res = locals_compare(elf, locs, dump)
        if res is not None:
            same, more, bad = res
            r[tag + " locals vs gdb"] = f"{same} values equal, {more} only gdb has (entry values it resolves), {len(bad)} differ"
            r["locals_equal"] = r.get("locals_equal", 0) + same
            problems += bad
    return problems


def write_core(path, regs, pages):
    """An i386 ELF core file: one NT_PRSTATUS with the registers (elf_prstatus,
    pr_reg = ebx ecx edx esi edi ebp eax ds es fs gs orig_eax eip cs eflags esp ss)
    and a PT_LOAD per 4 KiB page of memory, at its virtual address."""
    import base64
    import struct
    order = ["ebx", "ecx", "edx", "esi", "edi", "ebp", "eax", "ds", "es", "fs", "gs", None, "eip", "cs", "eflags", "esp", "ss"]
    prreg = struct.pack("<17I", *[(regs.get(k, 0) if k else 0xffffffff) & 0xffffffff for k in order])
    prstatus = bytes(72) + prreg + bytes(4)
    note = struct.pack("<III", 5, len(prstatus), 1) + b"CORE\0\0\0\0" + prstatus
    data = [base64.b64decode(b) for _, b in pages]
    phnum = 1 + len(pages)
    off = 52 + 32 * phnum
    ph = [struct.pack("<8I", 4, off, 0, 0, len(note), 0, 0, 4)]
    off += len(note)
    for (va, _), d in zip(pages, data):
        ph.append(struct.pack("<8I", 1, off, va, 0, len(d), len(d), 6, 4096))
        off += len(d)
    eh = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + bytes(9), 4, 3, 1, 0, 52, 0, 0, 52, 32, phnum, 40, 0, 0)
    pathlib.Path(path).write_bytes(eh + b"".join(ph) + note + b"".join(data))


# Runs inside gdb: the physical frames (inlined ones folded into the
# function they are in) from the core's registers: [pc, CFA or None].
GDB_BT = r"""
import gdb, json
out, f = [], gdb.newest_frame()
while f is not None and len(out) < 64:
    if f.type() != gdb.INLINE_FRAME:
        out.append(int(f.pc()))
    try:
        f = f.older()
    except gdb.error:
        break
json.dump(out, open(DST, "w"))
"""


def gdb_backtrace(elf, regs, pages):
    """gdb's backtrace (return addresses, innermost first) of a core made of
    the registers and stack pages, or None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "bt.py", "bt.json"))
        write_core(core, regs, pages)
        pathlib.Path(py).write_text(f"DST = {dst!r}\n" + GDB_BT)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=120)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else []


def unwind_compare(elf, dump, offset):
    """The page's unwound frames against gdb, segment by segment: from the
    CPU's registers, and from the registers of each frame found another
    way (pt_regs: gdb does not cross entry code; entry ESP, EBP chain).
    Returns (frames compared, problems)."""
    fr, n, bad = dump["frames"], 0, []
    # gdb starts again at each frame not found by CFI (its registers as the page has them).
    seg = [i for i, f in enumerate(fr) if f["method"] in ("regs", "pt_regs", "entry", "fp") and not f.get("user")]
    for k, i in enumerate(seg):
        end = seg[k + 1] if k + 1 < len(seg) else len(fr)
        # Only the frames CFI found: gdb's own fallbacks (prologue analysis) are not the page's.
        part = [fr[i]]
        for f in fr[i + 1:end]:
            if f["method"] != "cfi":
                break
            part.append(f)
        names = {0: "eax", 1: "ecx", 2: "edx", 3: "ebx", 4: "esp", 5: "ebp", 6: "esi", 7: "edi"}
        regs = {names[int(q)]: v for q, v in (fr[i]["regs"] or {}).items() if v is not None}
        regs.update({"eip": fr[i]["va"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b})
        want = gdb_backtrace(elf, regs, dump["pages"])
        if want is None:
            return 0, []
        # va: EIP, or a caller's return address (gdb's pc of that frame).
        got = [f["va"] for f in part]
        if want[:len(got)] != got:
            bad.append(f"from frame {i} ({fr[i]['name']}): page {[hex(x) for x in got]} gdb {[hex(x) for x in want[:len(got) + 2]]}")
        n += len(got)
    return n, bad


def unwind_case(browser, base, shots, name, fn, steps):
    """The Debugger's call stack is unwound with the DWARF call frame
    information at each pause: every frame names its method, the frames
    CFI and pt_regs found equal gdb's backtrace of a core made of the same
    registers and stack, the unwind reaches the stack's start (or says why
    not), and the CFI shards fetched are only the ones the frames are in."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    offset = sk["vaddr"] - sk["paddr"]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    cfi = set()
    page.on("response", lambda resp: ("/dwarf/cfi-" in resp.url or "/dwarf/locals-" in resp.url) and
            cfi.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    t0 = time.monotonic()
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) or "Halted" in page.inner_text("#dbg_status"), 240) \
            or page.get_attribute("#regs", "data-phys") != str(sk["paddr"]):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    r["runto_s"] = round(time.monotonic() - t0)
    elf = debug_elf(name)
    for n in steps:
        if n:
            before = page.evaluate("stepper.steps")
            page.fill("#dbg_n", str(n))
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("stepper.steps") - before >= n and
                     page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
        dump = page.evaluate(UNWIND_DUMP_JS)
        fr = dump["frames"]
        tag = f"{fn}+{n}"
        r[tag] = " <- ".join(f"{f['name']}[{f['method']}]" for f in fr) + f" ({dump['unwind']}: {dump['why']})"
        if dump["unwind"] not in ("ok", "stopped"):
            problems.append(f"{name} {tag}: unwind state {dump['unwind']}: {dump['why']}")
        methods = [f["method"] for f in fr]
        if not fr or methods[0] != "regs" or any(m not in ("regs", "cfi", "pt_regs", "entry", "fp", "recorded") for m in methods):
            problems.append(f"{name} {tag}: frames without a known method: {methods}")
        if sum(1 for m in methods if m == "cfi") < 2:
            problems.append(f"{name} {tag}: fewer than 2 frames from CFI: {r[tag]}")
        if fn in ("timer_interrupt", "tty_write"):
            # Kernel code an interrupt interrupted, or user space that made the system call.
            if "pt_regs" not in methods or (fn == "tty_write") != bool(fr[-1].get("user")):
                problems.append(f"{name} {tag}: no frame from the entry code's pt_regs (or the wrong kind): {r[tag]}")
            if dump["unwind"] != "ok":
                problems.append(f"{name} {tag}: the unwind did not reach the start of the stack: {r[tag]}")
        if dump["unwind"] == "stopped" and not dump["why"]:
            problems.append(f"{name} {tag}: the unwind stopped without saying why")
        if fr and fr[-1].get("user"):
            # The "user space" frame opens nothing (a disabled button): it must not be faded.
            op = page.evaluate("getComputedStyle([...document.querySelectorAll('#call_stack button')].pop()).opacity")
            if op != "1":
                problems.append(f"{name} {tag}: the user space frame is faded (opacity {op})")
        labels = page.eval_on_selector_all("#call_stack .fmeth", "els => els.map(e => e.textContent)")
        if len(labels) != len(fr):
            problems.append(f"{name} {tag}: {len(labels)} method labels for {len(fr)} frames")
        if elf:
            k, bad = unwind_compare(elf, dump, offset)
            r[tag + " vs gdb"] = f"{k} frames, {len(bad)} segments differ"
            problems += [f"{name} {tag}: {b}" for b in bad]
        problems += [f"{name} {tag}: {x}" for x in locals_check(page, elf, dump, r, tag)]
        r["locals"] = page.evaluate(LOCALS_DUMP_JS)
    if elf and shutil.which("gdb") and not r.get("locals_equal"):
        problems.append(f"{name}: no parameter or local value compared with gdb")
    r["frames"] = fr
    r["cfi_shards"] = sorted(x for x in cfi if x.startswith("cfi-"))
    r["locals_shards"] = sorted(x for x in cfi if x.startswith("locals-"))
    if not r["cfi_shards"]:
        problems.append(f"{name}: no CFI shard was fetched")
    if not r["locals_shards"]:
        problems.append(f"{name}: no locals shard was fetched")
    page.screenshot(path=str(shots / f"unwind-{name}-{fn}-1280-light.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"unwind-{name}-{fn}-390-{scheme}.png"), full_page=True)
    page.emulate_media(color_scheme="dark")
    page.set_viewport_size({"width": 1280, "height": 1000})
    page.screenshot(path=str(shots / f"unwind-{name}-{fn}-1280-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#locals").screenshot(path=str(shots / f"locals-{name}-{fn}-1280-{scheme}.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def unwind_import_case(browser, base, shots, want):
    """An imported -g vmlinux: the Worker parses its .debug_frame and the
    call stack at the same pause equals the one from the shipped shards
    (want: frames and Parameters and locals of unwind_case at
    memblock_add_range+9), and so do the parameters and locals the
    Worker reads per compile unit; after a reload the call frame
    information and the units' locals come back from IndexedDB."""
    problems, r = [], {}
    want, want_locals = want if want else (None, None)
    elf = debug_elf("minified-linux")
    if not elf or not want:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf or no shipped frames"]), {"skipped": "no -g ELF"}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "memblock_add_range")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel=minified-linux&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return ["imported -g vmlinux: Run to memblock_add_range did not stop there"], r
    before = page.evaluate("stepper.steps")
    page.fill("#dbg_n", "9")
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("stepper.steps") - before >= 9 and page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    got = json.loads(page.get_attribute("#call_stack", "data-frames"))
    # pc: the call instruction with the build's disassembly, return address - 1 without (an import has none).
    key = lambda fr: [(f["name"], f["va"], f["method"], f.get("cfa")) for f in fr]
    r["import"] = " <- ".join(f"{f['name']}[{f['method']}]" for f in got)
    if key(got) != key(want):
        problems.append(f"imported vmlinux: call stack {key(got)} differs from the shipped CFI's {key(want)}")
    r["title"] = page.get_attribute("#call_stack", "title")
    if "imported vmlinux" not in (r["title"] or ""):
        problems.append(f"imported vmlinux: the call stack does not say where its CFI is from: {r['title']!r}")
    page.screenshot(path=str(shots / "unwind-import-1280.png"))
    # The variables pass of the Worker first (dbgTypes), then each frame's unit.
    wait_for(lambda: page.evaluate("dbgTypes().state") != "loading", 120)
    locs = locals_open_all(page, 60)
    r["locals"] = sum(len(sc["vars"]) for d in locs for sc in d["scopes"])
    if locs != want_locals:
        problems.append(f"imported vmlinux: Parameters and locals differ from the shipped shards': {locs} vs {want_locals}")
    if not page.evaluate("document.getElementById('locals').textContent.length > 0") or not r["locals"]:
        problems.append("imported vmlinux: no parameters and locals")
    names_js = """(pas) => pas.map((pa) => { const x = dbgLocals(pa);
        return x.state === 'ok' ? x.frames.map((f) => [f.func, f.params.map((v) => v[0]), f.locals.map((v) => v[0])]) : x.state; })"""
    rows_js = """async (pas) => { const out = [];
        for (const pa of pas) { let x = dbgCfiRow(pa);
            for (let i = 0; i < 50 && x === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); x = dbgCfiRow(pa); }
            out.push(x && x.cfa ? cfiRowText(x) : x); }
        return out; }"""
    pas = [f["pc"] for f in got]
    before = page.evaluate(rows_js, pas)
    lbefore = page.evaluate(names_js, pas)
    # Reload: rows from IndexedDB, without the file.
    page.goto("about:blank")
    page.goto(url)
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
    rows = page.evaluate(rows_js, pas)
    r["after_reload"] = rows
    wait_for(lambda: page.evaluate("dbgTypes().state") == "ok", 30)
    lafter = page.evaluate(names_js, pas)
    if lafter != lbefore:
        problems.append(f"after a reload: parameters and locals {lafter}, before {lbefore}")
    if rows != before or sum(1 for x in rows if isinstance(x, str) and x.startswith("CFA=")) < len(rows) - 1:
        problems.append(f"after a reload: CFI rows {rows}, before {before}")
    page.click("#sym_forget")
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") != "imported", 30)
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


def coverage_case(browser, base, shots):
    """The Page listing tints instructions that have not executed in this boot.

    Paused at the reset vector nothing is recorded, so nothing is tinted.
    After a counted boot (?speed=full: stepped to the end, then the JIT)
    start_kernel's first instruction is not tinted, die() (never called in
    a good boot) is, hex rows never are, and the key says how it is recorded.
    """
    problems, r = [], {}
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]}
    sk, die = syms["start_kernel"], syms["die"]
    tinted = "e => [...e.querySelectorAll('.nx')].map(x => [Number(x.dataset.addr), x.classList.contains('ins')])"

    def line_tinted(page, addr):
        return page.eval_on_selector(f"#listing .ins[data-addr='{addr}']", "e => e.classList.contains('nx')")

    def show_listing_line(page, addr):
        page.eval_on_selector(f"#listing .ins[data-addr='{addr}']", """e => {
            const box = document.getElementById('listing');
            box.scrollTop = e.offsetTop - box.offsetTop - 40;
            document.getElementById('page_title').scrollIntoView(); }""")

    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        problems.append("did not pause at the reset vector")
    open_page(page, sk["paddr"] // 4096)
    r["paused"] = {"coverage": page.get_attribute("#listing", "data-coverage"),
                   "tinted": len(page.eval_on_selector("#listing", tinted)), "key": page.inner_text("#cov_info")}
    if r["paused"]["tinted"] or r["paused"]["coverage"] != "none" or "No execution coverage" not in r["paused"]["key"]:
        problems.append(f"tinted before anything ran: {r['paused']}")
    page.close()

    for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
        page = browser.new_page(viewport={"width": width, "height": 1000}, color_scheme=scheme)
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
            problems.append(f"{scheme}/{width}: the counted boot did not end")
        open_page(page, sk["paddr"] // 4096)
        marks = page.eval_on_selector("#listing", tinted)
        cov = page.get_attribute("#listing", "data-coverage")
        key = page.inner_text("#cov_info")
        if width == 1280 and scheme == "light":
            r["start_kernel_page"] = {"coverage": cov, "tinted": len(marks), "unexecuted": page.get_attribute("#listing", "data-unexecuted"),
                                      "insns": page.get_attribute("#listing", "data-insns"), "key": key}
        if cov not in ("stepped", "partial") or "have not executed" not in key:
            problems.append(f"{scheme}/{width}: no coverage key after a counted boot: {cov!r} {key!r}")
        if line_tinted(page, sk["paddr"]):
            problems.append(f"{scheme}/{width}: start_kernel's first instruction is tinted as not executed")
        if any(not ins for _, ins in marks):
            problems.append(f"{scheme}/{width}: a hex row is tinted")
        if not no_hscroll(page, "body"):
            problems.append(f"{scheme}/{width}: horizontal scroll")
        show_listing_line(page, sk["paddr"])
        page.screenshot(path=str(shots / f"coverage-start_kernel-{scheme}-{width}.png"))
        open_page(page, die["paddr"] // 4096)
        if not line_tinted(page, die["paddr"]):
            problems.append(f"{scheme}/{width}: die() is not tinted as not executed")
        if width == 1280 and scheme == "light":
            r["die_page"] = {"tinted": len(page.eval_on_selector("#listing", tinted)), "insns": page.get_attribute("#listing", "data-insns")}
        show_listing_line(page, die["paddr"])
        page.screenshot(path=str(shots / f"coverage-die-{scheme}-{width}.png"))
        page.close()
    return problems, r


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
        const T = covTree(DBG.files);
        const walk = (d) => { COV.open.add(d.path); d.dirs.forEach(walk); };
        walk(T.root);
        DBG.files.files.forEach(([p]) => COV.open.add(p));
        covRender(); }""")


def covtree_case(browser, base, shots):
    """The Coverage tab after a counted minified boot: init/main.c has lines
    that ran, die() (never called in a good boot) has none, the root and every
    directory are the sums of their files, the tree's numbers agree with
    files.json and with the Source panel's whole-file view, a function opens
    the Page tab, the lcov export has the same totals; screenshots light/dark
    at 1280 and 390 px with no horizontal scroll."""
    problems, r = [], {}
    entry = kernel_entries()["minified-linux"]
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
        problems.append("the counted boot did not end")
    show_tab(page, "coverage")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 30)
    r["coverage"] = page.get_attribute("#cov_tree", "data-coverage")
    cov_open_all(page)
    rows = cov_rows(page)
    files = {x["path"]: x for x in rows if x["kind"] == "file"}
    root = next((x for x in rows if x["kind"] == "root"), None)
    r["root"] = root
    r["rows"] = len(rows)
    want_lines = sum(f[1] for f in F["files"])
    want_ins = sum(f[3] for f in F["funcs"] if f[1] >= 0)
    if not root or root["lines"] != want_lines or root["ins"] != want_ins or len(files) != len(F["files"]):
        problems.append(f"root {root} != files.json: {want_lines} lines, {want_ins} instructions, {len(F['files'])} files ({len(files)} rows)")
    for key in ("lines", "linesRun", "ins", "insRun"):
        total = sum(x[key] or 0 for x in files.values())
        if root and root[key] != total:
            problems.append(f"root {key} {root[key]} != sum of the files {total}")
    # Every directory is the sum of the files below it.
    for d in (x for x in rows if x["kind"] == "dir"):
        below = [f for p, f in files.items() if p.startswith(d["path"] + "/")]
        for key in ("lines", "linesRun"):
            if d[key] != sum(f[key] or 0 for f in below):
                problems.append(f"directory {d['path']}: {key} {d[key]} != sum of its files")
                break
    main = files.get("init/main.c")
    r["init/main.c"] = main
    if not main or not main["linesRun"] or not main["insRun"] or main["linesRun"] >= main["lines"]:
        problems.append(f"init/main.c: {main}")
    die = next((x for x in rows if x["kind"] == "func" and x["path"].endswith(":die")), None)
    r["die"] = die
    if not die or die["linesRun"] != 0 or die["insRun"] != 0 or not die["lines"] or not die["ins"]:
        problems.append(f"die() should be in the tree with nothing run: {die}")
    sk = next((x for x in rows if x["kind"] == "func" and x["path"].endswith(":start_kernel")), None)
    r["start_kernel"] = sk
    if not sk or not sk["insRun"] or sk["path"].split(":")[0] != "init/main.c":
        problems.append(f"start_kernel: {sk}")

    # Sorting by lines not run: the top directories, most first.
    page.select_option("#cov_sort", "notrun")
    top = page.eval_on_selector_all('#cov_tree .covrow[data-kind="dir"][data-depth="1"]',
                                    "es => es.map(e => [e.dataset.path, e.dataset.lines - e.dataset.linesRun])")
    r["most_not_run"] = top[:3]
    if len(top) < 2 or any(a[1] < b[1] for a, b in zip(top, top[1:])):
        problems.append(f"sort by lines not run: top directories {top}")
    page.select_option("#cov_sort", "path")
    # The filter keeps matching functions and their file.
    page.fill("#cov_filter", "start_kernel")
    fr = cov_rows(page)
    r["filter_rows"] = len(fr)
    if not any(x["path"].endswith(":start_kernel") for x in fr) or any(x["kind"] == "func" and "start_kernel" not in x["path"] for x in fr):
        problems.append(f"filter start_kernel: {[x['path'] for x in fr][:8]}")
    page.fill("#cov_filter", "")

    # The whole file in the Source panel: same lines with code and run.
    page.locator('#cov_tree .covrow[data-path="init/main.c"] .covname').click()
    wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 20)
    r["panel"] = [page.inner_text("#src_title"), page.get_attribute("#src_body", "data-code"), page.get_attribute("#src_body", "data-executed")]
    if main and (r["panel"][0] != "init/main.c (whole file)" or int(r["panel"][1] or -1) != main["lines"] or int(r["panel"][2] or -1) != main["linesRun"]):
        problems.append(f"Source panel of init/main.c {r['panel']} != tree {main}")
    page.screenshot(path=str(shots / "covtree-panel-1280-light.png"))
    page.click("#src_close")

    with page.expect_download() as dl:
        page.click("#cov_export")
    lcov = pathlib.Path(dl.value.path()).read_text()
    lf = sum(int(l[3:]) for l in lcov.splitlines() if l.startswith("LF:"))
    lh = sum(int(l[3:]) for l in lcov.splitlines() if l.startswith("LH:"))
    r["lcov"] = [lf, lh, lcov.count("SF:")]
    if root and (lf != root["lines"] or lh != root["linesRun"] or lcov.count("SF:") != len(F["files"])):
        problems.append(f"lcov LF/LH/files {r['lcov']} != tree {root}")

    for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": width, "height": 1000})
        page.fill("#cov_filter", "init")
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1") or not no_hscroll(page, "#cov_tree"):
            problems.append(f"{scheme}/{width}: horizontal scroll")
        page.evaluate("document.getElementById('panel_coverage').scrollIntoView()")
        page.screenshot(path=str(shots / f"covtree-{scheme}-{width}.png"))
    page.fill("#cov_filter", "")
    page.set_viewport_size({"width": 1280, "height": 1000})
    page.locator('#cov_tree .covrow[data-kind="func"][data-path$=":start_kernel"] .covname').click()
    syms = {x["name"]: x for x in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    if not wait_for(lambda: page.get_attribute("#listing", "data-page") == str(syms["start_kernel"]["paddr"] // 4096), 5):
        problems.append("tapping start_kernel in the tree did not open its page")
    # Its instructions run = the listing's instructions in it that are not tinted.
    fn = next(f for f in F["funcs"] if f[0] == "start_kernel")
    lo, hi = fn[4], fn[4] + fn[5]
    marks = page.eval_on_selector_all("#listing .ins[data-addr]", "es => es.map(e => [Number(e.dataset.addr), e.classList.contains('nx')])")
    inside = [nx for a, nx in marks if lo <= a < hi]
    r["start_kernel_listing"] = [len(inside), inside.count(False)]
    if sk and (len(inside) != sk["ins"] or inside.count(False) != sk["insRun"]):
        problems.append(f"start_kernel: listing {len(inside)} instructions, {inside.count(False)} not tinted; tree {sk}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Multi-line C statements: the line rows name only the first line of
# `asm volatile("..."\n "..." : ...);`, the rest of it must show as part of
# the statement (V3).
MULTILINE_POINTS = (
    # (file, function whose statement it is, or None: inlined everywhere)
    ("arch/x86/lib/string_32.c", "strlen"),
    ("arch/x86/include/asm/irqflags.h", "native_save_fl"),
)


def multiline_statement(text, func):
    """1-based (first, last) line of the first asm statement in func's body."""
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if re.search(r"\b" + func + r"\(void\)|\b" + func + r"\(const", l))
    first = next(i for i in range(start, len(lines)) if "asm volatile(" in lines[i])
    last = next(i for i in range(first, len(lines)) if lines[i].rstrip().endswith(");"))
    return first + 1, last + 1


def panel_line_marks(page):
    """{line: (classes, mark, title)} of the Source panel's lines."""
    title = page.inner_text("#src_title")
    m = re.search(r":(\d+)–\d+$", title)
    first = int(m.group(1)) if m else 1
    spans = page.eval_on_selector_all("#src_body > span", "es => es.map(e => [e.className, e.textContent, e.title])")
    return {first + i: (c.split(), t[6:7], ti) for i, (c, t, ti) in enumerate(spans)}


def multiline_case(browser, base, shots):
    """After a counted minified boot, every line of a multi-line asm statement
    that ran shows as run in the Source panel (whole file and the function
    view), the lines after the first marked as its continuation; the panel's
    counts (lines with their own code only) equal the Coverage tree's and
    the lcov export's for that file."""
    problems, r = [], {}
    entry = kernel_entries()["minified-linux"]
    src = SITE / entry["src"]
    syms = {x["name"]: x for x in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
        problems.append("the counted boot did not end")
    show_tab(page, "coverage")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 30)
    cov_open_all(page)
    files = {x["path"]: x for x in cov_rows(page, "file")}
    with page.expect_download() as dl:
        page.click("#cov_export")
    lcov, cur = {}, None
    for l in pathlib.Path(dl.value.path()).read_text().splitlines():
        if l.startswith("SF:"):
            cur = lcov.setdefault(l[3:], {})
        elif l.startswith(("LF:", "LH:")):
            cur[l[:2]] = int(l[3:])

    def check(label, lo, hi, ran=True):
        """Lines lo..hi of the panel: the first ran (or has code that did not
        run), every other one shows the same as its continuation."""
        marks = panel_line_marks(page)
        got = {l: marks.get(l, ([], "", ""))[:2] for l in range(lo, hi + 1)}
        r[label] = {l: " ".join(c) + " " + m for l, (c, m) in got.items()}
        want = "exec" if ran else "code"
        if want not in got[lo][0]:
            problems.append(f"{label}: line {lo}, the statement's first line, is not {want}: {got[lo]}")
        for l in range(lo + 1, hi + 1):
            c, mark = got[l]
            if want not in c or "cont" not in c:
                problems.append(f"{label}: line {l} of the statement {lo}-{hi} is not shown as its continuation ({want}): {c} {mark!r}")
            elif "statement" not in marks[l][2] or "not counted" not in marks[l][2].lower():
                problems.append(f"{label}: line {l}: tooltip {marks[l][2]!r}")
        own = [c for c, _, _ in marks.values() if ("exec" in c or "code" in c) and "cont" not in c]
        code, ran = int(page.get_attribute("#src_body", "data-code")), int(page.get_attribute("#src_body", "data-executed"))
        if code != len(own) or ran != sum("exec" in c for c in own):
            problems.append(f"{label}: panel counts {code}/{ran} != its lines with their own code {len(own)}/{sum('exec' in c for c in own)}")
        return code, ran

    # The statement finder on C that trips naive scanners: `;` and parens
    # in strings, characters and comments, a #define with continuations
    # inside a call, if headers, a label, else, an initializer, a statement
    # expression, macro calls without `;` outside functions. Expected: {first line: last line} of each multi-line one.
    snippet = [
        'int f(int a,',                        # 1  header, ends at the { of 2
        '      int b)',                         # 2
        '{',                                    # 3
        '\tstruct s x = {',                    # 4  initializer 4-7
        '\t\t.a = ";(",',                     # 5
        '\t\t.b = \')\',',                  # 6
        '\t};',                                # 7
        '\tif (a &&',                          # 8  header 8-9
        '\t    b)',                            # 9
        '\t\tfoo(a, /* ); */',                # 10 call 10-14, a directive inside
        '#define X(y) \\',                    # 11
        '\t(y);',                              # 12
        '\t\t    b,',                         # 13
        '\t\t    c);',                        # 14
        'out:',                                 # 15 label alone
        '\telse',                              # 16 else alone
        '\t\tx = ({ int t = a;',              # 17 statement expression 17-18
        '\t\t\tt; });',                      # 18
        '\treturn a; // ;(',                   # 19 one line
        '}',                                    # 20
        'DEFINE_A(x)',                          # 21 file scope macro calls
        '\tDEFINE_B(y)',                       # 22 without `;`: one line each
        'static int g(void)',                   # 23 a definition after them
        '{ return 0; }',                        # 24
    ]
    got = page.evaluate("""lines => { const s = srcStatements({ lines });
        return [Object.fromEntries(s.end), Array.from(s.of).slice(1, lines.length + 1)]; }""", snippet)
    r["statements"] = got[0]
    want = {"1": 3, "4": 7, "8": 9, "10": 14, "17": 18, "23": 24}
    if got[0] != want or got[1][10] or got[1][11]:
        problems.append(f"srcStatements: {got} != {want}, lines 11-12 (a #define) in no statement")

    def panel_open(js, arg):
        page.evaluate(js, arg)
        wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 20)

    for path, func in MULTILINE_POINTS:
        text = gzip.decompress((src / (path + ".gz")).read_bytes()).decode()
        lo, hi = multiline_statement(text, func)
        r[path + " statement"] = [lo, hi]
        if hi <= lo:
            problems.append(f"{path}: {func}'s asm statement {lo}-{hi} is one line")
            continue
        # The whole file (the Coverage tab's view).
        panel_open("p => openSourceFile(p)", path)
        code, ran = check(path + " whole file", lo, hi)
        tree, lc = files.get(path), lcov.get(path, {})
        r[path + " counts"] = {"panel": [code, ran], "tree": tree and [tree["lines"], tree["linesRun"]], "lcov": [lc.get("LF"), lc.get("LH")]}
        if not tree or [code, ran] != [tree["lines"], tree["linesRun"]] or [code, ran] != [lc.get("LF"), lc.get("LH")]:
            problems.append(f"{path}: panel, tree and lcov disagree: {r[path + ' counts']}")
        if path.endswith("string_32.c"):
            page.screenshot(path=str(shots / "multiline-whole-1280-light.png"))
        page.click("#src_close")
        # The function view at a copy of the statement that ran (inlined
        # into a caller: the view must reach the statement's last line).
        at = page.evaluate("""([p, l]) => { const s = fileLines(p).code.get(l) || [];
            const x = s.find(([a, b]) => stepper.countIn(a, b) > 0); return x ? x[0] : -1; }""", [path, lo])
        r[path + " ran at"] = hex(at) if at >= 0 else at
        if at < 0:
            problems.append(f"{path}:{lo} has no address that ran")
        else:
            panel_open("pa => openSource(pa)", at)
            check(path + " at " + hex(at), lo, hi)
            for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
                page.emulate_media(color_scheme=scheme)
                page.set_viewport_size({"width": width, "height": 1000})
                time.sleep(0.3)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{path} {scheme}/{width}: horizontal scroll")
                page.screenshot(path=str(shots / f"multiline-{path.split('/')[-1]}-{scheme}-{width}.png"))
            page.emulate_media(color_scheme="light")
            page.set_viewport_size({"width": 1280, "height": 1000})
            page.click("#src_close")
        # The out-of-line function, whose own copy did not run in this boot.
        if func in syms and path.endswith("string_32.c"):
            panel_open("pa => openSource(pa)", syms[func]["paddr"])
            check(path + " " + func + "()", lo, hi, ran=False)
            page.click("#src_close")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
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


STAMPED = ("style.css", "clock.js", "app.js", "stepper.js", "graph.js", "source.js", "symimport.js", "livedis.js", "fastforward.js", "cpustate.js", "v86/libv86.js")


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
        if "src" in kernel_entries().get(name, {}):
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


def report(label, problems, r=None, **extra):
    """One JSON line per check (flushed: the CI log shows when each ran); True if it failed."""
    print(json.dumps({label: problems or "ok", **extra, **{k: str(v) for k, v in (r or {}).items()}}), flush=True)
    return bool(problems)


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


def suite_symbols(ctx):
    lp, lr = linux_slow_case(ctx.browser, ctx.base, ctx.shots)
    sp, sr = symbol_case(ctx.browser, ctx.base, "minified-linux", ctx.shots)
    return report("linux_slow", lp, lr) | report("symbols", sp, sr)


def suite_debugger(ctx):
    failed = False
    for name in [n for n in ("hello", "minified-linux") if n in ctx.entries]:
        dp, dr = debugger_case(ctx.browser, ctx.base, name, None, ctx.shots)
        failed |= report("debugger", dp, dr, kernel=name)
    return failed


def suite_misc(ctx):
    """Symbol import, initrds, panic text, determinism, follow mode."""
    failed = report("import", *import_case(ctx.browser, ctx.base, ctx.shots))
    failed |= report("initrd", *initrd_case(ctx.browser, ctx.base, ctx.shots))
    if os.environ.get("MKB_THIRD_PARTY"):
        failed |= report("third_party", *third_party_case(ctx.browser, ctx.base, ctx.shots, os.environ["MKB_THIRD_PARTY"]))
    failed |= report("panic_text", *panic_text_case(ctx.browser, ctx.base, ctx.shots))
    if os.environ.get("MKB_DISTRO_CACHE"):
        failed |= report("distro_panic", *distro_panic_case(ctx.browser, ctx.base, os.environ["MKB_DISTRO_CACHE"]))
    failed |= report("determinism", *determinism_case(ctx.browser, ctx.base))
    failed |= report("follow", *follow_case(ctx.browser, ctx.base, ctx.shots))
    return failed


def debian_names(ctx):
    return [n for n in ctx.entries if n.startswith("debian") and "initrd" not in ctx.entries[n]]


def suite_debian(ctx):
    failed = False
    for name in debian_names(ctx):
        failed |= report("debian", *debian_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed


def suite_dwarf(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "hello") if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        failed |= report("dwarf", *dwarf_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed


def suite_dwarf_import(ctx):
    return report("dwarf_import", *dwarf_import_case(ctx.browser, ctx.base, ctx.shots))


def suite_types(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "debian-6.1-686") if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        failed |= report("types", *types_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed | report("types_import", *types_import_case(ctx.browser, ctx.base, ctx.shots))


def suite_runto(ctx):
    failed = report("runto", *runto_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("cpu_state", *cpu_state_case(ctx.browser, ctx.base, ctx.shots, "minified-linux"))


def suite_initramfs(ctx):
    failed = False
    for name in [n for n in ctx.entries if "initrd" in ctx.entries[n]]:
        failed |= report("initramfs", *initramfs_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed


def suite_tasks(ctx):
    failed = False
    for name in debian_names(ctx):
        failed |= report("tasks_case", *tasks_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed


def suite_tasks_minified(ctx):
    return report("tasks_case", *tasks_case(ctx.browser, ctx.base, ctx.shots, "minified-linux"), kernel="minified-linux")


def suite_coverage(ctx):
    failed = report("live_decode", *livedecode_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("coverage", *coverage_case(ctx.browser, ctx.base, ctx.shots))


def suite_covtree(ctx):
    failed = report("covtree", *covtree_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("multiline", *multiline_case(ctx.browser, ctx.base, ctx.shots))


def suite_unwind(ctx):
    failed, want = False, None
    for name, fn, steps in UNWIND_POINTS:
        if name in ctx.entries and "dwarf" in ctx.entries[name]:
            t0 = time.monotonic()
            p, r = unwind_case(ctx.browser, ctx.base, ctx.shots, name, fn, steps)
            frames = r.pop("frames", None)
            locs = r.pop("locals", None)
            if fn == "memblock_add_range":
                want = (frames, locs)
            failed |= report("unwind", p, r, kernel=name, at=fn, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = unwind_import_case(ctx.browser, ctx.base, ctx.shots, want)
    return failed | report("unwind_import", p, r, seconds=round(time.monotonic() - t0))


def suite_touch(ctx):
    failed = report("touch", touch_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("stale_cache", stale_cache_case(ctx.browser, ctx.args.timeout))


# name -> (run, needs the minified Linux). The workflow runs them in
# parallel jobs (--suites); without --suites all run, in this order.
SUITES = {
    "boot": (suite_boot, False),
    "symbols": (suite_symbols, True),
    "debugger": (suite_debugger, False),
    "misc": (suite_misc, True),
    "debian": (suite_debian, False),
    "dwarf": (suite_dwarf, False),
    "dwarf_import": (suite_dwarf_import, True),
    "types": (suite_types, True),
    "runto": (suite_runto, True),
    "initramfs": (suite_initramfs, False),
    "tasks": (suite_tasks, False),
    "tasks_minified": (suite_tasks_minified, True),
    "coverage": (suite_coverage, True),
    "covtree": (suite_covtree, True),
    "unwind": (suite_unwind, True),
    "touch": (suite_touch, False),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*", help="NAME or NAME:MEMBYTES (only the boot suite then runs)")
    ap.add_argument("--suites", help="comma-separated, of: " + ", ".join(SUITES))
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--probe", action="store_true", help="report, do not assert")
    ap.add_argument("--shots", default=str(SITE / "test-screenshots"))
    args = ap.parse_args()

    cases = []
    for c in args.cases:
        name, mem, speed = (c.split(":") + ["", ""])[:3]
        cases.append((name, int(mem) if mem else None, speed or None))
    names = args.suites.split(",") if args.suites else ["boot"] if args.cases else list(SUITES)
    unknown = [n for n in names if n not in SUITES]
    if unknown:
        ap.error("unknown suites: " + ", ".join(unknown))
    shots = pathlib.Path(args.shots)
    shots.mkdir(parents=True, exist_ok=True)

    httpd = serve()
    entries = kernel_entries()
    ctx = argparse.Namespace(args=args, cases=cases or default_cases(), shots=shots, entries=entries,
                             base=f"http://127.0.0.1:{httpd.server_address[1]}")
    failed = False
    with sync_playwright() as p:
        ctx.browser = p.chromium.launch()
        for name in names:
            run, needs_linux = SUITES[name]
            if needs_linux and "minified-linux" not in entries:
                continue
            t0 = time.monotonic()
            failed |= bool(run(ctx))
            print(f"suite {name}: {time.monotonic() - t0:.0f} s", flush=True)
        ctx.browser.close()
    httpd.shutdown()
    if failed and not args.probe:
        print("FAIL", file=sys.stderr)
        return 1
    print("done" if args.probe else "PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
