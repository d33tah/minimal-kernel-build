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
import base64
import functools
import gzip
import http.server
import json
import os
import pathlib
import random
import re
import shutil
import subprocess
import socketserver
import sys
import tarfile
import tempfile
import threading
import time

from playwright.sync_api import Error as PlaywrightError, sync_playwright

from test_dwarf import write_core

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
    if "hello-os" in names:
        cases.append(("hello-os", None, None))
    if "minified-linux" in names:
        cases.append(("minified-linux", None, "full"))
        cases.append(("minified-linux", 18 * 1024 * 1024, "full"))
        # The smallest RAM it boots with in v86 (measured: 2176 KiB fails).
        cases.append(("minified-linux", 2180 * 1024, "full"))
    if "minified-linux-os" in names:
        # The -Os product build: the same boot at its default and smallest RAM.
        cases.append(("minified-linux-os", None, "full"))
        cases.append(("minified-linux-os", 2180 * 1024, "full"))
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
        for t in ("page", "memory", "debugger", "calls", "snapshots", "about"):
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
    """Calls tab: flame timeline, call tree, top-30 graph, exports, tap."""
    show_tab(page, "calls")
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
    if not name.startswith("hello"):
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
    show_tab(page, "machine")
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
    show_tab(page, "machine")
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
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=100000#calls")
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


def tasks_case(browser, base, shots, name):
    """Task switches on a full counted run: a resumed task continues its
    suspended calls (no scheduler code as a root, no orphaned calls), one
    root per task, and the roots never overlap; tasks are named from their
    task_struct (task_names_check)."""
    problems = []
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel={name}&speed=full#calls")
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


PHONE = dict(viewport={"width": 390, "height": 844}, device_scale_factor=3, is_mobile=True, has_touch=True)


def phone_flow(browser, base, target, speed="1", phone=True, query="", before=None, scheme="light"):
    """The operator's phone flow: set the speed with the real control,
    tap Boot, tap Pause, type target into the Debugger's Run to field and
    tap Run to. before(page, act) runs between Pause and the Run to.
    Returns (page, context) left open at the end."""
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1100, "height": 1000}}))
    page = ctx.new_page()
    act = page.tap if phone else page.click
    page.goto(f"{base}/index.html?kernel=hello{query}")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") not in (None, "", "off"), 60)
    page.select_option("#dbg_speed", speed)
    act("#tab_machine")
    act("#boot")
    wait_for(lambda: page.is_enabled("#dbg_pause"), 60)
    act("#dbg_pause")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    act("#tab_debugger")
    if before:
        before(page, act)
    page.evaluate("window.mkbBefore = document.getElementById('regs').dataset.phys")
    act("#dbg_addr")
    page.fill("#dbg_addr", "")
    page.keyboard.type(target)
    act("#dbg_runto")
    # Settled: paused (not running to it, not fast-forwarding) with the status no longer "Running to".
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
             and not page.inner_text("#dbg_status").startswith(("Running to", "Finding")), 60)
    time.sleep(1)
    return page, ctx


def runto_phone_case(browser, base, shots):
    """Run to on a 390 px touch phone at 1 instruction per second (the
    operator's report: "Run to hello" ran on to "Still alive"), the hello
    kernel. A function name stops at its first instruction; a name that is
    no symbol, data (the "Hello, World!" string) and the middle of an
    instruction are refused where the phone shows it (the paused screen's
    overlay too) and the CPU stays paused; a stepped Run to whose target
    comes right after a stepping slice ended (here: the slice that printed
    "Hello, World!") stops there instead of stepping over it."""
    problems, r = [], {}
    entry = kernel_entries()["hello"]
    syms = json.loads((SITE / entry["symbols"]).read_text())
    fn = {x["name"]: x["paddr"] for x in syms["symbols"]}
    rodata = next(s for s in syms["sections"] if s["name"] == ".rodata")
    main = fn["hello_main"]

    def state(page):
        return {"state": page.get_attribute("#dbg_state", "data-state"),
                "phys": int(page.get_attribute("#regs", "data-phys") or -1),
                "status": page.inner_text("#dbg_status"),
                "overlay": page.inner_text("#vm_overlay") if page.is_visible("#vm_overlay") else "",
                "alive": ALIVE in page.inner_text("#screen_container")}

    def stopped_at(tag, page, pa):
        s = state(page)
        r[tag] = s["status"]
        if s["state"] != "paused" or s["phys"] != pa or s["alive"]:
            problems.append(f"{tag}: want paused at {pa:#x} before {ALIVE!r}, got {s}")

    def refused(tag, page, words):
        s = state(page)
        r[tag] = s["status"]
        if s["state"] != "paused" or s["alive"] or s["phys"] != int(page.evaluate("window.mkbBefore")):
            problems.append(f"{tag}: the CPU ran on: {s}")
        for w in words:
            if w not in s["status"] or w not in s["overlay"]:
                problems.append(f"{tag}: {w!r} missing from the status or the paused overlay: {s}")

    lines = json.loads((SITE / entry["disasm"]).read_text())["insns"]
    # The stores of a character to the VGA cell (movw to memory): -Os has
    # one per inlined put_line, -O0 one in put_line.
    stores = [a for a, _, ins, *rest in lines if ins.startswith("movw") and "(" in ins.split(",")[-1] and a > main]
    # The first instruction of hello_main longer than one byte.
    multi = next(a for a, b, *rest in lines if a >= main and len(b) > 2)
    for phone, speed in ((True, "1"), (True, "10"), (False, "1")):
        tag = ("phone" if phone else "desktop") + "@" + speed
        # At 10/s the Pause comes a few instructions into hello_main: Run
        # to the last character store instead (-Os: of "Still alive", -O0:
        # put_line's only one, first reached for "Hello, World!").
        target, want = ("hello_main", main) if speed == "1" else (hex(stores[-1]), stores[-1])
        page, ctx = phone_flow(browser, base, target, speed, phone)
        stopped_at(f"{tag} {target}", page, want)
        ctx.close()
        page, ctx = phone_flow(browser, base, "hello", speed, phone)
        refused(tag + " hello", page, ["hello", "hello_main"])
        if phone and speed == "1":
            page.screenshot(path=str(shots / "runto-phone-hello-390.png"), full_page=True)
        ctx.close()
    for scheme in ("light", "dark"):
        page, ctx = phone_flow(browser, base, hex(rodata["paddr"]), scheme=scheme)
        refused(f"phone data {scheme}", page, [hex(rodata["paddr"]), ".rodata"])
        page.screenshot(path=str(shots / f"runto-phone-data-390-{scheme}.png"), full_page=True)
        ctx.close()
    page, ctx = phone_flow(browser, base, hex(multi + 1))
    refused("phone mid-instruction", page, [hex(multi + 1), hex(multi)])
    ctx.close()

    # The last character of "Hello, World!": Run to its store, 12 more
    # times (each Run to from the target steps past it), then Run to the
    # loop's next instruction. Stepped (fastforward=0) and with the JIT.
    store = stores[0]
    after = next(a for a, *rest in lines if a > store)

    def to_last_store(page, act):
        for _ in range(len(HELLO)):
            page.fill("#dbg_addr", hex(store))
            page.evaluate("document.getElementById('dbg_status').textContent = ''")
            act("#dbg_runto")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                     and page.inner_text("#dbg_status").startswith("Stopped"), 30)
            time.sleep(0.3)
    for q in ("&fastforward=0", ""):
        page, ctx = phone_flow(browser, base, hex(after), query=q, before=to_last_store)
        stopped_at("phone after the Hello store" + (q or " (JIT)"), page, after)
        if HELLO not in page.inner_text("#screen_container"):
            problems.append(f"after the Hello store{q}: {HELLO!r} not on the screen yet")
        ctx.close()
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
    # Calls tab: a frame's hover text has where it was called from.
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
        show_tab(page, "calls")
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
    # Calls tab: the detail of the innermost open frame names its call site and entry with file:line.
    show_tab(page, "calls")
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
    titles = page.eval_on_selector_all("#listing .src", "els => els.map(e => e.dataset.tip || e.title)")
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
    if page.get_attribute("#src_pick", "hidden") is not None or "file:line only" not in r["src_status_before"] or any(source_texts(page)):
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
    show_tab(page, "machine")
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
        show_tab(page, "machine")
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
        show_tab(page, "machine")
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


def boxed_line_ink(page, sel):
    """Whether the underscores of the boxed line sel (the EIP line: .cur, an
    outline drawn 1 px inside its box) are drawn: for every `_` of its
    text, some pixel row between the box's outline rows has ink (pixels
    far from both the background and the outline colour) across at
    least a third of the character cell. The screenshot is decoded in
    the page (canvas): no imaging module needed. [(char index, ok)] or
    None when there is no such line."""
    geo = page.evaluate("""(sel) => { const el = document.querySelector(sel); if (!el) return null;
        el.scrollIntoView({ block: 'center' });
        const cs = getComputedStyle(el), box = el.getBoundingClientRect(), cells = [];
        const w = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
        let k = 0;
        for (let n; (n = w.nextNode());)
            for (let i = 0; i < n.data.length; i++, k++)
                if (n.data[i] === '_') {
                    const rg = document.createRange(); rg.setStart(n, i); rg.setEnd(n, i + 1);
                    const r = rg.getBoundingClientRect(); cells.push([k, r.left, r.right]);
                }
        const rgb = (c) => (c.match(/[\\d.]+/g) || []).slice(0, 3).map(Number);
        let bg = cs.backgroundColor, e = el;
        while (e && /^rgba\\(.*, 0\\)$/.test(bg)) { e = e.parentElement; bg = getComputedStyle(e || document.body).backgroundColor; }
        return { top: box.top, bottom: box.bottom, cells, bg: rgb(bg), ol: rgb(cs.outlineColor) }; }""", sel)
    if not geo:
        return None
    out = []
    for k, left, right in geo["cells"]:
        clip = {"x": left, "y": geo["top"] - 2, "width": max(1, right - left), "height": geo["bottom"] - geo["top"] + 4}
        png = base64.b64encode(page.screenshot(clip=clip)).decode()
        ok = page.evaluate("""async ([b64, bg, ol]) => {
            const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
            const c = document.createElement('canvas'); c.width = img.width; c.height = img.height;
            const x = c.getContext('2d'); x.drawImage(img, 0, 0);
            const d = x.getImageData(0, 0, c.width, c.height).data;
            const dist = (i, q) => Math.abs(d[i] - q[0]) + Math.abs(d[i + 1] - q[1]) + Math.abs(d[i + 2] - q[2]);
            const rows = [];
            for (let y = 0; y < c.height; y++) {
                let ink = 0, line = 0;
                for (let x0 = 0; x0 < c.width; x0++) {
                    const i = (y * c.width + x0) * 4;
                    if (dist(i, ol) < 60) line++;
                    else if (dist(i, bg) > 90) ink++;
                }
                rows.push([ink / c.width, line / c.width]);
            }
            // The outline's rows: the first and last mostly in its colour.
            const lines = rows.map((r, y) => r[1] > 0.6 ? y : -1).filter((y) => y >= 0);
            if (lines.length < 2) return false;
            const a = lines.find((y, j) => j + 1 < lines.length && lines[j + 1] > y + 1), b = lines[lines.length - 1];
            if (a === undefined) return false;
            for (let y = a + 1; y < b; y++) if (rows[y][0] >= 0.34) return true;
            return false; }""", [png, geo["bg"], geo["ol"]])
        out.append((k, ok))
    return out


def eip_box_check(page, name, problems, r, shots):
    """The Debugger's boxed EIP line draws the underscores of its names
    (`start_kernel+0x0`, `<__fentry__>`), light and dark."""
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        got = boxed_line_ink(page, "#context .ins.cur")
        text = page.inner_text("#context .ins.cur") if page.locator("#context .ins.cur").count() else ""
        r[f"eip_box_{scheme}"] = f"{text!r}: {got}"
        if not got:
            problems.append(f"{name}: no underscore in the boxed EIP line to check ({text!r})")
        elif not all(ok for _, ok in got):
            problems.append(f"{name}: the boxed EIP line hides underscores ({scheme}): {text!r} {got}")
        page.locator("#context").screenshot(path=str(shots / f"eip-box-{name}-{scheme}.png"))
    page.emulate_media(color_scheme="light")


def var_decl(T, v):
    """Where types.json says variable v is defined: (file, line) or (None, None)."""
    return (T["files"][v[7]], v[8]) if "files" in T and len(v) > 7 and v[7] >= 0 else (None, None)


def vardecl_checks(page, name, T, off, elf, shots, problems, r):
    """Variables say where they are defined (DW_AT_decl_file/decl_line):
    the watch row's "defined at" (gdb's symbol's file:line) opens the
    Source panel there; for a name several variables share, the note lists
    the other definitions, and `file.c::name` (path or unique suffix)
    picks the one gdb's &'file.c'::name is; an unknown or ambiguous file
    is refused with the definitions; the hex-hover text and the memory
    operands' tooltips say it too. Screenshots light/dark, 1280/390 px."""
    from test_dwarf import gdb_vars
    use_gdb = bool(elf and shutil.which("gdb"))
    if not use_gdb and os.environ.get("CI"):
        problems.append(f"{name}: no gdb or -g ELF to check where variables are defined")
    if "files" not in T:
        problems.append(f"{name}: types.json has no files: variables do not say where they are defined")
        return
    show_tab(page, "debugger")
    it = next(v for v in T["vars"] if v[0] == "init_task")
    sel = '#watches .watch[data-expr="init_task"]'
    if not page.locator(sel).count():
        watch(page, "init_task")
    loc = page.locator(sel + " .wdecl .varloc")
    want = var_decl(T, it)
    have = (loc.get_attribute("data-file"), int(loc.get_attribute("data-line") or 0)) if loc.count() else None
    r["init_task defined at"] = have
    if have != want or want[0] is None:
        problems.append(f"{name}: init_task's watch says defined at {have}, types.json {want}")
    if use_gdb:
        g = gdb_vars(str(elf), [["init_task", "", it[1] + off, "", True]])[0]
        r["init_task gdb"] = g
        if g["why"] or not same_path(g["file"], want[0]) or g["line"] != want[1]:
            problems.append(f"{name}: init_task defined at {want}, gdb {g}")
    if loc.count():
        loc.click()
        if not wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-file") == want[0]
                        and page.locator(f'#src_body [data-line="{want[1]}"].pick').count() > 0, 60):
            problems.append(f"{name}: init_task's 'defined at' did not open {want[0]}:{want[1]} in the Source panel "
                            f"({page.inner_text('#src_body')[:200]!r})")
        else:
            r["init_task decl line"] = page.inner_text(f'#src_body [data-line="{want[1]}"]').strip()[:90]
            if "init_task" not in r["init_task decl line"]:
                problems.append(f"{name}: the Source panel's line {want[1]} does not define init_task: {r['init_task decl line']!r}")
        page.click("#src_close")
    # A qualifier no definition matches is refused.
    bad_q = watch(page, "nosuch/file.c::init_task")
    r["unknown file"] = bad_q
    if "not defined in" not in bad_q:
        problems.append(f"{name}: nosuch/file.c::init_task is not refused: {bad_q!r}")
    page.locator('#watches .watch[data-expr="nosuch/file.c::init_task"] .wdel').click()
    # A name several file statics share (Debian): the note lists the others; file::name picks by file.
    count, files = {}, {}
    for v in T["vars"]:
        count[v[0]] = count.get(v[0], 0) + 1
        f = var_decl(T, v)[0]
        if f and not v[5] & 4:
            files.setdefault(v[0], {}).setdefault(f, []).append(v)
    shared = sorted(n for n, fs in files.items() if len(fs) >= 3 and all(len(x) == 1 for x in fs.values()))
    if name == "debian-6.1-686" and not shared:
        problems.append(f"{name}: no name of several file statics to pick from")
    if shared:
        nm = "_acpi_module_name" if "_acpi_module_name" in shared else shared[0]
        text = watch(page, nm)
        wsel = f'#watches .watch[data-expr="{nm}"]'
        others = page.locator(wsel + " .wothers .varloc")
        r["shared"] = f"{nm}: {count[nm]} variables, note: " + (page.inner_text(wsel + " .wothers")[:160] if others.count() else "none")
        if f"{count[nm]} variables have this name" not in text or others.count() != min(count[nm] - 1, 20):
            problems.append(f"{name}: watch {nm} does not list the other {count[nm] - 1} definitions: {text[:300]!r}")
        chosen = int(page.get_attribute(wsel, "data-addr") or -1)
        # Each listed definition: its file:line is types.json's for the variable at that address.
        for k in range(others.count()):
            o = others.nth(k)
            v = next((x for x in T["vars"] if x[0] == nm and x[1] == int(o.get_attribute("data-var"))), None)
            if v is None or v[1] == chosen or var_decl(T, v) != (o.get_attribute("data-file"), int(o.get_attribute("data-line"))):
                problems.append(f"{name}: {nm}'s note lists {o.inner_text()!r} at {o.get_attribute('data-var')}, types.json {v}")
                break
        rnd = __import__("random").Random(5)
        fs = files[nm]
        picks = rnd.sample(sorted(f for f, vs in fs.items() if vs[0][1] != chosen), 2)
        probes, exprs = [], []
        for f in picks:
            v = fs[f][0]
            base = f.rsplit("/", 1)[-1]
            short = base if sum(1 for g in fs if g == base or g.endswith("/" + base)) == 1 else f
            for q in sorted({f, short}):
                exprs.append((q + "::" + nm, v))
            probes.append([nm, "", v[1] + off, f])
        for e, v in exprs:
            watch(page, e)
            got = page.get_attribute(f'#watches .watch[data-expr="{e}"]', "data-addr")
            r[e] = got
            if got != str(v[1]):
                problems.append(f"{name}: {e} picks {got}, the variable defined in that file is at {v[1]}")
        if use_gdb:
            for p, g in zip(probes, gdb_vars(str(elf), probes)):
                r[f"gdb &'{p[3]}'::{nm}"] = g["qual"]
                if g["qual"] != p[2]:
                    problems.append(f"{name}: gdb &'{p[3]}'::{nm} = {g['qual']}, the page's is {p[2]:#x}")
    # Several files end with the qualifier: refused, listing them.
    amb = None
    for n, fs in sorted(files.items()):
        base = {}
        for f in fs:
            base.setdefault(f.rsplit("/", 1)[-1], set()).add(f)
        b = next((b for b, x in sorted(base.items()) if len(x) > 1), None)
        if b:
            amb = (b, n)
            break
    if amb:
        e = amb[0] + "::" + amb[1]
        t = watch(page, e)
        r["ambiguous"] = t[:200]
        if "ambiguous" not in t:
            problems.append(f"{name}: {e} is not refused as ambiguous: {t[:200]!r}")
        page.locator(f'#watches .watch[data-expr="{e}"] .wdel').click()
    page.locator("#watches").scroll_into_view_if_needed()
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#watches").screenshot(path=str(shots / f"vardecl-watches-{name}-1280-{scheme}.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Variables with 'defined at' scroll horizontally at 390 px ({scheme})")
        page.locator("#watches .wothers, #watches .wdecl").first.scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"vardecl-watches-{name}-390-{scheme}.png"))
    page.set_viewport_size({"width": 1280, "height": 1000})
    # The Page listing: the hovered byte's text, and the memory operands' tooltips.
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    pa = it[1] + 16 + it[3] // 3
    hover_byte(page, pa, click=True)
    vl = page.locator("#var_info .varloc")
    r["hover"] = page.inner_text("#var_info")[-160:]
    if not vl.count() or (vl.first.get_attribute("data-file"), int(vl.first.get_attribute("data-line") or 0)) != want or \
            f"defined at {want[0]}:{want[1]}" not in page.inner_text("#var_info"):
        problems.append(f"{name}: the hovered byte of init_task does not say defined at {want}: {r['hover']!r}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        # Just below the sticky header and tabs (an element screenshot would show the header over it).
        page.evaluate("""() => { const tabs = document.querySelector('[role=tablist]').getBoundingClientRect().bottom;
            window.scrollBy(0, document.getElementById('var_info').getBoundingClientRect().top - tabs - 40); }""")
        page.screenshot(path=str(shots / f"vardecl-hover-{name}-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    sk = next(s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"] if s["name"] == "start_kernel")
    open_page(page, sk["paddr"] // 4096)
    wait_for(lambda: page.locator('#listing .opn[data-kind="var"]').count() > 0, 60)
    ops = page.evaluate("""() => [...document.querySelectorAll('#listing .opn[data-kind="var"], #listing .opn[data-kind="percpu"]')]
        .map((e) => [e.dataset.name, +e.dataset.va, e.title])""")
    checked, probes = 0, []
    for nm, va, title in ops:
        # "&name" for a lea (-O0 takes addresses of globals with lea).
        root = re.match(r"&?(\w+)", nm)[1]
        v = next((x for x in T["vars"] if x[0] == root and x[1] <= va - off < x[1] + max(x[3], 1)), None)
        if v is None:
            continue
        f, ln = var_decl(T, v)
        checked += 1
        if f and f"defined at {f}:{ln}" not in title or not f and "defined at" in title:
            problems.append(f"{name}: operand <{nm}> tooltip {title!r} does not say defined at {f}:{ln}")
        if f and len(probes) < 8 and root not in [p[0] for p in probes]:
            probes.append([root, v[6] if v[5] & 4 and len(v) > 6 else "", v[1] + off, "", True, f, ln])
    r["operands with defined at"] = f"{checked} of {len(ops)}"
    if not checked:
        problems.append(f"{name}: no global operand on start_kernel's page to check")
    if use_gdb and probes:
        g = gdb_vars(str(elf), [p[:5] for p in probes])
        diff = [(p[0], p[5], p[6], x) for p, x in zip(probes, g) if not x["why"] and (not same_path(x["file"], p[5]) or x["line"] != p[6])]
        r["operand vars vs gdb"] = f"{sum(1 for x in g if not x['why']) - len(diff)}/{len(g)} = gdb"
        if diff:
            problems.append(f"{name}: operand variables defined elsewhere for gdb: {diff[:3]}")
        if sum(1 for x in g if x["why"]) > len(g) // 2:
            problems.append(f"{name}: gdb confirms only {sum(1 for x in g if not x['why'])} of {len(g)} operand variables: {g[:3]}")
    show_tab(page, "debugger")


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
    eip_box_check(page, name, problems, r, shots)
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
    vardecl_checks(page, name, T, off, elf, shots, problems, r)
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
            vars: T.vars, sections: T.sections, files: T.files }); }"""
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
        show_tab(page, "machine")
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
    if not r["panel"].startswith("start_kernel, init/main.c:") or not 0 < r["panel_executed"] < r["panel_code"]:
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
    """Debian's Source tab at start_kernel: the whole tree from files.json
    without reading every shard (only those with code that ran), totals
    equal files.json, init/main.c has lines that ran."""
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    shards = json.loads((SITE / entry["dwarf"] / "index.json").read_text())["shards"]
    page.click("#src_close")
    show_tab(page, "source")
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
    page.evaluate("document.getElementById('panel_source').scrollIntoView()")
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
            # gdb also lists, as optimized out, the variables of a block whose
            # concrete copy has no DW_AT_abstract_origin, out of scope or
            # twice (test_dwarf.py gdb_extra_explained, with llvm-dwarfdump).
            from test_dwarf import gdb_extra_explained
            at = g["pc"] - (1 if g["pc"] != want[0]["pc"] else 0)  # a caller's block: the call, before the return address
            mine = [(v["name"], "out" if v["state"] == "out" else "x") for v in sc["vars"]]
            theirs = [(n, "out" if val == "out" else "x") for n, val in g["vars"]]
            if not gdb_extra_explained(str(elf), at, theirs, mine, sc["func"]):
                bad.append(f"frame {i} ({sc['func']}) at {at:#x}: variables page {sorted(mine)} gdb {sorted(theirs)}")
                continue
            g = {**g, "vars": [x for x in g["vars"] if x[0] in {v["name"] for v in sc["vars"]}]}
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
    shown = sum(1 for d in locs for sc in d["scopes"] for v in sc["vars"] if v["state"] not in ("out", "unmapped"))
    r[tag + " locals"] = f"{len(locs)} frames, {nv} variables, {shown} with a value; frame 0: " + "; ".join(
        f"{sc['func']}: " + ", ".join(f"{v['name']}={v['value']}" for v in sc["vars"]) for sc in (locs[0]["scopes"] if locs else []))
    if not locs or locs[0]["state"] != "ok" or not any(d["scopes"] for d in locs):
        problems.append(f"no parameters and locals in any frame: {locs[:2]}")
    for d in locs:
        for sc in d["scopes"]:
            for v in sc["vars"]:
                if not v["loc"].strip() or (v["state"] in ("out", "unmapped") and v["value"]):
                    problems.append(f"{sc['func']}.{v['name']}: no location or reason shown ({v})")
    if elf:
        res = locals_compare(elf, locs, dump)
        if res is not None:
            same, more, bad = res
            r[tag + " locals vs gdb"] = f"{same} values equal, {more} only gdb has (entry values it resolves), {len(bad)} differ"
            r["locals_equal"] = r.get("locals_equal", 0) + same
            problems += bad
    return problems


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


# Pauses where the instruction at EIP reads or writes a parameter or local
# on the stack: (kernel, function, offset, the variable the operand is).
OPERAND_POINTS = [
    ("minified-linux", "memblock_add_range", 0xa, "type"),  # -O0: movl %ecx, 0x18(%esp): type into its slot
    ("minified-linux-os", "memblock_add_range", 0x2b, "type"),  # -Os: movl (%esp), %eax: clang spills type at ESP+0
    ("debian-6.1-686", "start_kernel", 0x20, "command_line"),  # mov %ebx, -0x1c(%ebp): GCC, DW_OP_fbreg -36
]

# The memory operand names of the listing lines in a container: per
# instruction its address, text and names.
OPNAMES_JS = """(sel) => [...document.querySelectorAll(sel + ' .ins')].map((l) => ({
    pa: +l.dataset.addr, cur: l.classList.contains('cur'), text: l.textContent,
    ops: [...l.querySelectorAll('.opn')].map((o) => ({ op: o.dataset.op, name: o.dataset.name, kind: o.dataset.kind,
        va: o.dataset.va === undefined ? null : +o.dataset.va, title: o.title })) }))"""

# Runs inside gdb on a core of the pause: `info symbol` of ADDRS, and for
# each path of PATHS its [address, size] in the first frame (innermost,
# inlined calls included) where it is an lvalue, or None.
GDB_OPNAMES = r"""
import gdb, json
syms = []
for a in ADDRS:
    try:
        syms.append(gdb.execute("info symbol %d" % a, to_string=True).strip())
    except Exception as e:
        syms.append("error: %s" % e)
frames, f = [], gdb.newest_frame()
while f is not None and len(frames) < 8:
    frames.append(f)
    if f.type() != gdb.INLINE_FRAME:
        break
    f = f.older()
at = []
for path in PATHS:
    got = None
    for f in frames:
        try:
            f.select()
            v = gdb.parse_and_eval(path)
            if v.address is not None:
                got = [int(v.address), v.type.sizeof]
                break
        except gdb.error:
            pass
    at.append(got)
json.dump([syms, at], open(DST, "w"))
"""


def gdb_opnames(elf, dump, addrs, paths):
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "ops.py", "ops.json"))
        regs = {**dump["regs"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b}
        write_core(core, regs, dump["pages"])
        pathlib.Path(py).write_text(f"ADDRS, PATHS, DST = {addrs!r}, {paths!r}, {dst!r}\n" + GDB_OPNAMES)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=300)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else None


def opnames_vs_gdb(elf, dump, lines, problems, r, tag):
    """The names in listing lines against gdb on a core of the pause: a
    global (DWARF variable, per-CPU template or ELF symbol) is what `info
    symbol` says at its address; a parameter or local with an address
    (the operand at EIP, stack slots of EIP's frame) is where gdb puts
    that path in EIP's frame. Returns the number of names compared."""
    glob = [o for l in lines for o in l["ops"] if o["kind"] in ("var", "percpu", "sym") and o["va"] is not None]
    loc = [o for l in lines for o in l["ops"] if o["kind"] in ("param", "local") and o["va"] is not None]
    paths = sorted({re.sub(r"\+\d+$", "", o["name"].lstrip("&")) for o in loc})
    got = gdb_opnames(elf, dump, sorted({o["va"] for o in glob}), paths)
    if got is None:
        problems.append(f"{tag}: gdb wrote nothing")
        return 0
    syms = dict(zip(sorted({o["va"] for o in glob}), got[0]))
    at = dict(zip(paths, got[1]))
    n = 0
    # ELF symbols by address (llvm-nm): clang names a static local
    # function.name, which gdb prints demangled in odd ways.
    nm_at = {}
    for f in (l.split() for l in subprocess.run(["llvm-nm", "--defined-only", str(elf)], capture_output=True, text=True).stdout.splitlines()):
        if len(f) == 3:
            nm_at.setdefault(int(f[0], 16), set()).add(f[2])
    for o in glob:
        s = syms[o["va"]]
        m = re.match(r"^(\S+)(?: \+ (\d+))? in section", s)
        # "&name" for a lea; "a=b" for linker marks sharing an address.
        root = re.match(r"&?([A-Za-z_][\w.]*)", o["name"]).group(1)
        off = int(re.search(r"\+(\d+)$", o["name"]).group(1)) if o["kind"] != "sym" and re.search(r"\+(\d+)$", o["name"]) else 0
        if not m:
            problems.append(f"{tag}: {o['op']} <{o['name']}> but gdb: {s}")
            continue
        # The variable (or symbol) gdb names must start the page's path; a
        # static local is name.N (GCC) or function.name (clang) in the ELF.
        gname, var = m.group(1), root.split(".")[0]
        gaddr = o["va"] - int(m.group(2) or 0)
        if not (var == gname or re.fullmatch(re.escape(var) + r"\.\d+", gname) or
                o["kind"] == "sym" and gname in o["name"].lstrip("&").split("=") or
                o["kind"] != "sym" and any(x.endswith("." + var) and gname.startswith(x[:-len(var) - 1]) for x in nm_at.get(gaddr, ()))):
            problems.append(f"{tag}: {o['op']} <{o['name']}> but gdb: {s}")
            continue
        n += 1
    for o in loc:
        path = re.sub(r"\+\d+$", "", o["name"].lstrip("&"))
        a = at.get(path)
        if a and a[0] <= o["va"] < a[0] + max(a[1], 1):
            n += 1
            r[tag + " locals equal"] = r.get(tag + " locals equal", 0) + 1
        elif a is None:
            r[tag + " locals gdb has no address for"] = r.get(tag + " locals gdb has no address for", []) + [o["name"]]
        else:
            problems.append(f"{tag}: {o['op']} <{o['name']}> at {o['va']:#x}, gdb puts {path} at {a[0]:#x} ({a[1]} bytes)")
    return n


def operands_case(browser, base, shots, name, fn, off, want):
    """Memory operands in the listings are named from the DWARF by one
    helper (asmnames.js through vars.js opNamesHtml) in the Debugger and
    the Page tab's merged hex + asm listing: at a pause on an instruction
    that uses a stack slot, that operand names the parameter or local
    (at its address in this pause), absolute addresses name the variable
    and member (or ELF symbol), and gdb on a core of the same registers
    and stack agrees with every name that has an address. The Page tab's
    lines carry the same names as the Debugger's; tooltips say what each
    is; no horizontal scroll at 390 px."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    va, pa = sk["vaddr"] + off, sk["paddr"] + off
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(va))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(pa) or "Halted" in page.inner_text("#dbg_status"), 240) \
            or page.get_attribute("#regs", "data-phys") != str(pa):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn}+{off:#x} did not stop there: {status}"], r
    # The names come once the types, the CFI and the locals shard are in.
    wait_for(lambda: page.evaluate("[...document.querySelectorAll('#context .ins.cur .opn')].length") > 0, 60)
    dbg = page.evaluate(OPNAMES_JS, "#context")
    cur = next((l for l in dbg if l["cur"]), None)
    r["debugger"] = [f"{l['pa']:#x} " + " ".join(f"<{o['name']}>:{o['kind']}" for o in l["ops"]) for l in dbg if l["ops"]]
    if not cur or not any(o["name"] == want and o["kind"] in ("param", "local") and o["va"] is not None for o in cur["ops"]):
        problems.append(f"{name}: the operand at EIP does not name {want} with its address: {cur}")
    for l in dbg:
        for o in l["ops"]:
            if not o["title"] or o["name"] not in o["title"]:
                problems.append(f"{name}: <{o['name']}> has no tooltip saying what it is: {o['title']!r}")
    elf = debug_elf(name)
    dump = page.evaluate(UNWIND_DUMP_JS)
    if elf and shutil.which("gdb"):
        r["debugger vs gdb"] = opnames_vs_gdb(elf, dump, dbg, problems, r, f"{name} Debugger")
    elif os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR or no gdb")
    page.emulate_media(color_scheme="light")
    page.locator("#context").screenshot(path=str(shots / f"operands-{name}-debugger-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    time.sleep(0.2)
    page.locator("#context").screenshot(path=str(shots / f"operands-{name}-debugger-1280-dark.png"))
    # The Page tab: the same lines, the same names; the whole page's globals vs gdb.
    page.click(f"#context .ins.cur")
    wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true" and
             page.get_attribute("#listing", "data-page") == str(pa // 4096) and
             page.evaluate("document.querySelectorAll('#listing .ins .opn').length") > 0, 60)
    time.sleep(0.5)
    lst = page.evaluate(OPNAMES_JS, "#listing")
    by = {l["pa"]: l for l in lst}
    same = 0
    for l in dbg:
        o = by.get(l["pa"])
        if o is None:
            continue
        if [(x["name"], x["kind"]) for x in o["ops"]] != [(x["name"], x["kind"]) for x in l["ops"]]:
            problems.append(f"{name}: {l['pa']:#x} Page tab {[x['name'] for x in o['ops']]}, Debugger {[x['name'] for x in l['ops']]}")
        else:
            same += 1
    r["page lines equal to the debugger's"] = same
    kinds = {}
    for l in lst:
        for o in l["ops"]:
            kinds[o["kind"]] = kinds.get(o["kind"], 0) + 1
    r["page names"] = kinds
    info = page.inner_text("#dump_info")
    if f"{sum(kinds.values())} memory operands named" not in info:
        problems.append(f"{name}: the listing's info line does not count the named operands: {info!r}")
    if elf and shutil.which("gdb"):
        r["page vs gdb"] = opnames_vs_gdb(elf, dump, lst, problems, r, f"{name} Page")
    if not kinds.get("var"):
        problems.append(f"{name}: no global variable named in the page of {fn}: {kinds}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        line = page.locator("#listing .ins.cur")
        if line.count():
            line.scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"operands-{name}-page-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Page tab scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"operands-{name}-page-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Line -> address: per kernel, start_kernel statements of init/main.c (found
# by their text) and how the case reaches each: "panel" (click the line in
# the Source panel, then its Run to button) or "typed" (main.c:LINE in the
# Debugger's Run to field). local_irq_enable() is all inlined code (the
# breakpoint is where the inlined call starts).
LINEADDR_POINTS = {
    "minified-linux": [("setup_arch(", "panel"), ("sched_init();", "typed"), ("local_irq_enable();", "panel")],
    "debian-6.1-686": [("setup_arch(", "panel"), ("sched_init();", "typed")],
}


def main_c_lines(name, texts):
    """The line of init/main.c's start_kernel that starts with each text (the shipped source)."""
    src = gzip.decompress((SITE / "kernels" / name / "src" / "init" / "main.c.gz").read_bytes()).decode().splitlines()
    first = next(k for k, l in enumerate(src) if "start_kernel(void)" in l)
    return [next(k + 1 for k in range(first, len(src)) if src[k].strip().startswith(t)) for t in texts]


def line_code(page):
    """The Source panel's address list once loaded: (line, bps, rows, calls)."""
    wait_for(lambda: page.get_attribute("#src_addrs", "data-state") in ("ok", "none"), 60)
    g = lambda k: json.loads(page.get_attribute("#src_addrs", "data-" + k) or "[]")
    return int(page.get_attribute("#src_addrs", "data-line") or 0), g("bps"), g("rows"), g("calls")


def at_line(frames, path, line, inner):
    """llvm-symbolizer frames (innermost first) are path:line: the innermost one, or (not inner) any."""
    hit = lambda f: f[2] == line and (f[1] == path or f[1].endswith("/" + path))
    return bool(frames) and (hit(frames[0]) if inner else any(hit(f) for f in frames))


def run_to_wait(page, bps, timeout=240):
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                  int(page.get_attribute("#regs", "data-phys") or -1) in bps, timeout)
    return ok, int(page.get_attribute("#regs", "data-phys") or -1), page.inner_text("#dbg_status")


def lineaddr_case(browser, base, shots, name):
    """Line -> address: clicking a C line in the Source panel lists the
    addresses of its code (every inlined copy, every call inlined on it),
    each a jump to the Page tab; its Run to (and main.c:LINE typed in the
    Debugger's Run to field) stops at one of the line's breakpoint
    addresses, where llvm-symbolizer on the -g ELF names that line
    (innermost, or as the call site of inlined code). Every listed range
    is that line for llvm-symbolizer. A C line of the Page listing opens
    the panel with its addresses. Screenshots light/dark at 1280 and 390
    px, no horizontal scroll."""
    problems, r = [], {}
    points = LINEADDR_POINTS[name]
    lines = main_c_lines(name, [t for t, _ in points])
    r["lines"] = lines
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    off = syms[0]["vaddr"] - syms[0]["paddr"]
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("openSourceFile('init/main.c')")
    first_bps = None
    for (text, how), line in zip(points, lines):
        t0 = time.monotonic()
        if how == "panel":
            if page.is_hidden("#src_panel"):
                page.click("#cur_src .frame button")  # the Debugger's source row opens the function
                wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
            row = page.locator(f'#src_body [data-line="{line}"]')
            if not wait_for(lambda: row.count() > 0, 30):
                problems.append(f"{name}: no clickable line {line} in the Source panel")
                break
            row.click()
            got_line, bps, rows, calls = line_code(page)
            if got_line != line or not bps:
                problems.append(f"{name}: main.c:{line} in the panel lists line {got_line}, breakpoints {bps}")
                break
            # Every listed range (first and last byte) and inlined call is that line for llvm-symbolizer.
            if elf:
                probes = [a for a, b in rows for a in (a, b - 1)]
                fr = symbolize_frames(elf, [a + off for a in probes + [c[0] for c in calls]])
                wrong = [hex(a) for a, f in zip(probes, fr) if not at_line(f, "init/main.c", line, True)]
                wrong += [hex(c[0]) for c, f in zip(calls, fr[len(probes):]) if not at_line(f[1:], "init/main.c", line, False)]
                if wrong:
                    problems.append(f"{name}: main.c:{line}: listed code that llvm-symbolizer puts elsewhere: {wrong[:5]}")
            r[f"main.c:{line} listed"] = f"{len(rows)} ranges, {len(calls)} inlined calls, {len(bps)} breakpoints"
            if first_bps is None:
                first_bps = bps
                for scheme in ("light", "dark"):
                    page.emulate_media(color_scheme=scheme)
                    time.sleep(0.2)
                    page.screenshot(path=str(shots / f"lineaddr-{name}-1280-{scheme}.png"))
                    page.set_viewport_size({"width": 390, "height": 900})
                    time.sleep(0.3)
                    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                        problems.append(f"{name}: the Source panel's address list scrolls the page horizontally at 390 px ({scheme})")
                    page.screenshot(path=str(shots / f"lineaddr-{name}-390-{scheme}.png"))
                    page.set_viewport_size({"width": 1280, "height": 1000})
                page.emulate_media(color_scheme="light")
            page.click("#src_runto")
        else:
            # The Debian tree has several main.c: the short name is refused as ambiguous there.
            short = name == "minified-linux"
            if not short:
                page.fill("#dbg_addr", f"main.c:{line}")
                page.click("#dbg_runto")
                wait_for(lambda: "ambiguous" in page.inner_text("#dbg_status"), 30)
                r["main.c typed"] = page.inner_text("#dbg_status")
                if "init/main.c" not in r["main.c typed"]:
                    problems.append(f"{name}: main.c:{line} typed: {r['main.c typed']!r}")
            q = "main.c" if short else "init/main.c"
            got = page.evaluate("async ([q, l]) => { const r = await dbgLineCodeWait(q, l); return [r.bps || [], r.rows || []]; }", [q, line])
            bps, rows = got[0], [x[:2] for x in got[1]]
            if not bps:
                problems.append(f"{name}: {q}:{line} has no breakpoint addresses")
                break
            page.fill("#dbg_addr", f"{q}:{line}")
            page.click("#dbg_runto")
        ok, pa, status = run_to_wait(page, bps)
        # A stop in the line's own code is that line innermost; at the start
        # of a call inlined on it, the line is the call site.
        inner = any(a <= pa < b for a, b in rows)
        r[f"run to main.c:{line} ({how})"] = f"{status} {time.monotonic() - t0:.0f} s"
        if not ok:
            problems.append(f"{name}: Run to main.c:{line} ({how}) did not stop at one of {[hex(x) for x in bps[:8]]}: {status!r}")
            break
        if f"main.c:{line}" not in status:
            problems.append(f"{name}: the status does not name main.c:{line}: {status!r}")
        if elf:
            fr = symbolize_frames(elf, [pa + off])[0]
            r[f"llvm-symbolizer at {pa:#x}"] = fr
            if not at_line(fr, "init/main.c", line, inner):
                problems.append(f"{name}: Run to main.c:{line} stopped at {pa:#x}, which llvm-symbolizer calls {fr}")
    if not problems and not inner:
        # Stopped in inlined code (irqflags.h): the Debugger's source row opens its line, whose
        # code is in many functions; every copy is listed and is that line for llvm-symbolizer.
        page.click("#cur_src .frame button")
        got_line, bps, rows, calls = line_code(page)
        f0 = json.loads(page.get_attribute("#cur_src", "data-frames"))[0]
        head = page.inner_text("#src_addrs").splitlines()[0]
        r["inlined line"] = f"{f0['file']}:{got_line}: {head}"
        if got_line != f0["line"] or len(rows) < 2 or "inlined copies" not in head:
            problems.append(f"{name}: the inlined line {f0['file']}:{f0['line']} lists {got_line}, {len(rows)} ranges: {head!r}")
        elif elf:
            fr = symbolize_frames(elf, [a + off for a, _ in rows])
            wrong = [hex(a) for (a, _), f in zip(rows, fr) if not at_line(f, f0["file"], f0["line"], True)]
            if wrong:
                problems.append(f"{name}: copies of {f0['file']}:{f0['line']} that llvm-symbolizer puts elsewhere: {wrong[:5]}")
        page.locator(f'#src_body [data-line="{got_line}"]').scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"lineaddr-{name}-inlined-1280-light.png"))
        page.click("#src_close")
        page.evaluate("openSourceFile('init/main.c')")
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    if not problems:
        # An address of the list jumps to the Page tab; the listing's C line there lists the same addresses.
        page.locator(f'#src_body [data-line="{lines[0]}"]').click()
        line_code(page)
        btn = page.locator("#src_addrs li button").first
        a = int(btn.get_attribute("data-addr"))
        btn.click()
        if not wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true" and
                        page.get_attribute("#listing", "data-page") == str(a // 4096), 30):
            problems.append(f"{name}: the address {a:#x} of the list did not show its page")
        page.click("#src_close")
        src = page.locator("#listing .src", has_text=f"main.c:{lines[0]}").first
        if not wait_for(lambda: src.count() > 0, 30):
            problems.append(f"{name}: no C line main.c:{lines[0]} in the Page listing")
        else:
            src.click()
            got_line, bps, _, _ = line_code(page)
            r["listing C line"] = f"main.c:{got_line}, {len(bps)} breakpoints"
            if got_line != lines[0] or sorted(bps) != sorted(first_bps):
                problems.append(f"{name}: the listing's C line main.c:{lines[0]} lists line {got_line} {bps}, the panel {first_bps}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def lineaddr_import_case(browser, base, shots):
    """The same for an imported -g vmlinux (the minified Linux's): the
    Worker finds the line's code in every compile unit that names the
    file; the breakpoint addresses equal the shipped shards', and a typed
    Run to init/main.c:LINE stops at one of them."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    line = main_c_lines("minified-linux", ["setup_arch("])[0]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    shipped = page.evaluate("async (l) => (await dbgLineCodeWait('init/main.c', l)).bps", line)
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    got = page.evaluate("async (l) => { const r = await dbgLineCodeWait('init/main.c', l); return [r.state, r.bps, r.how || r.why]; }", line)
    r["import"] = got
    if got[0] != "ok" or sorted(got[1]) != sorted(shipped or []):
        problems.append(f"import: init/main.c:{line} breakpoints {got}, shipped {shipped}")
    page.fill("#dbg_addr", f"init/main.c:{line}")
    page.click("#dbg_runto")
    ok, pa, status = run_to_wait(page, got[1] or [], 120)
    r["run to"] = status
    if not ok:
        problems.append(f"import: Run to init/main.c:{line} did not stop at a breakpoint address: {status!r}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Function facts (fnfacts.js): per kernel the function a Run to stops at.
FNFACTS_POINTS = {"minified-linux": "start_kernel", "debian-6.1-686": "start_kernel"}

# The Debugger's function rows: per frame of the inline chain its facts
# (the .fnfacts element's data), its entry line and frame.
FNROWS_JS = """() => { const e = document.getElementById('cur_fn'); if (!e) return null;
    return { state: e.dataset.state, rows: [...e.querySelectorAll('.fnframe-row')].map((r) => {
        const f = r.querySelector('.fnfacts'); if (!f) return { func: r.dataset.func, facts: null, text: r.innerText };
        const fr = f.querySelector('.fnframe'), en = f.querySelector('[data-entry]');
        return { func: r.dataset.func, facts: { ...f.dataset }, frame: fr && fr.dataset.frame ? JSON.parse(fr.dataset.frame) : null,
            frameText: fr ? fr.textContent : '', entry: en ? en.dataset.entry : '' }; }) }; }"""


def gdb_info_line(elf, name):
    """gdb's `info line NAME`: (line, file) of the code at the function's entry."""
    out = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info line " + name, str(elf)], capture_output=True, text=True).stdout
    m = re.search(r'Line (\d+) of "([^"]+)" starts at address', out)
    return (int(m.group(1)), m.group(2)) if m else (None, out.strip()[:200])


def same_path(a, b):
    """A path from gdb or llvm (maybe with the build directory in front) and the page's tree-relative one."""
    return bool(a and b) and (a == b or a.endswith("/" + b))


def fnfacts_vs_refs(elf, name, va, facts, frame, entry, problems, r, tag):
    """The facts of the out-of-line function name at virtual va against gdb (ptype, the symbol's file:line,
    external = gdb's global symbol, `info line` for the entry line) and llvm-dwarfdump --debug-frame."""
    from test_dwarf import gdb_funcs, dwarfdump_cfi, frame_of_rows
    g = gdb_funcs(str(elf), [[name, va, True]])[0]
    r[f"{tag} gdb"] = g
    if g[0] == "error" or re.sub(r"\s+", "", g[0]) != re.sub(r"\s+", "", facts["type"]) or not same_path(g[1], facts["file"]) or \
            g[2] != int(facts["line"]) or g[3] != (facts["external"] == "1"):
        problems.append(f"{tag}: page {facts['type']!r} at {facts['file']}:{facts['line']} external={facts['external']}, gdb {g}")
    il = gdb_info_line(elf, name)
    r[f"{tag} gdb info line"] = il
    if not entry or il[0] is None or not same_path(il[1], entry.rsplit(":", 1)[0]) or int(entry.rsplit(":", 1)[1]) != il[0]:
        problems.append(f"{tag}: entry line {entry!r}, gdb info line {il}")
    fde = next((f for f in dwarfdump_cfi(str(elf)) if f[0] == va), None)
    want = frame_of_rows(fde[2]) if fde else None
    names = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"]
    have = (frame["max"], frame["fp"], frame["fpOff"], {names[int(k)]: v for k, v in frame["saved"].items()}) if frame else None
    r[f"{tag} frame"] = have
    if have != want:
        problems.append(f"{tag}: frame {have}, llvm-dwarfdump --debug-frame {want}")


def fnfacts_case(browser, base, shots, name):
    """Function facts from DW_TAG_subprogram: at a Run to stop in the
    function the Debugger's function row gives its prototype (gdb's
    ptype), where it is declared (gdb's symbol), external or static (gdb's
    global symbol), the line its code starts at (gdb's info line), its
    out-of-line code with the frame the call frame information gives
    (llvm-dwarfdump --debug-frame) and its inlined copies. At an inlined
    copy in it every function of the inline chain has a row (names as
    llvm-symbolizer --inlines), the inlined one with its prototype and
    declaration and copy count as llvm-dwarfdump has them and the copy
    there listed. The same facts in the call stack's tooltip, on the Page
    listing's label line, in the Source tab's function detail and (the
    minified Linux) in the Calls tab's detail of a selected frame.
    Screenshots light/dark at 1280 and 390 px, no horizontal scroll."""
    from test_dwarf import dwarfdump_named, ctype
    problems, r = [], {}
    fn = FNFACTS_POINTS[name]
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    off = sk["vaddr"] - sk["paddr"]
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                    page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 240):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    rows_ready = lambda: (page.evaluate(FNROWS_JS) or {}).get("state") == "ok" and all(
        x["facts"] and x["entry"] and "reading" not in x["frameText"] for x in page.evaluate(FNROWS_JS)["rows"])
    if not wait_for(rows_ready, 90):
        rows = page.evaluate(FNROWS_JS)
        page.close()
        return [f"{name}: no function facts at {fn}: {rows}"], r
    got = page.evaluate(FNROWS_JS)["rows"]
    r["at entry"] = [(x["func"], x["facts"]["proto"], x["facts"]["file"] + ":" + x["facts"]["line"], x["entry"], x["frameText"][:80]) for x in got]
    if len(got) != 1 or got[0]["func"] != fn:
        problems.append(f"{name}: function rows at {fn}'s entry: {got}")
    top = got[0]
    if elf:
        fnfacts_vs_refs(elf, fn, sk["vaddr"], top["facts"], top["frame"], top["entry"], problems, r, f"{name} {fn}")
    # The call stack's tooltip and the Page listing's label line say the same.
    tip = page.get_attribute("#call_stack button", "title") or ""
    if top["facts"]["proto"] not in tip or "defined at " + top["facts"]["file"] + ":" + top["facts"]["line"] not in tip:
        problems.append(f"{name}: the call stack's tooltip lacks the facts: {tip!r}")
    # "defined at" opens the Source panel at the declaration.
    page.click("#cur_fn .fnloc")
    decl = page.locator(f'#src_body [data-line="{top["facts"]["line"]}"].pick')
    if not wait_for(lambda: page.is_visible("#src_panel") and decl.count() > 0 and
                    page.get_attribute("#src_body", "data-file") == top["facts"]["file"], 60):
        problems.append(f"{name}: 'defined at' did not open {top['facts']['file']}:{top['facts']['line']} in the Source panel")
    else:
        r["defined at opens"] = decl.first.inner_text().strip()[:80]
    page.click("#src_close")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#sym_info").screenshot(path=str(shots / f"fnfacts-{name}-debugger-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    # An inlined copy in it: the first one after EIP that its facts list.
    target = page.evaluate("""async (pa) => {
        for (let t = 0; t < 200; t++) {
            const fn = dbgFunction(pa);
            if (fn.state === 'ok') {
                for (const [a, , frames] of fn.rows) {
                    if (a <= pa || frames.length < 2) continue;
                    const x = dbgFnFacts(frames[0].func);
                    if (x.state === 'loading') break;
                    const hit = x.state === 'ok' && x.fns.some((f) => f.e[8].includes(a));
                    if (hit) return [a, frames.map((f) => f.func)];
                }
                if (fn.rows.every(([a, , frames]) => a <= pa || frames.length < 2 || dbgFnFacts(frames[0].func).state !== 'loading')) return null;
            }
            await new Promise((res) => setTimeout(res, 100));
        }
        return null; }""", sk["paddr"])
    r["inlined copy"] = target
    if not target:
        problems.append(f"{name}: no inlined copy starts in {fn} after its entry")
    else:
        pa = target[0]
        page.fill("#dbg_addr", hex(pa + off))
        page.click("#dbg_runto")
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                        page.get_attribute("#regs", "data-phys") == str(pa), 240):
            problems.append(f"{name}: Run to the inlined copy at {pa:#x} did not stop there: {page.inner_text('#dbg_status')}")
        elif not wait_for(lambda: (page.evaluate(FNROWS_JS) or {}).get("state") == "ok" and
                          all(x["facts"] for x in page.evaluate(FNROWS_JS)["rows"]), 60):
            problems.append(f"{name}: no facts for the inline chain at {pa:#x}: {page.evaluate(FNROWS_JS)}")
        else:
            rows = page.evaluate(FNROWS_JS)["rows"]
            r["inline chain"] = [(x["func"], x["facts"]["proto"], x["facts"]["file"] + ":" + x["facts"]["line"], x["facts"]["copies"]) for x in rows]
            if [x["func"] for x in rows] != target[1] or rows[-1]["func"] != fn:
                problems.append(f"{name}: function rows {[x['func'] for x in rows]} at {pa:#x}, line table {target[1]}")
            if elf:
                want = [f[0] for f in symbolize_frames(elf, [pa + off])[0]]
                if [x["func"] for x in rows] != want:
                    problems.append(f"{name}: function rows {[x['func'] for x in rows]}, llvm-symbolizer --inlines {want}")
                inner = rows[0]
                f = inner["facts"]
                if not page.evaluate("([n, pa]) => dbgFnFacts(n).fns.some((x) => x.e[0] === n && x.e[8].includes(pa))", [inner["func"], pa]):
                    problems.append(f"{name}: {inner['func']}'s copies do not list {pa:#x}")
                dd = dwarfdump_named(str(elf), [inner["func"]], [])
                decl = [d for o, d in dd.items() if d["tag"] == "DW_TAG_subprogram" and "origin" not in d and d.get("name") == inner["func"]
                        and d.get("line") == int(f["line"]) and same_path(d.get("file"), f["file"])]
                if not decl:
                    problems.append(f"{name}: no DIE declares {inner['func']} at {f['file']}:{f['line']} in llvm-dwarfdump")
                else:
                    d = max(decl, key=lambda x: len(x["params"]))
                    want_t = ctype(d["type"]) + "(" + ", ".join([ctype(x["type"]) for x in d["params"]] + (["..."] if d["varargs"] else [])) + ")"
                    have_t = ctype(f["type"]).replace("(void)", "()").replace(" (", "(")
                    r["inlined vs llvm-dwarfdump"] = [have_t, want_t]
                    if have_t != want_t.replace(" (", "("):
                        problems.append(f"{name}: {inner['func']} {f['type']!r}, llvm-dwarfdump {want_t!r}")
                    origins = {o for o, x in dd.items() if x is d or (x["tag"] == "DW_TAG_subprogram" and "origin" not in x and
                               x.get("name") == inner["func"] and x.get("line") == d.get("line") and x.get("file") == d.get("file"))}

                    def root(o):
                        for _ in range(8):
                            x = dd.get(o)
                            if x is None or "origin" not in x:
                                return o
                            o = x["origin"]
                        return o
                    n = sum(1 for o, x in dd.items() if x["tag"] == "DW_TAG_inlined_subroutine" and x["code"] and root(o) in origins)
                    r["inlined copies"] = [f["copies"], n]
                    if int(f["copies"]) != n:
                        problems.append(f"{name}: {inner['func']} has {f['copies']} inlined copies on the page, {n} in llvm-dwarfdump")
            # Its copy list opens and lists this one.
            page.locator("#cur_fn .fnframe-row").first.locator("details.fncopies summary").click()
            if not wait_for(lambda: page.locator(f'#cur_fn .fnframe-row >> nth=0 >> details.fncopies li button[data-addr="{pa}"]').count() > 0, 5) \
                    and int(rows[0]["facts"]["copies"]) <= 40:
                problems.append(f"{name}: the opened copy list does not list {pa:#x}")
            for scheme in ("light", "dark"):
                page.emulate_media(color_scheme=scheme)
                time.sleep(0.2)
                page.locator("#sym_info").screenshot(path=str(shots / f"fnfacts-{name}-inlined-1280-{scheme}.png"))
                page.set_viewport_size({"width": 390, "height": 900})
                time.sleep(0.3)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{name}: the function rows scroll the page horizontally at 390 px ({scheme})")
                page.screenshot(path=str(shots / f"fnfacts-{name}-390-{scheme}.png"), full_page=True)
                page.set_viewport_size({"width": 1280, "height": 1000})
            page.emulate_media(color_scheme="light")
    # The Page listing: the function's label line has its prototype and the facts in its title.
    page.evaluate("(a) => jumpTo(a)", sk["paddr"])
    lbl = page.locator(f'#listing .lbl[data-sym="{fn}"]')
    if not wait_for(lambda: lbl.count() > 0 and lbl.first.get_attribute("data-proto"), 60):
        problems.append(f"{name}: the Page listing's {fn} label line has no prototype")
    else:
        r["label line"] = lbl.first.inner_text()
        if lbl.first.get_attribute("data-proto") != top["facts"]["proto"] or "defined at" not in (lbl.first.get_attribute("title") or ""):
            problems.append(f"{name}: label line {lbl.first.get_attribute('data-proto')!r} / {lbl.first.get_attribute('title')!r}")
    # The Source tab: the function row's arrow shows the same facts.
    show_tab(page, "source")
    page.fill("#cov_filter", fn)
    path = top["facts"]["file"] + ":" + fn
    tog = page.locator(f'#cov_tree .covrow[data-kind="func"][data-path="{path}"] .ttog')
    if not wait_for(lambda: tog.count() > 0, 60):
        problems.append(f"{name}: no Coverage row {path}")
    else:
        tog.first.click()
        det = page.locator(f'#cov_tree .covfacts[data-path="{path}"] .fnfacts')
        if not wait_for(lambda: det.count() > 0, 30):
            problems.append(f"{name}: the Coverage row {path} shows no facts")
        else:
            cf = det.first.evaluate("(e) => ({ ...e.dataset })")
            if cf != top["facts"]:
                problems.append(f"{name}: Coverage facts {cf} differ from the Debugger's {top['facts']}")
            page.emulate_media(color_scheme="dark")
            time.sleep(0.2)
            page.locator("#panel_source").screenshot(path=str(shots / f"fnfacts-{name}-coverage-1280-dark.png"))
            page.emulate_media(color_scheme="light")
    if name == "minified-linux":
        # The Calls tab: step to record calls, select the widest frame of a DWARF
        # function (tapping a box is graph_check's; the detail is the same render).
        show_tab(page, "debugger")
        page.fill("#dbg_n", "3000")
        page.click("#dbg_stepn")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        show_tab(page, "calls")
        time.sleep(0.5)
        xy = page.evaluate("""() => { const st = G.stepper;
            let best = -1, w = -1;
            for (let k = 0; k < st.fStart.length; k++) if (!hiddenFrame(st, k) && (() => { const x = dbgFnFacts(st.nodeName(st.fNode[k])); return x.state === 'ok' && x.fns.length; })()) {
                const e = (st.fEnd[k] < 0 ? st.steps : st.fEnd[k]) - Math.max(st.fStart[k], runStart()); if (e > w) { w = e; best = k; } }
            if (best < 0) return null;
            G.selected = best; renderGraphInfo();
            return [0, 0, st.nodeName(st.fNode[best])]; }""")
        if not xy:
            problems.append(f"{name}: no Graph frame of a DWARF function")
        else:
            gf = page.locator("#graph_fn .fnfacts")
            if not wait_for(lambda: gf.count() > 0, 30):
                problems.append(f"{name}: the Graph detail of {xy[2]} shows no facts: {page.inner_text('#graph_info')[:300]!r}")
            else:
                r["graph"] = [xy[2], gf.first.get_attribute("data-proto")]
                if gf.first.get_attribute("data-name") != xy[2].split(".")[0]:
                    problems.append(f"{name}: Graph detail of {xy[2]} names {gf.first.get_attribute('data-name')}")
                page.locator("#graph_info").screenshot(path=str(shots / f"fnfacts-{name}-graph-1280-light.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def fnfacts_import_case(browser, base, shots):
    """An imported -g vmlinux (the minified Linux's): the Worker collects
    the same function facts as the shipped shards (every function's
    prototype, declaration, flags, out-of-line code and inlined copies)
    and the Debugger's function row at start_kernel is the same."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    render = """async () => {
        for (let t = 0; t < 600 && dbgTypes().state === 'loading'; t++) await new Promise((res) => setTimeout(res, 100));
        const ty = dbgTypes(); if (ty.state !== 'ok') return ty.why;
        let F;
        if (DBG.mode === 'shards') {
            if (!DBG.index.funcs) return 'index.json has no function facts';
            F = { files: [], fns: [] };
            for (let k = 0; k < DBG.index.funcs.shards; k++) {
                const b = await dbgGunzip(await (await fetch(DBG.base + 'funcs-' + k.toString(16) + '.json.gz')).arrayBuffer());
                const s = JSON.parse(new TextDecoder().decode(b));
                F.fns.push(...s.fns.map((e) => [e[0], e[1] >= 0 ? s.files[e[1]] : null, ...e.slice(2)]));
            }
        } else if (!ty.T.funcs) return 'the import has no function facts';
        else F = { fns: ty.T.funcs.fns.map((e) => [e[0], e[1] >= 0 ? ty.T.funcs.files[e[1]] : null, ...e.slice(2)]) };
        return F.fns.map((e) => JSON.stringify([e[0], e[1], e[2], e[3], e[4], dwFuncProto(ty.T, e), e[7], e[8]])).sort(); }"""
    shipped = page.evaluate(render)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) and
             ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("entry"), 120)
    want_row = page.evaluate(FNROWS_JS)
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    imported = page.evaluate(render)
    r["functions"] = [len(shipped) if isinstance(shipped, list) else shipped, len(imported) if isinstance(imported, list) else imported]
    if not isinstance(imported, list) or imported != shipped:
        diff = sorted(set(imported) ^ set(shipped))[:4] if isinstance(imported, list) and isinstance(shipped, list) else imported
        problems.append(f"import: the function facts differ from the shipped shards: {diff}")
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) and
             ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("entry") and
             "reading" not in ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("frameText", "reading"), 120)
    got_row = page.evaluate(FNROWS_JS)
    r["row"] = (got_row or {}).get("rows")
    if got_row != want_row:
        problems.append(f"import: the function row at start_kernel {got_row}, shipped {want_row}")
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
    show_tab(page, "machine")
    page.click("#sym_forget")
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") != "imported", 30)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Branch targets in the listings: per kernel, where the case pauses (Run to)
# and the extra pages it reads (symbols whose page is opened; the audit's
# Debian sample page 0x1d8d000 is setup_arch's).
BRANCH_POINTS = {
    "minified-linux": ("start_kernel", []),
    "debian-6.1-686": ("start_kernel", ["setup_arch"]),
}

# The listing lines of a container: address, text, and the branch-target
# names (.brt) in them.
BRANCHES_JS = """(sel) => [...document.querySelectorAll(sel + ' .ins')].map((l) => ({
    pa: +l.dataset.addr, text: l.textContent,
    brt: [...l.querySelectorAll('.brt')].map((b) => ({ name: b.dataset.name, va: +b.dataset.va, pa: +b.dataset.pa,
        text: b.textContent })) }))"""

# A direct branch as llvm-objdump and iced-x86 print it (prefixes, the
# mnemonic, an absolute target), and the `<name+0xoff>` after it.
BRANCH_RE = re.compile(r"^(?:(?:lock|rep\w*|notrack|bnd|[cdefgs]s|data16|data32|addr16|addr32)\s+)*"
                       r"(?:j[a-z]+|call[lwq]?|loop[a-z]*|xbegin[lw]?)\s+(0x[0-9a-fA-F]+)(?:\s+<([^<>]+)>)?(?:\s|$)")

# Runs inside gdb on the -g ELF: `info symbol` of ADDRS, `ptype` of NAMES.
GDB_BRANCHES = r"""
import gdb, json
def run(cmd):
    try:
        return gdb.execute(cmd, to_string=True).strip()
    except Exception as e:
        return "error: %s" % e
json.dump([[run("info symbol %d" % a) for a in ADDRS], [run("ptype " + n) for n in NAMES]], open(DST, "w"))
"""


def gdb_branches(elf, addrs, names):
    with tempfile.TemporaryDirectory() as tmp:
        py, dst = os.path.join(tmp, "br.py"), os.path.join(tmp, "br.json")
        pathlib.Path(py).write_text(f"ADDRS, NAMES, DST = {addrs!r}, {names!r}, {dst!r}\n" + GDB_BRANCHES)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", "set print demangle off", "-ex", f"source {py}", str(elf)],
                       capture_output=True, text=True, timeout=600)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else None


def branch_lines(lines):
    """The direct branches of listing lines: [(pa, target va, `<name>` in the text or None, .brt spans)]."""
    out = []
    for l in lines:
        body = re.sub(r"^▶?\s*[0-9a-f]{8} +(?:[0-9a-f]{2})+…? *", "", l["text"].strip())
        m = BRANCH_RE.match(body)
        if m:
            out.append((l["pa"], int(m.group(1), 16), m.group(2), l["brt"]))
    return out


def code_symbol_at(syms, va):
    """The smallest sized symbol holding va, else a sizeless one exactly at
    va (the last listed, as llvm-objdump would also name it), or None."""
    best = None
    for s in syms:
        if s["size"] > 0 and s["vaddr"] <= va < s["vaddr"] + s["size"] and (not best or s["size"] < best["size"]):
            best = s
    return best or next((s for s in reversed(syms) if not s["size"] and s["vaddr"] == va), None)


def branches_vs_refs(syms, elf, lines, problems, r, tag):
    """Every direct branch whose target is inside a sized symbol (or at a
    sizeless one) carries `<name+0xoff>` (as llvm-objdump spells it) of
    that symbol, and gdb's `info symbol` there agrees (an alias at the
    same address counts); a target outside every symbol carries none;
    each name is one .brt span with the target's address."""
    br = branch_lines(lines)
    r[tag + " branches"] = len(br)
    want = {}
    for pa, va, name, spans in br:
        s = code_symbol_at(syms, va)
        exp = None if not s else s["name"] + (f"+{va - s['vaddr']:#x}" if va > s["vaddr"] else "")
        want[va] = (s, exp)
        if exp and name != exp:
            problems.append(f"{tag}: {pa:#x} branch to {va:#x} names <{name}>, want <{exp}>")
        elif not exp and name is not None:
            problems.append(f"{tag}: {pa:#x} branch to {va:#x} outside every symbol names <{name}>")
        if name is not None and [(x["name"], x["va"]) for x in spans] != [(name, va)]:
            problems.append(f"{tag}: {pa:#x} <{name}> is not one .brt span for {va:#x}: {spans}")
    named = sum(1 for _, _, n, _ in br if n)
    r[tag + " named"] = named
    if elf and shutil.which("gdb"):
        addrs = sorted(want)
        got = gdb_branches(elf, addrs, [])
        if got is None:
            problems.append(f"{tag}: gdb wrote nothing")
            return
        alias = same = outside = 0
        for va, g in zip(addrs, got[0]):
            s, exp = want[va]
            m = re.match(r"^(\S+)(?: \+ (\d+))? in section", g)
            if not s:
                if m is None:
                    outside += 1
                else:  # inside a sizeless symbol, past its start: not named (llvm-objdump would)
                    r.setdefault(tag + " targets in no sized symbol, gdb", []).append(f"{va:#x} {g}")
                continue
            off = va - s["vaddr"]
            if not m or int(m.group(2) or 0) != off and m.group(1) == s["name"]:
                problems.append(f"{tag}: target {va:#x} <{exp}>, gdb: {g}")
            elif m.group(1) == s["name"] and int(m.group(2) or 0) == off:
                same += 1
            elif any(x["name"] == m.group(1) and x["vaddr"] == s["vaddr"] for x in syms) and int(m.group(2) or 0) == off:
                alias += 1
            else:
                problems.append(f"{tag}: target {va:#x} <{exp}>, gdb: {g}")
        r[tag + " targets equal to gdb"] = same
        r[tag + " targets gdb names by an alias"] = alias
        r[tag + " targets outside every symbol (gdb too)"] = outside
    elif os.environ.get("CI"):
        problems.append(f"{tag}: no -g ELF or no gdb")


def callee_tooltips(page, sel, elf, problems, r, tag, n=4):
    """Hover the names of n distinct called functions (C functions with a
    prototype for gdb): the tooltip (dbgTitle, the function facts) shows
    the callee's prototype, which matches gdb's `ptype` (return type,
    then the parameter types in order), and where it is defined."""
    loc = page.locator(f"{sel} .brt")
    names = page.evaluate("""(sel) => [...document.querySelectorAll(sel + ' .brt')].map((b) =>
        /\\bcall/.test(b.parentElement.textContent) && !b.dataset.name.includes('+') ? b.dataset.name : '')""", sel)
    cands = list(dict.fromkeys(x for x in names if x))[:16]
    gdb = elf and shutil.which("gdb")
    ptypes = gdb_branches(elf, [], cands)[1] if gdb and cands else [None] * len(cands)
    picks = [(c, pt) for c, pt in zip(cands, ptypes)
             if pt is None or re.match(r"^type = [^(]*\(.*\)$", pt, re.S) and "(*" not in pt][:n]
    if not picks:
        problems.append(f"{tag}: no call to a C function to hover ({cands})")
        return
    checked = []
    for name, pt in picks:
        el = loc.nth(names.index(name))
        # The listing is redrawn when debug info it uses arrives (a locals or
        # facts shard): the locator finds the new line on the next try.
        for _ in range(5):
            try:
                el.scroll_into_view_if_needed(timeout=5000)
                break
            except PlaywrightError:
                time.sleep(0.5)
        title = ""
        for _ in range(60):
            try:
                el.hover(timeout=5000)
                title = el.get_attribute("title") or ""
            except PlaywrightError:
                title = ""
            if "reading" not in title and "defined at" in title:
                break
            page.mouse.move(0, 0)
            time.sleep(0.25)
        if f"{name}(" not in title or "defined at" not in title:
            problems.append(f"{tag}: the tooltip of <{name}> has no prototype and facts: {title!r}")
            continue
        m = re.match(r"^type = (.*?)\s*\((.*)\)$", pt or "", re.S)
        if m:
            ret, args = m.group(1), [a.strip() for a in m.group(2).split(",")] if m.group(2) else []
            # gdb says (void) also for an unprototyped function (assembly), which the page writes ().
            rx = re.escape(ret) + r"\s*" + re.escape(name) + r"\(" + ("(?:void)?" if args == ["void"] else r",\s*".join(
                re.escape(a) + ("" if a == "..." else r"\s*\w*") for a in args)) + r"\)"
            if not re.search(rx, title):
                problems.append(f"{tag}: the tooltip of <{name}> does not show gdb's {pt!r}: {title.splitlines()[:3]}")
                continue
        checked.append(name)
    r[tag + " callee tooltips with gdb's prototype"] = checked


def branches_case(browser, base, shots, name):
    """Direct call/jmp/jcc targets inside the kernel are named in both
    listings (the Debugger's Code at EIP and the Page tab's merged hex +
    asm), also when the code is decoded live from guest memory (iced-x86:
    the Debian sample, every import, the "live decode" toggle): `<name+off>`
    of the sized symbol holding the target, as llvm-objdump spells it,
    equal to gdb's `info symbol`; the name's tooltip is the callee's
    facts (prototype = gdb's ptype); clicking it opens the target. The
    minified Linux: the live names equal disasm.json's (llvm-objdump)
    line for line. No horizontal scroll at 390 px."""
    problems, r = [], {}
    fn, extra = BRANCH_POINTS[name]
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    elf = debug_elf(name)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 240):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    if name == "minified-linux":
        # Live decode on: the same code from guest memory, named by the page.
        show_tab(page, "page")
        page.check("#live_decode")
        show_tab(page, "debugger")
    wait_for(lambda: page.get_attribute("#context", "data-source") == "live" and
             page.evaluate("document.querySelectorAll('#context .brt').length") > 0, 60)
    dbg = page.evaluate(BRANCHES_JS, "#context")
    branches_vs_refs(syms, elf, dbg, problems, r, f"{name} Debugger")
    page.emulate_media(color_scheme="light")
    page.locator("#context").screenshot(path=str(shots / f"branches-{name}-debugger-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    time.sleep(0.2)
    page.locator("#context").screenshot(path=str(shots / f"branches-{name}-debugger-1280-dark.png"))
    # The Page tab: the page at EIP and the extra pages, decoded live.
    page.click("#context .ins.cur")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(sk["paddr"] // 4096) and
             page.get_attribute("#listing", "data-source") == "live", 60)
    time.sleep(0.5)
    lst = page.evaluate(BRANCHES_JS, "#listing")
    by = {l["pa"]: l for l in lst}
    same = 0
    for l in dbg:
        o = by.get(l["pa"])
        if o is not None and o["brt"] != l["brt"]:
            problems.append(f"{name}: {l['pa']:#x} Page tab {o['brt']}, Debugger {l['brt']}")
        elif o is not None:
            same += 1
    r["page lines equal to the debugger's"] = same
    branches_vs_refs(syms, elf, lst, problems, r, f"{name} Page {sk['paddr'] // 4096:#x}")
    callee_tooltips(page, "#listing", elf, problems, r, f"{name} Page")
    if name == "minified-linux":
        # disasm.json's text (llvm-objdump) for the same page, line for line.
        page.uncheck("#live_decode")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "build", 10)
        built = {l["pa"]: l for l in page.evaluate(BRANCHES_JS, "#listing")}
        n = 0
        for pa, va, nm, spans in branch_lines(lst):
            b = built.get(pa)
            bb = branch_lines([b]) if b else []
            if not bb or bb[0][1] != va or bb[0][2] != nm:
                problems.append(f"{name}: {pa:#x} live <{nm}>, llvm-objdump {bb[0][2] if bb else None}")
            elif [(x["name"], x["va"]) for x in bb[0][3]] != [(x["name"], x["va"]) for x in spans]:
                problems.append(f"{name}: {pa:#x} disasm.json line has not the same .brt span: {bb[0][3]}")
            else:
                n += 1
        r["live names equal to llvm-objdump's"] = n
        callee_tooltips(page, "#listing", elf, problems, r, f"{name} Page (disasm.json)", n=1)
        page.check("#live_decode")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
    for ex in extra:
        s = next(x for x in syms if x["name"] == ex)
        if not open_page(page, s["paddr"] // 4096):
            problems.append(f"{name}: could not open the page of {ex}")
            continue
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 page.evaluate("document.querySelectorAll('#listing .brt').length") > 0, 30)
        branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#listing"), problems, r, f"{name} Page {s['paddr'] // 4096:#x}")
    # A name opens its target.
    first = page.locator("#listing .brt").first
    if not first.count():
        problems.append(f"{name}: no branch target name to click")
    else:
        tgt, nm = int(first.get_attribute("data-pa")), first.get_attribute("data-name")
        first.click()
        if not wait_for(lambda: page.get_attribute("#listing", "data-page") == str(tgt // 4096), 10):
            problems.append(f"{name}: clicking <{nm}> did not open page {tgt // 4096:#x}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.screenshot(path=str(shots / f"branches-{name}-page-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Page tab scrolls horizontally at 390 px ({scheme})")
        # The listing is drawn again as DWARF shards arrive (the file:line
        # of jump sources in other code): a line found can be replaced.
        for _ in range(5):
            try:
                if page.locator("#listing .brt").count():
                    page.locator("#listing .brt").first.scroll_into_view_if_needed(timeout=5000)
                break
            except PlaywrightError:
                time.sleep(0.3)
        page.screenshot(path=str(shots / f"branches-{name}-page-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def branches_import_case(browser, base, shots):
    """An imported -g vmlinux (the minified Linux's; no disasm.json, so
    every listing is decoded live): the branch names at start_kernel equal
    gdb's, and the BIOS (outside the kernel) gets none."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return ["import: Run to start_kernel did not stop there"], r
    wait_for(lambda: page.get_attribute("#context", "data-source") == "live" and
             page.evaluate("document.querySelectorAll('#context .brt').length") > 0, 60)
    branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#context"), problems, r, "import Debugger")
    page.click("#context .ins.cur")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(sk["paddr"] // 4096) and
             page.get_attribute("#listing", "data-source") == "live", 60)
    time.sleep(0.5)
    branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#listing"), problems, r, "import Page")
    callee_tooltips(page, "#listing", elf, problems, r, "import Page", n=2)
    # The BIOS (page 0xff, 16-bit, outside the kernel): branches, no names.
    if open_page(page, 0xff):
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
        bios = branch_lines(page.evaluate(BRANCHES_JS, "#listing"))
        r["BIOS branches"] = len(bios)
        bad = [(hex(pa), nm) for pa, _, nm, sp in bios if nm or sp]
        if not bios or bad:
            problems.append(f"import: BIOS page 0xff: {len(bios)} branches, named: {bad[:4]}")
    else:
        problems.append("import: could not open the BIOS page 0xff")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def norm_insn(text):
    """Instruction text without addresses' symbol names, comments and spacing."""
    text = re.sub(r"<[^>]*>|#.*$", "", text.lower())
    text = re.sub(r"\s+", " ", text).strip().replace(", ", ",")
    # Numbers modulo formatting: -0x3fef0834 and 0xc010f7cc, 9 and 0x9 are equal.
    text = re.sub(r"-?(?:0x[0-9a-f]+|\b\d+\b)", lambda m: hex(int(m.group(0), 0) & 0xffffffff), text)
    # Spellings of the same byte instruction (-O0 code has many): iced-x86's
    # setneb is llvm-objdump's setne, and a byte operation's immediate is
    # one byte ($0xff, llvm-objdump's $-0x1).
    text = re.sub(r"^(set[a-z]+?)b ", r"\1 ", text)
    if re.match(r"^[a-z]+b \$", text):
        text = re.sub(r"^([a-z]+b \$)(0x[0-9a-f]+)", lambda m: m.group(1) + hex(int(m.group(2), 16) & 0xff), text)
    return text


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


def hello_case(browser, base, shots, name="hello"):
    """The hello kernel has the minified Linux's debug features where it
    has the code for them (D7). "hello" is built with -O0 (U1): Run to
    hello_main, then single-step to the halt; at every new instruction no
    parameter or local of any frame is optimized out (state "out" or the
    text "optimized out" in the Debugger); at the statement boundaries the
    values are the guest's: hello_main's row is the BIOS cursor row byte
    (clamped to 23), put_line's row is it (then +1), s points at
    "Hello, World!" (then "Still alive"), p at the VGA cell of row; the
    call stack is put_line <- hello_main <- startup_32, unwound to the
    stack's start. "hello-os" is the -Os build hello/Makefile makes by
    default, put_line inlined: hello_main's row and put_line's row are
    the registers the DWARF location lists give (EAX & 0xff, then EAX)
    and put_line's s is shown as optimized out. Both: Variables says there
    are no global variables, the task row that the DWARF has no
    current_task (no tasks); the Source tab's tree has boot/hello.c
    with lines run and totals equal to files.json; light/dark at 1280 and
    390 px."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sym = {s["name"]: s["paddr"] for s in syms}
    hm = next(s for s in syms if s["name"] == "hello_main")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(hm["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(hm["paddr"]), 120):
        page.close()
        return [f"{name}: Run to hello_main did not stop there: " + page.inner_text("#dbg_status")], r
    cursor = guest_bytes(page, 0x451, 1)[0]
    row0 = min(cursor, 23)

    def pc():
        return int(page.get_attribute("#regs", "data-phys"))

    def step_only(n):
        before = page.evaluate("stepper.steps")
        page.fill("#dbg_n", str(n))
        page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("stepper.steps") - before >= n and
                 page.get_attribute("#dbg_state", "data-state") == "paused", 30, 0.05)

    def variables():
        wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
        return {(sc["func"], v["name"]): v for fr in locals_open_all(page) for sc in fr["scopes"] for v in sc["vars"]}

    def step(n, want):
        step_only(n)
        if pc() != want:
            problems.append(f"{name}: stepped to {pc():#x}, not {want:#x}")
        return variables()

    def stack():
        dump = page.evaluate(UNWIND_DUMP_JS)
        return [f["name"] for f in dump["frames"]], dump["unwind"], \
            " <- ".join(f"{f['name']}[{f['method']}]" for f in dump["frames"]) + f" ({dump['unwind']}: {dump['why']})"

    def val(v, key):
        x = v.get(key, {})
        return int(x["raw"]) if x.get("state") == "ok" and x.get("raw") is not None else None

    if name == "hello":
        # Every instruction from hello_main's first to the halt: one step at
        # a time, the Variables panel read at each address seen first.
        seen, outs, facts, unmapped = set(), [], {}, []
        alive_s = None
        # -O0 keeps put_line out of line (-Os inlines it twice).
        pl = sym.get("put_line", -1)
        if pl < 0:
            problems.append(f"{name}: no put_line symbol (inlined: not an -O0 build?)")
        # The walk ends at hello_main's hlt (cli; hlt; jmp back).
        insns = json.loads((SITE / entry["disasm"]).read_text())["insns"]
        hlt = [a for a, _, ins, *rest in insns if ins.startswith("hlt") and a > hm["paddr"]][0]
        pl_size = next((x["size"] for x in syms if x["name"] == "put_line"), 0)
        # Every instruction of hello_main up to the hlt and of put_line runs,
        # except the clamp's store when the BIOS row is at most 23.
        expect = {a for a, _, ins, *rest in insns
                  if hm["paddr"] <= a <= hlt or pl <= a < pl + pl_size and not ins.startswith("nop")}
        for _ in range(1500):
            a = pc()
            if a not in seen:
                seen.add(a)
                v = variables()
                text = page.inner_text("#panel_debugger")
                for (fn, vn), x in v.items():
                    if x.get("state") == "out" or "optimized out" in (x.get("value") or "") + x.get("loc", ""):
                        outs.append(f"{a:#x} {fn}.{vn}: {x}")
                    elif x.get("state") == "unmapped":
                        # hello_main's first two instructions: EBP is still 0,
                        # so its EBP-based slot is at 0xfffffffc.
                        unmapped.append(f"{a:#x} {fn}.{vn}")
                        if a not in (hm["paddr"], hm["paddr"] + 1) or "not mapped" not in x.get("loc", ""):
                            outs.append(f"{a:#x} {fn}.{vn} not mapped outside hello_main's prologue: {x}")
                if "optimized out" in text:
                    outs.append(f"{a:#x}: the Debugger says 'optimized out': " +
                                text[max(0, text.find("optimized out") - 80):text.find("optimized out") + 40])
                names, unwind, desc = stack()
                if a in (hm["paddr"], pl + 0xa):
                    page.locator("#locals").screenshot(path=str(shots / f"{name}-locals-{a:x}.png"))
                # Statement boundaries (is_stmt rows past the prologues):
                # values and the call stack.
                if a == hm["paddr"] + 0x13:      # line 29, row stored
                    facts["row"] = val(v, ("hello_main", "row"))
                    if facts["row"] != cursor:
                        problems.append(f"{name}: hello_main's row is {v.get(('hello_main', 'row'))} at {a:#x}, "
                                        f"the BIOS cursor row byte is {cursor}")
                    if names != ["hello_main", "startup_32"] or unwind != "ok":
                        problems.append(f"{name}: at {a:#x} the call stack is not hello_main <- startup_32: {desc}")
                    r["stack_main"] = desc
                if a == pl + 0xa:   # line 14, put_line's prologue_end
                    prow, s = val(v, ("put_line", "row")), val(v, ("put_line", "s"))
                    text_at_s = bytes(guest_bytes(page, s, len(HELLO))).decode("latin-1") if s else None
                    facts["put_line"] = prow, s, text_at_s
                    if prow != row0 or text_at_s != HELLO:
                        problems.append(f"{name}: put_line's row, s are {prow}, {s} ({text_at_s!r}) at {a:#x}, "
                                        f"want {row0}, the address of {HELLO!r}")
                    if names != ["put_line", "hello_main", "startup_32"] or unwind != "ok":
                        problems.append(f"{name}: at {a:#x} the call stack is not put_line <- hello_main <- startup_32: {desc}")
                    r["stack_put_line"] = desc
                if a == pl + 0x1a:  # line 16, p set
                    p = val(v, ("put_line", "p"))
                    facts["p"] = p
                    if p != 0xb8000 + row0 * 160:
                        problems.append(f"{name}: put_line's p is {p} at {a:#x}, want {0xb8000 + row0 * 160:#x}")
            elif a == pl + 0xa and alive_s is None:
                # Second call: "Still alive" one row down.
                v = variables()
                prow, alive_s = val(v, ("put_line", "row")), val(v, ("put_line", "s"))
                text_at_s = bytes(guest_bytes(page, alive_s, len(ALIVE))).decode("latin-1") if alive_s else None
                facts["put_line2"] = prow, alive_s, text_at_s
                if prow != row0 + 1 or text_at_s != ALIVE:
                    problems.append(f"{name}: second put_line's row, s are {prow}, {alive_s} ({text_at_s!r}), "
                                    f"want {row0 + 1}, the address of {ALIVE!r}")
            if a == hlt or len(seen) > 200:
                break
            step_only(1)
        r["addresses"], r["facts"] = len(seen), facts
        r["optimized_out"], r["unmapped"] = len(outs), unmapped
        if outs:
            problems.append(f"{name}: {len(outs)} optimized out at the {len(seen)} addresses: " + "; ".join(outs[:8]))
        r["screen"] = gate_on_screen(page)
        missed = sorted(expect - seen)
        if len(missed) != (1 if cursor <= 23 else 0) or alive_s is None or pc() != hlt or not r["screen"]:
            problems.append(f"{name}: the single-step walk saw {len(seen)} of {len(expect)} addresses (not "
                            f"{[hex(x) for x in missed]}), second put_line {alive_s}, stopped at {pc():#x} "
                            f"(hlt {hlt:#x}), both lines on the screen: {r['screen']}")
    else:
        # movb 0x451,%al; cmpb: row lives in AL (DW_OP_breg0 EAX, 0xff, and, convert, stack_value).
        v = step(2, hm["paddr"] + 7)
        names, unwind, r["stack"] = stack()
        if names != ["hello_main", "startup_32"] or unwind != "ok":
            problems.append(f"{name}: the call stack is not hello_main <- startup_32, unwound to the stack's start: {r['stack']}")
        row = v.get(("hello_main", "row"), {})
        r["row"] = row.get("raw"), row.get("value")
        if row.get("state") != "ok" or row.get("raw") is None or int(row["raw"]) != cursor:
            problems.append(f"{name}: hello_main's row is {row} at {hm['paddr'] + 7:#x}, the BIOS cursor row byte is {cursor}")
        # jb over the clamp (taken when the row is below 23), movzbl: at the shll
        # put_line's row parameter is EAX (DW_OP_reg0).
        v = step(2 if cursor < 23 else 3, hm["paddr"] + 14)
        eax = page.evaluate("stepper.regs().eax")
        prow = v.get(("put_line", "row"), {})
        r["put_line.row"] = prow.get("raw"), eax
        if prow.get("state") != "ok" or prow.get("raw") is None or int(prow["raw"]) != eax:
            problems.append(f"{name}: put_line's row parameter is {prow}, EAX is {eax}")
        if v.get(("put_line", "s"), {}).get("state") != "out":
            problems.append(f"{name}: put_line's s is not shown as optimized out: {v.get(('put_line', 's'))}")
    r["watch_status"] = page.inner_text("#watch_status")
    if not r["watch_status"].startswith("0 variables"):
        problems.append(f"{name}: Variables does not say there are no global variables: {r['watch_status']}")
    r["task"] = page.inner_text("#cur_task")
    if "no current_task" not in r["task"]:
        problems.append(f"{name}: the task row does not say the DWARF has no current_task: {r['task']}")
    page.screenshot(path=str(shots / f"{name}-debugger-1280-light.png"), full_page=True)
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 30)
    cov_open_all(page)
    rows = {x["path"]: x for x in cov_rows(page)}
    f = rows.get("boot/hello.c")
    r["coverage"] = f
    want = next((x for x in F["files"] if x[0] == "boot/hello.c"), None)
    if not f or not want or f["lines"] != want[1] or not f["linesRun"] or f["ins"] != F["insns"]["inFunctions"]:
        problems.append(f"{name}: the Coverage tree's boot/hello.c is {f}, files.json has {want}, {F['insns']}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        for w in (1280, 390):
            page.set_viewport_size({"width": w, "height": 900})
            time.sleep(0.3)
            if w == 390 and not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                problems.append(f"{name}: the page scrolls horizontally at 390 px ({scheme})")
            page.screenshot(path=str(shots / f"{name}-coverage-{w}-{scheme}.png"), full_page=w == 390)
    show_tab(page, "debugger")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        page.screenshot(path=str(shots / f"{name}-debugger-390-{scheme}.png"), full_page=True)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Where firstlook_case pauses each kernel: a function that has run code,
# locals and (Linux) a task by then.
FIRSTLOOK_POINTS = {"hello": "hello_main", "minified-linux": "tty_write",
                    "debian-6.1-686": "start_kernel", "debian-6.1-686-initrd": "start_kernel"}

# Every element a debug feature (D1-D7, V1-V3) puts on the page and the
# explanation a first-time user can reach for it: a "?" tip (a .tip in it,
# its heading or its row) or a title.
FIRSTLOOK_HELP_JS = """() => {
  const out = [], q = (s) => [...document.querySelectorAll(s)];
  const vis = (e) => !!(e.offsetParent || e.getClientRects().length);
  const tipped = (e) => !!(e.title || e.dataset.tip || e.querySelector('.tip, [title], [data-tip]'));
  for (const t of q('.tip'))
    if (!(t.dataset.tip || t.title || '').trim()) out.push('empty ? tip: ' + t.outerHTML.slice(0, 80));
  for (const h of q('#panel_debugger h3, #panel_source h3')) if (!h.querySelector('.tip')) out.push('heading without ?: ' + h.textContent);
  for (const id of ['cur_sym', 'cur_src', 'cur_task', 'call_stack', 'prev_sym'])
    { const e = document.getElementById(id); if (!e) out.push('no #' + id); else if (!tipped(e)) out.push('#' + id + ' has no title'); }
  for (const e of q('#call_stack button, .fmeth')) if (!e.title && !e.closest('[title]')) out.push('call stack item without title: ' + e.textContent);
  for (const e of q('#locals summary')) if (!tipped(e)) out.push('locals frame without title: ' + e.textContent);
  for (const id of ['show_src', 'src_tar', 'cov_filter', 'cov_export', 'watch_expr', 'sym_offset', 'symfile']) {
    const e = document.getElementById(id);
    if (!e) { out.push('no #' + id); continue; }
    const host = e.closest('label, .srcpick, .scan');
    if (!(host && host.querySelector('.tip'))) out.push('#' + id + ' has no ? next to it');
  }
  const ids = q('[id]').map((e) => e.id), dup = ids.filter((x, i) => ids.indexOf(x) !== i);
  if (dup.length) out.push('duplicate ids: ' + [...new Set(dup)].join(', '));
  return out;
}"""


def firstlook_case(browser, base, shots, name):
    """A first-time user's look at each kernel (D8): paused at the reset
    vector, Run to a function (JIT and a breakpoint where the page can),
    then every debug feature's element has an explanation (? tip or title),
    no id is used twice (the Source tab's notes once went to the Page
    tab), the entry line says the kernel was entered (it kept saying
    "Stepping BIOS and setup to the kernel entry..."), the symbol history
    says the JIT ran instead of naming the BIOS as the previous symbol,
    the Source tab says its counts are incomplete when the JIT ran kernel
    code and shows its notes, and no tab scrolls sideways at 390 px in
    light or dark."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    fn = FIRSTLOOK_POINTS[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 90)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 0.5):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", "3")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    wait_for(lambda: page.locator("#locals summary").count() > 0, 30)
    r["entry_info"] = page.inner_text("#entry_info")
    if "Stepping BIOS and setup" in r["entry_info"] or "Entered the kernel" not in r["entry_info"]:
        problems.append(f"{name}: paused in {fn}, the entry line says {r['entry_info']!r}")
    jit = page.evaluate("stepper.jitAfterEntry || stepper.jitBeforeEntry")
    r["jit"] = jit
    r["path"] = page.inner_text("#sym_info .crumbs")
    # The JIT's gap is in the symbol history (the path shows its newest 6;
    # the history keeps 16, so after many stepped calls it has gone).
    r["history_gap"] = page.evaluate("stepper.history.some((h) => h.sym === -3)")
    if jit and not r["history_gap"] and page.evaluate("stepper.history.length") < 16:
        problems.append(f"{name}: the JIT ran but the symbol history has no gap for it: {r['path']!r}")
    if page.evaluate("stepper.jitAfterEntry") and "JIT ran here" not in r["path"]:
        problems.append(f"{name}: the JIT ran kernel code but the path does not say so: {r['path']!r}")
    help_bad = page.evaluate(FIRSTLOOK_HELP_JS)
    problems += [f"{name}: {b}" for b in help_bad]
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and not page.inner_text("#cov_status").startswith("Reading"), 60)
    r["cov_status"] = page.inner_text("#cov_status")
    info = page.evaluate("(document.getElementById('covtree_info') || {}).innerText || ''")
    r["covtree_info"] = info[:80]
    if not info.startswith("Lines:"):
        problems.append(f"{name}: the Source tab has no notes under the tree: {r['covtree_info']!r}")
    if page.evaluate("stepper.jitAfterEntry") and not r["cov_status"].startswith("Incomplete"):
        problems.append(f"{name}: the JIT ran kernel code, the Source tab does not say it is incomplete: {r['cov_status']!r}")
    if page.is_disabled("#cov_export"):
        problems.append(f"{name}: lcov disabled although the tree is there")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        for w in (1280, 390):
            page.set_viewport_size({"width": w, "height": 900})
            for t in ("debugger", "page", "source", "calls", "memory"):
                show_tab(page, t)
                time.sleep(0.3)
                if not no_hscroll(page, "html"):
                    problems.append(f"{name}: {t} tab scrolls sideways at {w} px {scheme}")
            if w == 390:
                show_tab(page, "debugger")
                page.screenshot(path=str(shots / f"firstlook-{name}-390-{scheme}.png"), full_page=True)
    problems += [f"{name}: page error {e}" for e in errors]
    page.close()
    return problems, r


def firstlook_import_case(browser, base, shots):
    """The imported unstripped vmlinux: same explanations; the Source tab
    says why there is no tree and lcov is disabled."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 60)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") not in (None, "loading"), 30)
    r["cov_status"] = page.inner_text("#cov_status")
    if "imported vmlinux" not in r["cov_status"]:
        problems.append(f"import: the Source tab does not say why there is no tree: {r['cov_status']!r}")
    if not page.is_disabled("#cov_export"):
        problems.append("import: lcov is enabled without a tree")
    problems += [f"import: {b}" for b in page.evaluate(FIRSTLOOK_HELP_JS)
                 if not b.startswith(("no #cur_", "no #call_stack", "no #prev_sym", "#cur_", "#call_stack", "#prev_sym"))]
    problems += [f"import: page error {e}" for e in errors]
    page.close()
    return problems, r


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
    """The Source tab after a counted minified boot: init/main.c has lines
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
    show_tab(page, "source")
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
        page.evaluate("document.getElementById('panel_source').scrollIntoView()")
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
    m = re.search(r":(\d+)-\d+$", title)
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
    show_tab(page, "source")
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

    def check(label, path, lo, hi, ran=True):
        """Lines lo..hi of the panel: the first ran (or has code that did not
        run), every other one shows the same as its continuation, except a
        line with line rows of its own (-O0: line 43 of strlen's asm, the
        "1"(s) input, loads s into EDI), which shows as code of its own."""
        marks = panel_line_marks(page)
        got = {l: marks.get(l, ([], "", ""))[:2] for l in range(lo, hi + 1)}
        r[label] = {l: " ".join(c) + " " + m for l, (c, m) in got.items()}
        want = "exec" if ran else "code"
        own_rows = set(page.evaluate("([p, lo, hi]) => { const c = fileLines(p).code; const o = [];"
                                     " for (let l = lo; l <= hi; l++) if (c.has(l)) o.push(l); return o; }", [path, lo + 1, hi]))
        if own_rows:
            r[label + " lines with rows of their own"] = sorted(own_rows)
        if want not in got[lo][0]:
            problems.append(f"{label}: line {lo}, the statement's first line, is not {want}: {got[lo]}")
        for l in range(lo + 1, hi + 1):
            c, mark = got[l]
            if l in own_rows:
                if "cont" in c or not ("exec" in c or "code" in c):
                    problems.append(f"{label}: line {l} has code of its own but shows {c} {mark!r}")
                continue
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
        # The whole file (the Source tab's view).
        panel_open("p => openSourceFile(p)", path)
        code, ran = check(path + " whole file", path, lo, hi)
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
            check(path + " at " + hex(at), path, lo, hi)
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
        # The out-of-line function: at -Os its own copy does not run in this
        # boot (strlen is inlined where it is called), at -O0 it does.
        if func in syms and path.endswith("string_32.c"):
            f = syms[func]
            own_ran = page.evaluate("([a, n]) => stepper.countIn(a, a + n) > 0", [f["paddr"], f["size"]])
            r[func + "() ran"] = own_ran
            panel_open("pa => openSource(pa)", f["paddr"])
            check(path + " " + func + "()", path, lo, hi, ran=own_ran)
            page.click("#src_close")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


# Where the srclinks suite pauses: Run to this function, then step until
# EIP is in code inlined into it (the source row has an "inlined into" line).
SRCLINK_POINT = ("minified-linux", "start_kernel")


def srclink_opens(page, sel, how="click"):
    """Activate the first visible .srclink under sel (click, Enter or tap)
    with the Source panel closed; the panel must show the link's file with
    its line picked (.pick) inside the visible part of #src_body. Returns
    (link's file:line, None) or (that, problem)."""
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    link = page.locator(sel).locator(".srclink").first
    if not link.count():
        return None, f"no .srclink in {sel}"
    # A listing can be drawn again (a scan, new DWARF) between finding the
    # link and acting on it: try again on the new copy.
    for attempt in range(5):
        try:
            link.scroll_into_view_if_needed()
            path, line = link.get_attribute("data-file"), int(link.get_attribute("data-line"))
            if how == "enter":
                link.focus()
                page.keyboard.press("Enter")
            elif how == "tap":
                link.tap()
            else:
                link.click()
            break
        except PlaywrightError:
            if attempt == 4:
                raise
            time.sleep(0.5)
    want = f"{path}:{line}"
    row = f'#src_body [data-line="{line}"].pick'
    ok = wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-file") == path
                  and page.get_attribute("#src_body", "data-state") in ("ok", "none")
                  and (page.get_attribute("#src_body", "data-state") == "none" or page.locator(row).count() > 0), 60)
    if not ok:
        return want, f"{sel}: {how} on {want} did not open it in the Source panel ({page.get_attribute('#src_body', 'data-file')}, " \
                     f"{page.get_attribute('#src_body', 'data-state')}, {page.inner_text('#src_title')!r})"
    if page.get_attribute("#src_body", "data-state") == "none":
        why = page.inner_text("#src_body").strip()
        return want, None if why else f"{sel}: {want} opened without text and without saying why"
    p = srclink_line_shown(page, line)
    return want, p and f"{sel}: {want}: {p}"


def srclink_line_shown(page, line):
    """None when line's row in the Source panel is picked and lies inside
    #src_body's visible rect and the viewport, a second after the panel
    drew (the address list above it and late DWARF or text change the
    layout); else what is wrong."""
    time.sleep(1)
    got = page.evaluate("""(line) => { const e = document.querySelector('#src_body [data-line="' + line + '"]');
        if (!e) return null;
        const r = e.getBoundingClientRect(), b = document.getElementById('src_body').getBoundingClientRect();
        return { pick: e.classList.contains('pick'), top: r.top, bottom: r.bottom, boxTop: b.top, boxBottom: b.bottom,
                 vh: window.innerHeight, first: document.querySelector('#src_body [data-line]').dataset.line,
                 scroll: document.getElementById('src_body').scrollTop }; }""", line)
    if not got:
        return f"no row for line {line} in the Source panel"
    if not got["pick"]:
        return f"line {line} is not picked"
    if not (got["top"] >= got["boxTop"] - 1 and got["bottom"] <= got["boxBottom"] + 1):
        return f"line {line} is outside the Source panel's visible rows: {got}"
    if not (got["top"] >= 0 and got["bottom"] <= got["vh"]):
        return f"line {line} is outside the viewport: {got}"
    # The texts use plain separators (the operator's rule: no em-dashes).
    title = page.inner_text("#src_title")
    if "\u2014" in title:
        return f"the Source panel's title has an em-dash: {title!r}"
    return None


# Visible text outside a .srclink that reads like file:line, anywhere in
# the document (the Source panel shows source text: close it first).
PLAIN_SRCLOC_JS = r"""() => {
  const re = /[\w./-]+\.(?:c|h|S|lds|tbl):\d+\b/;
  const out = [];
  const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let n; (n = w.nextNode());) {
    const el = n.parentElement;
    if (!re.test(n.nodeValue) || !el || el.closest('script,style,.srclink') || !el.checkVisibility()) continue;
    const r = el.getBoundingClientRect();
    if (r.width === 0 && r.height === 0) continue;
    const anc = el.closest('[id]');
    out.push((anc ? '#' + anc.id : el.tagName) + ': ' + n.nodeValue.trim().slice(0, 100));
  }
  return out;
}"""


def srclink_plain_text(page, where):
    """A problem when a visible file:line anywhere on the page is plain
    text instead of a link; else None."""
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    plain = page.evaluate(PLAIN_SRCLOC_JS)
    return plain and f"{where}: file:line shown as plain text: {plain[:5]}"


def state_link_shown(page):
    """None when the state line's file:line link lies fully inside the
    viewport and inside the state line's own box (not cut off by its
    ellipsis) and is what a tap at its centre hits; else what is wrong."""
    got = page.evaluate("""() => { const st = document.getElementById('dbg_state'), e = st.querySelector('.srclink');
        if (!e) return null;
        const r = e.getBoundingClientRect(), b = st.getBoundingClientRect();
        const h = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return { text: st.textContent, rect: [r.left, r.right, r.top, r.bottom], box: [b.left, b.right], vw: window.innerWidth,
                 vh: window.innerHeight, hit: !!h && (h === e || e.contains(h)) }; }""")
    if not got:
        return "no link in the state line"
    left, right, top, bottom = got["rect"]
    if not (left >= -0.5 and right <= got["vw"] + 0.5 and top >= 0 and bottom <= got["vh"]):
        return f"state line link outside the viewport: {got}"
    if not (left >= got["box"][0] - 0.5 and right <= got["box"][1] + 0.5):
        return f"state line link cut off by the state line's box: {got}"
    if not got["hit"]:
        return f"a tap at the state line link's centre does not hit it: {got}"
    return None


def srclinks_case(browser, base, shots):
    """Every file:line the Debugger shows is a link to the Source panel at
    that line, through one helper (source.js srcLinkEl/srcLinkHtml): the
    state line, the Run to status, the source row and its inline chain,
    the call stack, the Code at EIP C lines, the locals' frame headers, the
    function facts (defined at, starts at), a watch's "defined at"; the
    other tabs too (Page listing, hovered byte, Graph detail, Coverage
    facts). Each opens its file with the line picked and in view; Enter
    opens one from the keyboard and moves the focus into the panel, Escape
    gives it back; a tap opens one on a 390 px phone. Light/dark at 1280
    and 390 px."""
    problems, r = [], {}
    name, fn = SRCLINK_POINT
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", fn)
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.inner_text("#dbg_status").startswith("Stopped at"), 240):
        page.close()
        return [f"{name}: Run to {fn} did not stop: {page.inner_text('#dbg_status')}"], r
    wait_for(lambda: page.locator("#dbg_status .srclink").count() > 0, 30)
    want, p = srclink_opens(page, "#dbg_status")
    r["Run to status"] = want
    if p:
        problems.append(p)
    # The "VM paused" overlay on the screen repeats the Run to status.
    want, p = srclink_opens(page, "#vm_reason")
    r["VM paused overlay"] = want
    if p:
        problems.append(f"{name} VM paused overlay: {p}")
    p = srclink_plain_text(page, f"{name} after Run to {fn}")
    if p:
        problems.append(p)
    # Paused at fn's first instruction, the state line names fn and links
    # the line at EIP, as the source row does (not the caller's call).
    wait_for(lambda: page.locator("#cur_src .srclink").count() > 0, 30)
    st = page.inner_text("#dbg_state")
    st_link = page.locator("#dbg_state .srclink")
    src_link = page.locator("#cur_src .frame:not(.inl) .srclink").first
    got = [(e.get_attribute("data-file"), e.get_attribute("data-line")) if e.count() else None for e in (st_link, src_link)]
    if f"in {fn}" not in st.replace("\n", " ") or got[0] != got[1]:
        problems.append(f"{name}: after Run to {fn} the state line {st!r} ({got[0]}) does not match the source row ({got[1]})")
    # Step into inlined code: the source row then has an "inlined into" line.
    for _ in range(60):
        if page.locator("#cur_src .frame.inl .srclink").count():
            break
        before = page.evaluate("stepper.steps")
        page.fill("#dbg_n", "5")
        page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("stepper.steps") > before and page.get_attribute("#dbg_state", "data-state") == "paused", 30)
        wait_for(lambda: page.get_attribute("#cur_src", "data-state") not in (None, "loading"), 20)
    r["steps"] = page.evaluate("stepper.steps")
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    wait_for(lambda: page.locator("#context .src .srclink").count() > 0, 20)
    wait_for(lambda: page.locator("#locals .lhead .srclink").count() > 0, 30)
    wait_for(lambda: page.locator("#cur_fn .fnloc").count() > 0, 30)
    views = {
        "state line": "#dbg_state",
        "source row": "#cur_src .frame:not(.inl)",
        "inline chain": "#cur_src .frame.inl",
        "call stack (top)": "#call_stack .cframe >> nth=0",
        "call stack (caller)": "#call_stack .cframe >> nth=1",
        "Code at EIP": "#context .src",
        "locals frame header": "#locals .lhead",
        "function defined at": "#cur_fn .fnfacts >> nth=0",
        "function starts at": "#cur_fn [data-entry]",
    }
    for view, sel in views.items():
        want, p = srclink_opens(page, sel)
        r[view] = want
        if p:
            problems.append(f"{name} {view}: {p}")
    # Every file:line in the Debugger tab outside the Source panel is a link.
    plain = page.evaluate("""() => [...document.querySelectorAll('#panel_debugger .srcloc, #dbg_state .srcloc, #dbg_status .srcloc')]
        .filter((e) => !e.classList.contains('srclink')).map((e) => e.textContent)""")
    if plain:
        problems.append(f"{name}: file:line shown as plain text in the Debugger: {plain[:5]}")
    r["links in the Debugger"] = page.locator("#panel_debugger .srclink").count()
    # A watch's "defined at".
    watch(page, "init_task")
    want, p = srclink_opens(page, '#watches .watch[data-expr="init_task"] .wdecl')
    r["watch defined at"] = want
    if p:
        problems.append(f"{name} watch: {p}")
    # Keyboard: Enter on a focused link opens it and moves the focus into the panel; Escape gives it back.
    want, p = srclink_opens(page, "#cur_src .frame:not(.inl)", "enter")
    r["keyboard"] = want
    if p:
        problems.append(f"{name} keyboard: {p}")
    elif page.evaluate("document.activeElement.id") != "src_body":
        problems.append(f"{name} keyboard: the focus did not move into the Source panel: {page.evaluate('document.activeElement.outerHTML.slice(0, 80)')}")
    else:
        page.keyboard.press("Escape")
        if page.is_visible("#src_panel") or not page.evaluate("document.activeElement.classList.contains('srclink')"):
            problems.append(f"{name} keyboard: Escape did not close the panel and give the focus back to the link")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        if page.is_visible("#src_panel"):
            page.click("#src_close")
        page.locator("#sym_info").scroll_into_view_if_needed()
        time.sleep(0.2)
        page.screenshot(path=str(shots / f"srclinks-{name}-debugger-1280-{scheme}.png"))
        inl = page.locator("#cur_src .frame.inl .srclink")
        if inl.count():
            inl.first.click()
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 20)
        if inl.count():
            p = srclink_line_shown(page, int(inl.first.get_attribute("data-line")))
            if p:
                problems.append(f"{name} 1280 {scheme}: inline chain link: {p}")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"srclinks-{name}-panel-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    # Other tabs: the Page listing's C lines and a hovered byte's "defined at".
    T = json.loads((SITE / kernel_entries()[name]["dwarf"] / "types.json").read_text())
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "page")
    wait_for(lambda: page.locator("#listing .src .srclink").count() > 0, 20)
    want, p = srclink_opens(page, "#listing .src")
    r["Page listing"] = want
    if p:
        problems.append(f"{name} Page listing: {p}")
    p = srclink_plain_text(page, f"{name} Page tab")
    if p:
        problems.append(p)
    it = next(v for v in T["vars"] if v[0] == "init_task")
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    hover_byte(page, it[1] + 16, click=True)
    want, p = srclink_opens(page, "#var_info")
    r["hovered byte"] = want
    if p:
        problems.append(f"{name} hovered byte: {p}")
    # Graph detail: where the selected call came from and its entry.
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "calls")
    page.evaluate("""() => { const st = G.stepper; G.view = 'timeline';
        G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fStart.length - 1; renderGraphInfo(); }""")
    wait_for(lambda: page.locator("#graph_src .srclink").count() > 0, 20)
    want, p = srclink_opens(page, "#graph_src")
    r["Graph detail"] = want
    if p:
        problems.append(f"{name} Graph detail: {p}")
    p = srclink_plain_text(page, f"{name} Calls tab")
    if p:
        problems.append(p)
    # Coverage: a function's facts (open its row's arrow).
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "source")
    page.fill("#cov_filter", fn)
    frow = page.locator(f'#cov_tree .covrow[data-kind="func"][data-path$=":{fn}"]').first
    if wait_for(lambda: frow.count() > 0, 30):
        frow.locator(".ttog").click()
        wait_for(lambda: page.locator("#cov_tree .covfacts .fnloc").count() > 0, 20)
        want, p = srclink_opens(page, "#cov_tree .covfacts")
        r["Coverage facts"] = want
        if p:
            problems.append(f"{name} Coverage facts: {p}")
        p = srclink_plain_text(page, f"{name} Source tab")
        if p:
            problems.append(p)
    else:
        problems.append(f"{name}: no Coverage row for {fn}")
    page.close()
    # A 390 px phone: tap the source row's link.
    ctx = browser.new_context(**PHONE)
    ph = ctx.new_page()
    ph.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: ph.get_attribute("#dbg_state", "data-state") == "paused", 60)
    ph.fill("#dbg_addr", fn)
    ph.tap("#dbg_runto")
    wait_for(lambda: ph.get_attribute("#dbg_state", "data-state") == "paused"
             and ph.inner_text("#dbg_status").startswith("Stopped at"), 240)
    wait_for(lambda: ph.locator("#cur_src .srclink").count() > 0, 30)
    want, p = srclink_opens(ph, "#cur_src .frame:not(.inl)", "tap")
    r["phone tap"] = want
    if p:
        problems.append(f"{name} phone: {p}")
    # The state line ("paused, in <function> (file:line)") keeps its link
    # on the screen when the function name is long: step until a stop in a
    # function with a long name, checking every stop on the way.
    longest = ""
    for k in range(40):
        if ph.is_visible("#src_panel"):
            ph.click("#src_close")
        ph.locator("#dbg_state").scroll_into_view_if_needed()
        fname = ph.evaluate("stepper.curSym >= 0 ? stepper.symName[stepper.curSym] : ''")
        p = state_link_shown(ph)
        if p:
            problems.append(f"{name} phone, in {fname} after {ph.evaluate('stepper.steps')} steps: {p}")
            break
        if len(fname) > len(longest):
            longest = fname
        if len(fname) >= 20:
            want, p = srclink_opens(ph, "#dbg_state", "tap")
            r["phone state line"] = f"{fname}: {want}"
            if p:
                problems.append(f"{name} phone state line in {fname}: {p}")
            break
        before = ph.evaluate("stepper.steps")
        ph.fill("#dbg_n", "5")
        ph.tap("#dbg_stepn")
        wait_for(lambda: ph.evaluate("stepper.steps") > before and ph.get_attribute("#dbg_state", "data-state") == "paused", 30)
        wait_for(lambda: ph.locator("#dbg_state .srclink").count() > 0, 20)
    else:
        problems.append(f"{name} phone: no stop in a function with a name of 20 or more characters (longest {longest!r})")
    if ph.is_visible("#src_panel"):
        ph.click("#src_close")
    for scheme in ("light", "dark"):
        ph.emulate_media(color_scheme=scheme)
        if ph.is_visible("#src_panel"):
            p = srclink_line_shown(ph, int(ph.locator("#cur_src .srclink").first.get_attribute("data-line")))
            if p:
                problems.append(f"{name} 390 {scheme}: source row link: {p}")
        ph.screenshot(path=str(shots / f"srclinks-{name}-panel-390-{scheme}.png"))
        if ph.is_visible("#src_panel"):
            ph.click("#src_close")
        ph.locator("#sym_info").scroll_into_view_if_needed()
        time.sleep(0.2)
        ph.screenshot(path=str(shots / f"srclinks-{name}-debugger-390-{scheme}.png"))
        if ph.locator("#cur_src .srclink").count():
            ph.locator("#cur_src .srclink").first.tap()
        wait_for(lambda: ph.is_visible("#src_panel"), 10)
    if not no_hscroll(ph, "body"):
        problems.append(f"{name} phone: horizontal scroll with the links")
    ctx.close()
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
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
    page.tap("#tab_machine")
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


STAMPED = ("style.css", "clock.js", "app.js", "jumps.js", "stepper.js", "graph.js", "source.js", "symimport.js", "livedis.js", "fastforward.js", "cpustate.js", "v86/libv86.js")


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


def screen_flush_case(browser, base):
    """v86 draws the screen's changed rows in a requestAnimationFrame loop
    and stops drawing when the VM stops, so rows written in the last frame
    before a pause stayed blank (CI: the Debian panic stop without its
    "end Kernel panic" line). With the frames starved (no
    requestAnimationFrame at all) the screen must still show what the
    kernel wrote: after single steps while paused (hello) and after the
    run's own end stop at full speed (minified Linux)."""
    problems, r = [], {}
    page = browser.new_page()
    page.add_init_script("window.requestAnimationFrame = () => 0;")
    page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.inner_text("#dbg_status").startswith("Stopped at"), 120)
    page.fill("#dbg_n", "1000")
    page.click("#dbg_stepn")
    wait_for(lambda: page.inner_text("#dbg_status").startswith("Stepped"), 60)
    wait_for(lambda: page.evaluate("stepper.screenText().includes('Still alive')") or page.inner_text("#dbg_status").startswith("Stepped 1,000"), 5)
    if not page.evaluate("stepper.screenText().includes('Hello, World!')"):
        page.fill("#dbg_n", "1000")
        page.click("#dbg_stepn")
        time.sleep(2)
    r["hello status"] = page.inner_text("#dbg_status")
    screen = page.inner_text("#screen_text")
    if "Hello, World!" not in screen:
        problems.append(f"hello: stepped past Hello, World! while paused, the screen does not show it ({r['hello status']!r})")
    page.close()
    if "minified-linux" in kernel_entries():
        page = browser.new_page()
        page.add_init_script("window.requestAnimationFrame = () => 0;")
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                 and page.evaluate("!!(stepper && stepper.end)"), 120)
        r["minified end"] = page.inner_text("#end_banner")
        screen = page.inner_text("#screen_text")
        if "Still alive" not in screen:
            problems.append(f"minified-linux: the run ended ({r['minified end']!r}), the screen does not show Still alive")
        page.close()
    return problems, r


def suite_debugger(ctx):
    failed = False
    for name in [n for n in ("hello", "minified-linux") if n in ctx.entries]:
        dp, dr = debugger_case(ctx.browser, ctx.base, name, None, ctx.shots)
        failed |= report("debugger", dp, dr, kernel=name)
    failed |= report("screen after a pause", *screen_flush_case(ctx.browser, ctx.base))
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


def suite_runto_phone(ctx):
    return report("runto_phone", *runto_phone_case(ctx.browser, ctx.base, ctx.shots))


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


def suite_operands(ctx):
    failed = False
    for name, fn, off, want in OPERAND_POINTS:
        if name in ctx.entries and "dwarf" in ctx.entries[name]:
            t0 = time.monotonic()
            p, r = operands_case(ctx.browser, ctx.base, ctx.shots, name, fn, off, want)
            failed |= report("operands", p, r, kernel=name, at=f"{fn}+{off:#x}", seconds=round(time.monotonic() - t0))
    return failed


def suite_lineaddr(ctx):
    failed = False
    for name in [n for n in LINEADDR_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = lineaddr_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("lineaddr", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = lineaddr_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("lineaddr_import", p, r, seconds=round(time.monotonic() - t0))


def suite_fnfacts(ctx):
    failed = False
    for name in [n for n in FNFACTS_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = fnfacts_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("fnfacts", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = fnfacts_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("fnfacts_import", p, r, seconds=round(time.monotonic() - t0))


def suite_branches(ctx):
    failed = False
    for name in [n for n in BRANCH_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = branches_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("branches", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = branches_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("branches_import", p, r, seconds=round(time.monotonic() - t0))

# Pauses for the unoptimized suite, in boot order: Run to each function,
# then single-step that many instructions (into calls), reading the
# Variables panel at every address not seen before. kmem_cache_alloc's
# walk crosses its inlined slab_alloc_node, whose list_first_entry_or_null
# block (head__, pos__, __mptr) covers only part of it.
UNOPT_PAUSES = [("start_kernel", 30), ("setup_arch", 30), ("kmem_cache_init", 30), ("kmem_cache_alloc", 260),
                ("rest_init", 20), ("kernel_init", 30), ("kernel_execve", 30), ("vfs_write", 40)]


def unoptimized_case(browser, base, shots, name):
    """minified-linux is built with -O0 (U2): for each of UNOPT_PAUSES, a
    fresh boot runs to the function and single-steps from there; at every
    address of the walk no parameter or local of any frame is optimized out
    (state "out", or the text "optimized out" anywhere in the Debugger),
    and at the first stop the values equal gdb's on a core of the same
    registers and stack (locals_check). minified-linux-os (the -Os product
    build) walks the same functions it has as symbols and must show some
    optimized-out variables (the check sees them); its count is reported.
    No page errors. Screenshots of the Variables panel light/dark, 1280 px."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"{name}: no -g ELF in $MKB_DEBUG_ELF_DIR to compare the values with gdb")
    errors = []
    seen, outs, nvars, shown, walks, unmapped = set(), [], 0, 0, {}, 0
    for fn, steps in UNOPT_PAUSES:
        s = syms.get(fn)
        if not s:
            if name == "minified-linux":
                problems.append(f"{name}: no symbol {fn} (inlined: not an -O0 build?)")
            continue
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e, fn=fn: errors.append(f"{fn}: {e}"))
        page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)

        def pc():
            return int(page.get_attribute("#regs", "data-phys") or -1)

        def step1():
            before = page.evaluate("stepper.steps")
            page.fill("#dbg_n", "1")
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("stepper.steps") - before >= 1 and
                     page.get_attribute("#dbg_state", "data-state") == "paused", 30, 0.05)

        page.fill("#dbg_addr", hex(s["vaddr"]))
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
        if not ok:
            problems.append(f"{name}: Run to {fn} did not stop there: {status}")
            page.close()
            continue
        n0, here = len(seen), set()
        for k in range(steps + 1):
            a = pc()
            if a not in here:
                here.add(a)
                seen.add(a)
                wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
                for fr in locals_open_all(page):
                    for sc in fr["scopes"]:
                        for v in sc["vars"]:
                            nvars += 1
                            if v["state"] == "out" or "optimized out" in (v.get("value") or "") + v.get("loc", ""):
                                outs.append(f"{a:#x} {sc['func']}.{v['name']}: {v['loc']}")
                            elif v["state"] == "unmapped":
                                unmapped += 1
                            else:
                                shown += 1
                text = page.inner_text("#panel_debugger")
                if "optimized out" in text and not any(o.startswith(f"{a:#x} ") for o in outs):
                    outs.append(f"{a:#x}: the Debugger says 'optimized out': " +
                                text[max(0, text.find("optimized out") - 80):text.find("optimized out") + 40])
                if k == 0:
                    dump = page.evaluate(UNWIND_DUMP_JS)
                    problems += [f"{name} at {fn}: {p}" for p in locals_check(page, elf, dump, r, fn)]
                    if fn == "kmem_cache_alloc":
                        for scheme in ("light", "dark"):
                            page.emulate_media(color_scheme=scheme)
                            time.sleep(0.2)
                            page.locator("#locals").screenshot(path=str(shots / f"unoptimized-{name}-{fn}-{scheme}.png"))
                        page.emulate_media(color_scheme="light")
            if k < steps:
                step1()
        walks[fn] = len(seen) - n0
        page.close()
    r["walks (new addresses)"] = walks
    r["addresses"], r["variables read"], r["with a value"], r["not mapped"] = len(seen), nvars, shown, unmapped
    r["optimized_out"] = len(outs)
    r["optimized out, first"] = outs[:6]
    if name == "minified-linux":
        if outs:
            problems.append(f"{name}: {len(outs)} optimized out at {len(seen)} addresses: " + "; ".join(outs[:8]))
        if len(seen) < 300:
            problems.append(f"{name}: only {len(seen)} addresses walked")
    elif not outs:
        problems.append(f"{name}: no optimized-out variable seen in the -Os build: the check does not see them")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    return problems, r


# Jump paths (J1): where the oracle walk starts and how many single steps
# it takes, per kernel. minified-linux's walk starts once interrupts are
# on, so it crosses the timer interrupt (IRQ0), task switches and init's
# system calls (user code has no disasm.json: its pairs are skipped).
JUMP_WALKS = {"hello": ("hello_main", 300), "minified-linux": ("rest_init", 150000)}

# Single-step n instructions with the stepper itself and return its own
# EIP sequence (the address of each instruction executed, in order) and
# the observed paths recorded meanwhile (count deltas). The sequence
# starts with the instruction executed before the walk when the stepper
# has one (its transfer is recorded at the walk's first fetch).
JUMP_WALK_JS = """(n) => {
    const st = stepper, before = new Map();
    for (const [k, e] of st.jumps) before.set(k, e[3]);
    const seq = st.jLast >= 0 ? [st.lastPhys] : [];
    const irq0 = st.irq0;
    let steps = st.steps;
    for (let i = 0; i < n; i++) {
        st.run(1, -1, 100);
        if (st.steps !== steps) { seq.push(st.lastPhys); steps = st.steps; }
    }
    const diff = [];
    for (const [k, e] of st.jumps) { const c = e[3] - (before.get(k) || 0); if (c) diff.push([e[0], e[1], e[2], c]); }
    updateDebugUi();
    return { seq, diff, irq0: st.irq0 - irq0, pending: st.jPend };
}"""

# Paths in and out of pa as the page has them (static from disasm.json,
# observed from the stepper): [[at, mask, stat, count], ...] each.
JUMP_PATHS_JS = """(pa) => { const P = jumpPaths(jumpsFor(disasm.insns), stepper, pa);
    const f = (l) => l.map((e) => [e.at, e.mask, e.stat, e.count]);
    return { into: f(P.into), out: f(P.out), noDirect: P.noDirect, late: P.late }; }"""


def jump_oracle(entry, seq):
    """The paths of an EIP sequence: (from, to) -> [count, kind, direct
    target] for every pair where the next instruction is not the one after
    (address + length, from disasm.json), or where the first is an
    unconditional transfer; kind as test_jumps.decode reads the bytes
    (None: a plain instruction, so an interrupt or exception came in
    between). Two v86 behaviours the stepper's sequence shows: a rep
    string instruction stays at its address until done (no path), and sti
    with interrupts off runs the next instruction in the same step, so a
    pair from an sti that is not sti + 1 is the next instruction's."""
    import test_jumps
    insns = {pa: bytes.fromhex(raw) for pa, raw, _ in json.loads((SITE / entry["disasm"]).read_text())["insns"]}
    out, unknown = {}, set()
    for a, b in zip(seq, seq[1:]):
        if a not in insns:
            unknown.add(a)
            continue
        raw = insns[a]
        if raw == b"\xfb" and b != a + 1 and a + 1 in insns:
            a, raw = a + 1, insns[a + 1]
        d = test_jumps.decode(raw, 0)
        cond = d and d[0] in (test_jumps.JCC, test_jumps.LOOP) or raw.lstrip(bytes(test_jumps.PREFIXES))[:1] == b"\xce"
        if not d and b == a:
            continue
        if b == a + len(raw) and (not d or cond):
            continue
        direct = a + d[1] if d and d[0] in (test_jumps.JCC, test_jumps.LOOP, test_jumps.JMP, test_jumps.CALL) else -1
        out.setdefault((a, b), [0, d[0] if d else None, direct])[0] += 1
    return out, unknown


def jumps_case(browser, base, shots, name):
    """Jump paths (J1). Static: the Page listing's counts for a known jcc
    target equal the static sources jumps.js finds (test_jumps.py checks
    those against the ELF's bytes in the kernels job). Dynamic: a walk of
    JUMP_WALKS single steps run by the stepper; every non-fall-through
    pair of its own EIP sequence (the oracle, jump_oracle) must be
    recorded with the same count and a fitting kind, and nothing else.
    UI: the target's line shows "←n·k×", its list names each source with
    count and file:line, "go there" opens the Page tab at the source,
    Enter/Escape work from the keyboard, the arc of that jcc is drawn
    (taken), EIP's own paths are summarised in the Debugger, the JIT note
    is there after a Run to through the JIT. Screenshots light/dark at
    1280 and 390 px, no horizontal scroll."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    by_name = {s["name"]: s for s in syms}
    fn, n = JUMP_WALKS[name]
    errors = []
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    s = by_name[fn]
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    jit_note = page.evaluate("jumpsJitNote()")
    r["jit note"] = jit_note
    if page.evaluate("stepper.jitAfterEntry || stepper.jitBeforeEntry") and "JIT" not in jit_note:
        problems.append(f"{name}: the JIT ran but the page does not say paths are missing: {jit_note!r}")
    # EIP's own paths in the Debugger, at the function's entry: its
    # callers' calls (static), and the JIT note.
    eip = s["paddr"]
    P2 = page.evaluate(JUMP_PATHS_JS, eip)
    wait_for(lambda: page.get_attribute("#eip_jumps", "data-pa") == str(eip), 10)
    r["eip summary"] = page.inner_text("#eip_jumps")
    if page.get_attribute("#eip_jumps", "data-into") != str(len(P2["into"])) or "Paths into EIP: " + str(len(P2["into"])) not in r["eip summary"]:
        problems.append(f"{name}: the Debugger's paths into EIP {eip:#x} say {r['eip summary']!r}, want {len(P2['into'])}")
    if not any(e[2] and e[1] == 1 << page.evaluate("JK.call") for e in P2["into"]):
        problems.append(f"{name}: no static call into {fn} at EIP: {P2['into']}")
    if page.evaluate("stepper.jitAfterEntry") and "JIT" not in r["eip summary"]:
        problems.append(f"{name}: the Run to used the JIT but the Debugger's paths do not say so")
    jump_inline_eip_check(page, name, entry, eip, problems, r, shots)
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.locator("#context").screenshot(path=str(shots / f"jumps-{name}-context-1280-{scheme}.png"))
        page.locator("#eip_jumps").screenshot(path=str(shots / f"jumps-{name}-eip-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    t0 = time.monotonic()
    w = page.evaluate(JUMP_WALK_JS, n)
    r["walk"] = f"{len(w['seq'])} instructions in {time.monotonic() - t0:.1f} s, {w['irq0']} timer interrupts"
    want, unknown = jump_oracle(entry, w["seq"])
    r["sources outside disasm.json (skipped)"] = len(unknown)
    got = {(f, t): (c, mask) for f, t, mask, c in w["diff"] if f not in unknown}
    jk = page.evaluate("JK")
    trap = 1 << jk["irq"] | 1 << jk["exc"]
    kinds = {}
    for k in sorted(set(want) | set(got)):
        wc, wk, direct = want.get(k, (0, None, -1))
        gc, gm = got.get(k, (0, 0))
        if wc != gc:
            problems.append(f"{name}: path {k[0]:#x} -> {k[1]:#x}: recorded {gc}x, the EIP sequence has it {wc}x")
            continue
        # The kind: a direct transfer to its target is that kind; anything
        # else may also be (or be instead) an interrupt or exception that
        # came before the target's first instruction ran.
        if direct == k[1]:
            ok = gm == 1 << wk
        else:
            ok = gm and not gm & ~(trap | (1 << wk if wk else 0))
        if not ok:
            problems.append(f"{name}: path {k[0]:#x} -> {k[1]:#x}: kinds {page.evaluate('jumpKindsText', gm)!r}, the instruction is {wk}")
        kinds[page.evaluate("jumpKindsText", gm)] = kinds.get(page.evaluate("jumpKindsText", gm), 0) + gc
    r["paths in the walk"] = len(want)
    r["transfers by kind"] = kinds
    if name == "minified-linux" and not any("interrupt" in k for k in kinds):
        problems.append(f"{name}: no interrupt path in the walk ({w['irq0']} timer interrupts raised)")

    # A jcc target the walk took: the line's counts and its list.
    jcc = [(c, f, t) for (f, t), (c, k, d) in want.items() if k == 1 and d == t]
    if not jcc:
        problems.append(f"{name}: no taken jcc in the walk")
        page.close()
        return problems, r
    c, src, tgt = max(jcc)
    P = page.evaluate(JUMP_PATHS_JS, tgt)
    total = sum(e[3] for e in P["into"])
    r["jcc target"] = f"{tgt:#x} from {src:#x} taken {c}x in the walk; paths in {[(hex(e[0]), e[2], e[3]) for e in P['into']]}"
    st_src = [e for e in P["into"] if e[0] == src]
    if not st_src or not st_src[0][2] or st_src[0][3] < c:
        problems.append(f"{name}: {tgt:#x}'s paths in lack the static jcc from {src:#x} taken >= {c}x: {P['into']}")
    page.evaluate(f"jumpGo({tgt})")
    page.evaluate("jumpHit = -1")
    line = f'#listing .ins[data-addr="{tgt}"]'
    wait_for(lambda: page.locator(line).count() == 1, 20)
    badge = page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]')
    want_badge = f"←{len(P['into'])}" + (f"·{total:,}×" if total else "")
    r["badge"] = badge.inner_text() if badge.count() else None
    if r["badge"] != want_badge:
        problems.append(f"{name}: badge of {tgt:#x} is {r['badge']!r}, want {want_badge!r}")
    arcs = page.eval_on_selector_all("#listing svg.jarcs path.jarc", "ps => ps.map(p => [+p.dataset.from, +p.dataset.to, p.classList.contains('taken')])")
    r["arcs"] = f"{len(arcs)} drawn, {page.get_attribute('#listing', 'data-arcs-dropped')} not drawn"
    if [src, tgt, True] not in arcs and page.get_attribute("#listing", "data-page") == str(src // 4096):
        problems.append(f"{name}: no taken arc {src:#x} -> {tgt:#x} in the listing")
    if len(arcs) != int(page.get_attribute("#listing", "data-arcs") or -1):
        problems.append(f"{name}: {len(arcs)} arcs drawn, the listing says {page.get_attribute('#listing', 'data-arcs')}")
    # Keyboard: Enter opens the list, Escape closes it and gives the focus back.
    badge.focus()
    page.keyboard.press("Enter")
    if not wait_for(lambda: page.is_visible("#jump_pop"), 5):
        problems.append(f"{name}: Enter on the badge did not open the list")
    rows = page.eval_on_selector_all("#jump_pop li", "ls => ls.map(l => [+l.dataset.at, +l.dataset.count, l.dataset.stat === '1', l.textContent, !!l.querySelector('.srclink')])")
    r["list"] = [x[3] for x in rows[:4]]
    if sorted((x[0], x[1]) for x in rows) != sorted((e[0], e[3]) for e in P["into"]):
        problems.append(f"{name}: the list of {tgt:#x} is {[(x[0], x[1]) for x in rows]}, want {[(e[0], e[3]) for e in P['into']]}")
    row = [x for x in rows if x[0] == src]
    if row and ("conditional jump from" not in row[0][3] or f"taken {row[0][1]:,}×" not in row[0][3]):
        problems.append(f"{name}: the row of {src:#x} says {row[0][3]!r}")
    if row and "dwarf" in entry and not row[0][4]:
        problems.append(f"{name}: the row of {src:#x} has no file:line link: {row[0][3]!r}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jumps-{name}-list-1280-{scheme}.png"))
        # the source's name keeps its underscores (the button had clipped them)
        ink = boxed_line_ink(page, f'#jump_pop li[data-at="{src}"] button.jgo')
        if ink and not all(ok for _, ok in ink):
            problems.append(f"{name}: the list's button for {src:#x} hides underscores ({scheme}): {ink}")
        r[f"underscores {scheme}"] = ink
    page.emulate_media(color_scheme="light")
    # The listing is redrawn while the list is open (a scan does that; CI
    # once lost the focus this way): Escape still returns it to the badge.
    page.evaluate("inspect()")
    page.keyboard.press("Escape")
    if not wait_for(lambda: page.is_hidden("#jump_pop"), 5):
        problems.append(f"{name}: Escape did not close the list")
    elif not page.evaluate(f"document.activeElement && document.activeElement.matches('.jc[data-pa=\"{tgt}\"]')"):
        problems.append(f"{name}: Escape did not give the focus back to the badge")
    # "go there" from the list: the Page tab at the source, marked.
    badge = page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]')
    badge.click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    page.click(f'#jump_pop li[data-at="{src}"] button.jgo')
    hit = f'#listing .ins.jhit[data-addr="{src}"]'
    if not wait_for(lambda: page.locator(hit).count() == 1, 10):
        problems.append(f"{name}: go there did not open {src:#x} in the Page tab")
    elif not page.evaluate("""(sel) => { const l = document.querySelector(sel), b = document.querySelector('#listing').getBoundingClientRect(),
            r = l.getBoundingClientRect(); return r.top >= b.top && r.bottom <= b.bottom; }""", hit):
        problems.append(f"{name}: {src:#x} is not in view after go there")

    jump_inline_case(page, name, entry, want, problems, r, shots)

    # Phone: no horizontal scroll, the list fits, screenshots.
    page.set_viewport_size({"width": 390, "height": 844})
    time.sleep(0.5)
    page.evaluate(f"jumpGo({tgt})")
    wait_for(lambda: page.locator(line).count() == 1, 10)
    time.sleep(0.5)
    if not no_hscroll(page, "#listing") or not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append(f"{name}: horizontal scroll at 390 px")
    page.locator(f'#listing .jc[data-pa="{tgt}"][data-dir="in"]').click()
    wait_for(lambda: page.is_visible("#jump_pop"), 5)
    box = page.locator("#jump_pop").bounding_box()
    if box and (box["x"] < 0 or box["x"] + box["width"] > 390):
        problems.append(f"{name}: the list is outside the 390 px viewport: {box}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jumps-{name}-list-390-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


# What the inline lines show of an instruction's paths: the "← n paths
# in" line above it (.jsrc) with its named sources (.je), and the
# targets after it (.jdst); the gutter stubs drawn for it.
JUMP_INLINE_JS = """([box, pa]) => { const b = document.querySelector(box);
  const s = b.querySelector('.jsrc[data-addr="' + pa + '"]'), d = b.querySelector('.jdst[data-addr="' + pa + '"]');
  const vis = (e) => { if (!e) return false; const r = e.getBoundingClientRect(), q = b.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && r.top >= q.top - 1 && r.bottom <= q.bottom + 1; };
  const ents = (e) => e ? [...e.querySelectorAll('.je')].map((x) => [+x.dataset.at, x.dataset.where, x.innerText]) : [];
  return { src: s ? s.innerText : null, srcVisible: vis(s), srcEntries: ents(s),
    dst: d ? d.innerText : null, dstEntries: ents(d), stubs: [...b.querySelectorAll('svg.jarcs path.jstub[data-addr="' + pa + '"]')].map((p) => p.dataset.way),
    wraps: s ? s.getClientRects().length : 0, fits: b.scrollWidth <= b.clientWidth + 1 }; }"""

# Sources shown inline before "+n more": at least this many when a
# listing is this wide (jumps.js jumpInlineMax).
JUMP_INLINE_SHOWN = {1280: 3, 390: 2}


def static_sources(entry):
    """target -> [source] of every direct jump or call in disasm.json, from
    the instruction bytes (test_jumps.decode), not from jumps.js; and the
    mnemonic llvm-objdump prints for each instruction."""
    import test_jumps
    out, mnem = {}, {}
    for pa, raw, text in json.loads((SITE / entry["disasm"]).read_text())["insns"]:
        b = bytes.fromhex(raw)
        mnem[pa] = next((w for w in text.split() if not re.fullmatch(r"lock|rep\w*|notrack|bnd|[c-gs]s|data\d+|addr\d+", w)), "")
        d = test_jumps.decode(b, pa)
        if not d or d[1] < 0 or d[0] in (test_jumps.LJMP, test_jumps.LCALL):
            continue
        if d[0] in (test_jumps.JCC, test_jumps.LOOP) and d[1] == pa + len(b):
            continue
        out.setdefault(d[1], []).append(pa)
    return out, mnem


def code_name(syms, pa):
    """pa as llvm-objdump names a branch target: name or name+0xoff of the
    smallest sized symbol holding it; a sizeless one exactly there; hex."""
    best = None
    for s in syms:
        if s["size"] > 0 and s["paddr"] <= pa < s["paddr"] + s["size"] and (not best or s["size"] < best["size"]):
            best = s
    if best:
        return best["name"] + (f"+{pa - best['paddr']:#x}" if pa > best["paddr"] else ""), best
    marks = [s for s in syms if not s["size"] and s["paddr"] == pa]
    return (marks[0]["name"], None) if marks else (f"{pa:#x}", None)


def jump_inline_check(page, box, pa, label, syms, mnem, stat_src, problems, r, width, must=None, kind=None):
    """The inline sources of pa in box (#listing or #context), with no
    click: "← n path(s) in" with n = the paths jumps.js has (whose static
    ones must be the sources the bytes decode to), the first few of them
    in jumps.js's order, each naming its source as llvm-objdump does
    (symbol+offset), the source's mnemonic (static) or the kind
    (observed), how often it was taken, where it is (in view, above or
    below in the same function, another function) with a gutter stub for
    the ones out of view, and "+k more" for the rest."""
    P = page.evaluate(JUMP_PATHS_JS, pa)
    got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    tag = f"{label} {pa:#x} ({code_name(syms, pa)[0]}) in {box} at {width} px"
    r[f"inline {label} {width}"] = f"{pa:#x}: {got['src']!r}"
    n = len(P["into"])
    if got["src"] is None:
        problems.append(f"{tag}: no inline sources line ({n} paths in)")
        return got
    if not got["srcVisible"] and box == "#listing":
        # A redraw at a stop scrolls to EIP's line: bring the instruction
        # back as "go there" does, its sources line must come with it.
        r[f"inline {label} {width} rescrolled"] = True
        page.evaluate(f"scrollListingTo(document.querySelector('#listing .ins[data-addr=\"{pa}\"]'))")
        got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    if not got["srcVisible"]:
        problems.append(f"{tag}: the sources line is not in view")
    want_head = f"← {n} path{'s' if n > 1 else ''} in"
    if not got["src"].startswith(want_head):
        problems.append(f"{tag}: the line starts {got['src'][:40]!r}, want {want_head!r}")
    stat = sorted(e[0] for e in P["into"] if e[2])
    if stat != sorted(stat_src.get(pa, [])):
        problems.append(f"{tag}: jumps.js's static sources {[hex(x) for x in stat]} are not the decoded ones {[hex(x) for x in sorted(stat_src.get(pa, []))]}")
    shown = [e[0] for e in got["srcEntries"]]
    k = min(n, JUMP_INLINE_SHOWN[width])
    if shown[:k] != [e[0] for e in P["into"][:k]] or len(shown) > n:
        problems.append(f"{tag}: shows sources {[hex(x) for x in shown]}, jumps.js has them as {[hex(e[0]) for e in P['into'][:len(shown) or k]]}")
    if n > len(shown) and f"+{n - len(shown)} more" not in got["src"]:
        problems.append(f"{tag}: {n} paths, {len(shown)} shown, but no '+{n - len(shown)} more': {got['src']!r}")
    sym_here = code_name(syms, pa)[1]
    view = set(page.eval_on_selector_all(f"{box} .ins", "ls => ls.map((l) => +l.dataset.addr)"))
    jk = page.evaluate("JK")
    for at, where, text in got["srcEntries"]:
        e = next((x for x in P["into"] if x[0] == at), None)
        if not e:
            continue
        name, s = code_name(syms, at)
        if name not in text:
            problems.append(f"{tag}: source {at:#x} shown as {text!r}, llvm-objdump names it {name}")
        if e[2] and mnem.get(at) and not text.startswith(mnem[at] + " from"):
            problems.append(f"{tag}: source {at:#x} shown as {text!r}, its instruction is {mnem[at]}")
        if not e[2]:
            words = [w for kk, w in (("irq", "interrupt"), ("exc", "exception"), ("ret", "ret"), ("iret", "iret")) if e[1] & 1 << jk[kk]]
            if words and not any(w in text for w in words):
                problems.append(f"{tag}: observed source {at:#x} shown as {text!r}, its kinds are {page.evaluate('jumpKindsText', e[1])}")
        if e[3] and f"×{e[3]:,}" not in text:
            problems.append(f"{tag}: source {at:#x} taken {e[3]}x, shown as {text!r}")
        want_where = "view" if at in view else ("up" if at < pa else "down") if s and sym_here and s["paddr"] == sym_here["paddr"] else "far"
        if where != want_where:
            problems.append(f"{tag}: source {at:#x} marked {where!r}, it is {want_where!r} (listing shows {len(view)} instructions)")
    ways = {w for _, w, _ in got["srcEntries"] if w != "view"}
    if ways and not ways <= set(got["stubs"]):
        problems.append(f"{tag}: sources out of view {sorted(ways)} but gutter stubs only {got['stubs']}")
    if must is not None and must not in [e[0] for e in P["into"]]:
        problems.append(f"{tag}: {must:#x} is not among the paths in")
    if kind and not any(kind in t for _, _, t in got["srcEntries"]):
        problems.append(f"{tag}: no {kind!r} source shown: {got['src']!r}")
    if not got["fits"]:
        problems.append(f"{tag}: the listing scrolls horizontally")
    return got


def jump_inline_out_check(page, box, pa, label, syms, problems, r, want_to, kind):
    """The targets shown after the instruction at pa: kind and the name of
    want_to, with no click."""
    got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    r[f"inline out {label}"] = f"{pa:#x}: {got['dst']!r}"
    name = code_name(syms, want_to)[0]
    if kind == "direct":
        ok = want_to in [e[0] for e in got["dstEntries"]]
    else:
        ok = bool(got["dst"]) and re.search(kind + r"\w* to", got["dst"]) and name in got["dst"]
    if not ok:
        problems.append(f"{label} {pa:#x} in {box}: the targets after it are {got['dst']!r}, want {kind} to {name}")
    return got


def jump_inline_case(page, name, entry, want, problems, r, shots):
    """B2: where jumps come from is visible in the asm without a click.
    After the walk (want: its oracle), at 1280 and 390 px: a taken jcc's
    target, a loop head (a backward jump in one function), a function
    entry with callers in other functions, a return site and its ret, an
    interrupt entry (minified Linux), and EIP's line in Code at EIP."""
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    stat_src, mnem = static_sources(entry)
    jk = page.evaluate("JK")
    func = lambda pa: code_name(syms, pa)[1]
    picks = []
    jcc = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k == jk["jcc"] and d == t), reverse=True)
    if jcc:
        picks.append(("jcc target", jcc[0][2], jcc[0][1], None))
    loops = [(c, f, t) for (f, t), (c, k, d) in want.items() if k in (jk["jcc"], jk["jmp"]) and d == t and t < f and func(f) and func(t) and func(f)["paddr"] == func(t)["paddr"]]
    if not loops:
        loops = [(0, f, t) for t, fs in stat_src.items() for f in fs if t < f and func(f) and func(t) and func(f)["paddr"] == func(t)["paddr"] and mnem.get(f, "").startswith("j")]
    if loops:
        c, f, t = max(loops)
        picks.append(("loop head", t, f, None))
    # The function with the most callers elsewhere: other functions in
    # other pages (out of view in the Page listing).
    entries = [(len([f for f in fs if func(f) and func(f)["paddr"] != t and f // 4096 != t // 4096]), t)
               for t, fs in stat_src.items() if func(t) and func(t)["paddr"] == t]
    callers, fn_entry = max(entries)
    r["function entry"] = f"{code_name(syms, fn_entry)[0]}: {callers} callers in other functions and pages"
    picks.append(("function entry", fn_entry, None, None))
    rets = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k == jk["ret"]), reverse=True)
    if rets:
        picks.append(("return site", rets[0][2], rets[0][1], "ret"))
    else:
        problems.append(f"{name}: no ret in the walk")
    irqs = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k is None), reverse=True)
    if name == "minified-linux":
        if irqs:
            picks.append(("interrupt entry", irqs[0][2], irqs[0][1], "interrupt"))
        else:
            problems.append(f"{name}: no interrupt in the walk")
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000 if width > 500 else 844})
        time.sleep(0.5)
        for label, pa, must, kind in picks:
            page.evaluate(f"jumpGo({pa})")
            page.evaluate("jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{pa}"]').count() == 1, 20)
            time.sleep(0.3)
            jump_inline_check(page, "#listing", pa, f"{name} {label}", syms, mnem, stat_src, problems, r, width, must, kind)
            if label == "function entry" or (label == "jcc target" and width == 1280):
                for scheme in ("light", "dark"):
                    page.emulate_media(color_scheme=scheme)
                    time.sleep(0.3)
                    page.locator("#listing").screenshot(path=str(shots / f"jumpsrc-{name}-{label.replace(' ', '-')}-{width}-{scheme}.png"))
                page.emulate_media(color_scheme="light")
        if rets:
            c, f, t = rets[0]
            page.evaluate(f"jumpGo({f})")
            page.evaluate("jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{f}"]').count() == 1, 20)
            jump_inline_out_check(page, "#listing", f, f"{name} ret {width}", syms, problems, r, t, "ret")
        callers_of = sorted(stat_src.get(fn_entry, []), key=lambda x: (x // 4096 == fn_entry // 4096, x))
        if callers_of:
            page.evaluate(f"jumpGo({callers_of[0]})")
            page.evaluate("jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{callers_of[0]}"]').count() == 1, 20)
            # Out of view the target gets a button and a stub; in view
            # (hello is one page) its name in the text is all there is.
            if page.locator(f'#listing .ins[data-addr="{fn_entry}"]').count():
                r[f"call {width}"] = f"{callers_of[0]:#x} calls {fn_entry:#x} in view"
            else:
                got = jump_inline_out_check(page, "#listing", callers_of[0], f"{name} call {width}", syms, problems, r, fn_entry, "direct")
                if "out-far" not in got["stubs"]:
                    problems.append(f"{name}: the call at {callers_of[0]:#x} to another function has no gutter stub at {width} px: {got['stubs']}")
    page.set_viewport_size({"width": 1280, "height": 1000})
    time.sleep(0.3)
    # "+k more" opens the whole list; a named source goes there.
    page.evaluate(f"jumpGo({fn_entry})")
    wait_for(lambda: page.locator(f'#listing .jsrc[data-addr="{fn_entry}"]').count() == 1, 20)
    n = len(page.evaluate(JUMP_PATHS_JS, fn_entry)["into"])
    more = page.locator(f'#listing .jsrc[data-addr="{fn_entry}"] .jmore')
    if more.count():
        more.click()
        if not wait_for(lambda: page.is_visible("#jump_pop"), 5) or page.locator("#jump_pop li").count() != n:
            problems.append(f"{name}: '+k more' of {fn_entry:#x} did not list all {n} paths in")
        page.keyboard.press("Escape")
    elif n > JUMP_INLINE_SHOWN[1280]:
        problems.append(f"{name}: {fn_entry:#x} has {n} paths in and no '+k more'")
    far = page.locator(f'#listing .jsrc[data-addr="{fn_entry}"] .je[data-where="far"] button.jto')
    if far.count():
        at = int(far.first.get_attribute("data-go"))
        far.first.click()
        if not wait_for(lambda: page.locator(f'#listing .ins.jhit[data-addr="{at}"]').count() == 1, 10):
            problems.append(f"{name}: the inline source {at:#x} of {fn_entry:#x} did not open there")
        r["inline go there"] = f"{fn_entry:#x} -> {at:#x}"
    else:
        r["inline go there"] = "no source in another page"


def jump_inline_eip_check(page, name, entry, eip, problems, r, shots):
    """Code at EIP at a function's entry: its callers inline above it."""
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    stat_src, mnem = static_sources(entry)
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000 if width > 500 else 844})
        page.evaluate("renderContext()")
        time.sleep(0.5)
        got = jump_inline_check(page, "#context", eip, f"{name} EIP", syms, mnem, stat_src, problems, r, width)
        here = code_name(syms, eip)[1]
        if got["src"] and not any((code_name(syms, at)[1] or {}).get("paddr") != here["paddr"] for at, _, _ in got["srcEntries"]):
            problems.append(f"{name}: Code at EIP {eip:#x} shows no caller from another function: {got['src']!r}")
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            time.sleep(0.3)
            page.locator("#context").screenshot(path=str(shots / f"jumpsrc-{name}-eip-{width}-{scheme}.png"))
        page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    page.evaluate("renderContext()")


# The jump path texts of a listing (selector): its ⊘ titles and, for
# #eip_jumps / #jump_info, their text.
JUMP_CLAIMS_JS = """(sel) => { const out = [];
  for (const box of document.querySelectorAll(sel)) {
    if (box.id === 'eip_jumps' || box.id === 'jump_info') out.push([box.id, box.textContent]);
    for (const s of box.querySelectorAll('.jnone')) out.push(['title', s.dataset.tip || s.title]); }
  return out; }"""


def jump_claims_live(claims, where):
    """Claims of "no direct path" made from instructions decoded live must
    say so: jumps from code outside the decoded range are not known."""
    bad = {}
    for kind, t in claims:
        low = t.lower()
        if "no jump or call in the disassembly" in low or "nothing jumps or calls here" in low:
            why = "claims what only the whole disassembly could tell"
        elif ("no direct path" in low or "without a direct path" in low) and not ("decoded live" in low or "live decode" in low):
            why = "says 'no direct path' without the live-decode scope"
        else:
            continue
        bad.setdefault((kind, why), []).append(t)
    return [f"{where}: {len(ts)} {kind} {why}, e.g. {ts[0][:220]!r}" for (kind, why), ts in bad.items()]


def jumps_live_case(browser, base, shots):
    """J2: with no disasm.json (the Debian sample, an imported vmlinux)
    static paths come only from the instructions decoded live (a page, or
    a few around EIP); the ⊘ tooltip, the list notes, the Page note and
    the Debugger's paths at EIP must say so instead of claiming nothing
    jumps or calls here. With disasm.json the wording names the whole
    disassembly (hello's padding after a ret)."""
    problems, r = [], {}
    cases = []
    deb = next((n for n in kernel_entries() if n.startswith("debian") and "initrd" not in kernel_entries()[n]), None)
    if deb:
        cases.append((deb, None))
    elf = import_inputs()
    if elf and "minified-linux" in kernel_entries():
        cases.append(("minified-linux", elf[1]))
    elif os.environ.get("CI"):
        problems.append("no vmlinux to import (kernels/import-test)")
    for name, imp in cases:
        where = name + (" + imported " + imp.name if imp else "")
        errors = []
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        if imp:
            page.evaluate("startPaused = true")
            page.set_input_files("#symfile", str(imp))
            wait_for(lambda: page.get_attribute("#sym_status", "data-state") in ("imported", "error"), 60)
            if page.get_attribute("#sym_status", "data-state") != "imported":
                problems.append(f"{where}: import failed: {page.inner_text('#sym_status')[:200]!r}")
                page.close()
                continue
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.evaluate("typeof symbols !== 'undefined' && !!symbols && symbols.symbols.length > 0"), 120)
        sk = page.evaluate("symbols.symbols.find((s) => s.name === 'start_kernel')")
        page.fill("#dbg_addr", "start_kernel")
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {sk["paddr"]}, 400)
        if not ok:
            problems.append(f"{where}: Run to start_kernel did not stop there: {status!r}")
            page.close()
            continue
        if page.evaluate("!!disasm"):
            problems.append(f"{where}: has disasm.json, the case needs a live decode")
        wait_for(lambda: page.get_attribute("#eip_jumps", "data-pa") == str(sk["paddr"]), 20)
        wait_for(lambda: page.get_attribute("#context", "data-source") == "live", 20)
        r[where + " eip"] = page.inner_text("#eip_jumps")[:300]
        claims = page.evaluate(JUMP_CLAIMS_JS, "#context, #eip_jumps")
        # start_kernel's first instruction: callers are outside the
        # decoded range, so it must not be claimed unreachable.
        if not any("no direct path" in t.lower() for _, t in claims):
            r[where + " note"] = "no 'no direct path' claim at the start_kernel entry"
        problems += jump_claims_live(claims, where + " Code at EIP")
        show_tab(page, "page")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 int(page.get_attribute("#listing", "data-insns") or 0) > 0, 30)
        time.sleep(1)
        claims = page.evaluate(JUMP_CLAIMS_JS, "#listing, #jump_info")
        r[where + " page ⊘"] = sum(1 for k, _ in claims if k == "title")
        r[where + " page note"] = page.inner_text("#jump_info")[:300]
        problems += jump_claims_live(claims, where + " Page listing")
        # Z1: the live decode does not know what comes before its first
        # instruction, nor who calls a function: neither gets ⊘.
        for sel in ("#listing", "#context"):
            marked = [a for a, _, _ in page.evaluate(NOPATH_MARKS_JS, sel)]
            first = page.evaluate(f"(() => {{ const e = document.querySelector('{sel} .ins'); return e ? +e.dataset.addr : -1; }})()")
            entries = page.evaluate("(l) => l.filter((a) => symbols.symbols.some((s) => s.paddr === a && /^[Tt]$/.test(s.type || 'T')))", marked)
            r[f"{where} {sel} ⊘"] = len(marked)
            if first in marked:
                problems.append(f"{where} {sel}: ⊘ on the first instruction decoded ({first:#x}): nothing before it is known")
            if entries:
                problems.append(f"{where} {sel}: ⊘ on {len(entries)} function entries (their callers are not decoded), e.g. {[hex(a) for a in entries[:4]]}")
        # A list opened at an instruction with paths in shows the scope note.
        if page.locator("#listing .jc[data-dir=in]").count():
            page.click("#listing .jc[data-dir=in] >> nth=0")
            wait_for(lambda: page.is_visible("#jump_pop"), 5)
            notes = page.eval_on_selector_all("#jump_pop p.note", "ps => ps.map((p) => p.textContent)")
            problems += jump_claims_live([("list note", t) for t in notes], where + " list")
            if not any("decoded live" in t or "live decode" in t for t in notes):
                problems.append(f"{where}: the list does not say static paths are only from the live decode: {notes}")
            page.keyboard.press("Escape")
        page.screenshot(path=str(shots / f"jumps-live-{name}{'-import' if imp else ''}-page.png"))
        problems += stub_arrays_live(page, where, r, shots, name + ("-import" if imp else ""))
        if errors:
            problems.append(f"{where}: page errors: {errors[:3]}")
        page.close()
    # With disasm.json the claim is about the whole disassembly.
    if "hello" in kernel_entries():
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.evaluate("!!disasm && !!symbols"), 30)
        got = page.evaluate("""() => { const S = jumpsFor(disasm.insns);
          const pa = disasm.insns.map((x) => x[0]).find((a) => jumpPaths(S, null, a).noDirect);
          if (pa === undefined) return null;
          renderEipJumps(S, pa); const t = document.getElementById('eip_jumps').textContent;
          const d = document.createElement('div'); d.innerHTML = jumpBadgesHtml(jumpPaths(S, null, pa), pa);
          const n = d.querySelector('.jnone'); return { pa, eip: t, title: n ? n.dataset.tip || n.title : null }; }""")
        r["hello build"] = got
        if not got:
            problems.append("hello: no instruction without a direct path in disasm.json")
        else:
            if "nothing jumps or calls here" not in got["eip"] or "decoded live" in got["eip"]:
                problems.append(f"hello: the build's wording at EIP changed: {got['eip']!r}")
            if not got["title"] or "whole disassembly" not in got["title"] or "decoded live" in got["title"]:
                problems.append(f"hello: the build's ⊘ title changed: {got['title']!r}")
        page.close()
    return problems, r


# Linux's arrays of entry stubs (Z2) and the function that puts
# start + i * stride into the IDT. Nothing jumps to a stub after the first
# and the stub before it ends in a jmp, so without the rule each one would
# carry ⊘.
STUB_ARRAYS = {"early_idt_handler_array": "idt_setup_early_handler",
               "irq_entries_start": "idt_setup_apic_and_irq_gates",
               "spurious_entries_start": "idt_setup_apic_and_irq_gates"}

# The live-decoded instructions of the Page listing in [lo, hi) after a
# jump (padding int3/nop is not a stub), the ⊘ marks among them and, per
# stub, what Paths at EIP says about how it is reached.
STUB_LIVE_JS = """([lo, hi]) => { const box = document.getElementById('listing'), S = box._jS;
  const marks = new Set();
  for (const n of box.querySelectorAll('.jnone')) {
    let e = n.previousElementSibling; while (e && !e.matches('.ins')) e = e.previousElementSibling;
    if (e) marks.add(+e.dataset.addr); }
  const out = [];
  for (const e of box.querySelectorAll('.ins')) {
    const a = +e.dataset.addr; if (a < lo || a >= hi) continue;
    const i = S ? jumpsIndex(S, a) : -1, text = i >= 0 ? S.insns[i][2] : '';
    let eip = '';
    if (S && i >= 0) { renderEipJumps(S, a); eip = document.getElementById('eip_jumps').textContent; }
    out.push([a, text, marks.has(a), eip]); }
  renderContext();
  return out; }"""


def stub_arrays_live(page, where, r, shots, tag):
    """Z2: in a live decode (no disasm.json) the stubs of Linux's entry
    stub arrays have no ⊘ and say how they are reached (start + i *
    stride, from the function that fills the IDT); padding after a stub's
    jmp may keep ⊘."""
    problems = []
    for name, setter in STUB_ARRAYS.items():
        sym = page.evaluate("(n) => symbols.symbols.find((s) => s.name === n) || null", name)
        if not sym:
            r[f"{where} {name}"] = "not in the symbol table"
            if name == "early_idt_handler_array":
                problems.append(f"{where}: no early_idt_handler_array symbol")
            continue
        lo, hi = sym["paddr"], sym["paddr"] + sym["size"]
        page.evaluate("(a) => jumpTo(a)", lo)
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 page.locator(f"#listing .ins[data-addr='{lo}']").count() > 0, 30)
        time.sleep(0.5)
        lines = page.evaluate(STUB_LIVE_JS, [lo, hi])
        pad = re.compile(r"^(nop|int3)")
        stubs = [x for x in lines if not pad.match(x[1])]
        bad = [f"{a:#x} {t}" for a, t, m, _ in stubs if m]
        r[f"{where} {name}"] = (f"{len(lines)} instructions, {len(stubs)} not padding, {len(bad)} with ⊘, "
                                f"{sum(1 for x in lines if x[2])} ⊘ on padding" if not bad else bad[:4])
        if not stubs:
            problems.append(f"{where}: {name}: no instructions decoded in the Page listing")
            continue
        if bad:
            problems.append(f"{where}: {name}: ⊘ on {len(bad)} stubs, e.g. {bad[:4]}")
        # The stubs after the first (the first instruction after a jmp, past
        # any padding): Paths at EIP names the array, the stride and the setter.
        later = [b for a, b in zip(stubs, stubs[1:]) if a[1].startswith("jmp")]
        if len(later) < 3:
            problems.append(f"{where}: {name}: expected at least 3 stubs after the first, got {len(later)}")
        for a, t, _, eip in later:
            if not (f"{name} + " in eip and setter in eip):
                problems.append(f"{where}: {name}+{a - lo:#x}: Paths at EIP does not say how the stub is reached: {eip[:300]!r}")
                break
        if later:
            r[f"{where} {name} text"] = later[0][3][:300]
        if name == "early_idt_handler_array":
            page.screenshot(path=str(shots / f"jumps-live-{tag}-stubs.png"))
    return problems


# Instructions of a box (selector) that carry the ⊘ mark: the .ins line
# before each .jnone (the marks follow their line's span).
NOPATH_MARKS_JS = """(sel) => { const out = [];
  for (const box of document.querySelectorAll(sel))
    for (const n of box.querySelectorAll('.jnone')) {
      let e = n.previousElementSibling;
      while (e && !e.matches('.ins')) e = e.previousElementSibling;
      if (e) out.push([+e.dataset.addr, e.classList.contains('cur'), n.dataset.tip || n.title]); }
  return out; }"""

# The ⊘ oracle while stepping (Z1). In the build's disassembly, "reached
# only indirectly" = the instructions without a direct path in whose
# predecessor does not fall through (jumpPaths over the bare static paths,
# no taken addresses, no stepper: the old meaning of ⊘). Steps n
# instructions with the stepper; the first time EIP stops on one of them
# that has not run yet, Code at EIP is drawn there and its marks returned.
# Then: every instruction that has run, or has an observed path in, must
# not carry ⊘ (jumpPaths with the stepper, as the listings draw it).
NOPATH_WALK_JS = """(n) => {
  const st = stepper, S0 = jumpsStatic(disasm.insns, jumpsToPhys(symbols && symbols.sections));
  const only = new Set(disasm.insns.map((x) => x[0]).filter((pa) => jumpPaths(S0, null, pa).noDirect));
  let atEip = null, steps = st.steps;
  for (let i = 0; i < n; i++) {
    st.run(1, -1, 100);
    if (st.steps === steps) continue;
    steps = st.steps;
    const pc = st.nextPhys();
    if (!atEip && only.has(pc) && !st.executed(pc)) {
      renderContext();
      const marks = [];
      for (const x of document.querySelectorAll('#context .jnone')) {
        let e = x.previousElementSibling; while (e && !e.matches('.ins')) e = e.previousElementSibling;
        if (e) marks.push(+e.dataset.addr); }
      const sym = kernelSymLookup()(pc);
      atEip = { pc, at: i, sym: sym ? sym.name + '+0x' + (pc - sym.paddr).toString(16) : '', marks };
    }
  }
  updateDebugUi();
  const S = jumpsFor(disasm.insns);
  const ran = [], seenIn = [], bad = [];
  for (const [pa] of disasm.insns) {
    const r = st.executed(pa), o = st.jumpIn.has(pa);
    if (r && only.has(pa)) ran.push(pa);
    if (o && only.has(pa)) seenIn.push(pa);
    if ((r || o) && jumpPaths(S, st, pa).noDirect) bad.push([pa, r, o]);
  }
  let none = 0;
  for (const [pa] of disasm.insns) if (jumpPaths(S, st, pa).noDirect) none++;
  return { atEip, ran, seenIn, bad: bad.slice(0, 40), nbad: bad.length, only: only.size, none,
    executed: st.countIn(0, st.seen.length * 8), steps: st.steps };
}"""

NOPATH_WALKS = {"hello": ("hello_main", 400), "hello-os": ("hello_main", 400),
                "minified-linux": ("start_kernel", 2000000), "minified-linux-os": ("start_kernel", 1000000)}


def nopath_case(browser, base, shots, name):
    """Z1, oracle 1: no instruction the stepper has run, or has seen a path
    into, carries ⊘ ("no path leads here"), in the build's disassembly
    (jumpPaths, as the listings draw it) nor in the drawn Code at EIP and
    Page listing. Paused at the start, the EIP line has no ⊘ (nothing
    is known to come before the kernel's first instruction). Runs to
    NOPATH_WALKS[name] and walks its count of single steps; the first time EIP is on an instruction reached only
    indirectly (a function called through a pointer, a switch's jump
    table, an exception fixup: no direct path in, the instruction before
    does not fall through) Code at EIP must not mark that line. On the
    minified kernels the walk must meet such instructions (else the check
    proves nothing). Screenshots of such a line in the Page tab."""
    problems, r = [], {}
    errors = []
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("!!disasm && !!symbols && !!stepper"), 60)
    page.evaluate("renderContext()")
    if any(cur for _, cur, _ in page.evaluate(NOPATH_MARKS_JS, "#context")):
        problems.append(f"{name}: paused at the start, Code at EIP marks the EIP line with ⊘")
    fn, n = NOPATH_WALKS[name]
    s = page.evaluate(f"symbols.symbols.find((s) => s.name === '{fn}')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        page.close()
        return problems + [f"{name}: Run to {fn} did not stop there: {status}"], r
    t0 = time.monotonic()
    w = page.evaluate(NOPATH_WALK_JS, n)
    r["walk"] = f"{w['steps']} steps in {time.monotonic() - t0:.0f} s, {w['executed']} instructions run"
    r["reached only indirectly (static)"] = w["only"]
    r["of them run"] = len(w["ran"])
    r["of them with an observed path in"] = len(w["seenIn"])
    r["⊘ left after the walk"] = w["none"]
    r["first stop on one"] = w["atEip"] and {k: (hex(v) if k == "pc" else v) for k, v in w["atEip"].items() if k != "marks"}
    if name.startswith("minified") and not w["ran"]:
        problems.append(f"{name}: the walk ran no instruction reached only indirectly: the check proves nothing")
    if w["nbad"]:
        problems.append(f"{name}: {w['nbad']} instructions that ran or have an observed path in carry ⊘, e.g. "
                        f"{[(hex(a), 'ran' if x else '', 'path in' if o else '') for a, x, o in w['bad'][:8]]}")
    if name.startswith("minified") and not w["atEip"]:
        problems.append(f"{name}: EIP never stopped on an instruction reached only indirectly")
    elif w["atEip"] and w["atEip"]["pc"] in w["atEip"]["marks"]:
        problems.append(f"{name}: Code at EIP marks EIP {w['atEip']['pc']:#x} ({w['atEip']['sym']}) with ⊘")
    # The drawn listings: Code at EIP now, and the Page tab at a few of the
    # instructions that ran.
    marks = page.evaluate(NOPATH_MARKS_JS, "#context")
    if any(cur for _, cur, _ in marks):
        problems.append(f"{name}: Code at EIP marks the EIP line with ⊘")
    for pa in w["ran"][:4]:
        page.evaluate(f"jumpTo({pa})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
        time.sleep(0.3)
        drawn = [a for a, _, _ in page.evaluate(NOPATH_MARKS_JS, "#listing")]
        ran = set(page.evaluate("(l) => l.filter((a) => stepper.executed(a))", drawn))
        if ran:
            problems.append(f"{name}: the Page listing of {pa:#x} marks {len(ran)} instructions that ran with ⊘, e.g. {sorted(ran)[:4]}")
        if pa in drawn:
            problems.append(f"{name}: the Page listing marks {pa:#x} (it ran) with ⊘")
    if w["ran"]:
        pa = w["ran"][0]
        page.evaluate(f"jumpTo({pa})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
        page.evaluate(f"document.querySelector('#listing .ins[data-addr=\"{pa}\"]').scrollIntoView({{block: 'center'}})")
        for width in (1280, 390):
            page.set_viewport_size({"width": width, "height": 1000 if width > 400 else 844})
            time.sleep(0.3)
            page.evaluate(f"document.querySelector('#listing .ins[data-addr=\"{pa}\"]').scrollIntoView({{block: 'center'}})")
            for scheme in ("light", "dark"):
                page.emulate_media(color_scheme=scheme)
                time.sleep(0.3)
                page.screenshot(path=str(shots / f"nopath-{name}-ran-{width}-{scheme}.png"))
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"{name}: horizontal scroll at {width} px")
        page.emulate_media(color_scheme="light")
        page.set_viewport_size({"width": 1280, "height": 1000})
    # The tooltip of a mark left (padding after a ret or jmp, a function
    # nothing calls) says what it rests on, the run included.
    titles = [t for _, _, t in page.evaluate(NOPATH_MARKS_JS, "#listing")]
    if page.get_attribute("#listing", "data-source") != "build":
        titles = []
    r["a ⊘ title"] = titles[0] if titles else None
    r["jit note"] = page.evaluate("jumpsJitNote()")
    jit = r["jit note"].startswith("v86")
    for t in titles[:1]:
        for want in ("No path leads here: nothing jumps or calls to this instruction", "whole disassembly", "address is not stored anywhere",
                     "does not fall through", "has not run while stepping") + (("JIT",) if jit else ()):
            if want not in t:
                problems.append(f"{name}: the ⊘ title lacks {want!r}: {t!r}")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_nopath(ctx):
    failed = False
    for name in [n for n in NOPATH_WALKS if n in ctx.entries and "disasm" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = nopath_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("nopath", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed


# ---- marks in the asm listings: the page's popover (T1) --------------------

# The popover's state: shown, its text and where it is.
TIP_STATE_JS = """() => { const p = document.getElementById('tip_pop'), r = p.getBoundingClientRect();
  return { shown: !p.hidden, text: p.textContent, l: r.left, r: r.right, t: r.top, b: r.bottom, w: innerWidth, h: innerHeight }; }"""

# Where the lines of a box are (to see that a popover moves nothing).
TIP_LAYOUT_JS = """(sel) => { const b = document.querySelector(sel);
  return [b.scrollHeight, document.documentElement.scrollHeight,
          ...[...b.querySelectorAll('.ins')].slice(0, 80).map((e) => { const r = e.getBoundingClientRect(); return [r.x, r.y, r.width, r.height].map(Math.round).join(','); })]; }"""

# A point on the first marked element of sel (an SVG path: on its stroke).
TIP_POINT_JS = """(sel) => { const e = document.querySelector(sel); if (!e) return null;
  e.scrollIntoView({ block: 'center' });
  if (e.getTotalLength) { const p = e.getPointAtLength(e.getTotalLength() / 2), m = e.getScreenCTM();
    return { x: p.x * m.a + p.y * m.c + m.e, y: p.x * m.b + p.y * m.d + m.f }; }
  const r = e.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; }"""

# Marks in the listings that may still carry a title (the native tooltip
# a touch screen never shows).
TIP_TITLED = ("#listing .jnone[title], #listing .jc[title], #listing .jto[title], #listing .jmore[title], #listing .srclink[title], "
              "#listing .src[title], #listing .ins[title], #cov_info [title], #context .jnone[title], #context .jc[title], "
              "#context .jto[title], #context .jmore[title], #context .srclink[title], #context .src[title], #context .ins[title]")


def tip_wait(page, shown, timeout=3):
    wait_for(lambda: page.evaluate(TIP_STATE_JS)["shown"] == shown, timeout, 0.05)
    return page.evaluate(TIP_STATE_JS)


def tip_tap_away(page):
    """Tap the page's left margin (nothing there acts), off the popover."""
    page.touchscreen.tap(3, page.viewport_size["height"] // 2)


def tip_fits(st, where, problems):
    if st["l"] < 0 or st["r"] > st["w"] + 0.5 or st["t"] < 0 or st["b"] > st["h"] + 0.5:
        problems.append(f"{where}: the popover is off screen: {st}")


def tip_long_press(page, cdp, x, y, ms=800):
    cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
    time.sleep(ms / 1000)
    cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})


def tip_none_first_line(text, live, where, problems):
    first = text.split("\n")[0]
    if not first.startswith("No path leads here"):
        problems.append(f"{where}: the ⊘ popover does not start by saying what ⊘ means: {first!r}")
    if len(first) > 160:
        problems.append(f"{where}: the ⊘ popover's first line is long ({len(first)} chars): {first!r}")
    if live and "decoded live" not in text:
        problems.append(f"{where}: the live-decode ⊘ popover does not say only the instructions decoded live are known: {text!r}")
    if not live and ("whole disassembly" not in text or "decoded live" in text):
        problems.append(f"{where}: the ⊘ popover does not say it rests on the whole disassembly: {text!r}")
    if "has not run" not in text:
        problems.append(f"{where}: the ⊘ popover does not say what ran: {text!r}")


def tipmarks_open_none(page, name, problems):
    """Step a little (tint on), go to an instruction with ⊘ in the Page
    tab; its address or None."""
    page.goto(f"{page.context._tip_base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("!!disasm && !!symbols && !!stepper"), 60)
    fn = NOPATH_WALKS[name][0]
    s = page.evaluate(f"symbols.symbols.find((s) => s.name === '{fn}')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        problems.append(f"{name}: Run to {fn} did not stop there: {status}")
        return None
    steps = page.evaluate("stepper.steps")
    page.fill("#dbg_n", "30")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and page.evaluate("stepper.steps") >= steps + 30, 30)
    pa = page.evaluate("""() => { const S = jumpsFor(disasm.insns);
      const x = disasm.insns.find(([a]) => jumpPaths(S, stepper, a).noDirect); return x ? x[0] : null; }""")
    if pa is None:
        problems.append(f"{name}: no instruction with ⊘ in the disassembly")
        return None
    page.evaluate(f"jumpTo({pa})")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096) and page.locator("#listing .jnone").count() > 0, 20)
    time.sleep(0.3)
    return pa


def tipmarks_case(browser, base, shots, name="hello"):
    """T1: ⊘ and the other marks of the asm listings use the page's
    popover (title attributes never show on a touch screen): hover at
    1280, Enter/Space/Esc, a click pins it and it survives a redraw; tap at
    390 px touch; buttons that act on a tap (↑ ↓ ↗ and names, ←n, file:line)
    show it on hover, keyboard focus and a long press, and a plain tap
    still acts; the gutter stubs, the EIP mark and the tint key too. The
    popover moves nothing and fits on screen."""
    problems, r = [], {}
    errors = []
    # ---- desktop: hover, keyboard, click
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx._tip_base = base
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)))
    pa = tipmarks_open_none(page, name, problems)
    if pa is None:
        ctx.close()
        return problems, r
    titled = page.evaluate(f"[...document.querySelectorAll({TIP_TITLED!r})].map((e) => e.className + ': ' + e.title.slice(0, 60))")
    if titled:
        problems.append(f"{name}: {len(titled)} marks in the listings still rely on a title: {titled[:4]}")
    jn = page.locator("#listing .jnone").first
    attrs = jn.evaluate("e => [e.getAttribute('tabindex'), e.getAttribute('role'), !!e.dataset.tip]")
    if attrs != ["0", "button", True]:
        problems.append(f"{name}: ⊘ is not a focusable button with a popover text: tabindex, role, data-tip = {attrs}")
    jn.scroll_into_view_if_needed()
    before = page.evaluate(TIP_LAYOUT_JS, "#listing")
    jn.hover()
    st = tip_wait(page, True)
    if not st["shown"]:
        problems.append(f"{name}: hovering ⊘ at 1280 px shows no popover")
    else:
        r["⊘ popover"] = st["text"]
        tip_none_first_line(st["text"], False, f"{name} 1280 hover", problems)
        tip_fits(st, f"{name} 1280 ⊘", problems)
    if page.evaluate(TIP_LAYOUT_JS, "#listing") != before:
        problems.append(f"{name}: showing the ⊘ popover moved the listing's lines")
    page.mouse.move(5, 990)
    if tip_wait(page, False)["shown"]:
        problems.append(f"{name}: the hover popover of ⊘ stays after the mouse left")
    jn.focus()
    for key in ("Enter", " "):
        page.keyboard.press(key)
        if not tip_wait(page, True)["shown"]:
            problems.append(f"{name}: {key!r} on a focused ⊘ shows no popover")
        page.keyboard.press("Escape")
        if tip_wait(page, False)["shown"]:
            problems.append(f"{name}: Esc does not close the ⊘ popover")
    # A click pins it; a redraw of the listing keeps it on the new mark.
    jn.click()
    tip_wait(page, True)
    page.mouse.move(5, 990)
    page.evaluate("(() => { const l = document.getElementById('listing'), y = l.scrollTop; l.innerHTML = l.innerHTML; l.scrollTop = y; })()")
    time.sleep(0.6)
    st = page.evaluate(TIP_STATE_JS)
    anchored = page.evaluate("!!document.querySelector('#listing .jnone[aria-expanded=true]')")
    if not st["shown"] or not anchored:
        problems.append(f"{name}: the pinned ⊘ popover did not survive a redraw of the listing (shown {st['shown']}, anchored {anchored})")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.screenshot(path=str(shots / f"tipmarks-{name}-none-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    page.click("h1")
    if tip_wait(page, False)["shown"]:
        problems.append(f"{name}: a click elsewhere does not close the pinned ⊘ popover")
    # The tint key.
    key = page.locator("#cov_info .nxkey")
    if not key.count():
        problems.append(f"{name}: no tint key after stepping")
    else:
        key.hover()
        st = tip_wait(page, True)
        r["tint key"] = st["text"][:160]
        if not st["shown"] or "not executed" not in st["text"]:
            problems.append(f"{name}: the tint key's popover: {st['text']!r}")
        page.mouse.move(5, 990)
        tip_wait(page, False)
    # Buttons in the listing: hover shows the popover, a click still acts.
    jc = page.locator("#listing .jc").first
    if jc.count():
        jc.hover()
        st = tip_wait(page, True)
        if not st["shown"] or "path" not in st["text"]:
            problems.append(f"{name}: hovering a ←n/→n button: {st['text']!r}")
        jc.click()
        if not wait_for(lambda: page.is_visible("#jump_pop"), 3):
            problems.append(f"{name}: clicking a ←n/→n button no longer opens its list")
        page.keyboard.press("Escape")
        page.mouse.move(5, 990)
        tip_wait(page, False)
    # Code at EIP: the EIP mark, ↑ ↓ ↗ and the gutter stubs.
    show_tab(page, "debugger")
    page.evaluate("renderContext()")
    time.sleep(0.3)
    marks = page.evaluate("""() => ({ eip: document.querySelectorAll('#context .eipmark[data-tip]').length,
      jto: document.querySelectorAll('#context .jto[data-tip]').length, stub: document.querySelectorAll('#context .jstubhit[data-tip]').length })""")
    r["Code at EIP marks"] = marks
    if not marks["eip"]:
        problems.append(f"{name}: Code at EIP's EIP mark has no popover")
    else:
        page.locator("#context .eipmark").first.hover()
        st = tip_wait(page, True)
        if "EIP" not in st["text"]:
            problems.append(f"{name}: the EIP mark's popover: {st['text']!r}")
        page.mouse.move(5, 990)
        tip_wait(page, False)
    if not marks["jto"]:
        problems.append(f"{name}: no ↑ ↓ ↗ or named jump end with a popover in Code at EIP")
    else:
        b = page.locator("#context .jto").first
        b.focus()
        page.keyboard.press("Shift+Tab")
        page.keyboard.press("Tab")
        st = tip_wait(page, True)
        if not st["shown"] or "go there" not in st["text"]:
            problems.append(f"{name}: keyboard focus on a jump end in Code at EIP shows no popover: {st['text']!r}")
        b.hover()
        st = tip_wait(page, True)
        if not st["shown"] or "go there" not in st["text"]:
            problems.append(f"{name}: hovering a jump end in Code at EIP: {st['text']!r}")
        page.mouse.move(5, 990)
        page.keyboard.press("Escape")
        tip_wait(page, False)
    if not marks["stub"]:
        problems.append(f"{name}: no gutter stub with a popover in Code at EIP")
    else:
        pt = page.evaluate(TIP_POINT_JS, "#context .jstubhit")
        page.mouse.move(pt["x"], pt["y"])
        st = tip_wait(page, True)
        r["stub"] = st["text"][:120]
        if not st["shown"] or "out of view" not in st["text"] and "another function" not in st["text"]:
            problems.append(f"{name}: hovering a gutter stub: {st['text']!r}")
        page.mouse.move(5, 990)
        tip_wait(page, False)
    ctx.close()
    # ---- phone: tap, long press
    ctx = browser.new_context(**PHONE)
    ctx._tip_base = base
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)))
    cdp = ctx.new_cdp_session(page)
    pa = tipmarks_open_none(page, name, problems)
    if pa is not None:
        jn = page.locator("#listing .jnone").first
        jn.scroll_into_view_if_needed()
        before = page.evaluate(TIP_LAYOUT_JS, "#listing")
        jn.tap()
        st = tip_wait(page, True)
        if not st["shown"]:
            problems.append(f"{name}: tapping ⊘ at 390 px shows no popover")
        else:
            tip_none_first_line(st["text"], False, f"{name} 390 tap", problems)
            tip_fits(st, f"{name} 390 ⊘", problems)
        if page.evaluate(TIP_LAYOUT_JS, "#listing") != before:
            problems.append(f"{name}: showing the ⊘ popover at 390 px moved the listing's lines")
        if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
            problems.append(f"{name}: horizontal scroll at 390 px")
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            time.sleep(0.2)
            page.screenshot(path=str(shots / f"tipmarks-{name}-none-390-{scheme}.png"))
        page.emulate_media(color_scheme="light")
        tip_tap_away(page)
        if tip_wait(page, False)["shown"]:
            problems.append(f"{name}: a tap elsewhere does not close the ⊘ popover at 390 px")
        show_tab(page, "debugger")
        page.evaluate("renderContext()")
        time.sleep(0.3)
        # A long press on a jump end shows where it goes and does not go.
        if page.locator("#context .jto").count():
            pt = page.evaluate(TIP_POINT_JS, "#context .jto")
            hist = page.evaluate("history.length")
            tip_long_press(page, cdp, pt["x"], pt["y"])
            st = tip_wait(page, True)
            time.sleep(0.4)
            if not st["shown"] or "go there" not in st["text"]:
                problems.append(f"{name}: a long press on a jump end at 390 px shows no popover: {st['text']!r}")
            if page.get_attribute("#tab_debugger", "aria-selected") != "true" or page.evaluate("history.length") != hist:
                problems.append(f"{name}: a long press on a jump end went there")
            page.screenshot(path=str(shots / f"tipmarks-{name}-longpress-390.png"))
            tip_tap_away(page)
            tip_wait(page, False)
            page.evaluate(TIP_POINT_JS, "#context .jto")
            page.locator("#context .jto").first.tap()
            if not wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true", 5):
                problems.append(f"{name}: a tap on a jump end at 390 px no longer goes there")
            if page.evaluate(TIP_STATE_JS)["shown"]:
                problems.append(f"{name}: a plain tap on a jump end showed the popover: {page.evaluate(TIP_STATE_JS)['text']!r}")
            show_tab(page, "debugger")
            page.evaluate("renderContext()")
            time.sleep(0.3)
        for sel, want in (("#context .eipmark", "EIP"), ("#context .jstubhit", "function")):
            pt = page.evaluate(TIP_POINT_JS, sel)
            if not pt:
                problems.append(f"{name}: no {sel} at 390 px")
                continue
            page.touchscreen.tap(pt["x"], pt["y"])
            st = tip_wait(page, True)
            if not st["shown"] or want not in st["text"]:
                problems.append(f"{name}: tapping {sel} at 390 px: {st['text']!r}")
            else:
                tip_fits(st, f"{name} 390 {sel}", problems)
            tip_tap_away(page)
            tip_wait(page, False)
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def tipmarks_live_case(browser, base, shots):
    """T1, live decode (the Debian sample has no disasm.json): tapping ⊘ at
    390 px says only the instructions decoded live are known."""
    problems, r = [], {}
    deb = next((n for n in kernel_entries() if n.startswith("debian") and "initrd" not in kernel_entries()[n]), None)
    if not deb:
        return ([] if not os.environ.get("CI") else ["no Debian sample kernel"]), r
    errors = []
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={deb}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("typeof symbols !== 'undefined' && !!symbols && symbols.symbols.length > 0"), 120)
    sk = page.evaluate("symbols.symbols.find((s) => s.name === 'start_kernel')")
    page.fill("#dbg_addr", "start_kernel")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {sk["paddr"]}, 400)
    if not ok:
        ctx.close()
        return [f"{deb}: Run to start_kernel did not stop there: {status!r}"], r
    show_tab(page, "page")
    for _ in range(8):
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and int(page.get_attribute("#listing", "data-insns") or 0) > 0, 30)
        time.sleep(0.5)
        if page.locator("#listing .jnone").count():
            break
        page.click("#page_next")
    jn = page.locator("#listing .jnone")
    r["page"] = page.get_attribute("#listing", "data-page")
    if not jn.count():
        problems.append(f"{deb}: no ⊘ in 8 live-decoded pages from start_kernel")
    else:
        jn.first.scroll_into_view_if_needed()
        jn.first.tap()
        st = tip_wait(page, True)
        r["⊘ popover"] = st["text"]
        if not st["shown"]:
            problems.append(f"{deb}: tapping ⊘ at 390 px shows no popover")
        else:
            tip_none_first_line(st["text"], True, f"{deb} 390 tap", problems)
            tip_fits(st, f"{deb} 390 ⊘", problems)
        page.screenshot(path=str(shots / f"tipmarks-{deb}-none-390.png"))
    if errors:
        problems.append(f"{deb}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def suite_tipmarks(ctx):
    failed = False
    for name in [n for n in ("hello", "minified-linux") if n in ctx.entries and "disasm" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = tipmarks_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("tipmarks", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = tipmarks_live_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("tipmarks_live", p, r, seconds=round(time.monotonic() - t0))


# ---- the legend and the yellow lines (L1) -----------------------------------

def check_legend_module():
    import importlib.util
    spec = importlib.util.spec_from_file_location("check_legend", SITE / "check-legend.py")
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# Every class token of the page's elements now (the renderers' output).
LEGEND_LIVE_CLASSES_JS = """(scope) => { const out = {}; for (const e of document.querySelectorAll(scope)) {
  const c = e.getAttribute('class'); if (!c) continue;
  for (const t of c.split(/\\s+/)) if (t && !out[t]) out[t] = (e.closest('[id]') || {}).id || ''; } return out; }"""

# The computed look of the first element with exactly these classes under
# root (null when none): what the legend's sample must share with it.
LEGEND_LOOK_JS = """([root, cls, tag]) => { const want = cls.split(' ').sort().join(' ');
  for (const e of document.querySelectorAll(root + ' ' + tag + '.' + cls.split(' ').join('.'))) {
    if ((e.getAttribute('class') || '').split(/\\s+/).sort().join(' ') !== want) continue;
    if (e.closest('#about_legend') && root !== '#about_legend') continue;
    const s = getComputedStyle(e);
    return [s.color, s.backgroundColor, s.outlineStyle, s.outlineColor, s.fontStyle, s.opacity, s.stroke, s.fill, s.strokeDasharray].join(' | '); }
  return null; }"""

# The swatches the legend draws from the page's own tables.
LEGEND_SWATCHES_JS = """() => { const css = getComputedStyle(document.documentElement);
  const tok = (k) => { const d = document.createElement('span'); d.style.background = 'var(--' + k + ')'; document.body.append(d);
    const v = getComputedStyle(d).backgroundColor; d.remove(); return v; };
  const row = (id) => [...document.querySelectorAll('#' + id + ' li')].map((li) => { const sw = li.querySelector('span');
    return [li.dataset.cls || li.dataset.key || '', sw ? getComputedStyle(sw).backgroundColor : '', li.textContent]; });
  return { mem: row('legend_mem'), graph: row('legend_graph'), regions: Object.keys(REGION_NAMES), sections: Object.keys(SECTION_NAMES),
    palette: Object.entries(PALETTE), tok: Object.fromEntries([...Object.keys(REGION_NAMES), ...Object.keys(SECTION_NAMES)].map((k) => [k, tok(k)])) }; }"""


def css_rgb(h):
    h = h.lstrip("#")
    return "rgb(" + ", ".join(str(int(h[i:i + 2], 16)) for i in (0, 2, 4)) + ")"


def legend_case(browser, base, shots, name="minified-linux"):
    """L1: the About tab's legend has a sample of every colour and mark the
    page draws. After stepping, every class with a colour (style.css) on
    an element of the listings, the Debugger, the Source panel, the
    Source tab and the Memory tab is in the legend unless check-legend.py
    calls it page furniture, and each sample looks exactly like the real
    element (computed colour, background, outline, italics, opacity,
    stroke) in light and dark. The generated swatches (memory regions and
    sections, Graph subsystems) come from the page's own tables. Code at
    EIP and the Page listing link to the legend; Back returns. Screenshots
    1280 and 390 px, light and dark, no horizontal scroll."""
    problems, r = [], {}
    errors = []
    cl = check_legend_module()
    colour = cl.colour_classes((SITE / "style.css").read_text())
    want = sorted(set(colour) - set(cl.CHROME))
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("!!disasm && !!symbols && !!stepper"), 60)
    if not page.locator("#about_legend").count():
        page.close()
        return [f"{name}: the About tab has no legend (#about_legend)"], r
    legend = set(page.evaluate("() => [...document.querySelectorAll('#about_legend [class]')].flatMap((e) => e.getAttribute('class').split(/\\s+/))"))
    missing = [c for c in want if c not in legend]
    if missing:
        problems.append(f"{name}: the legend has no sample of {missing}")
    # Draw everything: the entry (yellow lines), the Memory scan, Run to
    # start_kernel, steps, the Page listing with C lines, the Source panel,
    # the Coverage tree, locals.
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    run_to_wait(page, {0x100000}, 300)
    page.evaluate("renderContext()")
    seen = page.evaluate(LEGEND_LIVE_CLASSES_JS, "#context *, #regs *")
    show_tab(page, "memory")
    page.click("#scan")
    time.sleep(1)
    show_tab(page, "debugger")
    s = page.evaluate("symbols.symbols.find((s) => s.name === 'start_kernel')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    run_to_wait(page, {s["paddr"]}, 300)
    page.fill("#dbg_n", "300")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and page.evaluate("stepper.steps") > 0, 60)
    time.sleep(1)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_debugger *"))
    pa = s["paddr"]
    page.evaluate(f"jumpTo({pa})")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
    time.sleep(0.5)
    page.click("#reread")
    time.sleep(0.5)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_page *"))
    page.evaluate("openSourceFile('init/main.c')")
    time.sleep(2)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#src_panel *"))
    page.click("#src_close")
    show_tab(page, "source")
    time.sleep(2)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_source *"))
    show_tab(page, "memory")
    time.sleep(0.5)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_memory *, #problems *, .banner"))
    drawn = sorted(c for c in seen if c in colour and c not in cl.CHROME)
    r["coloured classes drawn"] = len(drawn)
    lacking = [f"{c} (#{seen[c]})" for c in drawn if c not in legend]
    if lacking:
        problems.append(f"{name}: drawn with a colour but not in the legend: {lacking}")
    for must in ("bad", "nx", "cur", "jnone", "jc", "jto", "srcloc", "srctext", "lbl", "chg", "exec"):
        if must not in seen:
            problems.append(f"{name}: the walk drew no .{must}: the comparison proves less than it claims")
    # Each sample looks like the real thing (the same rules apply to it).
    pairs = [("#context", "ins bad", "*"), ("#listing", "ins nx", "*"), ("#listing", "ins", "*"), ("#context", "ins cur", "*"),
             ("#listing", "jnone tipmark", "*"), ("#listing", "jc", "*"), ("#listing", "jto", "*"), ("#listing", "lbl", "*"),
             ("#listing", "srcloc", "*"), ("#listing", "srctext", "*"), ("#listing", "hex", "*"), ("#listing", "opn", "*"),
             ("#regs", "chg", "*"), ("#listing", "jarc", "path"), ("#listing", "jarc taken", "path"), ("#listing", "jarrow", "path"),
             ("#context", "src", "*")]
    looks = {}
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        show_tab(page, "page")
        time.sleep(0.4)
        for root, cls, tag in pairs:
            if root == "#context":
                show_tab(page, "debugger")
                time.sleep(0.3)
            elif root != "#regs":
                show_tab(page, "page")
            real = page.evaluate(LEGEND_LOOK_JS, [root, cls, tag])
            sample = page.evaluate(LEGEND_LOOK_JS, ["#about_legend", cls, tag])
            if sample is None:
                problems.append(f"{name}: no legend sample <{tag} class=\"{cls}\">")
            elif real is not None and real != sample:
                problems.append(f"{name} {scheme}: the legend's .{cls.replace(' ', '.')} looks different from the one in {root}: {sample} vs {real}")
            looks[f"{scheme} {cls}"] = real is not None
    r["samples compared with a drawn one"] = sum(looks.values())
    page.emulate_media(color_scheme="light")
    # Generated swatches: memory regions and sections, Graph subsystems.
    show_tab(page, "about")
    sw = page.evaluate(LEGEND_SWATCHES_JS)
    mem = {k: c for k, c, _ in sw["mem"]}
    for k in sw["regions"] + sw["sections"]:
        if k not in mem:
            problems.append(f"{name}: the legend has no memory swatch for {k}")
        elif mem[k] != sw["tok"][k]:
            problems.append(f"{name}: the legend's {k} swatch is {mem[k]}, the token {sw['tok'][k]}")
    graph = {k: c for k, c, _ in sw["graph"]}
    for k, v in sw["palette"]:
        if graph.get(k) != css_rgb(v):
            problems.append(f"{name}: the legend's Graph swatch for {k} is {graph.get(k)}, the Graph draws {v}")
    # The Legend links of Code at EIP and the Page listing.
    for tab in ("debugger", "page"):
        show_tab(page, tab)
        time.sleep(0.3)
        link = page.locator(f"#panel_{tab} .legendlink")
        if not link.count():
            problems.append(f"{name}: the {tab} tab has no Legend link")
            continue
        link.first.click()
        ok = wait_for(lambda: page.get_attribute("#tab_about", "aria-selected") == "true", 5)
        top = page.evaluate("document.getElementById('about_legend').getBoundingClientRect().top - "
                            "(getComputedStyle(document.querySelector('header.top')).position === 'sticky' ? "
                            "document.querySelector('header.top').getBoundingClientRect().bottom : 0)")
        if not ok or not (0 <= top < 400):
            problems.append(f"{name}: the {tab} tab's Legend link does not show the legend (About selected {ok}, top {top})")
        page.click("#nav_back")
        if not wait_for(lambda: page.get_attribute(f"#tab_{tab}", "aria-selected") == "true", 5):
            problems.append(f"{name}: Back after the {tab} tab's Legend link does not return to it")
    show_tab(page, "about")
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000 if width > 400 else 844})
        show_tab(page, "debugger")
        page.click("#panel_debugger .legendlink")
        time.sleep(0.3)
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            time.sleep(0.3)
            page.screenshot(path=str(shots / f"legend-{width}-{scheme}.png"), full_page=width < 400)
        if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
            problems.append(f"{name}: horizontal scroll at {width} px in the About tab")
    page.emulate_media(color_scheme="light")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


def stub_shift(name):
    """Where the bzImage puts vmlinux's first bytes, relative to their link
    address, read from the files (the oracle for the page's "not in place
    yet"): 0 when the payload is vmlinux itself."""
    e = kernel_entries()[name]
    img = (SITE / e["path"]).read_bytes()
    payload = img[(img[0x1f1] or 4) * 512 + 512:]
    insns = json.loads((SITE / e["disasm"]).read_text())["insns"]
    lo = insns[0][0]
    want = b""
    for pa, raw, *_ in insns:
        if pa != lo + len(want):
            break
        want += bytes.fromhex(raw)
        if len(want) >= 48:
            break
    at = payload.find(want)
    return None if at < 0 else at - (lo - 0x100000)


# The yellow lines drawn in a box: address, its tip, the build's bytes, and
# what guest memory holds there and at pa + shift.
YELLOW_LINES_JS = """([sel, shift]) => [...document.querySelectorAll(sel + ' .ins.bad')].map((e) => {
  const pa = Number(e.dataset.addr), ins = disasm.insns[insnIndex(pa)], n = ins[1].length / 2;
  const b = e.querySelector('.difb');
  return { pa, tip: e.dataset.tip || '', btip: b ? b.dataset.tip || '' : null, mark: b ? b.matches('.tipmark[tabindex="0"]') : false, raw: ins[1],
    got: [...emulator.read_memory(pa, n)].map((x) => x.toString(16).padStart(2, '0')).join(''),
    moved: [...emulator.read_memory(pa + shift, n)].map((x) => x.toString(16).padStart(2, '0')).join('') }; })"""


def yellow_check(lines, shift, where, problems, bp=None):
    """Each yellow line's tip names the case the bytes show (oracle: the
    files and guest memory, read here independently of the page's code)."""
    kinds = {}
    start = len(problems)
    for x in lines:
        if len(problems) - start >= 6:
            problems.append(f"{where}: more yellow lines not checked after these")
            break
        tip = x["tip"]
        if x["btip"] != tip or not x["mark"]:
            problems.append(f"{where} {x['pa']:#x}: the yellow bytes are not a focusable mark with the line's text ({x['btip']!r})")
        if not tip.startswith("Yellow: "):
            problems.append(f"{where} {x['pa']:#x}: the yellow line's tip does not start by saying what yellow means: {tip[:80]!r}")
            continue
        if x["got"] == x["raw"]:
            problems.append(f"{where} {x['pa']:#x}: yellow but guest memory holds the build's bytes")
        if bp is not None and x["pa"] == bp:
            kind = "breakpoint"
            ok = "Run to" in tip and "HLT" in tip
        elif shift and x["moved"] == x["raw"]:
            kind = "stub" if x["pa"] < 0x100000 + shift else "not in place"
            ok = "relocation stub" in tip and (kind == "stub" or "not in place yet" in tip) and f"{shift:#x}" in tip
        elif set(x["got"]) == {"0"}:
            kind = "zero"
            ok = "zero" in tip
        else:
            kind = "changed"
            ok = "changed at run time" in tip
        kinds[kind] = kinds.get(kind, 0) + 1
        if not ok:
            problems.append(f"{where} {x['pa']:#x}: a {kind} line, its tip says: {tip!r}")
    return kinds


def yellow_case(browser, base, shots, name):
    """L1: why lines are yellow. Paused at the kernel entry of the minified
    Linux, vmlinux is not in place yet (the bzImage's relocation stub sits
    at 0x100000 and moves it down): Code at EIP and the Page listing are
    yellow and each yellow line says so (oracle: the shift found in the
    bzImage file, the build's bytes there in guest memory). Its popover:
    hover at 1280, Enter, tap at 390 px. Once the stub has run nothing is
    yellow. During a fast Run to, the target's HLT breakpoint bytes are
    yellow and say so; after it ends they are back. In slow motion over
    the stub (64 MiB: a memory scan every 2 s) the bytes it moved since the
    last scan are marked on lines that are the build's again, and say so."""
    problems, r = [], {}
    errors = []
    shift = stub_shift(name)
    r["stub shift"] = shift
    if not shift:
        return [f"{name}: no relocation stub found in the bzImage (shift {shift}): the case proves nothing"], r
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    # 64 MiB: the page scans memory every 2 s while running (every frame
    # below 4 MiB), so changed bytes stay marked long enough to see.
    page.goto(f"{base}/index.html?kernel={name}&paused=1&mem=67108864#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: page.evaluate("!!disasm && !!symbols && !!stepper"), 60)
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        page.close()
        return [f"{name}: Run to 0x100000 did not stop there: {status}"], r
    page.evaluate("renderContext()")
    lines = page.evaluate(YELLOW_LINES_JS, ["#context", shift])
    r["Code at EIP yellow at the entry"] = yellow_check(lines, shift, f"{name} Code at EIP", problems)
    if not lines:
        problems.append(f"{name}: no yellow line in Code at EIP at the entry")
    elif not page.locator("#context .ins.bad .difb").count():
        problems.append(f"{name}: the yellow lines of Code at EIP have no mark to hover or tap")
    else:
        b = page.locator("#context .ins.bad .difb").first
        b.hover()
        st = tip_wait(page, True)
        if not st["shown"] or not st["text"].startswith("Yellow: "):
            problems.append(f"{name}: hovering a yellow line's bytes shows no explanation: {st['text'][:80]!r}")
        r["entry popover"] = st["text"]
        tip_fits(st, f"{name} 1280 yellow", problems)
        page.mouse.move(5, 990)
        tip_wait(page, False)
        page.locator("#context .ins.bad:not(.cur)").first.hover(position={"x": 8, "y": 4})
        if not tip_wait(page, True)["shown"]:
            problems.append(f"{name}: hovering a yellow line (not its bytes) shows no explanation")
        page.mouse.move(5, 990)
        tip_wait(page, False)
        b.focus()
        page.keyboard.press("Enter")
        if not tip_wait(page, True)["shown"]:
            problems.append(f"{name}: Enter on a yellow line's bytes shows no explanation")
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            time.sleep(0.3)
            page.screenshot(path=str(shots / f"yellow-{name}-context-1280-{scheme}.png"))
        page.emulate_media(color_scheme="light")
        page.keyboard.press("Escape")
    # The Page listing at the entry.
    page.evaluate("jumpTo(0x100000)")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(0x100), 20)
    time.sleep(0.5)
    lines = page.evaluate(YELLOW_LINES_JS, ["#listing", shift])
    r["Page listing yellow at the entry"] = yellow_check(lines, shift, f"{name} Page", problems)
    if len(lines) < 100:
        problems.append(f"{name}: only {len(lines)} yellow lines in the Page listing at 0x100000 before the stub ran")
    # 390 px touch: a tap on the yellow bytes.
    url = page.url
    phone = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True)
    pp = phone.new_page()
    pp.on("pageerror", lambda e: errors.append(str(e)))
    pp.goto(url.split("#")[0] + "#debugger")
    wait_for(lambda: pp.get_attribute("#dbg_state", "data-state") == "paused", 120)
    wait_for(lambda: pp.evaluate("!!disasm && !!symbols && !!stepper"), 60)
    pp.fill("#dbg_addr", "0x100000")
    pp.click("#dbg_runto")
    run_to_wait(pp, {0x100000}, 300)
    pp.evaluate("jumpTo(0x100000)")
    wait_for(lambda: pp.get_attribute("#listing", "data-page") == str(0x100), 20)
    time.sleep(0.5)
    # the listing is drawn again once it has its width: wait for the draws to settle
    for _ in range(20):
        d0 = pp.get_attribute("#listing", "data-draws")
        time.sleep(0.5)
        if pp.get_attribute("#listing", "data-draws") == d0:
            break
    b = pp.locator("#listing .ins.bad .difb").nth(3)
    if b.count():
        pp.evaluate("document.querySelectorAll('#listing .ins.bad .difb')[3].scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        box = b.bounding_box()
        before = pp.evaluate(TIP_LAYOUT_JS, "#listing")
        pp.touchscreen.tap(box["x"] + box["width"] / 2, box["y"] + box["height"] / 2)
        st = tip_wait(pp, True)
        if not st["shown"] or not st["text"].startswith("Yellow: "):
            problems.append(f"{name}: a tap on yellow bytes at 390 px shows no explanation: {st['text'][:80]!r}")
        tip_fits(st, f"{name} 390 yellow", problems)
        if pp.get_attribute("#tab_page", "aria-selected") != "true":
            problems.append(f"{name}: the tap on yellow bytes at 390 px left the Page tab")
        for scheme in ("light", "dark"):
            pp.emulate_media(color_scheme=scheme)
            time.sleep(0.3)
            pp.screenshot(path=str(shots / f"yellow-{name}-page-390-{scheme}.png"))
        if pp.evaluate(TIP_LAYOUT_JS, "#listing") != before:
            problems.append(f"{name}: the yellow popover moved the listing's lines at 390 px")
        tip_tap_away(pp)
        if tip_wait(pp, False)["shown"]:
            problems.append(f"{name}: a tap elsewhere does not close the yellow popover")
        if pp.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
            problems.append(f"{name}: horizontal scroll at 390 px")
    else:
        problems.append(f"{name}: no yellow bytes in the Page listing at 390 px")
    phone.close()
    # Bytes changed since the last memory scan. The page scans memory at
    # every stop and every few frames while the CPU runs (every 2 s with 64
    # MiB), and while stepping the Page tab follows EIP. In slow motion
    # from the entry the stub moves vmlinux down within a few dozen steps:
    # the listing of 0x100000 then shows lines that are the build's again
    # with the bytes moved since the last scan marked, and they say so.
    show_tab(page, "page")
    page.select_option("#dbg_speed", "1000")
    page.click("#dbg_continue")
    got = None
    end = time.monotonic() + 30
    while got is None and time.monotonic() < end:
        got = page.evaluate("""() => { const e = document.querySelector('#listing .ins:not(.bad) .difb, #listing .hex[data-tip]');
          return e ? { tip: e.dataset.tip || '', line: e.closest('[data-addr]').className, marks: e.querySelectorAll('mark').length } : null; }""")
        time.sleep(0.05)
    if page.is_enabled("#dbg_pause"):
        page.click("#dbg_pause")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    r["bytes changed since the scan"] = got
    if not got:
        problems.append(f"{name}: slow motion over the relocation stub at 64 MiB: no line of the Page listing marked bytes changed since the last scan")
    elif not got["tip"].startswith("Yellow: ") or "changed since the last memory scan" not in got["tip"] or not got["marks"]:
        problems.append(f"{name}: bytes changed since the last scan do not say why they are yellow: {got}")
    # After the stub: nothing yellow.
    show_tab(page, "debugger")
    s = page.evaluate("symbols.symbols.find((s) => s.name === 'start_kernel')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    run_to_wait(page, {s["paddr"]}, 300)
    page.evaluate("renderContext()")
    n_ctx = page.locator("#context .ins.bad").count()
    page.evaluate("jumpTo(0x100000)")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(0x100), 20)
    page.click("#reread")
    time.sleep(0.5)
    n_page = page.locator("#listing .ins.bad").count()
    r["yellow after the stub (Code at EIP, Page)"] = [n_ctx, n_page]
    if n_ctx or n_page:
        problems.append(f"{name}: after the stub ran, {n_ctx} yellow lines in Code at EIP and {n_page} in the Page listing")
    # A fast Run to an instruction that never runs, in the entry's code
    # section: its HLT breakpoints are in memory while the JIT runs (here
    # well under a second, until the kernel halts). Re-read is pressed
    # between two JIT slices once they are armed (as a user would during a
    # long run), then the line must say it is a Run to breakpoint.
    show_tab(page, "debugger")
    tgt = page.evaluate("""() => { const [lo, hi] = codeRange(ffTarget().phys), S = jumpsFor(disasm.insns);
      const x = disasm.insns.find(([a]) => a >= lo + 0x1000 && a < hi && jumpPaths(S, stepper, a).noDirect && !stepper.executed(a));
      return x ? x[0] : null; }""")
    if tgt is None:
        problems.append(f"{name}: no instruction for the Run to breakpoint check")
    else:
        page.evaluate(f"jumpTo({tgt})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(tgt // 4096), 20)
        show_tab(page, "debugger")
        page.evaluate("""(t) => { const orig = jitChunk; window._bpSeen = null;
          jitChunk = function (...a) { if (!window._bpSeen && emulator.read_memory(t, 1)[0] === 0xf4) {
              showTab('page'); document.getElementById('reread').click();
              const e = document.querySelector('#listing .ins[data-addr="' + t + '"]');
              window._bpSeen = { cls: e ? e.className : '', tip: e ? e.dataset.tip || '' : '', lines: [] }; }
            return orig.apply(this, a); }; window._bpOrig = orig; }""", tgt)
        page.fill("#dbg_addr", hex(tgt + page.evaluate("symbols.symbols[0].vaddr - symbols.symbols[0].paddr")))
        page.click("#dbg_runto")
        wait_for(lambda: page.evaluate("!ffActive && !!window._bpSeen"), 120)
        page.evaluate("void (jitChunk = window._bpOrig)")
        seen = page.evaluate("window._bpSeen")
        r["during Run to"] = seen
        if not seen or "bad" not in seen["cls"].split():
            problems.append(f"{name}: while the fast Run to {tgt:#x} had its breakpoints in, its line was not yellow: {seen}")
        elif not seen["tip"].startswith("Yellow: ") or "Run to" not in seen["tip"] or "HLT" not in seen["tip"]:
            problems.append(f"{name}: the breakpoint line's tip does not say it is a Run to breakpoint: {seen['tip']!r}")
        show_tab(page, "page")
        page.click("#reread")
        time.sleep(0.3)
        if page.locator(f"#listing .ins.bad[data-addr=\"{tgt}\"]").count():
            problems.append(f"{name}: after the Run to ended the target {tgt:#x} is still yellow")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_legend(ctx):
    failed = False
    t0 = time.monotonic()
    p, r = legend_case(ctx.browser, ctx.base, ctx.shots)
    failed |= report("legend", p, r, seconds=round(time.monotonic() - t0))
    for name in [n for n in ("minified-linux", "minified-linux-os") if n in ctx.entries and "disasm" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = yellow_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("yellow", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed


def suite_jumps(ctx):
    failed = False
    for name in [n for n in JUMP_WALKS if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = jumps_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("jumps", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = jumps_live_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("jumps_live", p, r, seconds=round(time.monotonic() - t0))


def suite_unoptimized(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "minified-linux-os") if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = unoptimized_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("unoptimized", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed


def suite_hello(ctx):
    failed = False
    for name in [n for n in ("hello", "hello-os") if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = hello_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("hello", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed


def suite_firstlook(ctx):
    failed = False
    for name in [n for n in FIRSTLOOK_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = firstlook_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("firstlook", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed | report("firstlook_import", *firstlook_import_case(ctx.browser, ctx.base, ctx.shots))


def suite_srclinks(ctx):
    return report("srclinks", *srclinks_case(ctx.browser, ctx.base, ctx.shots))


def suite_touch(ctx):
    failed = report("touch", touch_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("stale_cache", stale_cache_case(ctx.browser, ctx.args.timeout))


# ---- jump history: Back and Forward ----------------------------------------

# What a jump changes and Back must restore: the tab, the hash, the Page
# listing's page, scroll and marked instruction, the Source panel (open or
# closed, file, picked line, title, scroll) and the window's scroll.
NAV_VIEW_JS = """() => {
  const tab = [...document.querySelectorAll('.tabbar [role=tab]')].find((t) => t.getAttribute('aria-selected') === 'true').dataset.tab;
  const l = document.getElementById('listing'), sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
  const hit = l.querySelector('.jhit'), pick = sb.querySelector('.pick');
  return { tab, hash: location.hash, page: tab === 'page' ? l.dataset.page : null, top: tab === 'page' ? Math.round(l.scrollTop) : 0,
           hit: tab === 'page' && hit ? hit.dataset.addr : null, y: Math.round(window.scrollY),
           src: sp.hidden ? null : { file: sb.dataset.file, pick: pick ? pick.dataset.line : null,
                                     title: document.getElementById('src_title').textContent, top: Math.round(sb.scrollTop) } };
}"""


def nav_view(page):
    return page.evaluate(NAV_VIEW_JS)


def nav_diff(got, want):
    """What differs between two views (scrolls within 2 px), or None."""
    out = []
    for k in ("tab", "hash", "page", "hit", "top", "y"):
        if k in ("top", "y") and abs(got[k] - want[k]) <= 2:
            continue
        if got[k] != want[k]:
            out.append(f"{k} {got[k]!r} != {want[k]!r}")
    gs, ws = got["src"], want["src"]
    if (gs is None) != (ws is None):
        out.append(f"Source panel {'open' if gs else 'closed'}, want {'open' if ws else 'closed'}")
    elif gs:
        for k in ("file", "pick", "title"):
            if gs[k] != ws[k]:
                out.append(f"Source panel {k} {gs[k]!r} != {ws[k]!r}")
        if abs(gs["top"] - ws["top"]) > 2:
            out.append(f"Source panel scroll {gs['top']} != {ws['top']}")
    return "; ".join(out) or None


def nav_at(page):
    """(the entry shown, entries); (0, 0) on a page without the history."""
    return tuple(page.evaluate("""() => { const e = document.getElementById('nav_hist');
        return e ? [Number(e.dataset.at || 0), Number(e.dataset.count || 0)] : [0, 0]; }"""))


def nav_settle(page):
    """A jump's listing is drawn again on the next frame (its width): wait
    for that, a little longer than NAV_SCROLL_MS."""
    time.sleep(1.3)


def nav_first(page, sel):
    loc = page.locator(sel)
    for i in range(min(loc.count(), 40)):
        if loc.nth(i).is_visible() and loc.nth(i).is_enabled():
            return loc.nth(i)
    return None


def nav_watch(page):
    """Record the view at every pointerdown (window.mkbView): the view a
    click leaves, after any scrolling Playwright did to reach the target
    (it scrolls again when the sticky top bar covers it)."""
    page.evaluate("addEventListener('pointerdown', () => { window.mkbView = (%s)(); }, true)" % NAV_VIEW_JS)


def nav_jump(page, views, kinds, kind, prep, problems, act="click"):
    """One jump: prep() gets to it the way a user does (another tab, a
    list opened) and returns the locator to click, or a function that
    clicks. The view the click left (nav_watch) is what Back must restore
    (views[-1] is replaced with it: tab switches and scrolls are no
    entries); the view after it is appended."""
    before = nav_at(page)
    try:
        target = prep()
        if target is None:
            problems.append(f"{kind}: nothing to click")
            return
        if callable(target):
            page.evaluate("window.mkbView = null")
            target(None)
            view = page.evaluate("window.mkbView")
        else:
            target.scroll_into_view_if_needed()
            page.evaluate("window.mkbView = null")
            target.tap() if act == "tap" else target.click()
            view = page.evaluate("window.mkbView")
        views[-1] = view
    except PlaywrightError as e:
        problems.append(f"{kind}: {str(e).splitlines()[0]}")
        return
    if not wait_for(lambda: nav_at(page)[0] == before[0] + 1, 10):
        problems.append(f"{kind}: no history entry (at, count {before} -> {nav_at(page)})")
        return
    nav_settle(page)
    if nav_at(page)[1] != nav_at(page)[0] + 1:
        problems.append(f"{kind}: forward entries kept after a new jump: {nav_at(page)}")
    views.append(nav_view(page))
    kinds.append(kind)


def nav_jumps(page, name, problems, r):
    """One jump of every kind; returns the views [before the first jump,
    before the second, ..., after the last] and the kinds."""
    views, kinds, probs = [nav_view(page)], [], []

    def on(tab, sel):
        def prep():
            show_tab(page, tab)
            return nav_first(page, sel)
        return prep

    def go_there():
        show_tab(page, "page")
        b = nav_first(page, "#listing button.jto")
        if b:
            return b
        c = nav_first(page, "#listing button.jc")
        if not c:
            return None
        c.scroll_into_view_if_needed()
        c.click()
        wait_for(lambda: page.is_visible("#jump_pop"), 5)
        return nav_first(page, "#jump_pop button.jgo")

    def grid():
        def click(_):
            x, y = page_center(page, 0x100)
            page.mouse.click(x, y)
        return click

    def coverage(kind):
        def prep():
            show_tab(page, "source")
            wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 30)
            cov_open_all(page)
            return nav_first(page, f'#cov_tree .covrow[data-kind="{kind}"] .covname')
        return prep

    def graph():
        show_tab(page, "calls")
        page.click("#gview_tree")
        wait_for(lambda: page.locator("#graph_tree .tname").count() > 1, 20)
        page.evaluate("document.querySelectorAll('#graph_tree .ttog').forEach((b) => b.textContent === '\u25b8' && b.click())")
        loc = page.locator("#graph_tree .tname")
        return loc.last if loc.count() else None

    steps = [
        ("Debugger symbol", on("debugger", "#sym_info button.symlink")),
        ("branch target name", on("debugger", "#context .brt")),
        ("jump source (go there)", go_there),
        ("listing file:line", on("page", "#listing .src .srclink")),
        ("Source panel address", lambda: nav_first(page, "#src_addrs button.symlink")
         if wait_for(lambda: page.locator("#src_addrs button.symlink").count() > 0, 30) else None),
        ("symbol index", lambda: page.locator("#symbol_index button").last if page.locator("#symbol_index button").count() else None),
        ("next page", on("page", "#page_next")),
        ("Memory grid page", grid),
        ("call stack frame", on("debugger", "#call_stack button.symlink")),
        ("Code at EIP line", on("debugger", "#context .ins[data-addr]")),
        ("Debugger file:line", on("debugger", "#cur_src .srclink")),
        ("Coverage function", coverage("func")),
        ("Coverage file", coverage("file")),
        ("Graph call tree", graph),
    ]
    for kind, prep in steps:
        nav_jump(page, views, kinds, kind, prep, probs)
    problems.extend(f"{name} {p}" for p in probs)
    r["jumps"] = kinds
    return views, kinds


# The text of the listing lines visible on screen (inside their scroll box
# and the window, not under the Source panel) that the Back and Forward
# box covers, and the Source panel's own header and text under it.
NAV_COVER_JS = """() => {
    const nav = document.getElementById('nav_hist');
    if (nav.hidden) return { hidden: true, hits: [] };
    const b = nav.getBoundingClientRect(), sp = document.getElementById('src_panel');
    const p = sp.hidden ? null : sp.getBoundingClientRect();
    const cut = (r, c) => ({ left: Math.max(r.left, c.left), top: Math.max(r.top, c.top),
                            right: Math.min(r.right, c.right), bottom: Math.min(r.bottom, c.bottom) });
    const meets = (r) => r.right - r.left > 0.5 && r.bottom - r.top > 0.5
        && Math.min(r.right, b.right) - Math.max(r.left, b.left) > 0.5
        && Math.min(r.bottom, b.bottom) - Math.max(r.top, b.top) > 0.5;
    const hits = [], view = { left: 0, top: 0, right: innerWidth, bottom: innerHeight };
    let seen = 0;
    for (const id of ['listing', 'context']) {
        const box = document.getElementById(id);
        if (!box || !box.offsetParent) continue;
        const br = box.getBoundingClientRect();
        let clip = cut({ left: br.left + box.clientLeft, top: br.top + box.clientTop,
                         right: br.left + box.clientLeft + box.clientWidth, bottom: br.top + box.clientTop + box.clientHeight }, view);
        if (p) clip = cut(clip, { left: -1e9, top: -1e9, right: p.left > 1 ? p.left : 1e9, bottom: p.left > 1 ? 1e9 : p.top });
        for (const el of box.querySelectorAll('.ins, .src')) {
            const rg = document.createRange();
            rg.selectNodeContents(el);
            for (const r of rg.getClientRects()) {
                const v = cut(r, clip);
                if (v.right - v.left > 0.5 && v.bottom - v.top > 0.5) seen++;
                if (meets(v)) {
                    hits.push(id + ': ' + el.textContent.trim().slice(0, 50));
                    break;
                }
            }
        }
    }
    if (p) {
        for (const id of ['src_title', 'src_close', 'src_info', 'src_body']) {
            const el = document.getElementById(id);
            if (!el || !el.offsetParent) continue;
            const rg = document.createRange();
            rg.selectNodeContents(el);
            const rs = id === 'src_close' ? [el.getBoundingClientRect()] : Array.from(rg.getClientRects());
            const br = el.getBoundingClientRect();
            if (rs.some(r => meets(cut(r, cut(br, view))))) hits.push('Source panel #' + id);
        }
    }
    return { nav: [b.left, b.top, b.right, b.bottom], panel: p && [p.left, p.top, p.right, p.bottom], seen, hits: hits.slice(0, 6) };
}"""


def nav_cover(page, where, problems, r, need_lines=False):
    """Light and dark: the Back and Forward box covers no listing text
    on screen (N2); need_lines: some listing line must be on screen."""
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        got = page.evaluate(NAV_COVER_JS)
        r.setdefault("covers", {})[f"{where} {scheme}"] = got
        if need_lines and not got.get("seen"):
            problems.append(f"{where} {scheme}: no listing line on screen to check")
        if got["hits"]:
            problems.append(f"{where} {scheme}: the Back and Forward box covers {got['hits']} (box {got.get('nav')}, panel {got.get('panel')})")
    page.emulate_media(color_scheme="light")


def navhist_case(browser, base, shots, name="hello"):
    """Jump history (N1). Paused in hello_main, one jump of each kind
    (a Debugger symbol, a jump source's "go there", file:line links in the
    listing and the Debugger, a Source panel address, the symbol index,
    the next page, a branch target, a page of the Memory grid, a call stack
    frame, a Code at EIP line, a Coverage function and file, a Graph call
    tree node) adds one entry each; Back through all of them (the button,
    Enter on it, Alt+Left, the browser's back) restores each earlier view
    exactly, Forward (button, Alt+Right, browser) each later one; the
    tooltips name the target; stepping adds nothing; Alt+Left after a
    click on the VM screen is the VM's; back past the first view stays on
    the page; the VM keeps running through browser back and forward; the
    buttons stay on screen while scrolling. Then a 390 px phone: tap, the
    buttons above the Source panel, covering no listing text on screen
    with the panel open or closed (N2). Screenshots light/dark at 1280 and
    390 px."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    fn = syms["hello_main"]
    errors = []
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(fn["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {fn["paddr"]}, 120)
    if not ok:
        page.close()
        return [f"{name}: Run to hello_main did not stop there: {status}"], r
    # A few steps in: a call stack and a branch or two recorded.
    page.fill("#dbg_n", "40")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    time.sleep(1)
    if page.locator("#nav_hist").count() != 1:
        page.close()
        return [f"{name}: no Back and Forward buttons (#nav_hist)"], r
    if page.is_visible("#nav_hist"):
        problems.append(f"{name}: the Back button shows before any jump (Run to and stepping are no jumps)")
    hist0 = page.evaluate("history.length")
    nav_watch(page)
    views, kinds = nav_jumps(page, name, problems, r)
    n = len(views) - 1
    r["entries"] = nav_at(page)
    if n < 12:
        problems.append(f"{name}: only {n} jumps made: {kinds}")
    r["history.length"] = [hist0, page.evaluate("history.length")]

    # Fixed while the window scrolls, clear of the tab bar and the VM screen.
    page.evaluate("window.scrollTo(0, document.documentElement.scrollHeight)")
    time.sleep(0.3)
    pos = page.evaluate("""() => { const b = document.getElementById('nav_hist').getBoundingClientRect();
        return { pos: getComputedStyle(document.getElementById('nav_hist')).position, top: b.top, bottom: b.bottom, left: b.left, right: b.right,
                 vh: innerHeight, vw: innerWidth }; }""")
    r["buttons"] = pos
    if pos["pos"] != "fixed" or not (0 <= pos["top"] and pos["bottom"] <= pos["vh"] and pos["left"] >= 0 and pos["right"] <= pos["vw"]):
        problems.append(f"{name}: the Back button is not fixed on screen while scrolled to the end: {pos}")
    page.evaluate(f"window.scrollTo(0, {views[-1]['y']})")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"navhist-{name}-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")

    # Back through every jump, by every means; each earlier view exactly.
    ways = ["button", "enter", "alt", "browser"]
    titles = []
    for k in range(n, 0, -1):
        title = page.get_attribute("#nav_back", "title") or ""
        label = page.evaluate("navList[navAt - 1].label")
        if not title.startswith("Back to ") or label not in title:
            problems.append(f"{name}: Back's tooltip {title!r} does not name the view it returns to ({label!r})")
        titles.append(title)
        way = ways[k % len(ways)]
        if way == "button":
            page.click("#nav_back")
        elif way == "enter":
            page.focus("#nav_back")
            page.keyboard.press("Enter")
        elif way == "alt":
            page.focus("#nav_back")  # the focus is on the page, not the VM screen
            page.keyboard.press("Alt+ArrowLeft")
        else:
            page.go_back()
        if not wait_for(lambda: nav_at(page)[0] == k - 1, 10):
            problems.append(f"{name}: Back ({way}) from {kinds[k - 1]} did not go back: {nav_at(page)}")
            break
        nav_settle(page)
        d = nav_diff(nav_view(page), views[k - 1])
        if d:
            problems.append(f"{name}: Back ({way}) from {kinds[k - 1]}: {d}")

    if page.is_enabled("#nav_back"):
        problems.append(f"{name}: Back is enabled at the first view")
    r["tooltips"] = titles[:4]
    if not any(re.match(r"Back to \w+\+0x[0-9a-f]+(, |\s)", t) for t in titles):
        problems.append(f"{name}: no Back tooltip names an instruction as symbol+offset: {titles}")
    page.screenshot(path=str(shots / f"navhist-{name}-first-1280.png"))
    for k in range(1, n + 1):
        way = ["button", "alt", "browser"][k % 3]
        if way == "button":
            page.click("#nav_fwd")
        elif way == "alt":
            page.focus("#nav_fwd")
            page.keyboard.press("Alt+ArrowRight")
        else:
            page.go_forward()
        if not wait_for(lambda: nav_at(page)[0] == k, 10):
            problems.append(f"{name}: Forward ({way}) to {kinds[k - 1]} did not go forward: {nav_at(page)}")
            break
        nav_settle(page)
        d = nav_diff(nav_view(page), views[k])
        if d:
            problems.append(f"{name}: Forward ({way}) to {kinds[k - 1]}: {d}")

    # Stepping follows EIP (the Page tab moves with it) without new entries.
    show_tab(page, "page")
    before, hl = nav_at(page), page.evaluate("history.length")
    p0 = page.get_attribute("#listing", "data-page")
    for _ in range(12):
        page.click("#dbg_step")
        time.sleep(0.2)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    time.sleep(0.5)
    r["stepping"] = f"page {p0} -> {page.get_attribute('#listing', 'data-page')}, {page.evaluate('stepper.steps')} steps"
    if nav_at(page) != before or page.evaluate("history.length") != hl:
        problems.append(f"{name}: stepping added history entries: {before} -> {nav_at(page)}, history.length {hl} -> {page.evaluate('history.length')}")

    # Alt+Left after a click on the VM screen is the VM's key.
    page.click("#screen_container")
    page.keyboard.press("Alt+ArrowLeft")
    time.sleep(0.5)
    if nav_at(page) != before:
        problems.append(f"{name}: Alt+Left went back with the VM screen focused: {before} -> {nav_at(page)}")

    # The VM runs on through the browser's back and forward, and back past
    # the first view stays on the page.
    page.evaluate("window.mkbEmu = emulator")
    page.select_option("#dbg_speed", "10")  # hello_main ends in a few hundred instructions
    page.click("#dbg_continue")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "stepped", 10)
    r["running"] = page.evaluate("({ state: runState, steps: stepper.steps })")
    url = page.url.split("#")[0]
    for _ in range(n + 1):
        page.go_back()
        time.sleep(0.3)
    s0 = page.evaluate("stepper.steps")
    time.sleep(2)
    got = page.evaluate("({ same: window.mkbEmu === emulator, steps: stepper.steps, state: runState, url: location.href })")
    r["after back past the start"] = got
    if not got["url"].startswith(url) or not got["same"] or got["steps"] <= s0 or got["state"] != "stepped" or nav_at(page)[0] != 0:
        problems.append(f"{name}: back past the first view: {got}, at {nav_at(page)} (want the page, the same VM running, at 0)")
    page.go_forward()
    page.go_forward()
    time.sleep(0.5)
    if not page.evaluate("window.mkbEmu === emulator && runState === 'stepped'") or nav_at(page)[0] != 1:
        problems.append(f"{name}: browser forward: at {nav_at(page)}, VM {page.evaluate('runState')}")
    if page.is_enabled("#dbg_pause"):
        page.click("#dbg_pause")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    page.close()

    # A 390 px phone: tap jumps and Back; with the Source panel open the
    # buttons sit above it.
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", "hello_main")
    page.tap("#dbg_runto")
    ok, at, status = run_to_wait(page, {fn["paddr"]}, 120)
    if not ok:
        ctx.close()
        return problems + [f"{name} 390 px: Run to hello_main did not stop there: {status}"], r
    time.sleep(1)
    nav_watch(page)
    views, kinds, probs = [nav_view(page)], [], []
    nav_jump(page, views, kinds, "Debugger symbol", lambda: nav_first(page, "#sym_info button.symlink"), probs, "tap")
    nav_cover(page, f"{name} 390 px after a jump, Source panel closed", probs, r, need_lines=True)
    nav_jump(page, views, kinds, "listing file:line", lambda: nav_first(page, "#listing .src .srclink"), probs, "tap")
    problems.extend(f"{name} 390 px {p}" for p in probs)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    time.sleep(0.5)
    nav_cover(page, f"{name} 390 px after a jump, Source panel open", problems, r)
    # The audit's case: the window scrolled so the listing fills the strip
    # above the panel.
    y0 = page.evaluate("window.scrollY")
    page.evaluate("window.scrollBy(0, document.getElementById('listing').getBoundingClientRect().top - 8)")
    time.sleep(0.4)
    nav_cover(page, f"{name} 390 px, the listing in the strip above the Source panel", problems, r, need_lines=True)
    page.screenshot(path=str(shots / f"navhist-{name}-390-strip.png"))
    page.evaluate(f"window.scrollTo(0, {y0})")
    time.sleep(0.4)
    geo = page.evaluate("""() => { const b = document.getElementById('nav_hist').getBoundingClientRect(),
        p = document.getElementById('src_panel').getBoundingClientRect();
        return { nav: [b.left, b.top, b.right, b.bottom], panel: [p.left, p.top, p.right, p.bottom], vw: innerWidth, vh: innerHeight,
                 hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }""")
    r["390 px"] = geo
    if geo["nav"][3] > geo["panel"][1] + 0.5 or geo["nav"][1] < 0 or geo["nav"][2] > geo["vw"] or geo["hscroll"]:
        problems.append(f"{name} 390 px: the buttons overlap the Source panel, leave the screen or scroll it sideways: {geo}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"navhist-{name}-390-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    for k in range(len(kinds) - 1, -1, -1):
        want = views[k]
        page.tap("#nav_back")
        if not wait_for(lambda: nav_at(page)[0] == k, 10):
            problems.append(f"{name} 390 px: tapping Back did not go back to {k}: {nav_at(page)}")
            break
        nav_settle(page)
        d = nav_diff(nav_view(page), want)
        if d:
            problems.append(f"{name} 390 px: tap Back to {k}: {d}")
        nav_cover(page, f"{name} 390 px after Back to {k}", problems, r)
    page.screenshot(path=str(shots / f"navhist-{name}-390-back.png"))
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def suite_navhist(ctx):
    return report("navhist", *navhist_case(ctx.browser, ctx.base, ctx.shots))


# ---- tabs: names, emoji, compact mode, the Machine tab (U3) ---------------
# The operator: "coverage zamień na Source, Graph na Calls. emoji przed
# etykietami. w ustawieniach możliwość zwinięcia do samego emoji.
# zarządzanie symulacją niech będzie kolejną kartą". The tab bar names the
# tabs with an emoji (aria-hidden) and the word (the accessible name); the
# old hashes and ?tab= values still open the renamed tabs; "Compact tabs"
# (Machine tab, Settings) shows the emoji only, remembered across a reload;
# the Machine tab holds the kernel, RAM, file, symbol, initrd, cmdline and
# offset controls and Boot, and the header keeps the screen, the run bar
# and the counters, so it is shorter at 390 px than before (TABS_HEADER_390_BEFORE).

TABS_WORDS = {"machine": "Machine", "memory": "Memory", "page": "Page", "debugger": "Debugger",
              "calls": "Calls", "source": "Source", "snapshots": "Snapshots", "about": "About"}
TABS_ALIASES = {"graph": "calls", "coverage": "source"}
# The header's height at 390 px (hello booted, light) on 8dc5c89c7, before
# the Machine tab took its controls: measured in this suite's setting.
TABS_HEADER_390_BEFORE = 1003
TABS_MACHINE_IDS = ["kernel", "memory", "file", "symfile", "initrd_file", "cmdline", "sym_offset", "boot",
                    "kinfo", "build_info", "sym_status", "initrd_status", "compact_tabs"]
TABS_HEADER_IDS = ["screen_container", "dbg_pause", "dbg_continue", "dbg_step", "dbg_speed", "clock_mips",
                   "count", "counters", "status", "dbg_state"]

# Per tab button: its id, accessible-name inputs, the emoji and the word as drawn.
TABS_LOOK_JS = """() => [...document.querySelectorAll('.tabbar [role=tab]')].map((b) => {
  const emo = b.querySelector('[aria-hidden=true]'); const word = b.querySelector('.tword');
  const r = (word || b).getBoundingClientRect();
  return { id: b.id, label: b.getAttribute('aria-label'), text: b.textContent.trim(), tip: b.dataset.tip || '',
    emoji: emo ? emo.textContent.trim() : '', wordShown: !!word && r.width > 0 && getComputedStyle(word).display !== 'none',
    top: Math.round(b.getBoundingClientRect().top) }; })"""


def tabs_look(page):
    return page.evaluate(TABS_LOOK_JS)


def tabs_where(page, ids):
    """The tab panel (or "header") each element is in."""
    return page.evaluate("""(ids) => Object.fromEntries(ids.map((id) => { const e = document.getElementById(id);
      if (!e) return [id, null]; const p = e.closest('[role=tabpanel]');
      return [id, p ? p.id : e.closest('header.top') ? 'header' : 'elsewhere']; }))""", ids)


def tabs_case(browser, base, shots):
    problems, r = [], {}
    errors = []

    def fresh(scheme="light", phone=False):
        c = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
        pg = c.new_page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        return c, pg

    def booted(pg, name="hello"):
        return wait_for(lambda: f"Running {name} " in (pg.text_content("#status") or "")
                        and ALIVE in pg.inner_text("#screen_container"), 60)

    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello")
    if not booted(page):
        problems.append(f"hello did not boot: {page.text_content('#status')!r}")
    look = tabs_look(page)
    r["labels"] = [(t["emoji"], t["label"] or t["text"]) for t in look]
    want_ids = ["tab_" + k for k in TABS_WORDS]
    if sorted(t["id"] for t in look) != sorted(want_ids):
        problems.append(f"tab ids {[t['id'] for t in look]}, want {want_ids}")
    for t in look:
        word = TABS_WORDS.get(t["id"][4:])
        if not t["emoji"]:
            problems.append(f"{t['id']}: no aria-hidden emoji before the label")
        if word and not t["text"].endswith(word):
            problems.append(f"{t['id']}: label {t['text']!r}, want the emoji then {word!r}")
        if word and page.get_by_role("tab", name=word, exact=True).count() != 1:
            problems.append(f"no tab whose accessible name is {word!r}")
        if not t["wordShown"]:
            problems.append(f"{t['id']}: the word is not shown with compact tabs off")
    if len({t["emoji"] for t in look}) != len(look):
        problems.append(f"two tabs share an emoji: {r['labels']}")
    for old in ("Graph", "Coverage"):
        if page.get_by_role("tab", name=old).count():
            problems.append(f"a tab is still called {old}")
    about = page.text_content("#panel_about")
    for old in ("Graph tab", "Coverage tab", "Graph:", "Coverage:"):
        if old in about:
            problems.append(f"the About text still says {old!r}")

    # Where the controls are.
    where = tabs_where(page, TABS_MACHINE_IDS + TABS_HEADER_IDS)
    r["where"] = where
    for i in TABS_MACHINE_IDS:
        if where.get(i) != "panel_machine":
            problems.append(f"#{i} is in {where.get(i)}, want the Machine tab")
    for i in TABS_HEADER_IDS:
        if where.get(i) != "header":
            problems.append(f"#{i} is in {where.get(i)}, want the header")
    r["header_1280"] = round(page.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))

    # Old hashes and ?tab= open the renamed tabs.
    for old, new in TABS_ALIASES.items():
        for url in (f"{base}/index.html?kernel=hello#{old}", f"{base}/index.html?kernel=hello&tab={old}",
                    f"{base}/index.html?kernel=hello#{new}"):
            page.goto(url)
            ok = wait_for(lambda: page.locator(f"#tab_{new}").count()
                          and page.get_attribute(f"#tab_{new}", "aria-selected") == "true"
                          and page.evaluate("location.hash") == "#" + new, 10)
            if not ok:
                problems.append(f"{url[len(base):]}: the {TABS_WORDS[new]} tab is not shown with #{new} "
                                f"(hash {page.evaluate('location.hash')!r})")
    page.goto(f"{base}/index.html?kernel=hello#machine")
    booted(page)
    if page.get_attribute("#tab_machine", "aria-selected") != "true" if page.locator("#tab_machine").count() else True:
        problems.append("#machine does not open the Machine tab")

    # Booting another kernel from the Machine tab, and Boot again.
    if page.locator("#tab_machine").count():
        show_tab(page, "machine")
        page.select_option("#kernel", "hello-os")
        if not booted(page, "hello-os"):
            problems.append(f"picking hello-os in the Machine tab did not boot it: {page.text_content('#status')!r}")
        page.select_option("#memory", str(4 << 20))
        page.click("#boot")
        if not wait_for(lambda: "4,096 KiB" in (page.text_content("#status") or "") and ALIVE in page.inner_text("#screen_container"), 60):
            problems.append(f"Boot with 4 MB from the Machine tab: {page.text_content('#status')!r}")
        page.screenshot(path=str(shots / "tabs-1280-machine.png"))

    # A bzImage dropped on the page boots from any tab (here: About).
    show_tab(page, "about")
    page.evaluate("""async () => { const b = await (await fetch('kernels/hello/bzImage')).arrayBuffer();
      const dt = new DataTransfer(); dt.items.add(new File([b], 'dropped.bzImage'));
      document.querySelector('#panel_about').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); }""")
    if not booted(page, "dropped.bzImage"):
        problems.append(f"a bzImage dropped on the About tab did not boot: {page.text_content('#status')!r}")

    # Compact tabs: emoji only, the word as tooltip and accessible name, kept across a reload.
    if not page.locator("#compact_tabs").count():
        problems.append("no Compact tabs setting (#compact_tabs)")
    else:
        show_tab(page, "machine")
        if page.is_checked("#compact_tabs"):
            problems.append("Compact tabs is on by default at 1280 px")
        page.check("#compact_tabs")
        for when in ("checked", "after a reload"):
            look = tabs_look(page)
            for t in look:
                word = TABS_WORDS.get(t["id"][4:])
                if t["wordShown"] or t["label"] != word or t["tip"] != word:
                    problems.append(f"compact ({when}) {t['id']}: word shown {t['wordShown']}, aria-label {t['label']!r}, tip {t['tip']!r}")
            if when == "checked":
                page.screenshot(path=str(shots / "tabs-1280-compact.png"))
                r["compact_width_1280"] = page.evaluate("Math.round(document.querySelector('.tabbar').scrollWidth)")
                page.reload()
                wait_for(lambda: page.locator("#compact_tabs").count() and page.is_checked("#compact_tabs"), 10)
        page.hover("#tab_calls")
        if not wait_for(lambda: page.is_visible("#tip_pop") and page.text_content("#tip_pop") == "Calls", 3):
            problems.append(f"hovering the compact Calls tab shows {page.text_content('#tip_pop')!r}")
        page.click("#tab_calls")
        if page.get_attribute("#tab_calls", "aria-selected") != "true":
            problems.append("a click on the compact Calls tab does not open it")
        show_tab(page, "machine")
        page.uncheck("#compact_tabs")
        page.reload()
        wait_for(lambda: page.locator("#compact_tabs").count(), 10)
        if page.is_checked("#compact_tabs") or not all(t["wordShown"] for t in tabs_look(page)):
            problems.append("Compact tabs off does not stay off after a reload")
    c.close()

    # 390 px, light and dark: shorter header, no sideways scroll, compact by default.
    for scheme in ("light", "dark"):
        c, page = fresh(scheme, phone=True)
        page.goto(f"{base}/index.html?kernel=hello")
        booted(page)
        time.sleep(0.5)
        h = round(page.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))
        r[f"header_390_{scheme}"] = h
        if h >= TABS_HEADER_390_BEFORE:
            problems.append(f"390 px {scheme}: header {h} px, not shorter than before ({TABS_HEADER_390_BEFORE} px)")
        look = tabs_look(page)
        rows = len({t["top"] for t in look})
        r[f"rows_390_{scheme}"] = rows
        compact = page.locator("#compact_tabs").count() and page.is_checked("#compact_tabs")
        if not compact or rows != 1:
            problems.append(f"390 px {scheme}: compact {compact}, tab bar rows {rows} (want compact by default, one row)")
        page.screenshot(path=str(shots / f"tabs-390-{scheme}.png"))
        for t in ["machine", "memory", "calls", "source", "about"]:
            if page.locator(f"#tab_{t}").count():
                page.tap(f"#tab_{t}")
                time.sleep(0.3)
                if page.get_attribute(f"#tab_{t}", "aria-selected") != "true":
                    problems.append(f"390 px {scheme}: a tap on {t} does not open it")
                if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                    problems.append(f"390 px {scheme}: horizontal scroll on the {t} tab")
                if t == "machine":
                    page.screenshot(path=str(shots / f"tabs-390-{scheme}-machine.png"), full_page=True)
        if page.locator("#compact_tabs").count():
            page.tap("#tab_machine")
            page.uncheck("#compact_tabs")
            time.sleep(0.3)
            r[f"rows_390_words_{scheme}"] = len({t["top"] for t in tabs_look(page)})
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"390 px {scheme}: horizontal scroll with words")
            page.screenshot(path=str(shots / f"tabs-390-{scheme}-words.png"))
        c.close()
    for scheme in ("light", "dark"):
        c, page = fresh(scheme)
        page.goto(f"{base}/index.html?kernel=hello")
        booted(page)
        if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
            problems.append(f"1280 px {scheme}: horizontal scroll")
        page.screenshot(path=str(shots / f"tabs-1280-{scheme}.png"))
        c.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_tabs(ctx):
    return report("tabs", *tabs_case(ctx.browser, ctx.base, ctx.shots))


# ---- PWA: manifest, service worker, offline, update notice ----------------
# Operator 2026-11-09: "make it a PWA". Locally the case deploys the site
# twice into a temporary directory the way the workflow does (site/stamp.py:
# ?v=<commit> on the assets, the commit in sw.js and version.json) and
# serves it with Pages' caching (max-age=600, index.html no-store). Offline
# means the server answers nothing (and context.set_offline): the page must
# come from the service worker. Against --base (the live site) the offline
# part aborts every request of the context and the second deploy is a
# version.json with another commit.

PWA_V = ("aaaaaaaaaaaa", "bbbbbbbbbbbb")
PWA_PRECACHED_KERNELS = ("hello", "minified-linux")

PWA_CACHES_JS = "async () => (await caches.keys()).sort()"
PWA_SW_VERSION_JS = """async () => {
    const w = navigator.serviceWorker.controller;
    if (!w) return null;
    const ch = new MessageChannel();
    const got = new Promise((res) => { ch.port1.onmessage = (e) => res(e.data); setTimeout(() => res('no answer'), 3000); });
    w.postMessage({ type: 'version' }, [ch.port2]);
    return got; }"""


def pwa_deploy(root, v, marker=None):
    """What the workflow does to site/ before Pages uploads it, into root."""
    for d in ("kernels", "v86", "iced"):
        if not (root / d).exists():
            (root / d).symlink_to(SITE / d)
    files = [f for f in SITE.iterdir() if f.is_file() and f.suffix in (".js", ".css", ".html", ".json", ".webmanifest")
             and f.name != "version.json"]
    for f in files:
        shutil.copy(f, root / f.name)
    if (SITE / "icons").is_dir():
        shutil.rmtree(root / "icons", ignore_errors=True)
        shutil.copytree(SITE / "icons", root / "icons")
    sha = v + "0" * 28
    (root / "version.json").write_text(json.dumps({"sha": sha, "date": "2026-11-09T12:00:00Z", "subject": "deploy " + v,
                                                   "run_id": "1", "run_number": "1", "repo": "d33tah/minimal-kernel-build"}))
    if (SITE / "stamp.py").exists():
        subprocess.run([sys.executable, str(SITE / "stamp.py"), str(root), sha], check=True, stdout=subprocess.DEVNULL)
    else:
        (root / "index.html").write_text(stamp((root / "index.html").read_text(), v))
    if marker:
        with open(root / "app.js", "a") as f:
            f.write(f'\nwindow.deployMarker = "{marker}";\n')


def pwa_server(root):
    """CachingHandler on root; state["down"] makes it answer nothing (offline)."""
    state = {"down": False}

    class Handler(CachingHandler):
        extensions_map = {**CachingHandler.extensions_map, ".webmanifest": "application/manifest+json"}

        def do_GET(self):
            if state["down"]:
                self.close_connection = True
                return
            super().do_GET()

        do_HEAD = do_GET

    return serve(root, Handler), state


def pwa_manifest_check(page, base, problems, r):
    """The manifest is linked, parses, its start_url and scope are the site's
    directory, its icons load at the sizes it says (192, 512, a maskable one)."""
    m = page.evaluate("""async () => {
        const l = document.querySelector('link[rel="manifest"]');
        if (!l) return { error: 'no <link rel="manifest">' };
        const resp = await fetch(l.href);
        let j = null;
        try { j = await resp.json(); } catch (e) { return { error: 'manifest is not JSON: ' + e }; }
        const icons = [];
        for (const i of j.icons || []) {
            const url = new URL(i.src, l.href).href;
            const img = new Image();
            img.src = url;
            try { await img.decode(); } catch (e) { icons.push({ ...i, url, error: String(e) }); continue; }
            icons.push({ ...i, url, w: img.naturalWidth, h: img.naturalHeight });
        }
        const themes = [...document.querySelectorAll('meta[name="theme-color"]')].map((e) => [e.content, e.media]);
        return { href: l.href, j, icons, themes, start: new URL(j.start_url || '', l.href).href,
                 scope: new URL(j.scope || '', l.href).href, type: resp.headers.get('content-type') }; }""")
    if m.get("error"):
        problems.append(m["error"])
        return
    j = m["j"]
    r["manifest"] = {k: j.get(k) for k in ("name", "short_name", "start_url", "scope", "display", "theme_color", "background_color")}
    site = base + "/"
    for k in ("name", "short_name", "description", "theme_color", "background_color"):
        if not j.get(k):
            problems.append(f"manifest has no {k}")
    if j.get("display") != "standalone":
        problems.append(f"manifest display {j.get('display')!r}, want standalone")
    if j.get("start_url") != "./" or m["start"] != site:
        problems.append(f"manifest start_url {j.get('start_url')!r} resolves to {m['start']}, want ./ = {site}")
    if j.get("scope") != "./" or m["scope"] != site:
        problems.append(f"manifest scope {j.get('scope')!r} resolves to {m['scope']}, want ./ = {site}")
    sizes = set()
    for i in m["icons"]:
        if i.get("error"):
            problems.append(f"icon {i['src']} does not load: {i['error']}")
            continue
        want = i.get("sizes", "")
        if want != f"{i['w']}x{i['h']}":
            problems.append(f"icon {i['src']}: sizes {want!r}, the image is {i['w']}x{i['h']}")
        if not i["url"].startswith(site):
            problems.append(f"icon {i['src']} is outside the site: {i['url']}")
        sizes.add((want, "maskable" in i.get("purpose", "any")))
    for want in (("192x192", False), ("512x512", False)):
        if want not in sizes:
            problems.append(f"no {want[0]} icon (purpose any)")
    if not any(mask for _, mask in sizes):
        problems.append("no maskable icon")
    media = sorted(t[1] for t in m["themes"])
    r["theme_color"] = m["themes"]
    if media != ["(prefers-color-scheme: dark)", "(prefers-color-scheme: light)"]:
        problems.append(f"theme-color metas {m['themes']}, want one for light and one for dark")


def pwa_controlled(page, timeout=30):
    return wait_for(lambda: page.evaluate("!!(navigator.serviceWorker && navigator.serviceWorker.controller)"), timeout)


def pwa_debug_views(page, base, tag, problems):
    """hello paused at the reset vector: Run to hello_main, then the
    Debugger (call stack, locals: DWARF shards), the Page listing
    (disasm.json) and the Source panel on boot/hello.c (C source)."""
    page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        problems.append(f"{tag}: hello did not start paused: {page.inner_text('#status')!r}")
        return {}
    syms = json.loads((SITE / kernel_entries()["hello"]["symbols"]).read_text())["symbols"]
    hm = next(s for s in syms if s["name"] == "hello_main")
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    out = {}
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(hm["paddr"]), 60):
        problems.append(f"{tag}: Run to hello_main did not stop there: {page.inner_text('#dbg_status')!r}")
        return out
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
    out["unwind"] = page.get_attribute("#call_stack", "data-unwind")
    out["stack"] = page.inner_text("#call_stack")[:80]
    out["locals"] = "row" in page.inner_text("#locals")
    if out["unwind"] != "ok" or "hello_main" not in out["stack"] or not out["locals"]:
        problems.append(f"{tag}: Debugger at hello_main: unwind {out['unwind']}, stack {out['stack']!r}, local row shown {out['locals']}")
    show_tab(page, "page")
    wait_for(lambda: page.locator("#listing .cur[data-addr]").count() > 0, 20)
    out["listing"] = page.locator("#listing .cur[data-addr]").count() > 0 and "hello_main" in page.inner_text("#listing")
    if not out["listing"]:
        problems.append(f"{tag}: the Page listing does not show hello_main with EIP marked")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 20)
    out["tree"] = page.get_attribute("#cov_tree", "data-state")
    page.evaluate("openSourceFile('boot/hello.c')")
    wait_for(lambda: HELLO in (page.text_content("#src_body") or ""), 20)
    out["source"] = HELLO in (page.text_content("#src_body") or "")
    if out["tree"] != "ok" or not out["source"]:
        problems.append(f"{tag}: Source tab tree {out['tree']}, boot/hello.c shown in the Source panel {out['source']}")
    return out


def pwa_case(browser, base, shots, timeout):
    problems, r = [], {}
    live = base is not None
    tmp = None
    if not live:
        tmp = tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP"))
        root = pathlib.Path(tmp.name)
        pwa_deploy(root, PWA_V[0], "A")
        httpd, state = pwa_server(root)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
    c = browser.new_context(viewport={"width": 1280, "height": 900})
    errors = []
    page = c.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    from_sw = []
    page.on("response", lambda resp: resp.from_service_worker and from_sw.append(resp.url))
    try:
        page.goto(f"{base}/index.html?kernel=hello")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append("online: hello did not boot")
        pwa_manifest_check(page, base, problems, r)
        if not pwa_controlled(page):
            problems.append("no service worker controls the page after the first visit")
            return problems, r
        # Chromium 145's Page.getInstallabilityErrors answers [] even for a
        # page without a manifest (checked on the live site before P1), and
        # headless never fires beforeinstallprompt: check Chromium's own
        # parse of the manifest against its install criteria instead.
        cdp = c.new_cdp_session(page)
        inst = cdp.send("Page.getInstallabilityErrors").get("installabilityErrors", [])
        if inst:
            problems.append(f"Chromium's installability errors: {inst}")
        am = cdp.send("Page.getAppManifest")
        pm = am.get("manifest") or {}
        r["chromium_manifest"] = {k: pm.get(k) for k in ("name", "display", "startUrl", "scope")}
        sizes = {i.get("sizes") for i in pm.get("icons", []) if i.get("type") == "image/png"}
        why = [w for w, bad in (
            (f"errors {am.get('errors')}", am.get("errors")),
            ("no manifest URL", not (am.get("url") or "").endswith("/manifest.webmanifest")),
            ("no name", not (pm.get("name") or pm.get("shortName"))),
            (f"display {pm.get('display')}", pm.get("display") not in ("kStandalone", "kFullscreen", "kMinimalUi")),
            (f"icons {sizes}", not {"192x192", "512x512"} <= sizes),
            (f"start URL {pm.get('startUrl')} outside the scope {pm.get('scope')}", not (pm.get("startUrl") or "").startswith(pm.get("scope") or "-")),
            ("prefer_related_applications", pm.get("preferRelatedApplications")),
            ("not a secure context", not page.evaluate("isSecureContext")),
        ) if bad]
        if why:
            problems.append(f"not installable by Chromium's criteria: {why}")
        want_v = PWA_V[0] if not live else (page.get_attribute("#build_info", "data-sha") or "")[:12]
        r["sw_version"] = page.evaluate(PWA_SW_VERSION_JS)
        if r["sw_version"] != want_v:
            problems.append(f"the service worker says version {r['sw_version']!r}, the page is {want_v!r}")
        r["online_views"] = pwa_debug_views(page, base, "online", problems)
        r["from_service_worker"] = len(from_sw)
        if not from_sw:
            problems.append("no response came from the service worker (no fetch handler?)")
        r["caches_online"] = page.evaluate(PWA_CACHES_JS)
        if not any(want_v in k for k in r["caches_online"]):
            problems.append(f"no cache named after the deployed commit: {r['caches_online']}")
        page.screenshot(path=str(shots / "pwa-1280-online.png"))

        # Offline.
        if not live:
            state["down"] = True
        else:
            c.route("**/*", lambda route: route.abort("internetdisconnected"))
        c.set_offline(True)
        try:
            page.goto(f"{base}/index.html?kernel=hello")
        except PlaywrightError as e:
            problems.append(f"offline: the page does not load: {str(e)[:150]}")
            return problems, r
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append(f"offline: hello did not boot: {page.inner_text('#status')!r}")
        for k in PWA_PRECACHED_KERNELS:
            if k == "hello":
                continue
            page.goto(f"{base}/index.html?kernel={k}&speed=full")
            ok = wait_for(lambda: gate_on_screen(page), timeout)
            r[f"offline_{k}"] = ok
            if not ok:
                problems.append(f"offline: {k} did not boot (never opened online): {page.inner_text('#status')!r}")
        r["offline_views"] = pwa_debug_views(page, base, "offline", problems)
        # Something never fetched online: a clear note, no page error.
        page.goto(f"{base}/index.html?kernel=debian-6.1-686")
        def note():
            notes = page.text_content("#pwa_notes") if page.locator("#pwa_notes").count() else ""
            return (page.text_content("#status") or "") + " " + (notes or "")
        wait_for(lambda: "not available offline yet" in note(), 30)
        r["offline_note"] = note()[:200]
        if "not available offline yet" not in r["offline_note"]:
            problems.append(f"offline, a kernel never opened online: no 'not available offline yet' note: {r['offline_note']!r}")
        for scheme in ("light", "dark"):
            for w in (1280, 390):
                page.emulate_media(color_scheme=scheme)
                page.set_viewport_size({"width": w, "height": 900})
                time.sleep(0.3)
                if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                    problems.append(f"offline note {w} px {scheme}: horizontal scroll")
                page.screenshot(path=str(shots / f"pwa-{w}-{scheme}-offline.png"))
        page.emulate_media(color_scheme="light")
        page.set_viewport_size({"width": 1280, "height": 900})
        page.goto(f"{base}/index.html?kernel=hello")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append(f"offline, after the note: hello did not boot again: {page.inner_text('#status')!r}")

        # Back online with a new deploy.
        if not live:
            pwa_deploy(root, PWA_V[1], "B")
            state["down"] = False
        else:
            c.unroute("**/*")
            fake = {"sha": "f" * 40, "date": "2026-11-09T12:00:00Z", "subject": "a newer deploy", "run_id": "1",
                    "run_number": "1", "repo": "d33tah/minimal-kernel-build"}
            c.route("**/version.json*", lambda route: route.fulfill(status=200, content_type="application/json",
                                                                     body=json.dumps(fake),
                                                                     headers={"Cache-Control": "no-store"}))
        c.set_offline(False)
        shown = wait_for(lambda: page.locator("#pwa_update").count() and page.is_visible("#pwa_update"), 30)
        r["update_note"] = page.text_content("#pwa_update") if page.locator("#pwa_update").count() else None
        if not shown or "new version" not in (r["update_note"] or "").lower():
            problems.append(f"no 'new version, reload' notice after a new deploy: {r['update_note']!r}")
            return problems, r
        for scheme in ("light", "dark"):
            for w in (1280, 390):
                page.emulate_media(color_scheme=scheme)
                page.set_viewport_size({"width": w, "height": 900})
                time.sleep(0.3)
                if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                    problems.append(f"update notice {w} px {scheme}: horizontal scroll")
                page.screenshot(path=str(shots / f"pwa-{w}-{scheme}-update.png"))
        page.emulate_media(color_scheme="light")
        page.set_viewport_size({"width": 1280, "height": 900})
        if not live:
            with page.expect_navigation(timeout=30000):
                page.click("#pwa_update button")
            if not wait_for(lambda: gate_on_screen(page), timeout):
                problems.append("after the update reload hello did not boot")
            r["after_reload"] = {
                "marker": page.evaluate("window.deployMarker"),
                "stamp": page.evaluate("new URL(document.querySelector('script[src*=\"app.js\"]').src).searchParams.get('v')"),
                "sw": page.evaluate(PWA_SW_VERSION_JS),
                "caches": page.evaluate(PWA_CACHES_JS),
            }
            a = r["after_reload"]
            if a["marker"] != "B" or a["stamp"] != PWA_V[1] or a["sw"] != PWA_V[1]:
                problems.append(f"after the update reload: app.js deploy {a['marker']}, stamp {a['stamp']}, service worker {a['sw']}")
            if any(PWA_V[0] in k for k in a["caches"]) or not any(PWA_V[1] in k for k in a["caches"]):
                problems.append(f"after the update the old deploy's caches are still there (or no new one): {a['caches']}")
            time.sleep(3)
            if page.locator("#pwa_update").count() and page.is_visible("#pwa_update"):
                problems.append("the update notice shows on the new deploy itself")

    finally:
        if errors:
            problems.append(f"page errors: {errors[:3]}")
        c.close()
        if tmp:
            httpd.shutdown()
            tmp.cleanup()
    return problems, r


def suite_pwa(ctx):
    base = ctx.base if ctx.args.base else None
    return report("pwa", *pwa_case(ctx.browser, base, ctx.shots, ctx.args.timeout))


# ---- follow EIP: the asm views show the stepper's EIP --------------------
# The operator's report: on the minified Linux, in the Debugger tab, the
# instructions went on but Code at EIP no longer moved. FOLLOW_EIP_JS is the
# invariant, watched in the page: each drawing of Code at EIP (Debugger) and
# of the Page listing (data-draws) must box the line holding EIP, and once an
# instruction has executed a view on screen must show the new EIP within
# max(1 s, twice the last drawing plus 400 ms) of its last drawing. It
# holds while stepping at any speed (slow motion, Run to, counting) and
# paused; not while the JIT runs (the views say so) or the tab is hidden.
# The Page listing need only show EIP while stepping, or paused on EIP's
# page.

FOLLOW_EIP_JS = """() => {
    if (window.mkbFollow)
        return;
    const M = window.mkbFollow = { viol: [], draws: { context: 0, listing: 0 }, good: {}, pend: {}, samples: 0 };
    const sel = { context: '#context .cur[data-addr]', listing: '#listing .cur[data-addr]' };
    const tabOf = { context: 'debugger', listing: 'page' };
    const now = () => realNow(); // performance.now is the guest's clock (clock.js)
    const boxed = (v) => { const e = document.querySelector(sel[v]); return e ? Number(e.dataset.eip ?? e.dataset.addr) : -1; };
    // The boxed line holds EIP: an instruction at EIP (or with EIP inside
    // it, data-eip), or the hex row around it.
    const shows = (v, eip) => { const e = document.querySelector(sel[v]);
        return !!e && (Number(e.dataset.eip ?? e.dataset.addr) === eip ||
            (e.dataset.end !== undefined && eip >= Number(e.dataset.addr) && eip < Number(e.dataset.end))); };
    const due = (v) => stepper && emuReady && !document.hidden && !ffActive && tab === tabOf[v] && stepper.nextPhys() >= 0 &&
        stepper.nextPhys() < emuRam && (runState === 'paused' || runState === 'stepped') &&
        (v === 'context' || runState === 'stepped' || selectedPage === Math.floor(stepper.nextPhys() / PAGE));
    const state = (v) => ({ view: v, boxed: boxed(v), eip: stepper.nextPhys(), steps: stepper.steps, state: runState,
        speed: speed(), fast: fastStepping(), runTo: runTo !== null, kernel: image && image.name,
        source: !document.getElementById('src_panel').hidden, page: Number(document.getElementById('listing').dataset.page) });
    const seen = (v) => {
        if (!stepper)
            return;
        M.draws[v]++;
        if (shows(v, stepper.nextPhys())) {
            M.good[v] = { t: now(), steps: stepper.steps, st: stepper };
            M.pend[v] = null;
        } else if (due(v) && M.viol.length < 20 && (v === 'context' ||
                Number(document.getElementById('listing').dataset.page) === Math.floor(stepper.nextPhys() / PAGE)))
            // The listing drawn at another page (the page EIP has just
            // left, before it is followed) is caught by the time limit.
            M.viol.push({ kind: 'drawn without EIP boxed', ...state(v) });
    };
    for (const v of ['context', 'listing'])
        new MutationObserver(() => seen(v)).observe(document.getElementById(v), { attributes: true, attributeFilter: ['data-draws'] });
    const limit = () => Math.max(1000, 2 * (viewCost || 0) + 400);
    M.timer = setInterval(() => {
        M.samples++;
        for (const v of ['context', 'listing']) {
            if (!due(v)) {
                M.pend[v] = null;
                continue;
            }
            // Fresh: drawn at this instruction, or showing EIP anyway (an
            // instruction repeated in place: rep movs, a jump to itself).
            const g = M.good[v];
            if ((g && g.st === stepper && g.steps === stepper.steps) || shows(v, stepper.nextPhys())) {
                M.pend[v] = null;
                continue;
            }
            const t = now();
            if (M.pend[v] == null)
                M.pend[v] = t;
            else if (t - M.pend[v] > limit()) {
                if (M.viol.length < 20)
                    M.viol.push({ kind: 'stale', for_ms: Math.round(t - M.pend[v]), limit_ms: Math.round(limit()),
                        drawn_steps: g && g.st === stepper ? g.steps : null, ...state(v) });
                M.pend[v] = t;
            }
        }
    }, 50);
}"""


def follow_install(page):
    wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!document.getElementById('listing')"), 60)
    page.evaluate(FOLLOW_EIP_JS)


def follow_eip_case(browser, base, shots, kernel, tab, throttle):
    """The invariant through a fixed sequence on one kernel and tab: from the
    kernel entry at 1/s, then 10/s, 1k/s, Pause, Step and Step 100,
    Continue at 1/s, a Run to that steps (as fast as it can) to the end of
    the run, then Boot at full speed (stepped and counted to the gate
    lines, then the JIT) to the end. Regression for two holes: Code at EIP and the Page
    listing kept the box where a Run to (or counting) had started while the
    count went on, and the listing marked nothing while EIP ran the
    relocation stub inside vmlinux's startup_32 (the instructions at
    0x100000 right after the entry)."""
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    if throttle > 1:
        ctx.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": throttle})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed=1#{tab}")
    if not wait_for(lambda: page.evaluate("typeof entryPhase !== 'undefined' && runState === 'stepped' && !entryPhase && !ffActive"), 180):
        ctx.close()
        return [{"no slow motion after the entry": page.evaluate(RUN_STATE_JS)}], {}
    follow_install(page)
    phases = []

    def phase(label, secs):
        time.sleep(secs)
        phases.append([label, page.evaluate("stepper.steps"), page.evaluate("mkbFollow.draws." + ("context" if tab == "debugger" else "listing"))])

    phase("1/s", 6)
    page.select_option("#dbg_speed", "10")
    phase("10/s", 4)
    # 1k/s for 4 s or a few instructions: hello's run ends about 450 after
    # its entry (and a click takes a while at 4x).
    s0 = page.evaluate("stepper.steps")
    page.select_option("#dbg_speed", "1000")
    wait_for(lambda: page.evaluate("stepper.steps") - s0 > (20 if kernel.startswith("hello") else 100), 4, 0.02)
    phase("1000/s", 0)

    def act(sel):
        # From either tab (Step N and Run to are in the Debugger tab); a
        # control the end of the run has disabled is left alone.
        page.evaluate("s => { const b = document.querySelector(s); if (!b.disabled) b.click(); }", sel)

    act("#dbg_pause")
    phase("Pause", 2)
    page.select_option("#dbg_speed", "1")
    act("#dbg_step")
    phase("Step", 1.5)
    act("#dbg_stepn")
    phase("Step 100", 2)
    act("#dbg_continue")
    phase("Continue at 1/s", 4)
    # Not in the kernel: no breakpoint, the Run to steps to the end of the run.
    page.evaluate("() => { document.getElementById('dbg_addr').value = '0x1000'; }")
    act("#dbg_runto")
    wait_for(lambda: page.evaluate("runState === 'paused' && runTo === null && !ffActive && !!stepper.end"), 300)
    phase("Run to 0x1000 (stepped to the end)", 2)
    # Boot at full speed: the boot is stepped and counted from the reset
    # vector to the gate lines, then the JIT runs it to its end.
    page.select_option("#dbg_speed", "full")
    show_tab(page, "machine")
    page.click("#boot")
    wait_for(lambda: page.evaluate("runState === 'paused' && !ffActive && !!stepper && !!stepper.end"), 300)
    phase("Boot at full speed (counting), to the end", 2)
    viol = page.evaluate("mkbFollow.viol")
    page.screenshot(path=str(shots / f"follow-eip-{kernel}-{tab}-{throttle}x.png"))
    problems = [{"follow EIP": viol[:4]}] if viol else []
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"phases": phases}


FOLLOW_FUZZ_HIDE = """() => {
    Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
    Object.defineProperty(document, 'hidden', { value: true, configurable: true });
    window.mkbRaf = [window.requestAnimationFrame, window.cancelAnimationFrame];
    window.mkbHeld = new Map(); let id = 1e9;
    window.requestAnimationFrame = (cb) => { window.mkbHeld.set(++id, cb); return id; };
    window.cancelAnimationFrame = (i) => { if (!window.mkbHeld.delete(i)) window.mkbRaf[1].call(window, i); };
    document.dispatchEvent(new Event('visibilitychange')); }"""
FOLLOW_FUZZ_SHOW = """() => { delete document.visibilityState; delete document.hidden;
    document.dispatchEvent(new Event('visibilitychange'));
    [window.requestAnimationFrame, window.cancelAnimationFrame] = window.mkbRaf;
    const held = [...window.mkbHeld.values()]; window.mkbHeld.clear();
    for (const cb of held) window.requestAnimationFrame(cb); }"""
FOLLOW_FUZZ_TARGETS = ["start_kernel", "printk", "rest_init", "kernel_init", "0x100000", "0x1000", "hello_main"]


def follow_fuzz_action(page, cdp, rng):
    """One random action for follow_fuzz_case, as a short description."""
    r = rng.random()

    def click(sel):
        what = page.evaluate(RUN_FUZZ_PICK, [sel, rng.random()])
        if what is None:
            return "nothing for " + sel
        try:
            page.locator("[data-fz]").first.click(timeout=1500)
            return "click " + what
        except PlaywrightError:
            return "could not click " + what
    if r < 0.18:
        return click(rng.choice(["#tab_debugger", "#tab_debugger", "#tab_page", "#tab_page", "#tab_memory", "#tab_calls", "#tab_source"]))
    if r < 0.32:
        v = rng.choice(["1", "10", "10", "1000", "100000", "1000000", "full"])
        page.select_option("#dbg_speed", v)
        return "speed " + v
    if r < 0.45:
        return click(rng.choice(["#dbg_pause", "#dbg_continue", "#dbg_step", "#dbg_stepn", "#vm_continue"]))
    if r < 0.50:
        v = rng.choice(FOLLOW_FUZZ_TARGETS)
        page.evaluate("v => { document.getElementById('dbg_addr').value = v; }", v)
        return "Run to " + v + ": " + click("#dbg_runto")
    if r < 0.60:
        return click(rng.choice([".srclink", ".srclink", "#src_close"]))
    if r < 0.70:
        return click(rng.choice(["#nav_back", "#nav_fwd"]))
    if r < 0.73:
        # The browser's back, only inside the page's jump history (past it
        # the browser leaves the page, as for any page).
        if page.evaluate("!!(history.state && history.state.nav > 0)"):
            page.go_back()
            return "browser back"
        return "no browser back (would leave the page)"
    if r < 0.78:
        return click(rng.choice(["#listing [data-addr]", "#context [data-addr]", ".symlink"]))
    if r < 0.81:
        v = rng.choice(["minified-linux", "minified-linux-os", "hello"])
        show_tab(page, "machine")
        page.select_option("#kernel", v)
        return "kernel " + v
    if r < 0.84:
        show_tab(page, "machine")
        return click("#boot")
    if r < 0.89:
        page.evaluate(FOLLOW_FUZZ_HIDE)
        h = rng.choice([1, 3, 6])
        time.sleep(h)
        page.evaluate(FOLLOW_FUZZ_SHOW)
        return f"hidden {h} s"
    if r < 0.93:
        t = rng.choice([1, 4])
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": t})
        return f"CPU {t}x slower"
    time.sleep(3)
    return "wait 3 s"


def follow_fuzz_case(browser, base, shots, kernel, seed, actions, verbose=False):
    """Seeded random actions (tabs, speeds, Pause, Step, Run to, the Source
    panel, Back and Forward, links into the listings, kernel switch, Boot
    during the fast-forward, a hidden tab, CPU throttling 1x/4x) with the
    follow-EIP invariant watched all along; stops at the first broken one."""
    rng = random.Random(seed)
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    if rng.choice([1, 4]) > 1:
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 4})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed={rng.choice(['1', '10'])}#{rng.choice(['debugger', 'page'])}")
    wait_for(lambda: page.evaluate("typeof entryPhase !== 'undefined' && runState === 'stepped' && !entryPhase && !ffActive"), 180)
    follow_install(page)
    log, problems = [], []
    for _ in range(actions):
        for extra in ctx.pages[1:]:
            extra.close()
        log.append(follow_fuzz_action(page, cdp, rng))
        time.sleep(rng.choice([0, 0.3, 1, 2, 3]))
        viol = page.evaluate("mkbFollow.viol")
        if viol or errors:
            problems.append({"after": log[-8:], "follow EIP": viol[:3], "page errors": errors})
            page.screenshot(path=str(shots / f"follow-fuzz-{kernel}-{seed}.png"))
            break
    ctx.close()
    return problems, {"actions": len(log), **({"log": log} if verbose else {})}


# (kernel, tab, CPU throttling) for follow_eip_case.
FOLLOW_EIP_CASES = [(k, t, c) for k in ("minified-linux", "minified-linux-os", "hello") for t in ("debugger", "page") for c in (1, 4)]
# (kernel, seed): fixed, so CI runs the same sequences every time.
FOLLOW_FUZZ_SEEDS = [("minified-linux", 1), ("minified-linux-os", 2), ("minified-linux", 3), ("hello", 4)]


def suite_followeip(ctx):
    failed = False
    for kernel, tab, throttle in [c for c in FOLLOW_EIP_CASES if c[0] in ctx.entries]:
        t0 = time.monotonic()
        p, r = follow_eip_case(ctx.browser, ctx.base, ctx.shots, kernel, tab, throttle)
        failed |= report("follow_eip", p, r, kernel=kernel, tab=tab, throttle=throttle, seconds=round(time.monotonic() - t0))
    return failed


def suite_followfuzz(ctx):
    failed = False
    seeds = [s for s in FOLLOW_FUZZ_SEEDS if s[0] in ctx.entries]
    if ctx.args.fuzz_seeds:
        seeds = [(["minified-linux", "minified-linux-os", "hello"][s % 3], s)
                 for s in range(ctx.args.fuzz_from, ctx.args.fuzz_from + ctx.args.fuzz_seeds)]
    for kernel, seed in seeds:
        t0 = time.monotonic()
        p, r = follow_fuzz_case(ctx.browser, ctx.base, ctx.shots, kernel, seed, ctx.args.fuzz_actions,
                                verbose=bool(ctx.args.fuzz_seeds))
        failed |= report("follow_fuzz", p, r, kernel=kernel, seed=seed, seconds=round(time.monotonic() - t0))
    return failed


# ---- run state: random clicks at 1/s ---------------------------------------
# The operator's report: clicking around at 1 instruction per second reached
# a state where the page was not paused yet nothing ran. run_state_problem()
# is the invariant, checked after every action of a seeded random-click
# fuzzer (desktop and 390 px touch, hello and the minified Linux).

RUN_STATE_JS = """() => ({
    state: document.getElementById('dbg_state').dataset.state || '',
    text: document.getElementById('dbg_state').textContent,
    steps: stepper ? stepper.steps : -1,
    end: !!(stepper && stepper.end),
    pauseOff: document.getElementById('dbg_pause').disabled,
    contOff: document.getElementById('dbg_continue').disabled,
    overlay: !document.getElementById('vm_overlay').hidden,
    speed: document.getElementById('dbg_speed').value,
    status: document.getElementById('status').textContent,
    note: document.getElementById('dbg_status').textContent,
    line: document.getElementById('vclock').textContent.split(' \u00b7 ')[0],
})"""


def run_state_problem(page, budget=3.5):
    """None, or what is wrong with the run state now. Paused: Pause disabled,
    Continue enabled (unless the run has ended), the "VM paused" overlay
    shown. Running: the opposite, the speed line never "speed 0/s" (the
    operator's "not paused and zero instructions per second"), and at 1/s
    or 10/s the instruction count moves within budget seconds."""
    bad = run_state_zero_speed(page)
    if bad:
        return bad
    s0 = page.evaluate(RUN_STATE_JS)
    if s0["state"] == "paused":
        if not s0["pauseOff"] or (s0["contOff"] and not s0["end"]) or not s0["overlay"]:
            return {"paused, controls disagree": s0}
        return None
    if s0["state"] in ("stepped", "jit", "ff") and (s0["pauseOff"] or s0["overlay"]):
        return {"running, controls disagree": s0}
    if s0["state"] == "jit" and not s0["contOff"]:
        return {"running, Continue enabled": s0}
    if s0["state"] == "off":
        # Between Boot and the machine being ready: must not stay there.
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") != "off", 30):
            return {"off for 30 s after a Boot": page.evaluate(RUN_STATE_JS)}
        return None
    if s0["state"] == "ff":
        # A fast-forward runs the JIT: the count moves, or it ends.
        moved = wait_for(lambda: page.evaluate("stepper ? stepper.steps : -1") != s0["steps"]
                         or page.get_attribute("#dbg_state", "data-state") != "ff", 10)
        return None if moved else {"fast-forward not moving for 10 s": page.evaluate(RUN_STATE_JS)}
    if s0["state"] != "stepped" or s0["speed"] not in ("1", "10"):
        return None
    moved = wait_for(lambda: page.evaluate(RUN_STATE_JS)["steps"] != s0["steps"]
                     or page.evaluate(RUN_STATE_JS)["state"] != "stepped"
                     or page.evaluate(RUN_STATE_JS)["speed"] != s0["speed"], budget)
    if not moved:
        return {"running at %s/s, no instruction for %s s" % (s0["speed"], budget): page.evaluate(RUN_STATE_JS)}
    return run_state_zero_speed(page)


def run_state_zero_speed(page):
    """None, or the state while the speed line says "speed 0/s" with the
    run going on (stepping, the JIT or a fast-forward)."""
    s = page.evaluate(RUN_STATE_JS)
    if s["state"] in ("stepped", "jit", "ff") and re.match(r"speed 0(\.0)?/s\b", s["line"]):
        return {"running, the speed line says 0/s": s}
    return None


RUN_FUZZ_PICK = """([sel, r]) => {
    document.querySelectorAll('[data-fz]').forEach((e) => e.removeAttribute('data-fz'));
    const all = [...document.querySelectorAll(sel)].filter((e) => {
        if (e.disabled)
            return false;
        const b = e.getBoundingClientRect();
        return b.width > 0 && b.height > 0 && getComputedStyle(e).visibility !== 'hidden';
    });
    if (!all.length)
        return null;
    const e = all[Math.floor(r * all.length)];
    e.setAttribute('data-fz', '1');
    return e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + ' ' + (e.textContent || '').trim().slice(0, 24);
}"""
# Anything clickable on the page, except file pickers, downloads and links
# to other pages.
RUN_FUZZ_ANY = ("button:not(#graph_json):not(#graph_dot):not(#cov_export):not(#src_dir_btn), a[href^='#'], [role=button], "
                "#listing [data-addr], #context [data-addr], .jc, #pages, #screen_container, input[type=checkbox]")
RUN_FUZZ_CONTROLS = ["#dbg_pause", "#dbg_continue", "#vm_continue", "#dbg_step", "#dbg_stepn", "#dbg_runto", "#boot",
                     "#tab_machine", "#tab_memory", "#tab_page", "#tab_debugger", "#tab_snapshots", "#tab_calls", "#src_close",
                     "#snap_save", "#snapshots .restore", "#count"]
RUN_FUZZ_TARGETS = ["0x100000", "start_kernel", "hello_main", "rest_init", "printk", "kernel_init", "do_exit",
                    "init/main.c:1000", "hello.c:30", "0x1000", "no_such_symbol"]
RUN_FUZZ_KERNELS = ["hello", "hello-os", "minified-linux", "minified-linux-os"]
RUN_FUZZ_THROTTLE = 4


def run_fuzz_action(page, rng, touch):
    """One random action, as a short description."""
    r = rng.random()
    if r < 0.1:
        # An asm listing watched for a while (the audit's repro of bug 1:
        # the Page tab at 1/s on a throttled phone ran 6 instructions in 30 s).
        t = rng.choice(["#tab_page", "#tab_page", "#tab_debugger"])
        hold = rng.choice([3, 5, 8])
        try:
            (page.tap if touch else page.click)(t, timeout=5000)
        except PlaywrightError as e:
            return "could not open " + t + ": " + str(e).splitlines()[0][:60]
        time.sleep(hold)
        return f"watch {t} for {hold} s"
    r = rng.random()
    if r < 0.06:
        k = rng.choice(["Escape", "Enter", "ArrowDown", "a", " ", "Tab"])
        page.keyboard.press(k)
        return "key " + k
    if r < 0.22:
        v = rng.choice(["1", "1", "1", "10", "10", "100", "1000", "full"])
        page.select_option("#dbg_speed", v)
        return "speed " + v
    if r < 0.25:
        v = rng.choice(["10", "100", "1000", "real"])
        page.select_option("#clock_mips", v)
        return "clock " + v
    if r < 0.28:
        v = rng.choice(RUN_FUZZ_KERNELS)
        show_tab(page, "machine")
        page.select_option("#kernel", v)
        return "kernel " + v
    if r < 0.33:
        v = rng.choice(RUN_FUZZ_TARGETS)
        page.evaluate("v => { document.getElementById('dbg_addr').value = v; }", v)
        return "Run to field " + v
    sel = rng.choice(RUN_FUZZ_CONTROLS) if r < 0.75 else RUN_FUZZ_ANY
    what = page.evaluate(RUN_FUZZ_PICK, [sel, rng.random()])
    if what is None:
        return "nothing to click for " + sel
    try:
        el = page.locator("[data-fz]").first
        if touch:
            el.tap(timeout=1500)
        else:
            el.click(timeout=1500)
        return ("tap " if touch else "click ") + what
    except PlaywrightError as e:
        return "could not click " + what + ": " + str(e).splitlines()[0][:60]


def run_fuzz_case(browser, base, shots, kernel, touch, seed, actions, verbose=False):
    """Seeded random clicks from kernel at 1/s; stops at the first broken
    invariant. Returns (problems, record)."""
    rng = random.Random(seed)
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    if touch:
        # A mid-tier phone (Chrome's "4x slowdown").
        ctx.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": RUN_FUZZ_THROTTLE})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") not in (None, "", "off"), 120)
    log, problems = [], []
    for _ in range(actions):
        for extra in ctx.pages[1:]:
            extra.close()
        if "/index.html" not in page.url:  # a link left the page: back to it
            page.go_back()
        pause = rng.choice([0, 0, 0.05, 0.3, 1, 2])
        log.append(run_fuzz_action(page, rng, touch))
        time.sleep(pause)
        bad = run_state_problem(page)
        if bad or errors:
            problems.append({"after": log[-8:], "problem": bad, "page_errors": errors})
            page.screenshot(path=str(shots / f"runfuzz-{kernel}-{'phone' if touch else 'desktop'}-{seed}.png"))
            break
    ctx.close()
    return problems, {"actions": len(log), **({"log": log} if verbose else {})}


def double_boot_case(browser, base, shots, touch):
    """Boot pressed twice in a row while the minified Linux fast-forwards
    to its entry (the first minimal sequence the fuzzer found): the second
    machine must come up and run at 1/s, with no page error. Desktop: both
    clicks in one task (always hit the race before the fix: "fast-forwarding
    to startup_32 with the JIT: 0 instructions", Pause enabled, nothing
    running); phone: two real taps (about every other run before the fix)."""
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=1")
    wait_for(lambda: page.evaluate("typeof ffActive !== 'undefined' && ffActive"), 60, 0.01)
    show_tab(page, "machine")
    if touch:
        page.tap("#boot", no_wait_after=True)
        page.tap("#boot", no_wait_after=True)
    else:
        page.evaluate("() => { const b = document.getElementById('boot'); b.click(); b.click(); }")
    problems = []
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "stepped"
                    and page.evaluate("!entryPhase"), 60):
        problems.append({"not running at 1/s 60 s after two Boots": page.evaluate(RUN_STATE_JS)})
    else:
        bad = run_state_problem(page)
        if bad:
            problems.append(bad)
    if errors:
        problems.append({"page errors": errors})
    tag = "phone" if touch else "desktop"
    page.screenshot(path=str(shots / f"double-boot-{tag}.png"))
    ctx.close()
    return problems


SPEED_LINE_JS = """() => ({
    state: runState, entry: entryPhase, runTo: runTo !== null, ff: ffActive, end: !!(stepper && stepper.end),
    halted: !!stepper && stepper.halted(), set: document.getElementById('dbg_speed').value,
    text: document.getElementById('vclock').textContent.split(' \u00b7 ')[0],
    steps: stepper ? stepper.steps : -1,
})"""


def speed_line_bad(s):
    """None, or why the speed line s (SPEED_LINE_JS) is wrong for a slow-motion run."""
    if s["state"] != "stepped" or s["entry"] or s["runTo"] or s["ff"] or s["end"] or s["set"] == "full":
        return None
    if s["text"] == "speed: measuring":
        return None
    # Below half the set rate the line adds " (set 1/s: too fast for this device)".
    m = re.fullmatch(r"speed ([0-9.]+)(k|M)?/s( \(set [^)]*\))?", s["text"])
    if not m:
        return "unreadable"
    v = float(m.group(1)) * {"k": 1e3, "M": 1e6}.get(m.group(2), 1)
    want = float(s["set"])
    return None if want / 2 <= v <= want * 2 else f"shows {v:g}/s, set {want:g}/s"


def speed_line_case(browser, base, shots, touch, rate):
    """The speed line while running in slow motion (the operator: "not paused
    and zero instructions per second", on a phone at 1/s). Sampled every
    100 ms for about 60 s, through Pause and Continue, a speed change, the
    tab hidden for 6 s (the run must go on), then frozen for 8 s (as a
    phone freezes a background tab) and shown again; phone: 390 px touch, CPU 5x slower. While running it must
    show the set rate within a factor of two, or "measuring" right after a
    change; never 0/s. And the run itself must not stall: the count moves
    at the set rate."""
    ctx = browser.new_context(**(PHONE if touch else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    if touch:
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 5})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed={rate}")
    wait_for(lambda: page.evaluate("typeof entryPhase !== 'undefined' && runState === 'stepped' && !entryPhase && !ffActive"), 120)
    act = page.tap if touch else page.click
    problems, samples = [], 0

    def sample(secs, label):
        nonlocal samples
        t0 = time.monotonic()
        s0 = page.evaluate(SPEED_LINE_JS)
        while time.monotonic() - t0 < secs:
            s = page.evaluate(SPEED_LINE_JS)
            samples += 1
            why = speed_line_bad(s)
            if why and len(problems) < 10:
                problems.append({"after": label, "at_s": round(time.monotonic() - t0, 1), "why": why, "line": s["text"]})
            time.sleep(0.1)
        s = page.evaluate(SPEED_LINE_JS)
        ran = s["steps"] - s0["steps"]
        if s["state"] == "stepped" and not s["end"] and ran < float(s["set"]) * secs / 3:
            problems.append({"after": label, "stalled": f"{ran} instructions in {secs} s at {s['set']}/s"})

    def hide(hidden_s, frozen_s):
        # As a browser does for a hidden tab: no animation frames until shown
        # (headless Chromium would go on running them).
        page.evaluate("""() => {
            Object.defineProperty(document, 'visibilityState', { value: 'hidden', configurable: true });
            Object.defineProperty(document, 'hidden', { value: true, configurable: true });
            window.mkbRaf = [window.requestAnimationFrame, window.cancelAnimationFrame];
            window.mkbHeld = new Map();
            let id = 1e9;
            window.requestAnimationFrame = (cb) => { window.mkbHeld.set(++id, cb); return id; };
            window.cancelAnimationFrame = (i) => { if (!window.mkbHeld.delete(i)) window.mkbRaf[1].call(window, i); };
            document.dispatchEvent(new Event('visibilitychange'));
        }""")
        # Hidden (a background tab: no animation frames): the run goes on.
        before = page.evaluate("stepper.steps")
        time.sleep(hidden_s)
        ran = page.evaluate("stepper.steps") - before
        if ran < float(rate) * hidden_s / 3:
            problems.append({"hidden": f"{ran} instructions in {hidden_s} s at {rate}/s"})
        cdp.send("Page.setWebLifecycleState", {"state": "frozen"})
        time.sleep(frozen_s)
        cdp.send("Page.setWebLifecycleState", {"state": "active"})
        page.evaluate("""() => {
            delete document.visibilityState;
            delete document.hidden;
            // The event first (frames it cancels stay cancelled), then the held frames run.
            document.dispatchEvent(new Event('visibilitychange'));
            [window.requestAnimationFrame, window.cancelAnimationFrame] = window.mkbRaf;
            const held = [...window.mkbHeld.values()];
            window.mkbHeld.clear();
            for (const cb of held)
                window.requestAnimationFrame(cb);
        }""")

    other = "10" if rate == "1" else "1"
    sample(15, "start")
    act("#dbg_pause")
    time.sleep(2)
    act("#dbg_continue")
    sample(12, "Pause, Continue")
    page.select_option("#dbg_speed", other)
    time.sleep(3)
    page.select_option("#dbg_speed", rate)
    sample(12, f"speed {other} then {rate}")
    hide(6, 8)
    sample(15, "hidden 6 s, frozen 8 s, shown again")
    page.screenshot(path=str(shots / f"speed-line-{rate}-{'phone' if touch else 'desktop'}.png"))
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"samples": samples}


LONG_TASKS_JS = """() => { window.mkbLong = []; new PerformanceObserver((l) => {
    for (const e of l.getEntries()) window.mkbLong.push(e.duration); }).observe({ type: 'longtask' }); }"""
# Page: data-draws counts full drawings and moves of the EIP mark (data-reads,
# the full ones, on apps from before B3).
VIEW_DRAWS_JS = """() => ({ page: Number(document.getElementById('listing').dataset.draws || document.getElementById('listing').dataset.reads || 0),
    debugger: Number(document.getElementById('context').dataset.draws || 0),
    long: (window.mkbLong || []).reduce((a, b) => a + b, 0), longMax: Math.max(0, ...(window.mkbLong || [])) })"""


def speed_line_asm_case(browser, base, shots, kernel, tab, throttle, rate, secs=25):
    """The operator's bug 1 as the audit reproduced it: slow motion with an
    asm listing open on a phone-class CPU (390 px touch, CDP CPU throttling).
    Before the fix the Page tab's listing (about 1,400 instructions) was
    drawn again on every animation frame and the step credit grew by at most
    250 ms per frame, so at 4x the run made 6 instructions in 30 s while the
    page said "slow motion 1/s" and "speed 0/s". Checks over secs seconds
    after the tab is opened: the speed line as speed_line_bad (never 0/s;
    the set rate within a factor of two once the speed window has passed
    since the tab was opened), the count moves at a
    third of the set rate or more, the listing (Page) or Code at EIP
    (Debugger) is drawn at most once per executed instruction (plus a
    full refresh every few seconds), and the main
    thread is not busy most of the time (long tasks under 70 % of the wall
    time)."""
    ctx = browser.new_context(**PHONE)
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": throttle})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={kernel}&speed={rate}")
    wait_for(lambda: page.evaluate("typeof entryPhase !== 'undefined' && runState === 'stepped' && !entryPhase && !ffActive"), 180)
    page.tap("#tab_" + tab)
    page.evaluate(LONG_TASKS_JS)
    problems, samples, zero = [], 0, 0
    d0 = page.evaluate(VIEW_DRAWS_JS)
    s0 = page.evaluate(SPEED_LINE_JS)
    t0 = time.monotonic()
    while time.monotonic() - t0 < secs:
        s = page.evaluate(SPEED_LINE_JS)
        samples += 1
        why = speed_line_bad(s)
        # Opening the tab draws the listing once (up to 2 s at 6x): the
        # 4 s speed window shows that dip until it has passed; 0/s never.
        if why and (time.monotonic() - t0 >= SPEED_WINDOW_SLOW_S or why.startswith("shows 0/s")):
            zero += why.startswith("shows 0/s")
            if len(problems) < 6:
                problems.append({"at_s": round(time.monotonic() - t0, 1), "why": why, "line": s["text"]})
        time.sleep(0.1)
    wall = (time.monotonic() - t0) * 1000
    s = page.evaluate(SPEED_LINE_JS)
    d = page.evaluate(VIEW_DRAWS_JS)
    ran = s["steps"] - s0["steps"]
    draws = d[tab] - d0[tab]
    if s["state"] == "stepped" and not s["end"] and ran < float(rate) * secs / 3:
        problems.append({"stalled": f"{ran} instructions in {secs} s at {rate}/s"})
    if draws > ran + secs / 2 + 2:
        problems.append({"listing drawn": f"{draws} times for {ran} instructions in {secs} s"})
    if d["long"] > 0.7 * wall:
        problems.append({"main thread busy": f"long tasks {d['long'] / 1000:.1f} s of {wall / 1000:.1f} s"})
    shot = f"speed-line-{tab}-{kernel}-{throttle}x-{rate}.png"
    page.screenshot(path=str(shots / shot))
    if errors:
        problems.append({"page errors": errors})
    ctx.close()
    return problems, {"samples": samples, "zero_samples": zero, "instructions": ran, "listing_draws": draws,
                      "long_tasks_s": round(d["long"] / 1000, 1), "longest_ms": round(d["longMax"]), "line": s["text"]}


# app.js speedWindowMs() in slow motion.
SPEED_WINDOW_SLOW_S = 4
# (kernel, tab, CPU throttling, rate): the asm listings open in slow motion.
SPEED_LINE_ASM = [("minified-linux-os", "page", 4, "1"), ("minified-linux-os", "page", 4, "10"),
                  ("minified-linux-os", "page", 6, "1"), ("minified-linux-os", "page", 6, "10"),
                  ("minified-linux-os", "debugger", 6, "1"), ("minified-linux-os", "debugger", 6, "10"),
                  ("hello", "page", 4, "1")]


# (kernel, touch, seed): fixed, so CI runs the same sequences every time.
RUN_FUZZ_SEEDS = [("minified-linux", False, 1), ("minified-linux", True, 2), ("hello", False, 3), ("hello", True, 4),
                  ("minified-linux", False, 5), ("minified-linux", True, 6)]


def runto_during_ff_case(browser, base, shots):
    """Run to start_kernel pressed twice at once (one task) while the boot
    fast-forwards to the entry at 1/s: the fast-forwards must run one at a
    time on the machine and it must stop at start_kernel."""
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=1#debugger")
    wait_for(lambda: page.evaluate("typeof ffActive !== 'undefined' && ffActive"), 60, 0.01)
    page.evaluate("""() => {
        document.getElementById('dbg_addr').value = 'start_kernel';
        const b = document.getElementById('dbg_runto');
        b.click();
        b.click();
    }""")
    problems = []
    done = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.evaluate("!ffActive && runTo === null")
                    and not page.inner_text("#dbg_status").startswith(("Running to", "Finding")), 120)
    s = page.evaluate(RUN_STATE_JS)
    sym = page.evaluate("stepper.symName[stepper.symbolOf(stepper.nextPhys())] || ''")
    if not done or sym != "start_kernel":
        problems.append({"not stopped at start_kernel": s, "at": sym})
    bad = run_state_problem(page)
    if bad:
        problems.append(bad)
    if errors:
        problems.append({"page errors": errors})
    page.screenshot(path=str(shots / "runto-during-ff.png"))
    page.close()
    return problems, {"at": sym, "note": s["note"][:100]}


def suite_speedline(ctx):
    failed = False
    for touch, rate in ((False, "1"), (True, "1"), (True, "10")):
        t0 = time.monotonic()
        p, r = speed_line_case(ctx.browser, ctx.base, ctx.shots, touch, rate)
        failed |= report("speed_line", p, r, touch=touch, rate=rate, seconds=round(time.monotonic() - t0))
    return failed


def suite_speedasm(ctx):
    failed = False
    for kernel, tab, throttle, rate in [c for c in SPEED_LINE_ASM if c[0] in ctx.entries]:
        t0 = time.monotonic()
        p, r = speed_line_asm_case(ctx.browser, ctx.base, ctx.shots, kernel, tab, throttle, rate)
        failed |= report("speed_line_asm", p, r, kernel=kernel, tab=tab, throttle=throttle, rate=rate,
                         seconds=round(time.monotonic() - t0))
    return failed


def suite_runstate(ctx):
    failed = False
    if "minified-linux" in ctx.entries:
        for touch in (False, True):
            failed |= report("double_boot", double_boot_case(ctx.browser, ctx.base, ctx.shots, touch), touch=touch)
        failed |= report("runto_during_ff", *runto_during_ff_case(ctx.browser, ctx.base, ctx.shots))
    seeds = [s for s in RUN_FUZZ_SEEDS if s[0] in ctx.entries]
    if ctx.args.fuzz_seeds:
        n = ctx.args.fuzz_seeds
        seeds = [(k, t, s) for s in range(ctx.args.fuzz_from, ctx.args.fuzz_from + n)
                 for k, t in [("minified-linux", False), ("minified-linux", True), ("hello", False), ("hello", True)]]
    for kernel, touch, seed in seeds:
        t0 = time.monotonic()
        p, r = run_fuzz_case(ctx.browser, ctx.base, ctx.shots, kernel, touch, seed, ctx.args.fuzz_actions,
                             verbose=bool(ctx.args.fuzz_seeds))
        failed |= report("run_fuzz", p, r, kernel=kernel, touch=touch, seed=seed, seconds=round(time.monotonic() - t0))
    return failed


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
    "runto_phone": (suite_runto_phone, False),
    "initramfs": (suite_initramfs, False),
    "tasks": (suite_tasks, False),
    "tasks_minified": (suite_tasks_minified, True),
    "coverage": (suite_coverage, True),
    "covtree": (suite_covtree, True),
    "unwind": (suite_unwind, True),
    "operands": (suite_operands, True),
    "lineaddr": (suite_lineaddr, True),
    "fnfacts": (suite_fnfacts, True),
    "branches": (suite_branches, True),
    "touch": (suite_touch, False),
    "hello": (suite_hello, False),
    "firstlook": (suite_firstlook, False),
    "srclinks": (suite_srclinks, True),
    "unoptimized": (suite_unoptimized, True),
    "jumps": (suite_jumps, False),
    "runstate": (suite_runstate, False),
    "speedline": (suite_speedline, True),
    "speedasm": (suite_speedasm, True),
    "navhist": (suite_navhist, False),
    "tabs": (suite_tabs, False),
    "pwa": (suite_pwa, False),
    "followeip": (suite_followeip, False),
    "followfuzz": (suite_followfuzz, False),
    "nopath": (suite_nopath, False),
    "tipmarks": (suite_tipmarks, False),
    "legend": (suite_legend, True),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cases", nargs="*", help="NAME or NAME:MEMBYTES (only the boot suite then runs)")
    ap.add_argument("--suites", help="comma-separated, of: " + ", ".join(SUITES))
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--probe", action="store_true", help="report, do not assert")
    ap.add_argument("--shots", default=str(SITE / "test-screenshots"))
    ap.add_argument("--fuzz-seeds", type=int, default=0, help="runstate, followfuzz: this many seeds instead of the fixed ones")
    ap.add_argument("--fuzz-from", type=int, default=100, help="runstate, followfuzz: first seed with --fuzz-seeds")
    ap.add_argument("--fuzz-actions", type=int, default=30, help="runstate, followfuzz: actions per seed")
    ap.add_argument("--base", help="test this deployed site (e.g. https://d33.pl/minimal-kernel-build) instead of serving site/")
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

    httpd = None if args.base else serve()
    entries = kernel_entries()
    ctx = argparse.Namespace(args=args, cases=cases or default_cases(), shots=shots, entries=entries,
                             base=args.base.rstrip("/") if args.base else f"http://127.0.0.1:{httpd.server_address[1]}")
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
    if httpd:
        httpd.shutdown()
    if failed and not args.probe:
        print("FAIL", file=sys.stderr)
        return 1
    print("done" if args.probe else "PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
