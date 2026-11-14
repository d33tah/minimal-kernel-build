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
import datetime
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
import traceback

from playwright.sync_api import Error as PlaywrightError, sync_playwright

import jscov
from test_dwarf import dwarfdump_cfi, write_core

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


def shown_tab(page):
    return page.get_attribute(".tabbar [role=tab][aria-selected=true]", "data-tab")


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
    machine_do(page, "select_option", "#dbg_speed", rate)
    show_tab(page, "memory")
    machine_do(page, "click", "#dbg_continue")
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
        machine_do(page, "click", "#dbg_pause")
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
    machine_do(page, "select_option", "#dbg_speed", "full")
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


def timeline_pan(page, problems, r):
    """A drag on the timeline moves its time window by the dragged share of
    it (the span stays) and stops following the run; a drag in the minimap
    centres the window where the pointer is."""
    show_canvas(page)
    if not page.is_checked("#graph_follow"):
        page.check("#graph_follow")
    box = page.locator("#graph_canvas").bounding_box()
    x0, x1, w = page.evaluate("[G.x0, G.x1, document.getElementById('graph_canvas').clientWidth]")
    cx, cy = box["x"] + box["width"] / 2, box["y"] + 120
    page.mouse.move(cx, cy)
    page.mouse.down()
    page.mouse.move(cx - 200, cy, steps=5)
    page.mouse.up()
    a0, a1, follow = page.evaluate("[G.x0, G.x1, G.follow || document.getElementById('graph_follow').checked]")
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
    mid, want_mid, full = page.evaluate("""() => { const t0 = runStart(), t1 = Math.max(t0 + 1, recTo(G.stepper));
        return [(G.x0 + G.x1) / 2, t0 + (t1 - t0) / 2, t1 - t0]; }""")
    r["minimap"] = [round(mid), round(want_mid)]
    if abs(mid - want_mid) > 0.01 * full:
        problems.append(f"a drag to the middle of the minimap centred the window at {mid:.0f}, want {want_mid:.0f}")


def timeline_views(page, problems, shots, tag, r):
    """Focus, sandwich, search, depth cap, transposed and flame views."""
    timeline_pan(page, problems, r)
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
    machine_do(page, "click", "#dbg_pause") if page.is_enabled("#dbg_pause") else None
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
        machine_do(page, "click", "#vm_continue")
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
        # U5: the screen lives in the Machine tab; after another tab and back
        # the keys typed on the screen still go to the VM.
        show_tab(page, "page")
        time.sleep(0.5)
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.5)
        page.keyboard.type("echo u5back")
        page.keyboard.press("Enter")
        if not wait_for(lambda: "u5back" in lines(), 60):
            problems.append("after the Page tab and back to Machine the shell did not run a typed command")
        r["keys_after_tabs"] = "u5back" in lines()
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
    if page.is_enabled("#dbg_pause"):
        machine_do(page, "click", "#dbg_pause")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    cpu_state_tools(page, sk, problems, r)
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


# Hand-made PAE tables in the top 64 KiB of RAM (saved and put back by the
# caller): PDPT, PD and PT 4 KiB apart. Linear 0x40000000-0x7fffffff (PDPTE
# 1) -> the PD; its PDE 0 -> the PT, whose PTE 5 maps 0x40005000 -> 0x123000;
# its PDE 1 is a 2 MiB page 0x40200000 -> 0x200000. PDPTE 2 is not present.
PAE_JS = """(on) => {
    const c = stepper.cpu, m = c.mem8, dv = new DataView(m.buffer, m.byteOffset, m.length);
    const S = (m.length - 0x10000) & ~0xfff;
    if (!on) {
        m.set(window.__paeSaved.bytes, S);
        c.cr[3] = window.__paeSaved.cr3;
        c.cr[4] = window.__paeSaved.cr4;
        return null;
    }
    window.__paeSaved = { bytes: m.slice(S, S + 0x3000), cr3: c.cr[3], cr4: c.cr[4] };
    m.fill(0, S, S + 0x3000);
    const q = (a, lo) => { dv.setUint32(a, lo >>> 0, true); dv.setUint32(a + 4, 0, true); };
    q(S + 1 * 8, (S + 0x1000) | 1);
    q(S + 0x1000, (S + 0x2000) | 3);
    q(S + 0x1000 + 8, 0x200000 | 0x83);
    q(S + 0x2000 + 5 * 8, 0x123000 | 3);
    c.cr[3] = S;
    c.cr[4] = c.cr[4] | 0x20;
    return S;
}"""


def cpu_state_tools(page, sk, problems, r):
    """The Debugger's paging and register tools on a paused machine: the
    translate box takes a symbol name and refuses junk, the page directory
    overview's run holding start_kernel maps it where translate does, a
    hole in it translates to a page fault; PAE (no shipped kernel turns it
    on) on hand-made tables: a 4 KiB and a 2 MiB page and a missing PDPTE;
    the GDT and IDT views say "not mapped" for a table at an unmapped
    address; MMX and SSE registers show once they hold a value, and a
    changed row is marked."""
    def xlate(text):
        page.fill("#xlate_in", text)
        page.click("#xlate_go")
        return page.inner_text("#xlate_out")
    want = f"physical 0x{sk['paddr']:08x}"
    out = xlate("start_kernel")
    r["translate_name"] = out.splitlines()[-2:]
    if want not in out:
        problems.append(f"translate start_kernel (by name) did not give {want}: {out!r}")
    out = xlate("no_such_symbol_xyz")
    if out != "Give a hex address or a symbol name.":
        problems.append(f"translate of an unknown name: {out!r}")
    page.click("#pd_view")
    pd = page.inner_text("#xlate_out").splitlines()
    r["pd_lines"] = len(pd)
    runs = []
    for line in pd:
        m = re.match(r"0x([0-9a-f]{8})-0x([0-9a-f]{8}) (?:(\d+) PDEs, )?(4 MiB pages|page tables) from 0x([0-9a-f]{8}) flags 0x([0-9a-f]{2})$", line)
        if not m:
            problems.append(f"page directory line not understood: {line!r}")
            continue
        lo, hi, n = int(m.group(1), 16), int(m.group(2), 16), int(m.group(3) or 1)
        if hi + 1 - lo != n << 22:
            problems.append(f"page directory line: {n} PDEs do not span {line!r}")
        runs.append((lo, hi, m.group(4), int(m.group(5), 16)))
    hit = [x for x in runs if x[0] <= sk["vaddr"] <= x[1]]
    r["pd_start_kernel"] = hit
    if len(hit) != 1:
        problems.append(f"no single page directory run holds start_kernel 0x{sk['vaddr']:08x}: {hit} of {len(runs)}")
    elif hit[0][2] == "4 MiB pages" and hit[0][3] + sk["vaddr"] - hit[0][0] != sk["paddr"]:
        problems.append(f"the page directory maps start_kernel to 0x{hit[0][3] + sk['vaddr'] - hit[0][0]:08x}, translate to 0x{sk['paddr']:08x}")
    hole = next((a for a in range(0, 1 << 32, 1 << 22) if not any(x[0] <= a <= x[1] for x in runs)), None)
    if hole is None:
        problems.append("the page directory has no hole to translate")
    else:
        out = xlate(hex(hole + 0x1234))
        r["hole"] = out.splitlines()[-1]
        if not out.endswith("not mapped (page fault)"):
            problems.append(f"translate 0x{hole + 0x1234:08x} (no PDE in the overview): {out!r}")
    # PAE.
    page.evaluate(PAE_JS, True)
    try:
        pae = {a: xlate(a) for a in ("0x40005abc", "0x40212345", "0x80000000")}
        page.click("#pd_view")
        pae["pd"] = page.inner_text("#xlate_out")
    finally:
        page.evaluate(PAE_JS, False)
    r["pae"] = {k: v.splitlines()[-1] for k, v in pae.items()}
    if not pae["0x40005abc"].endswith("physical 0x00123abc (4 KiB page)") or pae["0x40005abc"].count(" at ") != 3:
        problems.append(f"PAE: 0x40005abc through PDPTE, PDE, PTE to 0x00123abc: {pae['0x40005abc']!r}")
    if not pae["0x40212345"].endswith("physical 0x00212345 (2 MiB page)"):
        problems.append(f"PAE: 0x40212345 in a 2 MiB page at 0x200000: {pae['0x40212345']!r}")
    if not pae["0x80000000"].endswith("not mapped (page fault)"):
        problems.append(f"PAE: 0x80000000 (PDPTE 2 not present): {pae['0x80000000']!r}")
    pdl = pae["pd"].splitlines()
    if len(pdl) != 4 or "→ PD" not in pdl[1] or not pdl[2].endswith("not present"):
        problems.append(f"PAE page directory: the four PDPTEs, 1 present, 2 not: {pdl}")
    if xlate(hex(sk["vaddr"])).find(want) < 0:
        problems.append("translate after the PAE test: start_kernel no longer maps (CR3/CR4 not restored?)")
    # GDT and IDT at an unmapped linear address.
    if hole is not None:
        saved = page.evaluate(f"""() => {{ const c = stepper.cpu, s = [c.gdtr_offset[0], c.idtr_offset[0]];
            c.gdtr_offset[0] = c.idtr_offset[0] = {hole}; return s; }}""")
        try:
            page.click("#view_gdt")
            gdt = page.inner_text("#cpu_table").splitlines()
            page.click("#view_idt")
            idt = page.inner_text("#cpu_table").splitlines()
        finally:
            page.evaluate("(s) => { stepper.cpu.gdtr_offset[0] = s[0]; stepper.cpu.idtr_offset[0] = s[1]; }", saved)
        r["gdt_unmapped"], r["idt_unmapped"] = gdt[:1], idt[:1]
        if not gdt or not all(l.endswith(" not mapped") for l in gdt):
            problems.append(f"GDT at an unmapped address: {gdt[:3]}")
        if not idt or not all(l.endswith(" not mapped") for l in idt):
            problems.append(f"IDT at an unmapped address: {idt[:3]}")
    # MMX and SSE registers: shown once they hold a value; a changed row marked.
    fpu = lambda: page.evaluate("""() => { renderCpuState(); return [...document.querySelectorAll('#cpu_fpu [data-reg]')]
        .map((e) => [e.dataset.reg, e.textContent, e.classList.contains('chg')]); }""")
    before = fpu()
    saved = page.evaluate("""() => { const c = stepper.cpu, s = [Array.from(c.fpu_st), Array.from(c.reg_xmm32s), c.fpu_stack_empty[0]];
        c.fpu_st[4] = 0x12345678; c.reg_xmm32s[0] = 0x3f800000; return s; }""")
    try:
        mmx = fpu()
        page.evaluate("stepper.cpu.fpu_stack_empty[0] = stepper.cpu.fpu_stack_empty[0] & 0xfe")
        st = fpu()
    finally:
        page.evaluate("""(s) => { const c = stepper.cpu; c.fpu_st.set(s[0]); c.reg_xmm32s.set(s[1]); c.fpu_stack_empty[0] = s[2];
            renderCpuState(); }""", saved)
    names = lambda rows: [x[0] for x in rows]
    row = lambda rows, n: next((x for x in rows if x[0] == n), None)
    r["fpu_rows"] = [len(before), len(mmx), len(st)]
    if "MM1" in names(before) or "XMM0" in names(before):
        problems.append(f"MMX/SSE rows shown while unused: {names(before)}")
    if not row(mmx, "MM1") or "12345678" not in row(mmx, "MM1")[1] or "ST0" in names(mmx):
        problems.append(f"MM1 = 0x12345678 not shown (or ST rows while the stack is empty): {names(mmx)} {row(mmx, 'MM1')}")
    if not row(mmx, "XMM0") or not row(mmx, "XMM0")[1].rstrip().endswith("3f800000"):
        problems.append(f"XMM0 = 0x3f800000 not shown: {row(mmx, 'XMM0')}")
    if "ST0" not in names(st) or not row(st, "FTW") or not row(st, "FTW")[2]:
        problems.append(f"ST0 in use: ST rows, FTW marked changed: {names(st)[:12]} {row(st, 'FTW')}")


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
    machine_do(page, "select_option", "#dbg_speed", speed)
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
                # Set even while another tab hides the Machine tab's screen (U5).
                "overlay": page.inner_text("#vm_overlay") if not page.evaluate("document.getElementById('vm_overlay').hidden") else "",
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
    function (macros and multi-line calls do not; judged from 3 call lines on: in clang 22.1.0's -O0
    minified Linux 260 of 459 inlined call sites name the callee, so one or two can all be macros)."""
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
    if len(calls) >= 3 and named * 2 < len(calls):
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
    # Parameters and locals decoded in the browser from the imported DWARF
    # equal the shipped ones (gen-dwarf.js on the same ELF) at the same count.
    at = page.evaluate("stepper.steps")
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
    imported = locals_open_all(page)
    ship = browser.new_page(viewport={"width": 1280, "height": 1000})
    ship.on("pageerror", lambda e: errors.append(str(e)))
    ship.goto(url)
    wait_for(lambda: ship.get_attribute("#dbg_state", "data-state") == "paused", 60)
    ship.fill("#dbg_addr", hex(sk["vaddr"]))
    ship.click("#dbg_runto")
    wait_for(lambda: ship.get_attribute("#dbg_state", "data-state") == "paused"
             and ship.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120)
    ship.fill("#dbg_n", str(at - ship.evaluate("stepper.steps")))
    ship.click("#dbg_stepn")
    wait_for(lambda: ship.evaluate("stepper.steps") == at and ship.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: ship.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
    shipped = locals_open_all(ship)
    ship.close()
    nvars = sum(len(sc["vars"]) for f in imported for sc in f["scopes"])
    r["imported_locals"] = f"{len(imported)} frames, {nvars} variables, equal to the shipped: {imported == shipped}"
    if imported != shipped or not nvars:
        diff = next(((a, b) for a, b in zip(imported, shipped) if a != b), (imported[len(shipped):], shipped[len(imported):]))
        problems.append(f"imported -g vmlinux: parameters and locals at {at} differ from the shipped DWARF's: {str(diff)[:400]}")
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
    "minified-linux": ["init_task.comm", "jiffies_64", "init_task.pid", "current_task", "current_task->comm",
                       "init_task.comm[1]"],
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
    # What a watch cannot read says why, in its row.
    bad = {"init_task.nosuch": "struct task_struct has no member nosuch",
           "jiffies_64->x": "not a pointer",
           "init_task.comm!": "cannot read “!” (use .member, ->member, [index])",
           "init_task.pid.x": "pid_t has no member x"}
    r["watch errors"] = {}
    for e, want in bad.items():
        got = watch(page, e)
        state = page.get_attribute(f'#watches .watch[data-expr="{e}"]', "data-state")
        r["watch errors"][e] = got[:100]
        if state != "error" or got != f"{e}: {want}":
            problems.append(f"{name}: watch {e}: {state} {got!r}, want the error {want!r}")
        page.locator(f'#watches .watch[data-expr="{e}"] .wdel').click()
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
    machine_do(page, "click", "#dbg_pause")
    info = page.evaluate("ffInfo && { stop: ffInfo.stopPhys, steps: ffInfo.steps, ms: Math.round(ffInfo.ms) }")
    r["ff"] = info
    if not ok or not info or info["stop"] != st32["paddr"] or r["ff_seconds"] > 120:
        problems.append(f"fast-forward to startup_32 {st32['paddr']:#x}: {info}, {r['ff_seconds']} s")
    # D1: the guest's TSC runs with the virtual clock (v86: TSC = clock x
    # 10^6 per ms - offset, offset 0 from power-on). The fast-forward ends by
    # restoring the slice's snapshot; restore_state sets the offset from the
    # clock it reads, and the clock used to be put back only afterwards, so
    # the TSC lagged by however far the JIT ran past the slice start (in
    # Firefox and WebKit by a varying number of chunks, D0).
    r["tsc_minus_clock"] = page.evaluate("""() => { const c = emulator.v86.cpu; c.store_current_tsc();
      return c.current_tsc[0] + c.current_tsc[1] * 2 ** 32 - Math.round(CLOCK.now() * 1e6); }""")
    if abs(r["tsc_minus_clock"]) > 1_000_000:
        problems.append(f"after the fast-forward the TSC is off the virtual clock by {r['tsc_minus_clock']} ticks (1 ms = 10^6)")
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
    """srclink_open_here in the tab that holds sel (U5: the state line and
    the "VM paused" overlay are in the Machine tab), then back to the tab
    shown before."""
    tab = page.evaluate("""(s) => { const e = document.querySelector(s.split(' >> ')[0]);
        const p = e && e.closest('[role=tabpanel]'); return p && p.hidden ? p.id.slice(6) : null; }""", sel)
    if not tab:
        return srclink_open_here(page, sel, how)
    back = shown_tab(page)
    show_tab(page, tab)
    try:
        return srclink_open_here(page, sel, how)
    finally:
        show_tab(page, back)


def srclink_open_here(page, sel, how="click"):
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
        # The state line is in the Machine tab (U5), Step N in the Debugger tab.
        show_tab(ph, "machine")
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
        show_tab(ph, "debugger")
        ph.fill("#dbg_n", "5")
        ph.tap("#dbg_stepn")
        wait_for(lambda: ph.evaluate("stepper.steps") > before and ph.get_attribute("#dbg_state", "data-state") == "paused", 30)
        wait_for(lambda: ph.locator("#dbg_state .srclink").count() > 0, 20)
    else:
        problems.append(f"{name} phone: no stop in a function with a name of 20 or more characters (longest {longest!r})")
    if ph.is_visible("#src_panel"):
        ph.click("#src_close")
    show_tab(ph, "debugger")
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
    for t, sel in (("memory", "#panel_memory .tip"), ("machine", "#count_info .tip")):
        show_tab(page, t)
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
    # A touch swipe outside the graph scrolls the page. Since U5 the Memory
    # tab of a 2 MB machine fits a 390x844 screen: swipe on the Machine tab,
    # taller than the screen (screen, counters, boot controls).
    show_tab(page, "machine")
    page.evaluate("window.scrollTo(0, 0)")
    if page.evaluate("document.documentElement.scrollHeight") < 844 + 150:
        problems.append(f"the Machine tab is {page.evaluate('document.documentElement.scrollHeight')} px: nothing to swipe")
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


def fetch_fail_case(browser, base, shots):
    """A kernel or its initrd the server does not have (404) is named with
    the HTTP status, and an error nothing else catches shows in the status
    line with the kernel and RAM (hello at 1280 light, service worker off
    so the server's answer reaches the page)."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    ctx.add_init_script("if (navigator.serviceWorker) navigator.serviceWorker.register = () => Promise.reject(new DOMException('not allowed by the test', 'SecurityError'));")
    errors = []
    ctx.on("weberror", lambda e: errors.append(str(e.error)[:200]))
    ctx.route("**/kernels/hello/bzImage*", lambda route: route.fulfill(status=404, body="no"))
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=hello")
        wait_for(lambda: "HTTP 404" in (page.text_content("#status") or ""), 30)
        r["kernel 404"] = page.text_content("#status")
        if "kernels/hello/bzImage: HTTP 404" not in r["kernel 404"]:
            problems.append(f"hello's bzImage answered 404: status {r['kernel 404']!r}")
        page.screenshot(path=str(shots / "fetch-fail-1280-light.png"))
        initrds = [n for n, e in kernel_entries().items() if "initrd" in e]
        if initrds:
            ctx.route("**/" + kernel_entries()[initrds[0]]["initrd"] + "*", lambda route: route.fulfill(status=404, body="no"))
            page.goto(f"{base}/index.html?kernel={initrds[0]}")
            wait_for(lambda: "HTTP 404" in (page.text_content("#status") or ""), 60)
            r["initrd 404"] = page.text_content("#status")
            if kernel_entries()[initrds[0]]["initrd"] + ": HTTP 404" not in r["initrd 404"]:
                problems.append(f"{initrds[0]}'s initrd answered 404: status {r['initrd 404']!r}")
        ctx.unroute("**/kernels/hello/bzImage*")
        page.goto(f"{base}/index.html?kernel=hello&paused=1")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.evaluate("setTimeout(() => Promise.reject(new Error('nobody catches this')), 0)")
        wait_for(lambda: "nobody catches this" in (page.text_content("#status") or ""), 10)
        r["uncaught"] = page.text_content("#status")
        if not re.match(r"Emulator error: Error: nobody catches this \(hello, 2,048 KiB RAM\)", r["uncaught"] or ""):
            problems.append(f"an uncaught rejection: status {r['uncaught']!r}")
        other = [e for e in errors if "nobody catches this" not in e]
        if other:
            problems.append(f"page errors besides the test's own: {other[:3]}")
    finally:
        ctx.close()
    return problems, r


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
    failed |= report("fetch_fail", *fetch_fail_case(ctx.browser, ctx.base, ctx.shots))
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
             ("#context", "src", "*"), ("#panel_memory", "memeip", "*"), ("#panel_memory", "memesp", "*"),
             ("#panel_memory", "memtag tipmark", "*"), ("#panel_memory", "memtag esp tipmark", "*"), ("#panel_memory", "eiptick", "*")]
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
        nav_press(page, "#nav_back")
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
    machine_do(page, "select_option", "#dbg_speed", "1000")
    machine_do(page, "click", "#dbg_continue")
    got = None
    end = time.monotonic() + 30
    while got is None and time.monotonic() < end:
        got = page.evaluate("""() => { const e = document.querySelector('#listing .ins:not(.bad) .difb, #listing .hex[data-tip]');
          return e ? { tip: e.dataset.tip || '', line: e.closest('[data-addr]').className, marks: e.querySelectorAll('mark').length } : null; }""")
        time.sleep(0.05)
    if page.is_enabled("#dbg_pause"):
        machine_do(page, "click", "#dbg_pause")
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
    // H1: the menu toggle (closed) where the Back and Forward box was.
    const nav = document.getElementById('runbadge_toggle') || document.getElementById('nav_hist');
    if (!nav || nav.hidden || !nav.getClientRects().length) return { hidden: true, hits: [] };
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
        // The words of each line (H1: white space a pre-wrap line hangs past
        // its box's padding at a wrap is no text to cover).
        const words = (el) => {
            const out = [], tw = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
            for (let n = tw.nextNode(); n; n = tw.nextNode())
                for (const m of n.data.matchAll(/\\S+/g)) {
                    const rg = document.createRange();
                    rg.setStart(n, m.index);
                    rg.setEnd(n, m.index + m[0].length);
                    out.push(...rg.getClientRects());
                }
            return out;
        };
        for (const el of box.querySelectorAll('.ins, .src')) {
            for (const r of words(el)) {
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


def nav_cover(page, where, problems, r, need_lines=False, scheme_after="light"):
    """Light and dark: the Back and Forward box (H1: the menu toggle)
    covers no listing text on screen (N2); need_lines: some listing line
    must be on screen."""
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        got = page.evaluate(NAV_COVER_JS)
        r.setdefault("covers", {})[f"{where} {scheme}"] = got
        if need_lines and not got.get("seen"):
            problems.append(f"{where} {scheme}: no listing line on screen to check")
        if got["hits"]:
            problems.append(f"{where} {scheme}: the Back and Forward box covers {got['hits']} (box {got.get('nav')}, panel {got.get('panel')})")
    page.emulate_media(color_scheme=scheme_after)


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
    if page.is_enabled("#nav_back"):
        problems.append(f"{name}: Back is enabled before any jump (Run to and stepping are no jumps)")
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
    pos = page.evaluate("""() => { const b = document.getElementById('runbadge_toggle').getBoundingClientRect();
        return { pos: getComputedStyle(document.getElementById('runbadge')).position, top: b.top, bottom: b.bottom, left: b.left, right: b.right,
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
            nav_press(page, "#nav_back")
        elif way == "enter":
            nav_menu(page)
            page.focus("#nav_back")
            page.keyboard.press("Enter")
        elif way == "alt":
            page.focus("#runbadge_toggle")  # the focus is on the page, not the VM screen; the menu need not be open
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
            nav_press(page, "#nav_fwd")
        elif way == "alt":
            page.focus("#runbadge_toggle")
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
        machine_do(page, "click", "#dbg_step")
        time.sleep(0.2)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    time.sleep(0.5)
    r["stepping"] = f"page {p0} -> {page.get_attribute('#listing', 'data-page')}, {page.evaluate('stepper.steps')} steps"
    if nav_at(page) != before or page.evaluate("history.length") != hl:
        problems.append(f"{name}: stepping added history entries: {before} -> {nav_at(page)}, history.length {hl} -> {page.evaluate('history.length')}")

    # Alt+Left after a click on the VM screen (in the Machine tab) is the VM's key.
    show_tab(page, "machine")
    page.click("#screen_container")
    page.keyboard.press("Alt+ArrowLeft")
    time.sleep(0.5)
    if nav_at(page) != before:
        problems.append(f"{name}: Alt+Left went back with the VM screen focused: {before} -> {nav_at(page)}")

    # The VM runs on through the browser's back and forward, and back past
    # the first view stays on the page.
    page.evaluate("window.mkbEmu = emulator")
    machine_do(page, "select_option", "#dbg_speed", "10")  # hello_main ends in a few hundred instructions
    machine_do(page, "click", "#dbg_continue")
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
        machine_do(page, "click", "#dbg_pause")
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
    geo = page.evaluate("""() => { const b = document.getElementById('runbadge_toggle').getBoundingClientRect(),
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
        nav_press(page, "#nav_back", "tap")
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


# ---- runbadge: the menu toggle with the run state (H1) --------------------
# The operator: "back next niech będzie za hamburger toggle, który pokazuje
# też wizualnie czy vm pracuje i z jaką prędkością". One small fixed toggle
# (#runbadge_toggle, bottom right) replaces the Back and Forward box and the
# run status next to the tab bar (U5). Its face says at a glance what the VM
# does: a dot that pulses while the CPU runs (static under
# prefers-reduced-motion), static when paused, its own colour at the end
# (halted) and at a panic, and a short word: the speed while running (1/s,
# 1k/s, full), else paused / halted / panic. A click or tap opens a menu
# with Back and Forward (#nav_back, #nav_fwd, as before), the run state in
# words with the instruction count (a click opens Machine), Pause/Continue
# and the "new version" notice of the PWA. Esc and a tap outside close it.

# The toggle's face for each state (data-state of #runbadge).
RUNBADGE_FACE = {"paused": "paused", "halted": "halted", "panic": "panic", "off": "off"}
RUNBADGE_SPEED = {"1": "1/s", "10": "10/s", "1000": "1k/s", "full": "full"}
RUNBADGE_JS = """() => { const w = document.getElementById('runbadge'), t = document.getElementById('runbadge_toggle'),
  m = document.getElementById('runbadge_menu'), st = document.getElementById('dbg_state'), e = document.getElementById('end_banner');
  if (!w || !t || !m) return null;
  const r = t.getBoundingClientRect(), dot = t.querySelector('.rbdot'), cs = dot ? getComputedStyle(dot) : null;
  const words = document.getElementById('runbadge_state');
  return { state: w.dataset.state || '', vm: w.dataset.vm || '', steps: Number(w.dataset.steps || -1),
    face: t.innerText.replace(/\\s+/g, ' ').trim(), label: t.getAttribute('aria-label') || '', tip: t.title || '',
    expanded: t.getAttribute('aria-expanded'), controls: t.getAttribute('aria-controls'),
    open: !m.hidden && m.getClientRects().length > 0, words: words ? words.textContent.trim() : '',
    pos: getComputedStyle(w).position, rect: [r.left, r.top, r.right, r.bottom].map(Math.round), vw: innerWidth, vh: innerHeight,
    anim: cs ? cs.animationName : '', dot: cs ? cs.backgroundColor + ' ' + cs.borderColor : '',
    vmState: st ? st.dataset.state || '' : '', end: e ? e.dataset.kind || '' : '',
    vmSteps: typeof stepper !== 'undefined' && stepper ? stepper.steps : -1,
    focused: document.activeElement === t, hscroll: document.documentElement.scrollWidth > innerWidth + 1,
    newer: w.dataset.newer || '', runMini: !!document.getElementById('run_mini') }; }"""


def runbadge(page):
    return page.evaluate(RUNBADGE_JS)


def runbadge_want(b):
    """The badge state the VM's state asks for."""
    if b["vmState"] == "ff":
        return "ff"
    if b["vmState"] in ("stepped", "jit"):
        return "run"
    if b["vmState"] == "paused":
        return "panic" if b["end"].startswith("panic") else "halted" if b["end"] == "halt" else "paused"
    return "off"


def runbadge_check(page, tag, problems, r, want, speed=None, moving=True, reduced=False):
    """The toggle shows want (and the speed's short text while running);
    it matches the VM; it is fixed in the bottom right corner, at least
    40 px square, clear of the page width; the dot pulses only while
    running and never under reduced motion."""
    b = runbadge(page)
    r[tag] = b and {k: b[k] for k in ("state", "vm", "steps", "face", "anim", "rect", "dot")}
    if not b:
        problems.append(f"{tag}: no menu toggle (#runbadge, #runbadge_toggle, #runbadge_menu)")
        return None
    if b["state"] != want or runbadge_want(b) != want:
        problems.append(f"{tag}: the toggle says {b['state']!r} ({b['face']!r}), the VM {b['vmState']!r} end {b['end']!r}; want {want!r}")
    face = RUNBADGE_SPEED[speed] if want == "run" and speed else RUNBADGE_FACE.get(want)
    if face and face not in b["face"]:
        problems.append(f"{tag}: the toggle's face {b['face']!r} does not say {face!r}")
    if want not in b["label"] and (face or "") not in b["label"]:
        problems.append(f"{tag}: the toggle's accessible name {b['label']!r} does not say {want!r}")
    x0, y0, x1, y1 = b["rect"]
    if b["pos"] != "fixed" or x1 > b["vw"] or y1 > b["vh"] or x0 < b["vw"] / 2 or y0 < b["vh"] / 2:
        problems.append(f"{tag}: the toggle is not fixed in the bottom right corner: {b['pos']} {b['rect']} in {b['vw']}x{b['vh']}")
    if x1 - x0 < 40 or y1 - y0 < 40 or x1 - x0 > 64:
        problems.append(f"{tag}: the toggle is {x1 - x0}x{y1 - y0} px (want 40 to 64 wide, 40 high or more)")
    if b["hscroll"]:
        problems.append(f"{tag}: horizontal scroll")
    if b["runMini"]:
        problems.append(f"{tag}: the run status next to the tab bar (#run_mini) is still there")
    pulse = want in ("run", "ff") and not reduced
    if pulse != (b["anim"] not in ("", "none")):
        problems.append(f"{tag}: the dot's animation is {b['anim']!r} in state {want!r}{' with reduced motion' if reduced else ''}")
    if moving and want in ("run", "ff"):
        s0 = b["steps"]
        time.sleep(2)
        b2 = runbadge(page)
        if b2["steps"] <= s0 or abs(b2["steps"] - b2["vmSteps"]) > max(5000, b2["vmSteps"] // 50):
            problems.append(f"{tag}: the toggle's count does not follow the VM: {s0} -> {b2['steps']}, VM {b2['vmSteps']}")
    return b


def runbadge_open(page, phone):
    t = page.locator("#runbadge_toggle")
    if t.get_attribute("aria-expanded") != "true":
        t.tap() if phone else t.click()
        wait_for(lambda: t.get_attribute("aria-expanded") == "true", 3)


def runbadge_menu(page, tag, phone, problems, r, shots, pause_word):
    """The menu: opens on a click or tap, holds Back and Forward (disabled
    with no jumps, with tooltips), the state in words with the count, and
    Pause/Continue; Esc closes it and gives the focus back to the toggle; a
    tap outside closes it."""
    runbadge_open(page, phone)
    b = runbadge(page)
    if not b or not b["open"] or b["expanded"] != "true" or b["controls"] != "runbadge_menu":
        problems.append(f"{tag}: the toggle does not open its menu: {b and (b['open'], b['expanded'], b['controls'])}")
        return
    inside = page.evaluate("""() => ['nav_back', 'nav_fwd', 'runbadge_pause', 'runbadge_state'].map((id) => {
        const e = document.getElementById(id); return [id, !!e && !!e.closest('#runbadge_menu') && e.getClientRects().length > 0,
        e ? e.title || '' : '', e ? e.disabled : null, e ? e.textContent.trim() : '']; })""")
    r[f"{tag} menu"] = inside
    for id_, shown, title, disabled, text in inside:
        if not shown:
            problems.append(f"{tag}: #{id_} is not shown in the open menu")
    nb, nf = inside[0], inside[1]
    if not nb[3] or not nf[3] or "no earlier view" not in nb[2]:
        problems.append(f"{tag}: Back and Forward before any jump: {nb}, {nf} (want disabled, the tooltip saying so)")
    if inside[2][4] != pause_word:
        problems.append(f"{tag}: the menu's run button says {inside[2][4]!r}, want {pause_word!r}")
    if b["state"] not in b["words"] and RUNBADGE_FACE.get(b["state"], "running") not in b["words"] or "instructions" not in b["words"]:
        problems.append(f"{tag}: the menu's state in words {b['words']!r} lacks the state or the count")
    m = page.evaluate("""() => { const r = document.getElementById('runbadge_menu').getBoundingClientRect();
        return [r.left, r.top, r.right, r.bottom].map(Math.round); }""")
    if m[0] < 0 or m[1] < 0 or m[2] > b["vw"] or m[3] > b["vh"]:
        problems.append(f"{tag}: the open menu leaves the screen: {m}")
    page.screenshot(path=str(shots / f"runbadge-{tag}-menu.png"))
    if phone:
        page.tap("header.top h1")
    else:
        page.keyboard.press("Escape")
    if not wait_for(lambda: page.get_attribute("#runbadge_toggle", "aria-expanded") == "false", 3) or runbadge(page)["open"]:
        problems.append(f"{tag}: {'a tap outside' if phone else 'Esc'} does not close the menu")
    elif not phone and not runbadge(page)["focused"]:
        problems.append(f"{tag}: after Esc the focus is not back on the toggle")
    if not phone:
        # The keyboard: the toggle takes the focus, Enter opens, Esc closes.
        page.focus("#runbadge_toggle")
        page.keyboard.press("Enter")
        if page.get_attribute("#runbadge_toggle", "aria-expanded") != "true":
            problems.append(f"{tag}: Enter on the focused toggle does not open the menu")
        page.keyboard.press("Escape")


def runbadge_case(browser, base, shots, linux):
    problems, r, errors = [], {}, []

    def fresh(scheme, phone, reduced=False):
        c = browser.new_context(color_scheme=scheme, reduced_motion="reduce" if reduced else "no-preference",
                                **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
        pg = c.new_page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        return c, pg

    def run_at(page, v):
        machine_do(page, "select_option", "#dbg_speed", v)
        if page.is_enabled("#dbg_continue"):
            machine_do(page, "click", "#dbg_continue")
        wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 10)

    def tap(page, phone, sel):
        page.tap(sel) if phone else page.click(sel)

    for scheme, phone in (("light", False), ("dark", False), ("light", True), ("dark", True)):
        tag = f"{390 if phone else 1280}-{scheme}"
        c, page = fresh(scheme, phone)
        page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        time.sleep(0.5)
        b = runbadge_check(page, f"{tag} paused", problems, r, "paused")
        if not b:
            page.screenshot(path=str(shots / f"runbadge-{tag}-before.png"))
            c.close()
            continue
        dot_paused = b["dot"]
        runbadge_menu(page, tag, phone, problems, r, shots, "Continue")
        # Continue from the menu on the Debugger tab, at 1/s.
        machine_do(page, "select_option", "#dbg_speed", "1")
        runbadge_open(page, phone)
        tap(page, phone, "#runbadge_pause")
        wait_for(lambda: runbadge(page)["state"] == "run", 10)
        if phone:
            page.tap("header.top h1")
        else:
            page.keyboard.press("Escape")
        b = runbadge_check(page, f"{tag} 1/s", problems, r, "run", "1", moving=False)
        time.sleep(3)
        b2 = runbadge(page)
        if b and b2["steps"] <= b["steps"]:
            problems.append(f"{tag} 1/s: the count does not rise: {b['steps']} -> {b2['steps']}")
        if b and b["dot"] == dot_paused:
            problems.append(f"{tag}: the dot looks the same running and paused ({dot_paused})")
        page.screenshot(path=str(shots / f"runbadge-{tag}-running.png"))
        # The toggle covers no listing text on the Page tab.
        show_tab(page, "page")
        page.evaluate("window.scrollBy(0, document.getElementById('listing').getBoundingClientRect().top - 200)")
        time.sleep(0.6)
        nav_cover(page, f"{tag} Page tab", problems, r, need_lines=True, scheme_after=scheme)
        page.screenshot(path=str(shots / f"runbadge-{tag}-page.png"))
        show_tab(page, "debugger")
        runbadge_open(page, phone)
        tap(page, phone, "#runbadge_pause")
        wait_for(lambda: runbadge(page)["state"] == "paused", 10)
        runbadge_check(page, f"{tag} paused from the menu", problems, r, "paused")
        # The state in words opens the Machine tab.
        tap(page, phone, "#runbadge_state")
        if not wait_for(lambda: shown_tab(page) == "machine", 5):
            problems.append(f"{tag}: the menu's state in words does not open Machine ({shown_tab(page)})")
        # The PWA's "new version" notice shows on the toggle on every tab.
        show_tab(page, "about")
        page.evaluate("typeof pwaNewer === 'function' && pwaNewer('0123456789ab')")
        time.sleep(0.3)
        b = runbadge(page)
        if not b["newer"] or not page.locator("#runbadge_toggle .rbnews").is_visible():
            problems.append(f"{tag}: a new deploy shows no mark on the toggle (newer {b['newer']!r})")
        runbadge_open(page, phone)
        rl = page.locator("#runbadge_menu button", has_text="Reload")
        if not rl.count() or not rl.first.is_visible() or "new version" not in page.inner_text("#runbadge_menu"):
            problems.append(f"{tag}: the open menu has no new version notice with Reload")
        page.screenshot(path=str(shots / f"runbadge-{tag}-newer.png"))
        if phone:
            page.tap("header.top h1")
        else:
            page.keyboard.press("Escape")
        # The end of the run: hello halts.
        run_at(page, "full")
        if not wait_for(lambda: runbadge(page)["state"] == "halted", 60):
            problems.append(f"{tag}: hello at full speed: the toggle never says halted: {runbadge(page)}")
        runbadge_check(page, f"{tag} halted", problems, r, "halted")
        page.screenshot(path=str(shots / f"runbadge-{tag}-halted.png"))
        c.close()

    if any("no menu toggle" in p for p in problems):
        return problems, r
    # Reduced motion: no animation, the state still told apart.
    c, page = fresh("light", False, reduced=True)
    page.goto(f"{base}/index.html?kernel=hello&speed=10#debugger")
    wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 60)
    b = runbadge_check(page, "1280 reduced motion 10/s", problems, r, "run", "10", reduced=True)
    machine_do(page, "click", "#dbg_pause")
    wait_for(lambda: runbadge(page)["state"] == "paused", 10)
    b2 = runbadge_check(page, "1280 reduced motion paused", problems, r, "paused", reduced=True)
    if b and b2 and b["dot"] == b2["dot"]:
        problems.append(f"reduced motion: the dot looks the same running and paused ({b['dot']})")
    page.screenshot(path=str(shots / "runbadge-1280-reduced.png"))
    c.close()

    # The minified Linux at 1k/s, then at full speed into its own panic (VFS:
    # no root); Debian at full speed (the JIT), then the panic text on its screen.
    if linux:
        for scheme, phone in (("light", False), ("dark", True)):
            tag = f"linux-{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000#page")
            wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 90)
            wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 10)
            runbadge_check(page, f"{tag} 1k/s", problems, r, "run", "1000")
            page.screenshot(path=str(shots / f"runbadge-{tag}-1k.png"))
            machine_do(page, "select_option", "#dbg_speed", "full")
            if not wait_for(lambda: runbadge(page)["state"] == "panic", 90):
                problems.append(f"{tag}: at full speed the minified Linux's panic does not show on the toggle: {runbadge(page)}")
            runbadge_check(page, f"{tag} panic", problems, r, "panic")
            page.screenshot(path=str(shots / f"runbadge-{tag}-panic.png"))
            c.close()
    if "debian-6.1-686" in kernel_entries():
        for scheme, phone in (("light", False), ("dark", True)):
            tag = f"debian-{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=debian-6.1-686&speed=full#debugger")
            if not wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run" and runbadge(page)["vmSteps"] > 0, 90):
                problems.append(f"{tag}: Debian at full speed: the toggle says {runbadge(page)}")
            runbadge_check(page, f"{tag} full", problems, r, "run", "full")
            page.screenshot(path=str(shots / f"runbadge-{tag}-full.png"))
            page.evaluate("""() => { const cpu = emulator.v86.cpu;
                const t = "---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---";
                for (let i = 0; i < t.length; i++) { cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i, t.charCodeAt(i)); cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i + 1, 7); } }""")
            if not wait_for(lambda: runbadge(page)["state"] == "panic", 10):
                problems.append(f"{tag}: after the panic text the toggle says {runbadge(page)}")
            runbadge_check(page, f"{tag} panic text", problems, r, "panic")
            c.close()

    # The About tab's legend has a sample of every state of the toggle.
    c, page = fresh("light", False)
    page.goto(f"{base}/index.html?kernel=hello&paused=1#about")
    got = page.evaluate("[...document.querySelectorAll('#about_legend .rbtog[data-state]')].map((e) => e.dataset.state)")
    r["legend"] = got
    for s in ("run", "ff", "paused", "halted", "panic", "off"):
        if s not in got:
            problems.append(f"the legend has no sample of the toggle in state {s!r}")
    c.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_runbadge(ctx):
    return report("runbadge", *runbadge_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries))


# ---- memeip: where EIP is in the Memory tab (M1) --------------------------
# The operator: "na memory view pokazuj eip". The page square holding the
# next instruction's physical address (paging-aware) carries a mark of its
# own (#pages_eip, .memeip; ESP's page a weaker .memesp), labelled in the
# grid's left gutter (.memtag, with a popover); the region bar holding it is
# marked (.eiphere, a tick .eiptick); a line above the grid (#mem_eip) says
# "EIP 0x... (phys 0x...) symbol+0x.., page 0x..." and opens the Page tab at
# EIP. The checks read the CPU independently: CS base + EIP from v86's
# registers, then its page tables from guest RAM (2-level, PSE, PAE).

MEMEIP_JS = """() => {
  if (window.mkbEip) return;
  const u32 = (a) => { const b = emulator.read_memory(a, 4); return (b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24) >>> 0; };
  const walk = (lin) => {
    const c = emulator.v86.cpu;
    if (!(c.cr[0] & 0x80000000)) return lin >>> 0;
    const cr3 = c.cr[3] >>> 0, pae = !!(c.cr[4] & 0x20), pse = !!(c.cr[4] & 0x10);
    if (pae) {
      const pdpte = u32((cr3 & ~0x1f) + ((lin >>> 30) & 3) * 8); if (!(pdpte & 1)) return -1;
      const pde = u32((pdpte & ~0xfff) + ((lin >>> 21) & 0x1ff) * 8); if (!(pde & 1)) return -1;
      if (pde & 0x80) return ((pde & 0xffe00000) | (lin & 0x1fffff)) >>> 0;
      const pte = u32((pde & ~0xfff) + ((lin >>> 12) & 0x1ff) * 8); if (!(pte & 1)) return -1;
      return ((pte & ~0xfff) | (lin & 0xfff)) >>> 0;
    }
    const pde = u32((cr3 & ~0xfff) + (lin >>> 22) * 4); if (!(pde & 1)) return -1;
    if (pse && (pde & 0x80)) return ((pde & 0xffc00000) | (lin & 0x3fffff)) >>> 0;
    const pte = u32((pde & ~0xfff) + ((lin >>> 12) & 0x3ff) * 4); if (!(pte & 1)) return -1;
    return ((pte & ~0xfff) | (lin & 0xfff)) >>> 0;
  };
  const cpu = () => { const c = emulator.v86.cpu, eip = c.get_real_eip() >>> 0, lin = (c.segment_offsets[1] + eip) >>> 0;
    const pm = !!(c.cr[0] & 1), esp = c.reg32[4] >>> 0, slin = (c.segment_offsets[2] + (pm ? esp : esp & 0xffff)) >>> 0;
    return { eip, lin, phys: walk(lin), esp, sphys: pm ? walk(slin) : -1, steps: stepper.steps, paging: !!(c.cr[0] & 0x80000000) }; };
  const M = window.mkbEip = { checks: 0, viol: [], walk, cpu };
  // Each drawing of the mark: in the same task as the drawing, the CPU is
  // where the mark says.
  const seen = new WeakSet();
  const watch = () => { const e = document.getElementById('pages_eip'); if (!e || seen.has(e)) return; seen.add(e);
    new MutationObserver(() => { if (e.hidden || e.dataset.page === undefined || e.dataset.page === '') return;
      const w = cpu(); M.checks++;
      if (Number(e.dataset.steps) !== w.steps || Number(e.dataset.page) !== Math.floor(w.phys / 4096))
        if (M.viol.length < 20) M.viol.push({ kind: 'drawn', mark_page: e.dataset.page, mark_steps: e.dataset.steps, ...w });
    }).observe(e, { attributes: true, attributeFilter: ['data-steps', 'data-page'] }); };
  watch(); setInterval(watch, 500);
  // Slow motion: the mark follows within 1.5 s of an instruction.
  let pend = null;
  setInterval(() => { const e = document.getElementById('pages_eip');
    if (!e || typeof stepper === 'undefined' || !stepper || runState !== 'stepped' || entryPhase || ffActive || fastStepping()
        || document.getElementById('panel_memory').hidden) { pend = null; return; }
    if (Number(e.dataset.steps) === stepper.steps) { pend = null; return; }
    const t = Date.now(); if (pend === null) pend = t;
    else if (t - pend > 1500) { if (M.viol.length < 20) M.viol.push({ kind: 'stale', for_ms: t - pend, mark_steps: e.dataset.steps, steps: stepper.steps }); pend = t; }
  }, 100);
}"""

# The marks as drawn: the squares under each mark's centre, the line, the bar.
MEMEIP_STATE_JS = """() => {
  const canvas = document.getElementById('pages'), cr = canvas.getBoundingClientRect();
  const cols = Number(canvas.dataset.cols), pages = Number(canvas.dataset.pages);
  const shown = (e) => !!e && !e.hidden && e.getClientRects().length > 0 && getComputedStyle(e).visibility !== 'hidden';
  const under = (e) => { const r = e.getBoundingClientRect(), x = (r.left + r.right) / 2, y = (r.top + r.bottom) / 2;
    const cw = (cr.width - 2) / cols; return Math.floor((y - cr.top - 1) / cw) * cols + Math.floor((x - cr.left - 1) / cw); };
  const marks = [...document.querySelectorAll('#panel_memory .memeip')].filter(shown);
  const esps = [...document.querySelectorAll('#panel_memory .memesp')].filter(shown);
  const line = document.getElementById('mem_eip'), go = document.getElementById('mem_eip_go');
  const bars = [...document.querySelectorAll('#map_all .eiphere, #map_low .eiphere')].map((d) => [d.parentNode.id, d.title]);
  const ticks = [...document.querySelectorAll('#map_all .eiptick, #map_low .eiptick')].filter(shown).map((d) => d.parentNode.id);
  const tags = [...document.querySelectorAll('#panel_memory .memtag')].filter(shown).map((t) => t.textContent.trim());
  const m = marks[0], cs = m ? getComputedStyle(m) : null, rb = document.getElementById('runbadge_toggle');
  const rect = (e) => { if (!e) return null; const r = e.getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom].map(Math.round); };
  return { n: marks.length, page: m ? Number(m.dataset.page) : null, steps: m ? Number(m.dataset.steps) : null, under: m ? under(m) : null,
    esp: esps.map((e) => [Number(e.dataset.page), under(e)]), tags, pages, cols,
    line: line ? line.textContent.replace(/\\s+/g, ' ').trim() : null, go: !!go && shown(go),
    lineShown: shown(line), bars, ticks, look: cs ? [cs.outlineColor, cs.borderTopColor, cs.boxShadow].join(' | ') : '',
    mark: rect(m), toggle: rect(rb), vw: innerWidth, vh: innerHeight,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1,
    tokens: Object.fromEntries([...Object.keys(REGION_NAMES), ...Object.keys(SECTION_NAMES), 'accent', 'bg']
      .map((k) => [k, getComputedStyle(document.documentElement).getPropertyValue('--' + k).trim()])) }; }"""


def memeip_syms(name):
    return json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]


def memeip_check(page, tag, syms, problems, r, want_esp=True, shift=0):
    """Exactly one square has the EIP mark; it is the page of the CPU's
    phys EIP (read independently); the line above the grid says EIP, phys,
    the symbol and the page; the region bar holding it is marked."""
    st = page.evaluate(MEMEIP_STATE_JS)
    w = page.evaluate("mkbEip.cpu()")
    r[tag] = {"cpu": [hex(w["eip"]), hex(w["phys"]) if w["phys"] >= 0 else -1, w["steps"]], "marks": st["n"],
              "page": st["page"], "esp": st["esp"], "line": st["line"], "bars": st["bars"]}
    if st["n"] != 1:
        problems.append(f"{tag}: {st['n']} squares carry the EIP mark (want exactly 1)")
        return st, w
    want = w["phys"] // 4096
    if st["page"] != want or st["under"] != want:
        problems.append(f"{tag}: the EIP mark is on page {st['page']} (drawn over square {st['under']}), "
                        f"the CPU's EIP {w['eip']:#x} is at phys {w['phys']:#x}, page {want}")
    if st["steps"] != w["steps"]:
        problems.append(f"{tag}: the mark was drawn at instruction {st['steps']}, the CPU is at {w['steps']} (paused)")
    line = st["line"] or ""
    for part in (f"EIP 0x{w['eip']:08x}", f"phys 0x{w['phys']:08x}", f"page {want:#x}"):
        if part not in line:
            problems.append(f"{tag}: the line above the grid {line!r} lacks {part!r}")
    names = [s["name"] for s in syms or [] if s["size"] > 0 and s["paddr"] <= w["phys"] < s["paddr"] + s["size"]
             and re.match(r"^\.(head\.|init\.)?text", s.get("section", ".text"))]
    # The minified Linux's entry runs the bzImage's relocation stub, whose
    # bytes are not vmlinux's (stub_shift): the line says so.
    stub = shift and 0x100000 <= w["phys"] < 0x100000 + shift and "relocation stub" in line
    if names and not stub and not any(n + "+0x" in line or n + "," in line for n in names):
        problems.append(f"{tag}: the line above the grid {line!r} names none of {names}")
    if not st["lineShown"] or not st["go"]:
        problems.append(f"{tag}: the EIP line or its link to the Page tab is not shown ({st['lineShown']}, {st['go']})")
    if len([b for b in st["bars"] if b[0] == "map_all"]) != 1:
        problems.append(f"{tag}: the All RAM bar marks {st['bars']} as holding EIP (want one segment)")
    if (w["phys"] < 0x100000) != any(b[0] == "map_low" for b in st["bars"]):
        problems.append(f"{tag}: the First MiB bar's EIP mark {st['bars']} does not match phys {w['phys']:#x}")
    if "map_all" not in st["ticks"]:
        problems.append(f"{tag}: no EIP tick in the All RAM bar ({st['ticks']})")
    if "EIP" not in st["tags"]:
        problems.append(f"{tag}: no EIP label next to the grid ({st['tags']})")
    if want_esp and w["sphys"] >= 0 and w["sphys"] // 4096 < st["pages"]:
        sp = w["sphys"] // 4096
        if [e[0] for e in st["esp"]] != [sp] or st["esp"][0][1] != sp:
            problems.append(f"{tag}: ESP {w['esp']:#x} is in page {sp}, the stack mark: {st['esp']}")
    look = st["look"]
    for k, v in st["tokens"].items():
        if k not in ("accent", "bg") and v and css_rgb(v) in look.split(" | ")[0:2]:
            problems.append(f"{tag}: the EIP mark's colour {look} is the {k} region/section colour")
    if st["hscroll"]:
        problems.append(f"{tag}: horizontal scroll")
    return st, w


def memeip_clear_of_toggle(page, tag, problems, r):
    """With the mark in the middle of the screen, the menu toggle (H1) covers
    neither the mark nor its label."""
    page.evaluate("document.getElementById('pages_eip').scrollIntoView({ block: 'center' })")
    time.sleep(0.3)
    got = page.evaluate("""() => { const t = document.getElementById('runbadge_toggle').getBoundingClientRect();
      return [...document.querySelectorAll('#pages_eip, #pages_eip_tag')].filter((e) => !e.hidden).map((e) => {
        const r = e.getBoundingClientRect(); return [e.id, r.right > t.left && r.left < t.right && r.bottom > t.top && r.top < t.bottom]; }); }""")
    r[f"{tag} clear of the toggle"] = got
    for id_, hit in got:
        if hit:
            problems.append(f"{tag}: the menu toggle covers #{id_}")


def memeip_tip(page, tag, phone, problems, r):
    """The EIP label's popover (T1 style): hover at 1280, tap at 390."""
    t = page.locator("#pages_eip_tag")
    if not t.count() or not t.is_visible():
        problems.append(f"{tag}: no EIP label (#pages_eip_tag) to explain the mark")
        return
    t.scroll_into_view_if_needed()
    if phone:
        t.tap()
    else:
        t.hover()
    st = tip_wait(page, True)
    r[f"{tag} popover"] = st["text"][:120]
    if not st["shown"] or "EIP" not in st["text"] or "next instruction" not in st["text"]:
        problems.append(f"{tag}: the EIP label's popover {st}")
    else:
        tip_fits(st, tag, problems)
    if phone:
        tip_tap_away(page)
    else:
        page.mouse.move(2, 2)
    tip_wait(page, False)


def memeip_run(browser, base, shots, name, scheme, phone, full):
    problems, r, errors = [], {}, []
    tag0 = f"{name} {390 if phone else 1280} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    syms = memeip_syms(name)
    shift = stub_shift(name) or 0
    shot = lambda s: page.screenshot(path=str(shots / f"memeip-{name}-{390 if phone else 1280}-{scheme}-{s}.png"))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#memory")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag0}: did not start paused at the reset vector"], r
    wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
    page.evaluate(MEMEIP_JS)
    time.sleep(0.5)
    st, w = memeip_check(page, f"{tag0} reset", syms, problems, r, want_esp=False, shift=shift)
    if st["n"] != 1:
        shot("reset")
        ctx.close()
        return problems, r
    shot("reset")
    memeip_tip(page, tag0, phone, problems, r)

    def runto(addr, label):
        show_tab(page, "debugger")
        page.fill("#dbg_addr", hex(addr))
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {addr}, 300)
        show_tab(page, "memory")
        time.sleep(0.5)
        if not ok:
            problems.append(f"{tag0}: Run to {label} did not stop there ({at:#x}: {status})")
        return ok

    # The kernel entry.
    runto(0x100000, "the kernel entry")
    memeip_check(page, f"{tag0} kernel entry", syms, problems, r, want_esp=False, shift=shift)
    shot("entry")
    # Mid-boot: 1/s, then 1k/s, on the Memory tab; each drawing checked.
    page.evaluate("mkbEip.checks = 0")
    machine_do(page, "select_option", "#dbg_speed", "1")
    machine_do(page, "click", "#dbg_continue")
    time.sleep(6)
    c1 = page.evaluate("mkbEip.checks")
    machine_do(page, "select_option", "#dbg_speed", "1000")
    time.sleep(5)
    c2 = page.evaluate("mkbEip.checks") - c1
    r[f"{tag0} drawings checked at 1/s, 1k/s"] = [c1, c2]
    if c1 < 3 or c2 < 5:
        problems.append(f"{tag0}: the EIP mark was drawn {c1} times in 6 s at 1/s and {c2} times in 5 s at 1k/s")
    shot("1k")
    ended = page.evaluate("!!stepper.end")
    if not ended:
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        time.sleep(0.4)
        memeip_check(page, f"{tag0} after Pause", syms, problems, r, shift=shift)
        machine_do(page, "click", "#dbg_step")
        wait_for(lambda: page.evaluate("mkbEip.cpu().steps") == page.evaluate("Number(document.getElementById('pages_eip').dataset.steps)"), 5)
        memeip_check(page, f"{tag0} after Step", syms, problems, r, shift=shift)
    if full:
        # Run to a function further on.
        target = next((s for s in syms if s["name"] in ("start_kernel",)), None) if name.startswith("minified") else None
        if target and not ended:
            if runto(target["paddr"], target["name"]):
                memeip_check(page, f"{tag0} after Run to {target['name']}", syms, problems, r, shift=shift)
                shot("runto")
        # The line opens the Page tab at EIP; Back returns.
        before = page.evaluate("mkbEip.cpu()")
        page.locator("#mem_eip_go").tap() if phone else page.click("#mem_eip_go")
        ok = wait_for(lambda: shown_tab(page) == "page" and page.get_attribute("#listing", "data-page") == str(before["phys"] // 4096), 10)
        cur = page.evaluate("(() => { const c = document.querySelector('#listing .ins.cur'); return c ? Number(c.dataset.addr) : null; })()")
        r[f"{tag0} line -> Page"] = [ok, cur]
        if not ok or cur != before["phys"]:
            problems.append(f"{tag0}: the EIP line does not open the Page tab at EIP: tab {shown_tab(page)}, "
                            f"listing page {page.get_attribute('#listing', 'data-page')}, boxed {cur} (want {before['phys']})")
        nav_press(page, "#nav_back", "tap" if phone else "click")
        if not wait_for(lambda: shown_tab(page) == "memory", 5):
            problems.append(f"{tag0}: Back after the EIP line does not return to the Memory tab ({shown_tab(page)})")
        memeip_clear_of_toggle(page, tag0, problems, r)
        shot("mark")
        # The end of the run: the minified Linux's panic, hello's halt, at full speed.
        machine_do(page, "select_option", "#dbg_speed", "full")
        if page.is_enabled("#dbg_continue"):
            machine_do(page, "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("runState === 'paused' && !ffActive && !!stepper.end"), 180):
            problems.append(f"{tag0}: the run did not end at full speed")
        else:
            time.sleep(0.5)
            end = page.get_attribute("#end_banner", "data-kind") or ""
            memeip_check(page, f"{tag0} after the end ({end})", syms, problems, r, shift=shift)
            shot("end")
    viol = page.evaluate("mkbEip.viol")
    if viol:
        problems.append(f"{tag0}: the mark did not follow EIP: {viol[:4]}")
    if errors:
        problems.append(f"{tag0}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def memeip_switch(browser, base, shots):
    """A kernel switch and Boot: the mark follows the new machine."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.goto(f"{base}/index.html?kernel=hello&speed=1#memory")
    wait_for(lambda: page.evaluate("typeof entryPhase !== 'undefined' && runState === 'stepped' && !entryPhase && !ffActive"), 120)
    page.evaluate(MEMEIP_JS)
    time.sleep(3)
    for label, act in (("kernel switch to hello-os", lambda: machine_do(page, "select_option", "#kernel", "hello-os")),
                       ("Boot", lambda: machine_do(page, "click", "#boot"))):
        act()
        wait_for(lambda: page.evaluate("runState === 'stepped' && !entryPhase && !ffActive"), 120)
        page.evaluate("mkbEip.checks = 0")
        time.sleep(4)
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        time.sleep(0.4)
        r[f"{label} drawings"] = page.evaluate("mkbEip.checks")
        if r[f"{label} drawings"] < 2:
            problems.append(f"after {label}: the mark was drawn {r[f'{label} drawings']} times in 4 s at 1/s")
        memeip_check(page, f"after {label}", memeip_syms("hello-os"), problems, r)
        machine_do(page, "click", "#dbg_continue")
    viol = page.evaluate("mkbEip.viol")
    if viol:
        problems.append(f"kernel switch / Boot: the mark did not follow EIP: {viol[:4]}")
    # The legend has the marks.
    show_tab(page, "about")
    got = page.evaluate("[...document.querySelectorAll('#about_legend .memeip, #about_legend .memesp, #about_legend .eiptick')].length")
    if got < 3:
        problems.append(f"the About legend has {got} samples of the EIP/ESP marks (want the EIP and ESP squares and the bar tick)")
    page.screenshot(path=str(shots / "memeip-legend.png"))
    ctx.close()
    return problems, r


def memeip_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "light", False, True), ("hello", "dark", True, True), ("hello", "dark", False, False), ("hello", "light", True, False)]
    if linux:
        runs += [("minified-linux", "light", False, True), ("minified-linux", "dark", True, True)]
        if "minified-linux-os" in kernel_entries():
            runs += [("minified-linux-os", "dark", False, True), ("minified-linux-os", "light", True, False)]
    for name, scheme, phone, full in runs:
        t0 = time.monotonic()
        problems, r = memeip_run(browser, base, shots, name, scheme, phone, full)
        out.append(report("memeip", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
        if any("squares carry the EIP mark" in p for p in problems) and name == "hello":
            break
    else:
        problems, r = memeip_switch(browser, base, shots)
        out.append(report("memeip switch", problems, r))
    return any(out)


def suite_memeip(ctx):
    return memeip_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)


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

GRAPH_STATE_JS = """() => ({ tab, view: G.view, x0: G.x0, x1: G.x1, yOff: G.yOff, xOff: G.xOff, selected: G.selected,
    focus: G.focus, flame: G.flame, transpose: G.transpose, node: document.getElementById('graph_info').dataset.node || '' })"""


def graph_box_xy(page, fn, view="timeline"):
    """Viewport point inside the widest drawn box of function fn (timeline
    frame or Top 30 node), the canvas scrolled below the sticky top bar."""
    js = """([fn, view]) => { const st = G.stepper, c = document.getElementById('graph_canvas').getBoundingClientRect();
        let best = null;
        if (view === 'timeline') {
            for (const [k, x, y, w, h] of G.rects)
                if (st.nodeName(st.fNode[k]) === fn && (!best || w > best[3])) best = [k, x, y, w, h];
        } else
            for (const [n, x, y, w, h] of G.boxes) if (st.nodeName(n) === fn) best = [n, x, y, w, h];
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


def graph_empty_xy(page):
    """A point in the timeline's drawing area that no box, merged column or
    task switch mark covers (below the deepest row)."""
    return page.evaluate("""() => { const c = document.getElementById('graph_canvas').getBoundingClientRect();
        let low = G.areaTop || 40; for (const r of G.rects) low = Math.max(low, r[2] + r[4]);
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
        return { page: selectedPage, ins: ins.length, insVisible: ins.some(vis), hdr: !!hdr, hdrVisible: vis(hdr),
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
GRAPHOPEN_SLOW_JS = """(ms) => { if (!window.__openFunction0) window.__openFunction0 = openFunction;
    window.__opens = 0;
    openFunction = (...a) => { __opens++; __openFunction0(...a); const t = realNow(); while (realNow() - t < ms); }; }"""


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
        page.evaluate("() => { openFunction = __openFunction0; }")  # an expression's function value would be called
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
    if not wait_for(lambda: page.evaluate("G.rects.length > 0"), 20):
        ctx.close()
        return [f"{name}: the Calls timeline drew no box"], r
    tagw = f"{name} {width} {scheme}"

    def select(xy):
        page.touchscreen.tap(xy["x"], xy["y"]) if phone else page.mouse.click(xy["x"], xy["y"])

    orientations = [False] if phone else [False, True]
    for flame in orientations:
        if page.evaluate("G.flame") != flame:
            page.click("#graph_flame")
            time.sleep(0.5)
        how = ("double tap" if phone else "double click") + (" flame" if flame else "")
        xy = graph_box_xy(page, fn)
        if not xy:
            problems.append(f"{tagw}: no {fn} box drawn ({how})")
            continue
        # A single click (tap) selects and stays in the Calls tab, as before.
        focus0 = page.evaluate("G.focus")
        select(xy)
        if not wait_for(lambda: page.get_attribute("#graph_info", "data-node") == fn, 3):
            problems.append(f"{tagw}: a single {'tap' if phone else 'click'} did not select {fn}: {page.get_attribute('#graph_info', 'data-node')}")
        time.sleep(0.7)  # longer than a double click: the next press starts afresh
        if shown_tab(page) != "calls" or page.evaluate("G.focus") != focus0:
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
    if page.evaluate("G.flame"):
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
        down = page.evaluate("[G.selected >= 0 ? G.stepper.fParent[G.selected] : -2, G.selected]")
        page.keyboard.press("ArrowUp")
        time.sleep(0.3)
        up = page.evaluate("G.selected")
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


def graphopen_case(browser, base, shots, linux):
    out = []
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


# ---- srcfiles: another file from the Source panel (F1) --------------------
# The operator: "pop up z pojedynczym plikiem powinien pozwalać łatwo
# obejrzeć inny plik". The Source panel opened from Code at EIP has a file
# switcher (#src_files: type to filter on path and basename, arrows and
# Enter, Esc), the call stack's and recently opened files on top, each file
# with its lines that ran; the path in the title is a breadcrumb (a
# directory lists its files); a whole file has a function outline
# (#src_outline) and the headers inlined into it (#src_incl). Every switch
# is a history entry and the hash keeps the file across a reload.
SRCFILES_POINTS = {"hello": ("hello_main", 40, "hello.c", "boot/hello.c", "hello_main"),
                   "minified-linux": ("start_kernel", 3000, "main.c", "init/main.c", None)}

SRCFILES_STATE_JS = """() => { const sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
    const sw = document.getElementById('src_switch');
    return { open: !sp.hidden, title: document.getElementById('src_title').textContent, file: sb.dataset.file || '',
             state: sb.dataset.state || '', code: Number(sb.dataset.code || 0), executed: Number(sb.dataset.executed || 0),
             top: sb.scrollTop, hash: location.hash, switcher: !!sw && !sw.hidden,
             exec: [...sb.querySelectorAll('.exec:not(.cont)')].map(e => Number(e.dataset.line)),
             pick: (sb.querySelector('.pick') || {dataset: {}}).dataset.line || '' }; }"""

SRCFILES_LIST_JS = """() => [...document.querySelectorAll('#src_list [role=option]')].map(e => ({ path: e.dataset.path || '',
    dir: e.dataset.dir || '', group: e.dataset.group || '', executed: e.dataset.executed ?? '', code: e.dataset.code ?? '',
    active: e.classList.contains('active'), text: e.textContent }))"""


def srcfiles_state(page):
    return page.evaluate(SRCFILES_STATE_JS)


def srcfiles_wait_file(page, path, whole=True, timeout=25):
    want = path + " (whole file)" if whole else None
    ok = wait_for(lambda: (lambda s: s["open"] and s["file"] == path and s["state"] == "ok" and
                           (want is None or s["title"] == want))(srcfiles_state(page)), timeout)
    time.sleep(0.4)
    return ok


def srcfiles_seen_lines(page, name, path):
    """The lines of path that ran, independently of the page's coverage
    code: every instruction start the stepper recorded in the kernel's
    DWARF shards, symbolized with llvm-symbolizer --inlines on the -g ELF
    (or gen-dwarf.js --lookup without one), every frame in path."""
    pas = page.evaluate("""() => { const st = stepper, out = [], bits = DBG.index.shardBits;
        for (let a = st.kernelLo; a < st.seen.length * 8; a++)
            if ((st.seen[a >> 3] >> (a & 7) & 1) && DBG.shardSet.has(Math.floor(a / 2 ** bits))) out.push(a);
        return out; }""")
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    off = next(s["vaddr"] - s["paddr"] for s in syms if s["name"] == SRCFILES_POINTS[name][0])
    elf = debug_elf(name)
    if elf:
        frames = symbolize_frames(elf, [a + off for a in pas])
        how = "llvm-symbolizer"
    else:
        from test_dwarf import page_frames
        frames = page_frames(str(SITE / entry["dwarf"]), pas)
        how = "gen-dwarf.js --lookup"
    return {f[2] for fr in frames for f in fr if f[2] and (f[1] == path or f[1].endswith("/" + path))}, len(pas), how


def srcfiles_fn_line(name, path, fn):
    """The line llvm-symbolizer gives for fn's first instruction in path."""
    entry = kernel_entries()[name]
    sym = next((s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"] if s["name"] == fn), None)
    elf = debug_elf(name)
    if not sym or not elf:
        return None
    fr = symbolize_frames(elf, [sym["vaddr"]])[0]
    outer = fr[-1] if fr else None
    return outer[2] if outer and (outer[1] == path or outer[1].endswith("/" + path)) else None


def srcfiles_open_switcher(page, how, problems, tag):
    if not page.locator("#src_files").count():
        problems.append(f"{tag}: the Source panel has no file switcher button (#src_files)")
        return False
    if how == "tap":
        page.tap("#src_files")
    elif how == "key":
        page.focus("#src_files")
        page.keyboard.press("Enter")
    else:
        page.click("#src_files")
    if not wait_for(lambda: page.is_visible("#src_switch") and page.is_visible("#src_q"), 5):
        problems.append(f"{tag}: the file switcher did not open ({how})")
        return False
    if page.get_attribute("#src_files", "aria-expanded") != "true":
        problems.append(f"{tag}: #src_files aria-expanded is not true with the switcher open")
    if how != "tap" and not page.evaluate("document.activeElement && document.activeElement.id === 'src_q'"):
        problems.append(f"{tag}: the filter field does not have the focus after opening ({how})")
    return True


def srcfiles_fit(page, tag, problems, r):
    """The switcher fits in the viewport and the panel, nothing scrolls sideways."""
    got = page.evaluate("""() => { const v = [innerWidth, innerHeight];
        const box = (id) => { const e = document.getElementById(id); if (!e || e.hidden || !e.getClientRects().length) return null;
            const b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
        const sp = document.getElementById('src_panel');
        return { view: v, sw: box('src_switch'), q: box('src_q'), list: box('src_list'), panel: box('src_panel'),
                 hdoc: document.documentElement.scrollWidth > innerWidth + 1,
                 hpanel: sp.scrollWidth > sp.clientWidth + 1,
                 hlist: (() => { const l = document.getElementById('src_list'); return !!l && l.scrollWidth > l.clientWidth + 1; })() }; }""")
    r[f"fit {tag}"] = got
    for k in ("sw", "q", "list"):
        b = got[k]
        if not b:
            problems.append(f"{tag}: #{'src_' + ('switch' if k == 'sw' else k)} not shown")
            continue
        if b[0] < -1 or b[2] > got["view"][0] + 1 or b[1] < -1 or b[3] > got["view"][1] + 1:
            problems.append(f"{tag}: {k} {b} outside the viewport {got['view']}")
        p = got["panel"]
        if p and (b[0] < p[0] - 1 or b[2] > p[2] + 1):
            problems.append(f"{tag}: {k} {b} wider than the Source panel {p}")
    if got["hdoc"] or got["hpanel"] or got["hlist"]:
        problems.append(f"{tag}: horizontal scroll (page {got['hdoc']}, panel {got['hpanel']}, list {got['hlist']})")


def srcfiles_shots(page, shots, stem, scheme):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))
    page.emulate_media(color_scheme=scheme)


def srcfiles_run(browser, base, shots, name, scheme, phone):
    fn, steps, query, target, outline_fn = SRCFILES_POINTS[name]
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    shipped = json.loads((SITE / entry["src"] / "index.json").read_text())["files"]
    known = sorted(set(f[0] for f in F["files"]) | set(shipped))
    problems, r = [], {}
    errors = []
    width = 390 if phone else 1280
    tag = f"{name} {width} {scheme}"
    how = "tap" if phone else "click"
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True) if phone else \
        browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(syms[fn]["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {syms[fn]["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{tag}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)

    # 1. The panel from Code at EIP: the function around EIP.
    show_tab(page, "debugger")
    if not wait_for(lambda: page.locator("#context .src:not(.nol)").count() > 0, 30):
        ctx.close()
        return [f"{tag}: Code at EIP shows no C line"], r
    line = page.locator("#context .src:not(.nol)").first
    line.scroll_into_view_if_needed()
    page.tap("#context .src:not(.nol) >> nth=0") if phone else line.click()
    if not wait_for(lambda: (lambda s: s["open"] and s["state"] == "ok")(srcfiles_state(page)), 25):
        ctx.close()
        return [f"{tag}: the Source panel did not open from Code at EIP: {srcfiles_state(page)}"], r
    time.sleep(0.5)
    start = srcfiles_state(page)
    r["start"] = {k: start[k] for k in ("title", "file", "code", "executed", "hash")}
    if "(whole file)" in start["title"]:
        problems.append(f"{tag}: Code at EIP opened a whole file, not the function: {start['title']!r}")
    entries0 = nav_at(page)

    # 2. The switcher: call stack and recent files on top, then every file.
    if not srcfiles_open_switcher(page, how, problems, tag):
        ctx.close()
        return problems, r
    wait_for(lambda: page.evaluate("document.querySelectorAll('#src_list [role=option]').length") > 0, 10)
    items = page.evaluate(SRCFILES_LIST_JS)
    r["first items"] = [(x["group"], x["path"] or x["dir"]) for x in items[:6]]
    stack = [x["path"] for x in items if x["group"] == "stack"]
    recent = [x["path"] for x in items if x["group"] == "recent"]
    listed = sorted({x["path"] for x in items if x["group"] == "all"})
    if start["file"] not in stack:
        problems.append(f"{tag}: the call stack's files {stack} lack EIP's {start['file']}")
    if start["file"] not in recent:
        problems.append(f"{tag}: the recently opened files {recent} lack {start['file']}")
    groups = [x["group"] for x in items]
    if groups and groups.index("all") < max(i for i, g in enumerate(groups) if g in ("stack", "recent")):
        problems.append(f"{tag}: the call stack and recent files are not on top: {groups[:12]}")
    if listed != known:
        problems.append(f"{tag}: the switcher lists {len(listed)} files, the DWARF and the shipped source know {len(known)}: "
                        f"missing {sorted(set(known) - set(listed))[:5]}, extra {sorted(set(listed) - set(known))[:5]}")
    srcfiles_fit(page, f"{tag} switcher", problems, r)
    srcfiles_shots(page, shots, f"srcfiles-{name}-{width}-switcher", scheme)

    # 3. Filter by a basename, pick the file.
    page.fill("#src_q", query)
    time.sleep(0.5)
    items = page.evaluate(SRCFILES_LIST_JS)
    r[f"filter {query}"] = [(x["path"], x["executed"], x["code"]) for x in items[:6]]
    paths = [x["path"] for x in items]
    if target not in paths:
        problems.append(f"{tag}: filtering {query!r} does not list {target}: {paths[:8]}")
        ctx.close()
        return problems, r
    if paths[0] != target and name == "hello":
        problems.append(f"{tag}: {target} is not the best match for {query!r}: {paths[:4]}")
    if any(query.lower() not in p.lower() and not srcfiles_fuzzy(query, p) for p in paths):
        problems.append(f"{tag}: filter {query!r} lists non-matching files {[p for p in paths if not srcfiles_fuzzy(query, p)][:4]}")
    sel = f'#src_list [role=option][data-path="{target}"]'
    if not wait_for(lambda: page.get_attribute(sel, "data-executed") not in (None, ""), 20):
        problems.append(f"{tag}: the switcher does not say how many lines of {target} ran")
    pick = next(x for x in page.evaluate(SRCFILES_LIST_JS) if x["path"] == target)
    page.tap(sel) if phone else page.click(sel)
    if not srcfiles_wait_file(page, target):
        problems.append(f"{tag}: picking {target} did not open the whole file: {srcfiles_state(page)}")
        ctx.close()
        return problems, r
    got = srcfiles_state(page)
    r["picked"] = {k: got[k] for k in ("title", "code", "executed", "hash", "switcher")}
    if got["switcher"]:
        problems.append(f"{tag}: the switcher stays open after picking a file")
    if got["title"] == start["title"] or got["code"] == 0:
        problems.append(f"{tag}: the panel did not change: {start['title']!r} -> {got['title']!r}, {got['code']} lines with code")
    if "src=" + target not in urllib_unquote(got["hash"]):
        problems.append(f"{tag}: the hash {got['hash']!r} does not keep {target}")
    at = nav_at(page)
    if at[0] != entries0[0] + 1:
        problems.append(f"{tag}: one history entry expected for the switch: before {entries0}, after {at}")
    # Coverage: the panel's ● lines are the lines that ran (independent set);
    # the switcher's count says the same.
    want, n_seen, how_sym = srcfiles_seen_lines(page, name, target)
    r["coverage"] = [len(got["exec"]), len(want), n_seen, how_sym]
    if set(got["exec"]) != want:
        problems.append(f"{tag}: {target}'s ran lines differ from {how_sym}: only page {sorted(set(got['exec']) - want)[:8]}, "
                        f"only {how_sym} {sorted(want - set(got['exec']))[:8]}")
    if got["executed"] != len(want):
        problems.append(f"{tag}: data-executed {got['executed']} != {len(want)}")
    if str(pick["executed"]) != str(len(want)) or str(pick["code"]) != str(got["code"]):
        problems.append(f"{tag}: the switcher said {pick['executed']} of {pick['code']} lines ran, the panel {len(want)} of {got['code']}")
    srcfiles_shots(page, shots, f"srcfiles-{name}-{width}-file", scheme)

    # 4. The outline: a function of the file, scrolled to and picked.
    opts = page.evaluate("""() => [...document.querySelectorAll('#src_outline option')].filter(o => o.value).map(o => ({ v: o.value, t: o.textContent, fn: o.dataset.fn || '' }))""")
    r["outline"] = len(opts)
    file_fns = {f[0] for f in F["funcs"] if f[1] >= 0 and F["files"][f[1]][0] == target}
    named = {o["fn"] for o in opts}
    if not opts or not file_fns <= named:
        problems.append(f"{tag}: the outline lacks functions of {target}: {sorted(file_fns - named)[:6]} (has {len(opts)})")
    else:
        want_fn = outline_fn or next((o["fn"] for o in opts[len(opts) // 2:] if o["fn"] in file_fns and o["v"].isdigit()), opts[-1]["fn"])
        o = next(x for x in opts if x["fn"] == want_fn)
        before = nav_at(page)
        page.select_option("#src_outline", o["v"])
        time.sleep(0.8)
        s = srcfiles_state(page)
        vis = page.evaluate("""(l) => { const sb = document.getElementById('src_body'), e = sb.querySelector('[data-line="' + l + '"]');
            if (!e) return false; const a = e.getBoundingClientRect(), b = sb.getBoundingClientRect(); return a.top >= b.top - 1 && a.bottom <= b.bottom + 1; }""", o["v"])
        wl = srcfiles_fn_line(name, target, want_fn)
        r["outline jump"] = [want_fn, o["v"], s["pick"], vis, wl]
        if s["pick"] != o["v"] or not vis:
            problems.append(f"{tag}: the outline's {want_fn} (line {o['v']}) is not picked and in view: pick {s['pick']}, visible {vis}")
        if wl is not None and int(o["v"]) != wl:
            problems.append(f"{tag}: the outline puts {want_fn} at line {o['v']}, llvm-symbolizer at {wl}")
        if nav_at(page)[0] != before[0] + 1:
            problems.append(f"{tag}: the outline jump is not a history entry: {before} -> {nav_at(page)}")

    # 5. The breadcrumb: the directory lists its files.
    d = target.rsplit("/", 1)[0]
    crumb = page.locator(f'#src_title button.crumb[data-dir="{d}"]')
    if not crumb.count():
        problems.append(f"{tag}: no breadcrumb button for {d}/ in the title")
    else:
        crumb.tap() if phone else crumb.click()
        wait_for(lambda: page.is_visible("#src_switch"), 5)
        time.sleep(0.4)
        items = page.evaluate(SRCFILES_LIST_JS)
        files = sorted(x["path"] for x in items if x["path"])
        here = sorted(p for p in known if p.rsplit("/", 1)[0] == d)
        subdirs = sorted({p[len(d) + 1:].split("/")[0] for p in known if p.startswith(d + "/") and "/" in p[len(d) + 1:]})
        r["crumb"] = [d, len(files), [x["dir"] for x in items if x["dir"]][:4]]
        if page.get_attribute("#src_switch", "data-dir") != d or files != here or \
                sorted(x["dir"].rsplit("/", 1)[-1] for x in items if x["dir"] and x["dir"].startswith(d + "/")) != subdirs:
            problems.append(f"{tag}: the breadcrumb {d}/ lists {files[:6]} (+dirs {[x['dir'] for x in items if x['dir']][:4]}), want {here[:6]} (+{subdirs[:4]})")
        srcfiles_fit(page, f"{tag} directory", problems, r)
        page.keyboard.press("Escape")
        if not wait_for(lambda: not page.is_visible("#src_switch"), 3):
            problems.append(f"{tag}: Escape does not close the switcher")
        if srcfiles_state(page)["file"] != target:
            problems.append(f"{tag}: Escape in the switcher changed the file")

    # 6. Headers inlined into the file: one pick away.
    incl = page.evaluate("""() => [...document.querySelectorAll('#src_incl option')].filter(o => o.value).map(o => o.value)""")
    r["headers"] = incl[:6]
    if name != "hello":
        if not incl:
            problems.append(f"{tag}: no inlined headers offered for {target}")
        else:
            h = incl[0]
            page.select_option("#src_incl", h)
            if not srcfiles_wait_file(page, h):
                problems.append(f"{tag}: the header {h} did not open: {srcfiles_state(page)}")
            else:
                page.go_back()
                if not srcfiles_wait_file(page, target):
                    problems.append(f"{tag}: Back from {h} did not return to {target}: {srcfiles_state(page)}")

    # 7. Back returns to the previous file and its scroll.
    while nav_at(page)[0] > entries0[0]:  # the outline jump, the switch
        page.go_back()
        time.sleep(0.6)
    if not wait_for(lambda: srcfiles_state(page)["title"] == start["title"], 15):
        problems.append(f"{tag}: Back did not return to {start['title']!r}: {srcfiles_state(page)['title']!r}")
    else:
        time.sleep(0.6)
        b = srcfiles_state(page)
        if abs(b["top"] - start["top"]) > 2:
            problems.append(f"{tag}: Back restored the scroll {b['top']}, left at {start['top']}")
    page.go_forward()
    if not srcfiles_wait_file(page, target):
        problems.append(f"{tag}: Forward did not reopen {target}: {srcfiles_state(page)}")

    # 8. Keyboard only: open, type, arrows, Enter.
    if not phone:
        page.focus("#src_body")
        if srcfiles_open_switcher(page, "key", problems, f"{tag} keyboard"):
            page.keyboard.type("main" if name != "hello" else "hello")
            time.sleep(0.4)
            items = page.evaluate(SRCFILES_LIST_JS)
            want_i = 0
            for key in ("ArrowDown", "ArrowDown", "ArrowUp"):
                page.keyboard.press(key)
                want_i = min(want_i + 1, len(items) - 1) if key == "ArrowDown" else max(want_i - 1, 0)
            time.sleep(0.2)
            items = page.evaluate(SRCFILES_LIST_JS)
            act = [x for x in items if x["active"]]
            r["keyboard"] = [x["path"] for x in items[:4]], [x["path"] for x in act]
            if not act or act[0]["path"] != items[want_i]["path"]:
                problems.append(f"{tag}: Down, Down, Up does not make item {want_i} active: {r['keyboard']}")
            elif page.get_attribute("#src_q", "aria-activedescendant") is None:
                problems.append(f"{tag}: the filter has no aria-activedescendant")
            else:
                page.keyboard.press("Enter")
                kp = act[0]["path"]
                if not srcfiles_wait_file(page, kp):
                    problems.append(f"{tag}: Enter did not open {kp}: {srcfiles_state(page)}")
                page.go_back()
                srcfiles_wait_file(page, target)

    # 9. A reload reopens the file in the hash.
    page.reload()
    if not srcfiles_wait_file(page, target, timeout=60):
        problems.append(f"{tag}: the reload did not reopen {target}: {srcfiles_state(page)}")
    if errors:
        problems.append(f"{tag}: page errors {errors}")
    ctx.close()
    return problems, r


def srcfiles_fuzzy(q, p):
    """q's characters in order in p (case-insensitive)."""
    it = iter(p.lower())
    return all(c in it for c in q.lower())


def urllib_unquote(s):
    from urllib.parse import unquote
    return unquote(s)


def srcfiles_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if linux:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = srcfiles_run(browser, base, shots, name, scheme, phone)
        out.append(report("srcfiles", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_srcfiles(ctx):
    return srcfiles_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)


# ---- snapdiff: named snapshots, kept across a reload, and their diff (S1) --
# The operator: "dodaj diffowanie snapshotów, ich nazywanie i przechowywanie
# między refresh". Snapshots get a default name the user can rename, are
# stored in IndexedDB per kernel identity (bzImage SHA-256) and survive a
# reload; a diff of two snapshots (or one and now) lists the registers, the
# instruction count, the VGA lines and the 4 KiB pages that changed, with a
# side-by-side hexdump of a page and a link into the Page tab at the
# snapshot's contents. Without storage the page keeps them in memory and
# says so; a full quota is reported and the in-memory copy kept.
SNAPDIFF_STEPS = {"hello": 60, "minified-linux": 3000}

SNAP_JS = """() => { if (window.mkbSnap) return; window.mkbSnap = {
  cpu() { const c = emulator.v86.cpu, r = c.reg32, s = c.sreg;
    return { eip: (c.instruction_pointer[0] - c.segment_offsets[1]) >>> 0, icount: c.instruction_counter[0] >>> 0,
      regs: { EAX: r[0] >>> 0, ECX: r[1] >>> 0, EDX: r[2] >>> 0, EBX: r[3] >>> 0, ESP: r[4] >>> 0, EBP: r[5] >>> 0,
        ESI: r[6] >>> 0, EDI: r[7] >>> 0, EIP: (c.instruction_pointer[0] - c.segment_offsets[1]) >>> 0,
        EFLAGS: c.get_eflags() >>> 0, ES: s[0], CS: s[1], SS: s[2], DS: s[3], FS: s[4], GS: s[5],
        CR0: c.cr[0] >>> 0, CR2: c.cr[2] >>> 0, CR3: c.cr[3] >>> 0, CR4: c.cr[4] >>> 0 },
      screen: [...document.getElementById('screen_text').children].map((e) => e.textContent.replace(/\\s+$/, '')) }; },
  keep(k) { this[k] = emulator.read_memory(0, emuRam).slice(); },
  pages(a, b) { const A = this[a], B = this[b], out = [];
    for (let p = 0; p < A.length / 4096; p++) { let n = 0;
      for (let i = p * 4096; i < p * 4096 + 4096; i++) if (A[i] !== B[i]) n++;
      if (n) out.push([p, n]); }
    return out; },
  hex(k, p) { return Array.from(this[k].subarray(p * 4096, p * 4096 + 4096), (b) => b.toString(16).padStart(2, '0')).join(''); },
  b64(k) { let s = ''; const a = this[k]; for (let i = 0; i < a.length; i += 0x8000) s += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000)); return btoa(s); } }; }"""

SNAP_STATE_JS = """() => { const rows = [...document.querySelectorAll('#snapshots tbody tr[data-id]')];
  const shown = (e) => !!e && !e.hidden && e.getClientRects().length > 0;
  return { rows: rows.map((tr) => ({ id: tr.dataset.id, name: (tr.querySelector('.snapname') || {}).textContent || '',
      kernel: tr.dataset.kernel || '', where: tr.dataset.where || '', stored: Number(tr.dataset.stored || 0),
      raw: Number(tr.dataset.raw || 0), restore: !!tr.querySelector('button.restore'), sw: !!tr.querySelector('button.snapswitch'),
      text: tr.innerText.replace(/\\s+/g, ' ') })),
    store: (document.getElementById('snap_store') || {}).textContent || '', status: document.getElementById('snap_status').textContent,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, storeShown: shown(document.getElementById('snap_store')) }; }"""

SNAP_DIFF_JS = """() => { const o = document.getElementById('snap_diff_out');
  if (!o) return null;
  return { state: o.dataset.state || '', pages: [...o.querySelectorAll('#snap_pages [data-page]')].map((e) => [Number(e.dataset.page), Number(e.dataset.bytes)]),
    regs: [...o.querySelectorAll('#snap_regs [data-reg]')].map((e) => [e.dataset.reg, e.dataset.old, e.dataset.new]),
    screen: [...o.querySelectorAll('#snap_screen [data-line]')].map((e) => Number(e.dataset.line)),
    summary: (document.getElementById('snap_diff_sum') || {}).textContent || '',
    icount: [o.dataset.icountA, o.dataset.icountB], text: o.innerText.slice(0, 4000) }; }"""


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


def snap_parse_state(raw):
    """v86 0.5.471 save_state() (src/state.js): 16-byte header (magic
    0x86768676, version 6, total length, info length), the JSON info block
    {buffer_infos, state} padded to 4, then the buffers, each aligned to 4.
    The CPU's state[77] is the non-zero pages packed, state[78] the bitmap
    (bit p of byte p >> 3) of which pages they are, state[0] the RAM size."""
    import struct
    magic, version, total, info_len = struct.unpack_from("<iiii", raw, 0)
    if magic & 0xffffffff != 0x86768676 or version != 6 or total != len(raw):
        raise ValueError(f"not a v86 state: {magic & 0xffffffff:#x} v{version} {total}/{len(raw)}")
    info = json.loads(raw[16:16 + info_len])
    start = (16 + info_len + 3) & ~3
    def buf(o):
        bi = info["buffer_infos"][o["buffer_id"]]
        return raw[start + bi["offset"]:start + bi["offset"] + bi["length"]]

    st = info["state"]
    size, packed, bitmap = st[0], buf(st[77]), buf(st[78])
    ram = bytearray(size)
    k = 0
    for p in range(size // 4096):
        if bitmap[p >> 3] >> (p & 7) & 1:
            ram[p * 4096:p * 4096 + 4096] = packed[k * 4096:k * 4096 + 4096]
            k += 1
    return ram, st


def lz4_frame_decode(data):
    """The LZ4 frame format (lz4/doc/lz4_Frame_format.md, version 01) and
    its block format, decoded here independently of the page's lz4frame.js:
    magic 0x184D2204, FLG and BD bytes, an optional content size and
    dictionary ID, the header checksum, then blocks (a 32-bit size, the
    high bit set for a stored block; an optional block checksum) up to a
    zero size, and an optional content checksum. Blocks may refer back
    into earlier blocks (FLG bit 5 clear), so the output is one history.
    The block format's end rules are enforced as the reference decoder
    does for a full block: the last match starts at least 12 bytes before
    the block's end and the last 5 bytes are literals (lz4js 0.2.0 broke
    them and `lz4 -d` refused its 4 MiB blocks)."""
    import struct
    if struct.unpack_from("<I", data, 0)[0] != 0x184D2204:
        raise ValueError(f"not an LZ4 frame: magic {data[:4].hex()}")
    flg = data[4]
    if flg >> 6 != 1:
        raise ValueError(f"LZ4 frame version {flg >> 6}")
    i = 6 + (8 if flg & 8 else 0) + (4 if flg & 1 else 0) + 1
    out = bytearray()
    while True:
        size = struct.unpack_from("<I", data, i)[0]
        i += 4
        if size == 0:
            break
        n = size & 0x7fffffff
        block = data[i:i + n]
        i += n + (4 if flg & 0x10 else 0)
        if size & 0x80000000:
            out += block
            continue
        j, base, last_start, last_end = 0, len(out), 0, 0
        while j < n:
            token = block[j]
            j += 1
            lit = token >> 4
            if lit == 15:
                while True:
                    b = block[j]
                    j += 1
                    lit += b
                    if b != 255:
                        break
            out += block[j:j + lit]
            j += lit
            if j >= n:
                break
            off = block[j] | block[j + 1] << 8
            j += 2
            ml = token & 15
            if ml == 15:
                while True:
                    b = block[j]
                    j += 1
                    ml += b
                    if b != 255:
                        break
            ml += 4
            if off == 0 or off > len(out):
                raise ValueError(f"LZ4 match offset {off} outside the {len(out)} bytes decoded")
            start = len(out) - off
            last_start, last_end = len(out) - base, len(out) - base + ml
            if off >= ml:
                out += out[start:start + ml]
            else:
                for k in range(ml):
                    out.append(out[start + k])
        size_out = len(out) - base
        if last_end and (last_start > size_out - 12 or last_end > size_out - 5):
            raise ValueError(f"LZ4 block of {size_out} bytes: a match at {last_start}..{last_end} breaks the end rules "
                             "(last match 12 bytes before the end, last 5 bytes literals)")
    return bytes(out)


def snap_check_download(path, want_ram, codec):
    """A downloaded snapshot: LZ4 frame (or gzip for an S1 one) of v86's
    state, decoded in Python (and by the lz4 tool when it is installed;
    CI installs it). Returns problems and the decoded sizes."""
    problems = []
    data = pathlib.Path(path).read_bytes()
    try:
        raw = lz4_frame_decode(data) if codec == "lz4" else gzip.decompress(data)
    except (ValueError, OSError, IndexError, EOFError) as e:
        return [f"the {codec} download ({len(data)} bytes, starts {data[:6].hex()}) does not decode: {e}"], [len(data)]
    if codec == "lz4":
        tool = shutil.which("lz4")
        if tool:
            cli = subprocess.run([tool, "-d", "-c", str(path)], capture_output=True)
            if cli.returncode or cli.stdout != raw:
                problems.append(f"lz4 -d of the download: exit {cli.returncode}, {len(cli.stdout)} bytes, "
                                f"same as the Python decoder: {cli.stdout == raw} {cli.stderr[:200]!r}")
        elif os.environ.get("CI"):
            problems.append("no lz4 tool on the CI runner to check the download with")
    ram, _ = snap_parse_state(raw)
    if bytes(ram) != want_ram:
        problems.append(f"the downloaded {codec} state's RAM differs from v86's RAM")
    return problems, [len(data), len(raw), len(ram)]


SNAP_ROW_CODEC_JS = """(id) => { const tr = document.querySelector(`#snapshots tr[data-id='${id}']`);
  return tr ? { codec: tr.dataset.codec || '', text: tr.innerText.replace(/\\s+/g, ' ') } : null; }"""


def snap_row_codec(page, tag, row_id, codec):
    """The row names the compression, its level and how long it took."""
    x = page.evaluate(SNAP_ROW_CODEC_JS, row_id) or {"codec": "", "text": ""}
    want = {"lz4": r"LZ4 level 1\b.*\d+ ms", "gzip": r"gzip level 6\b"}[codec]
    if x["codec"] != codec or not re.search(want, x["text"]):
        return [f"{tag}: row {row_id} says compression {x['codec']!r}: {x['text'][:200]!r} (want {codec}, /{want}/)"]
    return []


def snapdiff_run(browser, base, shots, name, scheme, phone):
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"{name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, accept_downloads=True,
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    def shot(s):
        page.screenshot(path=str(shots / f"snapdiff-{name}-{w}-{scheme}-{s}.png"), full_page=True)

    url = f"{base}/index.html?kernel={name}&paused=1#debugger"

    def start():
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
        page.evaluate(SNAP_JS)
        return ok

    if not start():
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        problems.append(f"{tag}: Run to the kernel entry stopped at {at:#x}: {status}")
    page.evaluate("mkbSnap.keep('A')")
    cpu_a = page.evaluate("mkbSnap.cpu()")
    st = snap_save(page, 1, phone)
    if len(st["rows"]) != 1:
        problems.append(f"{tag}: Save snapshot gave {len(st['rows'])} rows: {st['status']}")
        ctx.close()
        return problems, r
    a = st["rows"][0]
    r["A"] = a["name"]
    # The default name: kernel, instruction count, symbol at EIP, time.
    if name not in a["name"] or not re.search(r"\d", a["name"]):
        problems.append(f"{tag}: default name {a['name']!r} lacks the kernel or the instruction count")
    # Step N.
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS[name]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != cpu_a["icount"] and
             page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)
    page.evaluate("mkbSnap.keep('B')")
    cpu_b = page.evaluate("mkbSnap.cpu()")
    want_pages = page.evaluate("mkbSnap.pages('A', 'B')")
    want_regs = sorted(k for k in cpu_a["regs"] if cpu_a["regs"][k] != cpu_b["regs"][k])
    want_screen = [i for i in range(max(len(cpu_a["screen"]), len(cpu_b["screen"])))
                   if (cpu_a["screen"] + [""] * 99)[i] != (cpu_b["screen"] + [""] * 99)[i]]
    b_hex = {p: page.evaluate(f"mkbSnap.hex('B', {p})") for p, _ in want_pages[:6]}
    r["want"] = {"pages": len(want_pages), "regs": want_regs, "screen": want_screen,
                 "icount": [cpu_a["icount"], cpu_b["icount"]]}
    if not want_pages or not want_regs:
        problems.append(f"{tag}: stepping {SNAPDIFF_STEPS[name]} changed no page or register (test setup)")
    st = snap_save(page, 2, phone)
    if len(st["rows"]) != 2:
        problems.append(f"{tag}: the second save gave {len(st['rows'])} rows")
        ctx.close()
        return problems, r
    a_id, b_id = st["rows"][0]["id"], st["rows"][1]["id"]
    if st["rows"][0]["name"] == st["rows"][1]["name"]:
        problems.append(f"{tag}: two snapshots got the same default name {st['rows'][0]['name']!r}")

    # Rename B (Enter saves), start renaming A and press Esc (cancels).
    new_name = f"after {SNAPDIFF_STEPS[name]} steps ({scheme})"
    snap_click(page, f"#snapshots tr[data-id='{b_id}'] .snapname", phone)
    inp = page.locator(f"#snapshots tr[data-id='{b_id}'] input.snapedit")
    if not inp.count():
        problems.append(f"{tag}: tapping the name gives no text field to rename it")
    else:
        inp.fill(new_name)
        inp.press("Enter")
    snap_click(page, f"#snapshots tr[data-id='{a_id}'] .snapname", phone)
    inp = page.locator(f"#snapshots tr[data-id='{a_id}'] input.snapedit")
    if inp.count():
        inp.fill("not this")
        inp.press("Escape")
    time.sleep(0.5)
    st = snap_rows(page)
    names = {x["id"]: x["name"] for x in st["rows"]}
    if names.get(b_id) != new_name or names.get(a_id) != r["A"]:
        problems.append(f"{tag}: after renaming B and cancelling A the names are {names}")
    if f"{name}" not in st["store"] or "stored" not in st["store"].lower():
        problems.append(f"{tag}: the storage line {st['store']!r} does not say what is stored")
    for rid in (a_id, b_id):
        problems += snap_row_codec(page, tag, rid, "lz4")
    shot("saved")

    # The download is an LZ4 frame of v86's state (C1): decoded and parsed
    # here independently, its RAM is what v86 had at B.
    if name == "hello" and not phone:
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{b_id}'] button.snapdl").click()
        fname = dl.value.suggested_filename
        if not fname.endswith(".v86state.lz4"):
            problems.append(f"{tag}: the download is named {fname!r}, want *.v86state.lz4")
        bad, r["download"] = snap_check_download(dl.value.path(), base64.b64decode(page.evaluate("mkbSnap.b64('B')")), "lz4")
        problems += [f"{tag}: {b}" for b in bad]

    # Reload: both are listed with names, sizes and a matching kernel.
    if not start():
        problems.append(f"{tag}: the reload did not start paused")
    show_tab(page, "snapshots")
    st = snap_wait_rows(page, 2)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    st = snap_rows(page)
    r["after reload"] = [(x["name"], x["kernel"], x["where"], x["stored"]) for x in st["rows"]]
    names = {x["id"]: x["name"] for x in st["rows"]}
    if names.get(a_id) != r["A"] or names.get(b_id) != new_name:
        problems.append(f"{tag}: after the reload the snapshots are {names} (want {a_id}: {r['A']!r}, {b_id}: {new_name!r})")
    for x in st["rows"]:
        if x["kernel"] != "match" or x["where"] != "stored" or not x["stored"] or not x["restore"]:
            problems.append(f"{tag}: after the reload row {x['id']} says kernel {x['kernel']!r}, kept {x['where']!r}, "
                            f"{x['stored']} bytes, Restore {x['restore']}")
        if not re.search(r"\d[\d,.]* ?(KiB|MiB|bytes)", x["text"]):
            problems.append(f"{tag}: row {x['id']} shows no size: {x['text'][:120]}")
    if not re.search(r"\d+ snapshots?", st["store"]) or not re.search(r"(KiB|MiB|bytes)", st["store"]):
        problems.append(f"{tag}: the storage line {st['store']!r} lacks the count or the total size")
    if st["hscroll"]:
        problems.append(f"{tag}: horizontal scroll on the Snapshots tab")

    # Diff A -> B.
    page.select_option("#snap_a", a_id)
    page.select_option("#snap_b", b_id)
    snap_click(page, "#snap_diff", phone)
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"{tag}: the diff did not finish: {page.evaluate(SNAP_DIFF_JS)}")
        ctx.close()
        return problems, r
    d = page.evaluate(SNAP_DIFF_JS)
    r["diff"] = {"pages": len(d["pages"]), "regs": [x[0] for x in d["regs"]], "screen": d["screen"], "summary": d["summary"][:200]}
    if sorted(map(tuple, d["pages"])) != sorted(map(tuple, want_pages)):
        miss = sorted(set(map(tuple, want_pages)) - set(map(tuple, d["pages"])))
        extra = sorted(set(map(tuple, d["pages"])) - set(map(tuple, want_pages)))
        problems.append(f"{tag}: the diff's pages differ from v86's RAM: missing {miss[:6]}, extra {extra[:6]}")
    got_regs = sorted(x[0] for x in d["regs"])
    if got_regs != want_regs:
        problems.append(f"{tag}: the diff lists registers {got_regs}, these changed: {want_regs}")
    for reg, old, new in d["regs"]:
        if reg in cpu_a["regs"] and (int(old, 16) != cpu_a["regs"][reg] or int(new, 16) != cpu_b["regs"][reg]):
            problems.append(f"{tag}: {reg} {old} -> {new}, v86 had {cpu_a['regs'][reg]:#x} -> {cpu_b['regs'][reg]:#x}")
    if d["screen"] != want_screen:
        problems.append(f"{tag}: the diff lists screen lines {d['screen']}, these changed: {want_screen}")
    if d["icount"] != [str(cpu_a["icount"]), str(cpu_b["icount"])]:
        problems.append(f"{tag}: instruction counts {d['icount']}, v86 counted {cpu_a['icount']} and {cpu_b['icount']}")
    for n in (r["A"], new_name):
        if n not in d["text"]:
            problems.append(f"{tag}: the diff does not name {n!r}")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll with the diff shown")
    shot("diff")

    # The side-by-side hexdump of one page: one mark per changed byte in B's column.
    p0, n0 = want_pages[0]
    snap_click(page, f"#snap_pages [data-page='{p0}'] button.snaphex", phone)
    wait_for(lambda: page.get_attribute("#snap_hex", "data-page") == str(p0), 20)
    marks = page.evaluate("[...document.querySelectorAll('#snap_hex .snapcol-b mark')].reduce((n, m) => n + m.textContent.trim().split(/\\s+/).length, 0)")
    r["hexdump"] = [p0, n0, marks]
    if marks != n0:
        problems.append(f"{tag}: the hexdump of page {p0:#x} marks {marks} bytes, {n0} changed")
    shot("hex")

    # Open B's page in the Page tab: its bytes as B had them; Back returns.
    p1 = next((p for p, _ in want_pages if p in b_hex), p0)
    snap_click(page, f"#snap_pages [data-page='{p1}'] button.snapopen", phone)
    ok = wait_for(lambda: shown_tab(page) == "page" and page.get_attribute("#listing", "data-page") == str(p1), 15)
    got = page.evaluate("(() => { const d = document.getElementById('listing')._drawn; return d ? Array.from(d.bytes, (b) => b.toString(16).padStart(2, '0')).join('') : ''; })()")
    banner = page.evaluate("(() => { const e = document.getElementById('page_snap'); return e && !e.hidden ? e.textContent : ''; })()")
    r["open"] = [p1, ok, banner[:100]]
    if not ok or got != b_hex.get(p1, got):
        problems.append(f"{tag}: opening page {p1:#x} of B: tab {shown_tab(page)}, listing {page.get_attribute('#listing', 'data-page')}, "
                        f"bytes are B's: {got == b_hex.get(p1)}")
    if new_name not in banner:
        problems.append(f"{tag}: the Page tab does not say it shows {new_name!r}: {banner!r}")
    shot("page")
    nav_press(page, "#nav_back", "tap" if phone else "click")
    if not wait_for(lambda: shown_tab(page) == "snapshots", 5):
        problems.append(f"{tag}: Back from the snapshot's page goes to {shown_tab(page)}")

    # Restore B after the reload: same EIP, registers, instruction count, screen.
    snap_click(page, f"#snapshots tr[data-id='{b_id}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == b_id, 30)
    wait_for(lambda: page.evaluate("mkbSnap.cpu()")["screen"] == cpu_b["screen"], 10)
    now = page.evaluate("mkbSnap.cpu()")
    r["restored"] = [hex(now["eip"]), now["icount"]]
    if now["eip"] != cpu_b["eip"] or now["icount"] != cpu_b["icount"] or now["regs"] != cpu_b["regs"]:
        problems.append(f"{tag}: restoring B after the reload: EIP {now['eip']:#x} (want {cpu_b['eip']:#x}), "
                        f"instructions {now['icount']} (want {cpu_b['icount']}), regs equal {now['regs'] == cpu_b['regs']}")
    if now["screen"] != cpu_b["screen"]:
        problems.append(f"{tag}: the screen after restoring B differs from B's")
    # v86's set_state leaves last_result (the lazy flags' input), the FPU
    # status word and MXCSR outside wasm memory: the page puts them back.
    views = page.evaluate("(() => { const c = emulator.v86.cpu; return ['last_result', 'fpu_status_word', 'mxcsr']"
                          ".filter((k) => c[k].buffer !== c.wasm_memory.buffer); })()")
    if views:
        problems.append(f"{tag}: after the restore {views} are not in v86's wasm memory")

    # Diff B -> now is empty right after the restore (RAM and registers).
    page.select_option("#snap_a", b_id)
    page.select_option("#snap_b", "now")
    snap_click(page, "#snap_diff", phone)
    wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60)
    d = page.evaluate(SNAP_DIFF_JS)
    r["B -> now"] = [len(d["pages"]), [x[0] for x in d["regs"]]]
    if d["pages"] or d["regs"]:
        problems.append(f"{tag}: right after restoring B, B -> now lists pages {d['pages'][:5]} and registers {d['regs'][:5]}")

    # Delete A: gone, also after a reload.
    snap_click(page, f"#snapshots tr[data-id='{a_id}'] button.snapdel", phone)
    st = snap_wait_rows(page, 1)
    if [x["id"] for x in st["rows"]] != [b_id]:
        problems.append(f"{tag}: after deleting A: {[x['id'] for x in st['rows']]}")
    start()
    show_tab(page, "snapshots")
    time.sleep(1)
    st = snap_wait_rows(page, 1)
    if [(x["id"], x["name"]) for x in st["rows"]] != [(b_id, new_name)]:
        problems.append(f"{tag}: after deleting A and a reload: {[(x['id'], x['name']) for x in st['rows']]}")
    shot("deleted")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_storage(browser, base, shots):
    """No storage (a private window, blocked site data): snapshots stay in
    memory with a note. A full quota: the save says so and keeps the copy
    in memory. Delete all empties the store."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script("Object.defineProperty(window, 'indexedDB', { get() { throw new DOMException('blocked by the test', 'SecurityError'); } });")
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    r["blocked"] = [(x["where"], x["kernel"]) for x in st["rows"]] + [st["store"][:160]]
    if len(st["rows"]) != 1 or st["rows"][0]["where"] != "memory" or not st["rows"][0]["restore"]:
        problems.append(f"blocked storage: the snapshot is not kept in memory with a Restore: {st['rows']}")
    if "memory" not in st["store"] or "not" not in st["store"]:
        problems.append(f"blocked storage: the note {st['store']!r} does not say the snapshots are only in memory")
    page.screenshot(path=str(shots / "snapdiff-blocked.png"), full_page=True)
    if errors:
        problems.append(f"blocked storage: page errors {errors[:3]}")
    ctx.close()

    # A quota below what the site uses (the service worker's cache alone is
    # megabytes), set before the page opens its database (Chrome checks the
    # quota when the database is opened): the save fails with
    # QuotaExceededError and the snapshot stays in memory.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    cdp = ctx.new_cdp_session(page)
    cdp.send("Storage.overrideQuotaForOrigin", {"origin": base, "quotaSize": 20000})
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    wait_for(lambda: "quota" in snap_rows(page)["status"].lower(), 15)
    st = snap_rows(page)
    r["quota"] = [[x["where"] for x in st["rows"]], st["status"][:200], st["store"][:200]]
    if [x["where"] for x in st["rows"]] != ["memory"] or "quota" not in st["status"].lower():
        problems.append(f"full quota: rows kept {[x['where'] for x in st['rows']]}, status {st['status']!r}")
    if "memory only" not in st["store"]:
        problems.append(f"full quota: the storage line {st['store']!r} does not count the snapshot as in memory only")
    page.screenshot(path=str(shots / "snapdiff-quota.png"), full_page=True)
    cdp.send("Storage.overrideQuotaForOrigin", {"origin": base})
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    show_tab(page, "snapshots")
    time.sleep(2)
    if snap_rows(page)["rows"]:
        problems.append("full quota: the snapshot that was not stored is listed after a reload")
    snap_save(page, 1, False)
    snap_save(page, 2, False)
    st = snap_rows(page)
    if [x["where"] for x in st["rows"]] != ["stored", "stored"]:
        problems.append(f"with the quota back: rows kept {[x['where'] for x in st['rows']]}")
    # Delete all asks again, then empties the store.
    page.click("#snap_delall")
    if len(snap_rows(page)["rows"]) != 2:
        problems.append("Delete all deleted without asking again")
    page.click("#snap_delall")
    st = snap_wait_rows(page, 0)
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    show_tab(page, "snapshots")
    time.sleep(2)
    st = snap_rows(page)
    if st["rows"]:
        problems.append(f"Delete all: {len(st['rows'])} snapshots listed after a reload")
    if errors:
        problems.append(f"quota: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_kernels(browser, base, shots, phone):
    """A snapshot of another kernel is listed as such, without Restore, with
    a switch to its kernel; after the switch it restores."""
    problems, r = [], {}
    ctx = browser.new_context(color_scheme="dark", **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    snap_save(page, 1, phone)
    machine_do(page, "select_option", "#kernel", "hello-os")
    wait_for(lambda: page.evaluate("image && image.name === 'hello-os' && emuReady"), 120)
    show_tab(page, "snapshots")
    wait_for(lambda: (snap_rows(page)["rows"] or [{}])[0].get("kernel") == "other", 20)
    st = snap_rows(page)
    x = st["rows"][0]
    r["other"] = [x["kernel"], x["restore"], x["sw"], x["text"][:120]]
    if x["kernel"] != "other" or x["restore"] or not x["sw"] or "another kernel" not in x["text"]:
        problems.append(f"another kernel: the hello snapshot under hello-os shows {r['other']}")
    if st["hscroll"]:
        problems.append("another kernel: horizontal scroll")
    page.screenshot(path=str(shots / f"snapdiff-other-{390 if phone else 1280}.png"), full_page=True)
    snap_click(page, "#snapshots button.snapswitch", phone)
    wait_for(lambda: page.evaluate("image && image.name === 'hello' && emuReady"), 120)
    wait_for(lambda: (snap_rows(page)["rows"] or [{}])[0].get("kernel") == "match", 20)
    x = snap_rows(page)["rows"][0]
    if x["kernel"] != "match" or not x["restore"]:
        problems.append(f"another kernel: after the switch the row shows kernel {x['kernel']!r}, Restore {x['restore']}")
    if errors:
        problems.append(f"another kernel: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_LONG_JS = """() => { window.mkbLong = []; new PerformanceObserver((l) => {
    for (const e of l.getEntries()) window.mkbLong.push(Math.round(e.duration)); }).observe({ type: 'longtask' }); }"""


def snapdiff_perf(browser, base, shots, mem):
    """A diff of two snapshots of mem bytes of RAM, and of one and now, at
    4x CPU throttle: no task on the page's main thread over 200 ms."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={mem}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    run_to_wait(page, {0x100000}, 300)
    snap_save(page, 1, False)
    show_tab(page, "debugger")
    page.fill("#dbg_n", "60")
    page.click("#dbg_stepn")
    time.sleep(2)
    st = snap_save(page, 2, False)
    ids = [x["id"] for x in st["rows"]]
    cdp = ctx.new_cdp_session(page)
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": 4})
    page.evaluate(SNAP_LONG_JS)
    for a, b in ((ids[0], ids[1]), (ids[1], "now")):
        page.select_option("#snap_a", a)
        page.select_option("#snap_b", b)
        t0 = time.monotonic()
        page.click("#snap_diff")
        done = wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 180)
        r[f"{a}->{b} seconds"] = round(time.monotonic() - t0, 1)
        if not done:
            problems.append(f"{mem >> 20} MiB: the diff {a} -> {b} did not finish at 4x throttle")
    # the hexdump and the Page tab of a changed page
    d = page.evaluate(SNAP_DIFF_JS)
    if d and d["pages"]:
        page.click(f"#snap_pages [data-page='{d['pages'][0][0]}'] button.snaphex")
        wait_for(lambda: page.get_attribute("#snap_hex", "data-page") == str(d["pages"][0][0]), 30)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    r["long tasks ms"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"{mem >> 20} MiB at 4x throttle: a {max(long)} ms task on the main thread (want <= 200)")
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": 1})
    if errors:
        problems.append(f"{mem >> 20} MiB: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_LEGACY_JS = """async (id) => {
  // An S1 (gzip) record of the same state, stored as S1 stored it.
  const s = snapshots.find((x) => x.id === id);
  const raw = await snapUnpack(s);
  const gz = await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer();
  const rec = { id: s.id + 'gz', seq: s.seq + 1, name: 'S1 gzip snapshot', time: s.time + 1, kernel: s.kernel,
    kernelKey: s.kernelKey, ram: s.ram, raw: raw.byteLength, size: gz.byteLength, gzip: true, saveMs: 5, gzipMs: 25,
    meta: s.meta, data: new Blob([gz]) };
  await snapTx('readwrite', (st) => st.put(rec));
  return rec.id; }"""


def snapdiff_legacy(browser, base, shots):
    """Snapshots stored by S1 (gzip, no codec field) still list, restore,
    diff against an LZ4 one and download as .gz after the switch to LZ4."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel=hello&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate("mkbSnap.keep('A')")
    cpu_a = page.evaluate("mkbSnap.cpu()")
    st = snap_save(page, 1, False)
    lz_id = st["rows"][0]["id"]
    gz_id = page.evaluate(SNAP_LEGACY_JS, lz_id)
    # Step on, so the restore has something to undo.
    show_tab(page, "debugger")
    page.fill("#dbg_n", "40")
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != cpu_a["icount"] and
             page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    show_tab(page, "snapshots")
    st = snap_wait_rows(page, 2)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    r["rows"] = [(x["name"], x["where"]) for x in snap_rows(page)["rows"]]
    problems += snap_row_codec(page, "S1 gzip row", gz_id, "gzip")
    problems += snap_row_codec(page, "LZ4 row", lz_id, "lz4")
    # Restore the gzip one: v86 is back at A.
    snap_click(page, f"#snapshots tr[data-id='{gz_id}'] button.restore", False)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == gz_id, 30)
    now = page.evaluate("mkbSnap.cpu()")
    r["restored"] = [hex(now["eip"]), now["icount"]]
    if now["regs"] != cpu_a["regs"] or now["icount"] != cpu_a["icount"]:
        problems.append(f"restoring the S1 gzip snapshot: EIP {now['eip']:#x} (want {cpu_a['eip']:#x}), "
                        f"instructions {now['icount']} (want {cpu_a['icount']})")
    page.evaluate("mkbSnap.keep('A2')")
    # The same state twice: the diff gzip -> LZ4 is empty.
    page.select_option("#snap_a", gz_id)
    page.select_option("#snap_b", lz_id)
    page.click("#snap_diff")
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"diff S1 gzip -> LZ4 did not finish: {page.inner_text('#snap_diff_status')!r}")
    else:
        d = page.evaluate(SNAP_DIFF_JS)
        r["gzip -> lz4"] = [len(d["pages"]), d["regs"]]
        if d["pages"] or d["regs"]:
            problems.append(f"diff of the same state as gzip and as LZ4 lists pages {d['pages'][:5]} registers {d['regs'][:5]}")
    # Downloads: .gz for the S1 one, .lz4 for the new one; both are A's RAM.
    mem = base64.b64decode(page.evaluate("mkbSnap.b64('A2')"))
    for rid, codec, ext in ((gz_id, "gzip", ".v86state.gz"), (lz_id, "lz4", ".v86state.lz4")):
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{rid}'] button.snapdl").click()
        if not dl.value.suggested_filename.endswith(ext):
            problems.append(f"the {codec} snapshot downloads as {dl.value.suggested_filename!r}, want *{ext}")
        bad, r[f"download {codec}"] = snap_check_download(dl.value.path(), mem, codec)
        problems += bad
    page.screenshot(path=str(shots / "snapdiff-legacy-1280.png"), full_page=True)
    if errors:
        problems.append(f"S1 gzip: page errors {errors[:3]}")
    ctx.close()
    return problems, r


# 24 MiB of guest RAM filled with random runs, repeated runs and zeros
# (gzip -6 keeps about 35% of it, LZ4 level 1 too): a state the size of
# the Debian 64 MiB one at start_kernel (23 MB) that is harder to compress.
SNAP_FILL_JS = """(mib) => { let x = 12345; const rnd = () => (x = (Math.imul(x, 1103515245) + 12345) >>> 0) >>> 8;
  const chunk = new Uint8Array(1 << 20);
  for (let m = 0; m < mib; m++) {
    for (let i = 0; i < chunk.length;) { const n = 16 + (rnd() & 1023), kind = rnd() % 3, v = rnd() & 255;
      for (let k = 0; k < n && i < chunk.length; k++, i++) chunk[i] = kind === 0 ? rnd() & 255 : kind === 1 ? v : 0; }
    emulator.v86.cpu.mem8.set(chunk, (8 + m) << 20); } }"""


def snapdiff_bigsave(browser, base, shots, phone):
    """C1: Save snapshot of a 64 MiB machine with 24 MiB of data is
    compressed with LZ4 in a Worker: the row says LZ4 level 1 and its
    times, no task on the page's main thread over 200 ms while saving, the
    snapshot is smaller than the state; Restore gives the RAM back."""
    problems, r = [], {}
    ctx = browser.new_context(color_scheme="dark" if phone else "light",
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={64 << 20}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    # At hello_main, where the 24 MiB of data goes (the power-on save is
    # snapdiff_poweron's).
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate(SNAP_FILL_JS, 24)
    page.evaluate("mkbSnap.keep('F')")
    time.sleep(1)
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    st = snap_save(page, 1, phone)
    r["save seconds"] = round(time.monotonic() - t0, 2)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    s = page.evaluate("(() => { const { data, meta, ...s } = snapshots[0]; return s; })()")
    r["snapshot"] = {k: s.get(k) for k in ("raw", "size", "codec", "level", "saveMs", "packMs")}
    r["long tasks ms (save)"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"Save snapshot of {s['raw']} bytes: a {max(long)} ms task on the main thread (want <= 200)")
    problems += snap_row_codec(page, "big save", st["rows"][0]["id"], "lz4")
    if not s.get("size") or s["size"] >= s["raw"]:
        problems.append(f"the LZ4 snapshot is {s.get('size')} bytes for {s['raw']} raw")
    page.screenshot(path=str(shots / f"snapdiff-bigsave-{390 if phone else 1280}.png"), full_page=True)
    # Restore after scribbling over the filled RAM.
    page.evaluate("emulator.v86.cpu.mem8.fill(0x55, 8 << 20, 9 << 20)")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    snap_click(page, f"#snapshots tr[data-id='{st['rows'][0]['id']}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == st["rows"][0]["id"], 60)
    r["restore seconds"] = round(time.monotonic() - t0, 2)
    r["restore status"] = page.inner_text("#snap_status")[:200]
    r["long tasks ms (restore)"] = sorted(page.evaluate("window.mkbLong"))[-5:]
    page.evaluate("mkbSnap.keep('G')")
    if page.evaluate("mkbSnap.pages('F', 'G')"):
        problems.append("after Restore the 64 MiB machine's RAM differs from the snapshot's")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"horizontal scroll at {390 if phone else 1280} px")
    if errors:
        problems.append(f"big save: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_POWERON_TARGET = {"hello": "hello_main", "minified-linux": "start_kernel"}


def snapdiff_poweron(browser, base, shots, name, scheme, phone):
    """B1: a snapshot saved while paused at power-on (the reset vector, before
    SeaBIOS ran) restores. v86 0.5.471's PCI set_state moved the I/O BARs of
    every BAR slot with bit 0 set, including slots with no BAR (at power-on
    the config space still holds the device's initial bytes there), and
    threw "Cannot read properties of undefined (reading 'original_bar')".
    Save at 0xF000:0xFFF0, boot on to hello_main / start_kernel, Restore:
    the registers, the instruction count and the RAM are the snapshot's;
    then the machine boots on from there to the same target (and hello
    prints its lines). No page error; screenshots."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"power-on {name} {w} {scheme}"
    target = SNAP_POWERON_TARGET[name]
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
    page.evaluate(SNAP_JS)
    cpu_p = page.evaluate("mkbSnap.cpu()")
    r["at save"] = {"CS:EIP": f"{cpu_p['regs']['CS']:#x}:{cpu_p['eip']:#x}", "icount": cpu_p["icount"]}
    if cpu_p["regs"]["CS"] != 0xF000 or cpu_p["eip"] != 0xFFF0:
        problems.append(f"{tag}: not paused at the reset vector: {r['at save']['CS:EIP']} (test setup)")
    # The slots that broke v86's PCI set_state: no BAR, I/O bit set.
    r["BAR slots without a BAR, I/O bit set"] = page.evaluate("""() => { const pci = emulator.v86.cpu.devices.pci, out = [];
      pci.devices.forEach((d, id) => { if (d && d.pci_bars) for (let i = 0; i < d.pci_bars.length; i++)
        if (!d.pci_bars[i] && pci.device_spaces[id][4 + i] & 1) out.push((d.name || id) + ' BAR' + i); }); return out; }""")
    page.evaluate("mkbSnap.keep('P')")
    st = snap_save(page, 1, phone)
    row = st["rows"][0]["id"]
    r["saved"] = st["status"][:160]

    def run_to(what):
        show_tab(page, "debugger")
        page.fill("#dbg_addr", target)
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                      target in page.inner_text("#dbg_status"), 300)
        if not ok:
            problems.append(f"{tag}: Run to {target} {what} did not stop there: {page.inner_text('#dbg_status')[:200]}")
        return ok

    run_to("before Restore")
    show_tab(page, "snapshots")
    snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
             "failed" in page.inner_text("#snap_status"), 60)
    r["restore status"] = page.inner_text("#snap_status")[:200]
    if page.get_attribute("#snap_status", "data-restored") != row:
        problems.append(f"{tag}: Restore of the power-on snapshot: {r['restore status']}")
    page.screenshot(path=str(shots / f"snapdiff-poweron-{name}-{w}-{scheme}-restored.png"), full_page=True)
    cpu_q = page.evaluate("mkbSnap.cpu()")
    r["after restore"] = {"CS:EIP": f"{cpu_q['regs']['CS']:#x}:{cpu_q['eip']:#x}", "icount": cpu_q["icount"]}
    bad = {k: (f"{cpu_p['regs'][k]:#x}", f"{cpu_q['regs'][k]:#x}") for k in cpu_p["regs"] if cpu_p["regs"][k] != cpu_q["regs"][k]}
    if bad:
        problems.append(f"{tag}: registers after Restore differ from the snapshot's (saved, now): {bad}")
    if cpu_q["icount"] != cpu_p["icount"]:
        problems.append(f"{tag}: instruction count after Restore {cpu_q['icount']}, saved {cpu_p['icount']}")
    page.evaluate("mkbSnap.keep('Q')")
    diff = page.evaluate("mkbSnap.pages('P', 'Q')")
    if diff:
        problems.append(f"{tag}: after Restore {len(diff)} RAM pages differ from the snapshot's, first {diff[:4]}")
    if page.get_attribute("#dbg_state", "data-state") != "paused":
        problems.append(f"{tag}: after Restore the machine is {page.get_attribute('#dbg_state', 'data-state')}, want paused")
    # It boots on from the reset vector.
    if run_to("after Restore") and name == "hello":
        machine_do(page, "click", "#dbg_continue")
        if not wait_for(lambda: ALIVE in page.inner_text("#screen_text"), 60):
            problems.append(f"{tag}: after Restore and Continue the screen never showed {ALIVE!r}: "
                            f"{page.inner_text('#screen_text')[:200]!r}")
    r["status"] = page.inner_text("#dbg_status")[:160]
    page.screenshot(path=str(shots / f"snapdiff-poweron-{name}-{w}-{scheme}-booted.png"), full_page=True)
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")
    if errors:
        problems.append(f"{tag}: page errors {errors[:3]}")
    ctx.close()
    return problems, r


# D1: a snapshot is a point on the virtual-clock timeline. Restoring it
# and stepping on must give the same machine as stepping on from it the
# first time: v86's devices keep absolute timestamps (the PIT's counter
# start, the RTC's last update) and restore_state sets the TSC offset from
# the clock it reads at that moment, so the page puts its virtual clock and
# the stepper's count back to the snapshot's before restore_state. Before
# that, the clock ran on from where the machine was when Restore was
# pressed, so the timer interrupts and every TSC/PIT read after a restore
# were shifted by the time stepped in between.
SNAP_REPLAY = {"hello": (600_000, 600_000), "minified-linux": (1_500_000, 500_000)}
# R3: saved at 1.3 M, hello's SeaBIOS reads the TSC before 1.4 M (its
# calibration). v86's set_tsc left a restored TSC off by the offset the
# machine had before the Restore, and SeaBIOS's data then differed (RAM
# after the 100,000 steps).
SNAP_REPLAY_TSC = (1_300_000, 100_000)
SNAP_REPLAY_STATE_JS = """async () => { const c = emulator.v86.cpu, m = emulator.read_memory(0, emuRam);
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  return { steps: stepper.steps, eip: c.instruction_pointer[0] >>> 0, regs: Array.from(c.reg32, (x) => x >>> 0), eflags: c.get_eflags() >>> 0,
    clock: CLOCK.now(), irq0: stepper.irq0, ram: sha }; }"""


def snap_step_to(page, target):
    """Step (in slices, as the Step button does) until stepper.steps == target."""
    for _ in range(200):
        left = target - page.evaluate("stepper.steps")
        if left <= 0:
            break
        page.evaluate(f"doStep({min(left, 400000)})")
    return page.evaluate("stepper.steps")


def snapdiff_replay(browser, base, shots, name, scheme, phone, point=None):
    """Save at S on the virtual clock, step N, note the machine; Restore,
    step N again: the same EIP, registers, RAM, IRQ0 count (from S) and clock."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    at, n = point or SNAP_REPLAY[name]
    tag = f"replay {name} {w} {scheme}" + (f" at {at:,}" if point else "")
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
    if snap_step_to(page, at) != at:
        problems.append(f"{tag}: could not step to {at} (test setup): {page.inner_text('#dbg_status')[:160]}")
    st = snap_save(page, 1, phone)
    row = st["rows"][0]["id"]
    s0 = page.evaluate(SNAP_REPLAY_STATE_JS)
    snap_step_to(page, at + n)
    first = page.evaluate(SNAP_REPLAY_STATE_JS)
    # Some more time passes before the Restore (as when someone reads the screen).
    snap_step_to(page, at + n + 777)
    show_tab(page, "snapshots")
    snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
             "failed" in page.inner_text("#snap_status"), 60)
    if page.get_attribute("#snap_status", "data-restored") != row:
        problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
    s1 = page.evaluate(SNAP_REPLAY_STATE_JS)
    for k in ("steps", "clock"):
        if s1[k] != s0[k]:
            problems.append(f"{tag}: right after Restore {k} is {s1[k]}, at the save it was {s0[k]}")
    snap_step_to(page, s1["steps"] + n)
    again = page.evaluate(SNAP_REPLAY_STATE_JS)
    r["first"] = {k: first[k] for k in ("steps", "eip", "irq0", "clock")} | {"ram": first["ram"][:16]}
    r["again"] = {k: again[k] for k in ("steps", "eip", "irq0", "clock")} | {"ram": again["ram"][:16]}
    first["irq0"] -= s0["irq0"]  # the page's IRQ0 count is not part of the state
    again["irq0"] -= s1["irq0"]
    for k in ("eip", "regs", "eflags", "irq0", "clock", "ram"):
        if first[k] != again[k]:
            problems.append(f"{tag}: {k} after Restore + {n} steps differs from the first time: {str(again[k])[:80]} vs {str(first[k])[:80]}")
    page.screenshot(path=str(shots / f"snapdiff-replay-{name}-{w}-{scheme}{'-' + str(at) if point else ''}.png"), full_page=True)
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")
    if errors:
        problems.append(f"{tag}: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_guard(fn, *args):
    """A step that times out (or finds nothing to look at) is a failure of
    that case, not of the suite."""
    try:
        return fn(*args)
    except (PlaywrightError, IndexError, KeyError, TypeError) as e:
        return [f"{fn.__name__}: {type(e).__name__}: {(str(e).splitlines() or [''])[0][:300]}"], {}


# ---- one count: every place names the Machine tab's count (X2) -------------
# The Machine tab, the counters table, the run-badge menu and the Reverse
# line count the stepper's instructions; v86's own instruction_counter runs
# 86 (hello) or 87 (minified Linux) ahead of it. Snapshot names and rows, the
# diff summary and the "This position" bookmark showed v86's number until X2.
ONECOUNT_JS = """() => { const now = document.querySelector('#counters tr[data-milestone="now"]');
  return { now: now ? Number(now.dataset.steps) : null, text: document.getElementById('count_text').textContent,
    badge: Number(document.getElementById('runbadge').dataset.steps || -1), steps: stepper.steps,
    icount: emulator.v86.cpu.instruction_counter[0] >>> 0 }; }"""


def onecount_run(browser, base, shots, name, scheme, phone):
    """Pause, read the Machine tab's count N, save a snapshot and a "This
    position" bookmark: each name, row and the diff summary say N."""
    problems, r, errors = [], {}, []
    fn, steps, _ = BOOKMARKS_POINTS[name]
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]}
    w = 390 if phone else 1280
    tag = f"{name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", hex(syms[fn]["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {syms[fn]["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{tag}: Run to {fn} stopped at {at:#x}: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             page.evaluate("stepper.steps") > 0, 120)
    time.sleep(1)
    show_tab(page, "machine")
    time.sleep(0.5)
    m = page.evaluate(ONECOUNT_JS)
    n = m["now"]
    r["machine"] = m
    fmt = f"{n:,}" if n is not None else "?"
    if n is None or n != m["steps"] or n != m["badge"] or fmt not in m["text"]:
        problems.append(f"{tag}: the Machine tab does not show one count: counters {n}, run badge {m['badge']}, "
                        f"line {m['text'][:120]!r}, stepper {m['steps']}")
        ctx.close()
        return problems, r
    if m["icount"] == n:
        problems.append(f"{tag}: v86's counter equals the stepper's ({n}): the case no longer tells the counts apart")
    show_tab(page, "machine")
    shot = lambda s: page.screenshot(path=str(shots / f"onecount-{name}-{w}-{scheme}-{s}.png"), full_page=False)
    shot("machine")

    # The snapshot: default name, its row and the diff summary against now.
    st = snap_save(page, 1, phone)
    row = (st["rows"] or [{}])[0]
    r["snapshot"] = [row.get("name"), row.get("text", "")[:160]]
    if "@" + fmt + " " not in row.get("name", ""):
        problems.append(f"{tag}: snapshot default name {row.get('name')!r} lacks the Machine tab's count @{fmt}")
    if "instruction " + fmt + "," not in row.get("text", ""):
        problems.append(f"{tag}: snapshot row {row.get('text', '')[:160]!r} lacks the Machine tab's count {fmt}")
    page.select_option("#snap_a", row["id"])
    page.select_option("#snap_b", "now")
    snap_click(page, "#snap_diff", phone)
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"{tag}: the diff did not finish")
    else:
        summ = page.evaluate(SNAP_DIFF_JS)["summary"]
        r["diff"] = summ[:200]
        if "instruction " + fmt + " → " + fmt + " (" not in summ:
            problems.append(f"{tag}: the diff summary {summ[:160]!r} does not say instruction {fmt} → {fmt}")
    shot("snapshots")

    # The "This position" bookmark: default name, the list row (and its snapshot).
    bm_open_list(page, phone)
    bm_tap(page, "#bm_here", phone)
    b = bm_dialog_save(page, problems, tag, "pos", want_default="@" + fmt + " ")
    if b:
        r["bookmark"] = [b["name"], b["t"]]
        st = bm_state(page)
        bm_open_list(page, phone)
        st = bm_state(page)
        what = next((x["what"] for x in st["rows"] if x["id"] == b["id"]), "")
        r["bookmark row"] = what
        if "after " + fmt + " instructions" not in what:
            problems.append(f"{tag}: the bookmark row {what!r} does not say after {fmt} instructions")
        if b["t"].get("snapName") != row.get("name"):
            problems.append(f"{tag}: the bookmark is not tied to the snapshot of the same count: {b['t'].get('snapName')!r}")
        shot("bookmark")
    bm_close_menu(page)
    for x in (r.get("snapshot", [""])[0] or "", r.get("diff", ""), (r.get("bookmark") or [""])[0], r.get("bookmark row", "")):
        if f"{m['icount']:,}" in x:
            problems.append(f"{tag}: {x[:120]!r} shows v86's counter {m['icount']:,}")
    # A position bookmark stored before X2 (no steps, only v86's icount) still
    # lists after a reload, with the count it was saved with.
    if b:
        page.evaluate("""(id) => { const j = JSON.parse(localStorage.getItem('mkb-bookmarks'));
          const o = JSON.parse(JSON.stringify(j.bookmarks.find((x) => x.id === id)));
          delete o.t.steps; o.id = id + 'old'; o.name = 'pre-X2 position'; j.bookmarks.push(o);
          localStorage.setItem('mkb-bookmarks', JSON.stringify(j)); }""", b["id"])
        page.reload()
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: len(bm_state(page)["rows"]) == 2, 20)
        old = next((x for x in bm_state(page)["rows"] if x["name"] == "pre-X2 position"), None)
        r["pre-X2 bookmark"] = old and old["what"]
        if not old or "after " + f"{m['icount']:,}" + " instructions" not in old["what"]:
            problems.append(f"{tag}: a pre-X2 position bookmark lists as {old}")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


# Records put into the snapshot store beside the page's own last one: a
# snapshot of a kernel that is not in the list, one of a listed kernel at a
# RAM size the menu does not offer, and a delta whose base is gone.
SNAP_INJECT_JS = """async (recs) => {
  const s = snapshots[snapshots.length - 1];
  for (const x of recs) {
    const rec = { ...snapRecord(s), id: s.id + x.tag, seq: s.seq + x.n, time: s.time + x.n, name: x.name,
      kernel: x.kernel || s.kernel, kernelKey: x.key || s.kernelKey, ram: x.ram || s.ram, delta: x.delta || null };
    await snapTx('readwrite', (st) => st.put(rec));
  }
  return s.id; }"""


def snapdiff_edges(browser, base, shots):
    """What the table does with records it cannot use as they are, and the
    small controls around it: a store a newer deploy opened at a higher
    version (kept in memory, says why), Delete all's second press timing
    out, a rename shown in the diff, Show live memory after a snapshot's
    page, a snapshot of a kernel not in the list, one of a listed kernel at
    a RAM size the menu lacks (the switch adds it), a delta whose base is
    gone (Download and Restore say so), and Restore at another RAM size."""
    problems, r, errors = [], {}, []

    # A newer deploy's database version: open() fails with a VersionError.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script("indexedDB.open('mkb-snapshots', 2)")
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    r["version"] = [[x["where"] for x in st["rows"]], st["store"][:200]]
    if [x["where"] for x in st["rows"]] != ["memory"] or "not stored" not in st["store"] or "version" not in st["store"]:
        problems.append(f"database at a newer version: rows {r['version'][0]}, note {st['store']!r} (want in memory, the reason named)")
    page.screenshot(path=str(shots / "snapdiff-edges-version.png"), full_page=True)
    # In memory only the last 8 stay: the ninth save drops the oldest.
    first = st["rows"][0]["id"]
    for n in range(2, 10):
        snap_save(page, min(n, 8), False)
    wait_for(lambda: page.text_content("#snap_status").startswith("Saved #9"), 15)
    ids = [x["id"] for x in snap_rows(page)["rows"]]
    r["in memory, 9 saves"] = [len(ids), first in ids, page.text_content("#snap_status")[:40]]
    if len(ids) != 8 or first in ids:
        problems.append(f"in memory only: 9 saves leave {len(ids)} rows, the first one kept: {first in ids} (want 8, no)")
    ctx.close()

    # No LZ4 module (a failed download of it): the snapshot is kept raw, says
    # so, restores and downloads as v86's plain state.
    # No service worker either (its cache would have the module): register()
    # fails as in a browser that does not allow them.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    ctx.add_init_script("if (navigator.serviceWorker) navigator.serviceWorker.register = () => Promise.reject(new DOMException('not allowed by the test', 'SecurityError'));")
    ctx.route("**/lz4_wasm_bg.wasm*", lambda route: route.fulfill(status=404, body="gone"))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.evaluate("mkbSnap.keep('A')")
    icount = page.evaluate("mkbSnap.cpu().icount")
    st = snap_save(page, 1, False)
    raw_id = st["rows"][0]["id"]
    row = page.evaluate(SNAP_ROW_CODEC_JS, raw_id) or {}
    r["no lz4"] = [st["status"][-90:], row.get("codec"), row.get("text", "")[-80:]]
    if "Not compressed (" not in st["status"] or "HTTP 404" not in st["status"] or row.get("codec") != "raw" or \
            "not compressed" not in row.get("text", ""):
        problems.append(f"no LZ4 module: status {st['status']!r}, row {row}")
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS["hello"]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != icount and
             page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    show_tab(page, "snapshots")
    page.click(f"#snapshots tr[data-id='{raw_id}'] button.restore")
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == raw_id, 30)
    back = page.evaluate("mkbSnap.cpu().icount")
    with page.expect_download() as dl:
        page.click(f"#snapshots tr[data-id='{raw_id}'] button.snapdl")
    d = dl.value
    data = pathlib.Path(d.path()).read_bytes()
    try:
        ram, _ = snap_parse_state(data)
        same = bytes(ram) == bytes(page.evaluate("Array.from(mkbSnap.A)"))
    except (ValueError, KeyError, IndexError) as e:
        same = str(e)[:100]
    r["no lz4 restore, download"] = [back == icount, d.suggested_filename, len(data), same]
    if back != icount:
        problems.append(f"no LZ4 module: Restore of the raw snapshot gave instruction {back}, want {icount}")
    if not d.suggested_filename.endswith(".v86state") or same is not True:
        problems.append(f"no LZ4 module: the download {d.suggested_filename!r} ({len(data)} bytes) is v86's state "
                        f"with the saved RAM: {same}")
    ctx.close()

    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def start(url):
        # The same document with another hash would not load again.
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
        return ok

    url = f"{base}/index.html?kernel=hello&paused=1#debugger"
    if not start(url):
        ctx.close()
        return ["edges: hello did not start paused"], r
    ram = page.evaluate("emuRam")
    st = snap_save(page, 1, False)
    a_id = st["rows"][0]["id"]

    # Delete all: the first press arms it for 5 s, then it is a plain button again.
    page.click("#snap_delall")
    armed = page.text_content("#snap_delall")
    time.sleep(5.5)
    after = page.text_content("#snap_delall")
    r["delete all"] = [armed, after, len(snap_rows(page)["rows"])]
    if "Press again" not in armed or after != "Delete all" or len(snap_rows(page)["rows"]) != 1:
        problems.append(f"Delete all: armed {armed!r}, 5 s later {after!r}, {len(snap_rows(page)['rows'])} rows (want 1)")

    # Step on, diff the snapshot with now; a rename shows in the diff at once.
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS["hello"]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             page.evaluate("stepper.steps") >= SNAPDIFF_STEPS["hello"], 60)
    show_tab(page, "snapshots")
    page.select_option("#snap_a", a_id)
    page.select_option("#snap_b", "now")
    page.click("#snap_diff")
    wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60)
    d = page.evaluate(SNAP_DIFF_JS)
    page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
    page.fill("#snapshots input.snapedit", "edge renamed")
    page.press("#snapshots input.snapedit", "Enter")
    renamed = wait_for(lambda: "edge renamed" in (page.evaluate(SNAP_DIFF_JS) or {}).get("text", ""), 10)
    r["rename in diff"] = renamed
    if not renamed:
        problems.append("a rename while the diff is shown: the diff still has the old name")
    if not d["pages"]:
        problems.append(f"stepping {SNAPDIFF_STEPS['hello']} instructions changed no page (test setup)")
    else:
        p0 = d["pages"][0][0]
        page.click(f"#snap_pages [data-page='{p0}'] button.snapopen")
        wait_for(lambda: shown_tab(page) == "page" and page.is_visible("#page_snap"), 15)
        banner = page.text_content("#page_snap_text") or ""
        # Renamed again while the Page tab holds the snapshot's page: its note follows.
        show_tab(page, "snapshots")
        page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
        page.fill("#snapshots input.snapedit", "edge renamed twice")
        page.press("#snapshots input.snapedit", "Enter")
        show_tab(page, "page")
        wait_for(lambda: "edge renamed twice" in (page.text_content("#page_snap_text") or ""), 10)
        banner2 = page.text_content("#page_snap_text") or ""
        page.screenshot(path=str(shots / "snapdiff-edges-page.png"))
        page.click("#page_snap_live")
        live = wait_for(lambda: not page.is_visible("#page_snap"), 10)
        same = page.evaluate(f"""(() => {{ const d = document.getElementById('listing')._drawn;
            const m = emulator.read_memory({p0} * 4096, 4096);
            return !!d && d.bytes.length === m.length && d.bytes.every((b, i) => b === m[i]); }})()""")
        r["live memory"] = [p0, banner[:100], banner2[:100], live, same]
        if "edge renamed" not in banner or "edge renamed twice" not in banner2:
            problems.append(f"the snapshot's page in the Page tab: notes {banner[:100]!r}, after a rename {banner2[:100]!r}")
        if not live or not same:
            problems.append(f"Show live memory: note hidden {live}, listing is live RAM {same}")

    # Three records the page did not write, then a reload reads them.
    gone, odd, orphan = a_id + "g", a_id + "o", a_id + "d"
    page.evaluate(SNAP_INJECT_JS, [
        {"tag": "g", "n": 1, "name": "of a gone kernel", "kernel": "gone-kernel", "key": "sha256:" + "0" * 64},
        {"tag": "o", "n": 2, "name": "hello at 3 MiB", "key": "sha256:" + "1" * 64, "ram": 3 << 20},
        {"tag": "d", "n": 3, "name": "orphan delta", "delta": {"base": "no-such-snapshot", "pages": 3}}])
    start(url)
    show_tab(page, "snapshots")
    snap_wait_rows(page, 4)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    rows = {x["id"]: x for x in snap_rows(page)["rows"]}
    r["injected"] = {k: [rows.get(k, {}).get("kernel"), rows.get(k, {}).get("restore"), rows.get(k, {}).get("sw")]
                     for k in (gone, odd, orphan)}
    if r["injected"] != {gone: ["other", False, True], odd: ["other", False, True], orphan: ["match", True, False]}:
        problems.append(f"stored records after a reload: {r['injected']}")
    page.screenshot(path=str(shots / "snapdiff-edges-rows.png"), full_page=True)

    def status_after(sel, want):
        snap_click(page, sel, False)
        wait_for(lambda: want in page.text_content("#snap_status"), 15)
        return page.text_content("#snap_status")

    s = status_after(f"#snapshots tr[data-id='{orphan}'] button.snapdl", "Download failed")
    r["orphan download"] = s[:160]
    if "Download failed" not in s or "is a delta of a snapshot that is not here" not in s:
        problems.append(f"Download of a delta without its base: {s!r}")
    s = status_after(f"#snapshots tr[data-id='{orphan}'] button.restore", "failed")
    r["orphan restore"] = s[:160]
    if "is a delta of a snapshot that is not here" not in s:
        problems.append(f"Restore of a delta without its base: {s!r}")
    s = status_after(f"#snapshots tr[data-id='{gone}'] button.snapswitch", "not one of the listed kernels")
    r["gone switch"] = s[:160]
    if "“gone-kernel” is not one of the listed kernels" not in s or "load that bzImage from disk" not in s:
        problems.append(f"Switch to a kernel that is not listed: {s!r}")

    # Restore at another RAM size: the page names the size to boot with.
    other = next(m for m in (4 << 20, 2 << 20) if m != ram)
    start(f"{base}/index.html?kernel=hello&mem={other}&paused=1#snapshots")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 4)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    s = status_after(f"#snapshots tr[data-id='{a_id}'] button.restore", "boot with that size")
    r["ram mismatch"] = s[:160]
    want = f"was taken with {page.evaluate(f'kib({ram})')} of RAM; boot with that size to restore it."
    if want not in s:
        problems.append(f"Restore at {other} bytes of RAM of a {ram}-byte snapshot: {s!r}, want {want!r}")

    # A listed kernel at 3 MiB: the switch adds the size to the menu and boots it.
    snap_click(page, f"#snapshots tr[data-id='{odd}'] button.snapswitch", False)
    booted = wait_for(lambda: page.evaluate("emuRam === 3145728 && !!emulator && emuReady"), 120)
    r["odd ram"] = [page.input_value("#memory"), booted, page.text_content("#snap_status")[:120]]
    if page.input_value("#memory") != "3145728" or not booted:
        problems.append(f"Switch to hello at 3 MiB: memory menu {page.input_value('#memory')!r}, booted {booted}")
    if errors:
        problems.append(f"edges: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if linux:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(snapdiff_run, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(snapdiff_storage, browser, base, shots)
    out.append(report("snapdiff storage", problems, r))
    problems, r = snapdiff_guard(snapdiff_legacy, browser, base, shots)
    out.append(report("snapdiff S1 gzip", problems, r))
    problems, r = snapdiff_guard(snapdiff_edges, browser, base, shots)
    out.append(report("snapdiff edges", problems, r))
    for phone in (False, True):
        problems, r = snapdiff_guard(snapdiff_bigsave, browser, base, shots, phone)
        out.append(report("snapdiff big save", problems, r, width=390 if phone else 1280))
    pw = [("hello", "light", False), ("hello", "dark", True)] + ([("minified-linux", "dark", False)] if linux else [])
    for name, scheme, phone in pw:
        problems, r = snapdiff_guard(snapdiff_poweron, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff power-on", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    rp = [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else [])
    for name, scheme, phone in rp:
        problems, r = snapdiff_guard(snapdiff_replay, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff replay", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    problems, r = snapdiff_guard(snapdiff_replay, browser, base, shots, "hello", "light", False, SNAP_REPLAY_TSC)
    out.append(report("snapdiff replay tsc", problems, r, kernel="hello", scheme="light", width=1280))
    for phone in (False, True):
        problems, r = snapdiff_guard(snapdiff_kernels, browser, base, shots, phone)
        out.append(report("snapdiff kernels", problems, r, width=390 if phone else 1280))
    for name, scheme, phone in [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else []):
        problems, r = snapdiff_guard(onecount_run, browser, base, shots, name, scheme, phone)
        out.append(report("snapdiff one count", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    for mem in (2 << 20, 64 << 20):
        problems, r = snapdiff_guard(snapdiff_perf, browser, base, shots, mem)
        out.append(report("snapdiff perf", problems, r, mem=mem))
    return any(out)


def suite_snapdiff(ctx):
    return snapdiff_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)


# ---- deltasnap: delta snapshots (R1) ---------------------------------------
# A snapshot after the first one of a kernel stores only the 4 KiB pages
# whose hash changed since the snapshot before it (its base) plus v86's
# state with no RAM; Restore puts the base's RAM back and the changed pages
# on top. Checked against v86's own RAM and registers at every save, after
# a reload, after deleting snapshots in the middle and at the root of the
# chain, through the diff (which reads a delta's page list) and the
# download (a full v86 state that `lz4 -d` reads).
DELTA_STEPS = {"hello": 15, "minified-linux": 2500}

DELTA_JS = """() => { if (window.mkbDelta) return; window.mkbDelta = {
  shas: {},
  async sha(k) { const d = await crypto.subtle.digest('SHA-256', emulator.v86.cpu.mem8.slice(0, emuRam));
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join(''); } }; }"""

DELTA_ROWS_JS = """() => [...document.querySelectorAll('#snapshots tbody tr[data-id]')].map((tr) => ({
  id: tr.dataset.id, base: tr.dataset.base || '', depth: Number(tr.dataset.depth || 0), pages: Number(tr.dataset.pages || -1),
  stored: Number(tr.dataset.stored || 0), raw: Number(tr.dataset.raw || 0), full: Number(tr.dataset.full || 0),
  where: tr.dataset.where || '', restore: !!tr.querySelector('button.restore'), text: tr.innerText.replace(/\\s+/g, ' ') }))"""


def delta_rows(page):
    return {x["id"]: x for x in page.evaluate(DELTA_ROWS_JS)}


def delta_restore(page, rid, phone):
    show_tab(page, "snapshots")
    page.evaluate("document.getElementById('snap_status').removeAttribute('data-restored')")
    snap_click(page, f"#snapshots tr[data-id='{rid}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rid or
             "failed" in page.inner_text("#snap_status"), 60)
    return page.get_attribute("#snap_status", "data-restored") == rid


def delta_check_restore(page, tag, rid, want, phone):
    """Restore rid: RAM SHA-256, registers and instruction count as v86 had them at the save."""
    if not delta_restore(page, rid, phone):
        return [f"{tag}: Restore {rid}: {page.inner_text('#snap_status')[:200]}"]
    got_sha = page.evaluate("mkbDelta.sha()")
    cpu = page.evaluate("mkbSnap.cpu()")
    out = []
    if got_sha != want["sha"]:
        out.append(f"{tag}: after restoring {rid} the RAM SHA-256 is {got_sha[:16]}, at the save it was {want['sha'][:16]}")
    if cpu["regs"] != want["cpu"]["regs"] or cpu["icount"] != want["cpu"]["icount"]:
        diff = {k: (hex(cpu['regs'][k]), hex(want['cpu']['regs'][k])) for k in cpu["regs"] if cpu["regs"][k] != want["cpu"]["regs"].get(k)}
        out.append(f"{tag}: after restoring {rid}: registers differ {diff}, instructions {cpu['icount']} (want {want['cpu']['icount']})")
    return out


def deltasnap_chain(browser, base, shots, name, scheme, phone):
    """Save at the kernel entry (full), then step and save 10 more: each is
    a delta of the one before with exactly the pages v86's RAM changed in;
    smaller than the full one; the storage line counts the deltas. After a
    reload every one restores to v86's RAM and registers at its save. The
    diff of a delta and its base compares only the delta's pages and finds
    the same pages as v86's RAM; a diff across the chain too. Deleting a
    snapshot in the middle folds its pages into the next one; deleting the
    root makes the next one full; the rest still restore exactly, also
    after a reload. The download of a delta is a full v86 state."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"delta {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, accept_downloads=True,
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel={name}&paused=1#debugger"

    def start():
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 30)
        page.evaluate(SNAP_JS)
        page.evaluate(DELTA_JS)
        return ok

    def shot(s):
        page.screenshot(path=str(shots / f"deltasnap-{name}-{w}-{scheme}-{s}.png"), full_page=True)

    if not start():
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        problems.append(f"{tag}: Run to the kernel entry stopped at {at:#x}: {status}")
    want, ids, step_pages = [], [], []
    for i in range(11):
        if i:
            snap_step_to(page, page.evaluate("stepper.steps") + DELTA_STEPS[name] * (1 + i % 3))
            page.evaluate("mkbSnap.keep('B')")
            step_pages.append(page.evaluate("mkbSnap.pages('A', 'B')"))
            page.evaluate("mkbSnap.A = mkbSnap.B")
        else:
            page.evaluate("mkbSnap.keep('A')")
            page.evaluate("mkbSnap.keep('first')")
        want.append({"sha": page.evaluate("mkbDelta.sha()"), "cpu": page.evaluate("mkbSnap.cpu()")})
        st = snap_save(page, i + 1, phone)
        ids.append(st["rows"][-1]["id"])
    page.evaluate("mkbSnap.keep('last')")
    # Read before the reload drops the page's copies.
    pages_0_10 = page.evaluate("mkbSnap.pages('first', 'last')")
    last_ram = base64.b64decode(page.evaluate("mkbSnap.b64('last')"))
    rows = delta_rows(page)
    r["rows"] = [(rows[x]["base"][-4:], rows[x]["depth"], rows[x]["pages"], rows[x]["stored"]) for x in ids]
    r["pages per step"] = [len(p) for p in step_pages]
    first = rows.get(ids[0], {})
    if first.get("base") or first.get("depth"):
        problems.append(f"{tag}: the first snapshot is a delta of {first.get('base')!r} (depth {first.get('depth')})")
    for i in range(1, 11):
        x = rows.get(ids[i], {})
        if x.get("base") != ids[i - 1] or x.get("depth") != i:
            problems.append(f"{tag}: snapshot {i} has base {x.get('base')!r} depth {x.get('depth')} (want {ids[i - 1]}, {i})")
        if x.get("pages") != len(step_pages[i - 1]):
            problems.append(f"{tag}: snapshot {i} stores {x.get('pages')} pages, {len(step_pages[i - 1])} changed in v86's RAM")
        if not x.get("stored") or x["stored"] >= first.get("stored", 0):
            problems.append(f"{tag}: delta {i} is {x.get('stored')} bytes, the full snapshot {first.get('stored')}")
        if not re.search(r"delta of #\d+.*\b" + str(x.get("pages")) + r" pages?", x.get("text", "")):
            problems.append(f"{tag}: row {i} does not say it is a delta and how many pages: {x.get('text', '')[:200]!r}")
        if x.get("full", 0) <= x.get("raw", 0):
            problems.append(f"{tag}: row {i} gives the full state as {x.get('full')} bytes, its own state {x.get('raw')}")
    store = page.inner_text("#snap_store")
    total = sum(x["stored"] for x in rows.values())
    r["store"] = store[:200]
    if not re.search(r"\b10 deltas?\b", store):
        problems.append(f"{tag}: the storage line does not count the 10 deltas: {store[:200]!r}")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll on the Snapshots tab")
    shot("chain")

    # Reload, then restore out of order: every one is v86's state at its save.
    if not start():
        problems.append(f"{tag}: the reload did not start paused")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 11)
    wait_for(lambda: all(x["restore"] for x in delta_rows(page).values()), 30)
    for i in (10, 4, 0, 7, 1):
        problems += delta_check_restore(page, tag, ids[i], want[i], phone)
    r["restore status"] = page.inner_text("#snap_status")[:160]

    # Diff of a delta and its base: only its pages compared; same pages as v86.
    for a, b in ((3, 4), (0, 10)):
        page.select_option("#snap_a", ids[a])
        page.select_option("#snap_b", ids[b])
        snap_click(page, "#snap_diff", phone)
        if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
            problems.append(f"{tag}: the diff {a} -> {b} did not finish")
            continue
        d = page.evaluate(SNAP_DIFF_JS)
        wp = step_pages[a] if b == a + 1 else pages_0_10
        if sorted(map(tuple, d["pages"])) != sorted(map(tuple, wp)):
            problems.append(f"{tag}: diff {a} -> {b} lists {len(d['pages'])} pages, v86's RAM changed in {len(wp)}")
        compared = page.get_attribute("#snap_diff_out", "data-compared")
        st = page.inner_text("#snap_diff_status")
        r[f"diff {a}->{b}"] = [len(d["pages"]), compared, st[:120]]
        if b == a + 1 and (compared != str(len(step_pages[a])) or "delta" not in st):
            problems.append(f"{tag}: the diff of delta {b} and its base compared {compared!r} pages "
                            f"(want the delta's {len(step_pages[a])}): {st[:160]!r}")
    shot("diff")

    # The download of a delta is a full v86 state.
    if not phone:
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{ids[10]}'] button.snapdl").click()
        fname = dl.value.suggested_filename
        if not fname.endswith(".v86state.lz4"):
            problems.append(f"{tag}: the delta's download is named {fname!r}")
        bad, r["download"] = snap_check_download(dl.value.path(), last_ram, "lz4")
        problems += [f"{tag}: download of delta 10: {b}" for b in bad]

    # Delete 5 (middle): 6 takes 5's pages; delete 0 (root): 1 becomes full.
    for gone, nxt, newbase in ((5, 6, 4), (0, 1, None)):
        snap_click(page, f"#snapshots tr[data-id='{ids[gone]}'] button.snapdel", phone)
        wait_for(lambda: ids[gone] not in delta_rows(page), 60)
        wait_for(lambda: delta_rows(page).get(ids[nxt], {}).get("base", "?") == (ids[newbase] if newbase is not None else ""), 60)
        x = delta_rows(page).get(ids[nxt], {})
        if x.get("base", "?") != (ids[newbase] if newbase is not None else ""):
            problems.append(f"{tag}: after deleting {gone}, snapshot {nxt} has base {x.get('base')!r}")
        if newbase is not None:
            union = {p for p, _ in step_pages[gone - 1]} | {p for p, _ in step_pages[nxt - 1]}
            if x.get("pages") != len(union):
                problems.append(f"{tag}: after deleting {gone}, snapshot {nxt} stores {x.get('pages')} pages, "
                                f"{len(union)} changed since {newbase}")
    r["after deletes"] = page.inner_text("#snap_status")[:160]
    if not start():
        problems.append(f"{tag}: the second reload did not start paused")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 9)
    wait_for(lambda: all(x["restore"] for x in delta_rows(page).values()), 30)
    rows = delta_rows(page)
    r["rows after deletes"] = [(rows[x]["base"][-4:], rows[x]["depth"], rows[x]["pages"]) for x in ids if x in rows]
    for i in (6, 10, 1, 3):
        problems += delta_check_restore(page, tag + " after deletes", ids[i], want[i], phone)
    # A save right after restoring 3 is a delta of 3 with no page changed.
    snap_save(page, 10, phone)
    rows = delta_rows(page)
    new = [x for x in rows.values() if x["id"] not in ids]
    if not new or new[0]["base"] != ids[3] or new[0]["pages"] != 0:
        problems.append(f"{tag}: a save right after restoring 3: {new[:1]}")
    shot("deleted")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll after the deletes")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def deltasnap_big(browser, base, shots):
    """64 MiB machine with 24 MiB of data: the second snapshot, a few
    pages later, is a delta under 1% of the full one; saving it makes no
    main-thread task over 200 ms; Restore of the delta gives the RAM back."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={64 << 20}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.evaluate(DELTA_JS)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate(SNAP_FILL_JS, 24)
    st = snap_save(page, 1, False)
    full_id = st["rows"][0]["id"]
    # 37 pages of the filled area change, one goes back to zero.
    page.evaluate("(() => { const m = emulator.v86.cpu.mem8; for (let p = 0; p < 36; p++) m[(9 << 20) + p * 8192 + 5] ^= 0xa5;"
                  " m.fill(0, 12 << 20, (12 << 20) + 4096); })()")
    want = page.evaluate("mkbDelta.sha()")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    st = snap_save(page, 2, False)
    r["save seconds"] = round(time.monotonic() - t0, 2)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    r["long tasks ms (save)"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"saving the delta: a {max(long)} ms task on the main thread (want <= 200)")
    rows = delta_rows(page)
    d_id = next(x for x in rows if x != full_id)
    full, d = rows[full_id], rows[d_id]
    r["full stored"], r["delta stored"], r["delta pages"] = full["stored"], d["stored"], d["pages"]
    r["status"] = page.inner_text("#snap_status")[:200]
    if d["base"] != full_id or d["pages"] != 37:
        problems.append(f"the 64 MiB delta has base {d['base']!r} and {d['pages']} pages (want {full_id}, 37)")
    if not d["stored"] or d["stored"] * 100 >= full["stored"]:
        problems.append(f"the 64 MiB delta is {d['stored']} bytes, the full snapshot {full['stored']} (want under 1%)")
    page.evaluate("emulator.v86.cpu.mem8.fill(0x55, 8 << 20, 9 << 20)")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    if not delta_restore(page, d_id, False):
        problems.append(f"restoring the 64 MiB delta: {page.inner_text('#snap_status')[:200]}")
    r["restore seconds"] = round(time.monotonic() - t0, 2)
    got = page.evaluate("mkbDelta.sha()")
    if got != want:
        problems.append(f"after restoring the 64 MiB delta the RAM SHA-256 is {got[:16]}, want {want[:16]}")
    page.screenshot(path=str(shots / "deltasnap-big-1280.png"), full_page=True)
    if errors:
        problems.append(f"big delta: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def deltasnap_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else [("hello", "light", False)])
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(deltasnap_chain, browser, base, shots, name, scheme, phone)
        out.append(report("deltasnap chain", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(deltasnap_big, browser, base, shots)
    out.append(report("deltasnap 64 MiB", problems, r))
    return any(out)


def suite_deltasnap(ctx):
    return deltasnap_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)


# ---- tabs: names, emoji, compact mode, the Machine tab (U3) ---------------
# The operator: "coverage zamień na Source, Graph na Calls. emoji przed
# etykietami. w ustawieniach możliwość zwinięcia do samego emoji.
# zarządzanie symulacją niech będzie kolejną kartą". The tab bar names the
# tabs with an emoji (aria-hidden) and the word (the accessible name); the
# old hashes and ?tab= values still open the renamed tabs; "Compact tabs"
# (Settings tab since U4) shows the emoji only, remembered across a reload;
# the Machine tab holds the kernel, RAM, file, symbol, initrd, cmdline and
# offset controls and Boot, and the header keeps the screen, the run bar
# and the counters, so it is shorter at 390 px than before (TABS_HEADER_390_BEFORE).
# U4 (operator: "settings jako oddzielna zakładka", "wymyśl mniej
# metaforyczne emoji"): a Settings tab before About holds Compact tabs and
# every preference the page remembers (each localStorage key it wrote and
# the IndexedDB of imported DWARF), each with an explanation, a way to
# change or forget it, and "Reset all settings"; the Machine tab keeps
# only machine controls. The emoji show what each tab holds (TABS_EMOJI)
# and render as colour emoji, not as text glyphs, in two font stacks.
# U5 (operator: "a i wszystko nad tabami niech będzie w pierwszym tabie"):
# above the tabs only the title and the tab bar remain (H1 moved the
# one-line run status U5 put next to the tab bar into the menu toggle,
# #runbadge: state, speed, instruction count, Pause/Continue); the
# screen, the run bar, the counters and the notes are in the Machine tab,
# which opens on the first load. The VM keeps running and its screen keeps
# changing while another tab is shown (tabs_u5).

# In the order of the tab bar.
TABS_WORDS = {"machine": "Machine", "memory": "Memory", "page": "Page", "debugger": "Debugger",
              "calls": "Calls", "source": "Source", "snapshots": "Snapshots", "settings": "Settings",
              "about": "About"}
TABS_EMOJI = {"machine": "\U0001F5A5\uFE0F", "memory": "\U0001F5FA\uFE0F", "page": "\U0001F522",
              "debugger": "\U0001F41E", "calls": "\U0001F4CA", "source": "\U0001F4DD", "snapshots": "\U0001F4BE",
              "settings": "\u2699\uFE0F", "about": "\u2139\uFE0F"}
TABS_ALIASES = {"graph": "calls", "coverage": "source"}
# The header's height at 390 px (hello booted, light) on 8dc5c89c7, before
# the Machine tab took its controls: measured in this suite's setting.
TABS_HEADER_390_BEFORE = 1003
TABS_MACHINE_IDS = ["kernel", "memory", "file", "symfile", "initrd_file", "cmdline", "sym_offset", "boot",
                    "kinfo", "build_info", "sym_status", "initrd_status"]
TABS_SETTINGS_IDS = ["compact_tabs", "set_speed", "set_mips", "set_count", "settings_stored", "settings_reset"]
# The preferences with a control of their own in the Settings tab: key, control, default at 1280 px.
TABS_PREFS = [("mkb-compact-tabs", "compact_tabs", False), ("mkb-speed", "set_speed", "1000"),
              ("mkb-mips", "set_mips", "10"), ("mkb-count", "set_count", True)]
# What was above the tab bar before U5: now in the Machine tab.
TABS_RUN_IDS = ["status", "problems", "pwa_notes", "screen_container", "screen_text", "screen_canvas", "vm_overlay",
                "dbg_pause", "dbg_continue", "dbg_step", "dbg_speed", "clock_mips", "count", "dbg_state", "counters",
                "vclock", "entry_info", "end_banner", "count_info"]
# Leaf elements drawn in the header outside the title, the tab bar and the run status.
TABS_HEADER_EXTRA_JS = """() => [...document.querySelectorAll('header.top *')].filter((e) => e.getClientRects().length
  && !e.children.length && !e.closest('h1, .tabbar')).map((e) => e.id || e.className || e.tagName)"""
# The run status on every tab (H1: the menu toggle, bottom right): its face
# and the menu's state in words, the VM state it shows, the count.
TABS_MINI_JS = """() => { const m = document.getElementById('runbadge'), st = document.getElementById('dbg_state');
  if (!m) return null; const r = document.getElementById('runbadge_toggle').getBoundingClientRect();
  return { text: m.textContent.replace(/\\s+/g, ' ').trim(), state: m.dataset.vm || '', steps: Number(m.dataset.steps || -1),
    vmState: st ? st.dataset.state || '' : '', shown: r.width > 0 && r.height > 0, top: Math.round(r.top),
    bottom: Math.round(r.bottom), right: Math.round(r.right) }; }"""

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


# Each tab's emoji drawn on a canvas in magenta, in the tab button's font and
# in a serif stack: a colour emoji ignores the fill colour, a text-style glyph
# (or a missing-glyph box) comes out magenta.
TABS_EMOJI_DRAW_JS = """(fonts) => [...document.querySelectorAll('.tabbar [role=tab] [aria-hidden=true]')].map((e) => {
  const out = { id: e.closest('[role=tab]').id };
  for (const [name, fam] of Object.entries(fonts)) {
    const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d');
    g.font = '40px ' + (fam || getComputedStyle(e).fontFamily); g.fillStyle = 'rgb(255,0,255)'; g.textBaseline = 'top';
    g.fillText(e.textContent.trim(), 4, 8);
    const d = g.getImageData(0, 0, 64, 64).data; let ink = 0, magenta = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i + 3] > 128) { ink++;
      if (d[i] > 200 && d[i + 1] < 80 && d[i + 2] > 200) magenta++; }
    out[name] = { ink, magenta };
  }
  return out; })"""

TABS_STORED_JS = """() => Object.keys(localStorage).filter((k) => k.startsWith('mkb-')).sort()"""
# What the Settings tab shows for a key: its row (or control) and the explanation next to it.
TABS_ROW_JS = """(k) => { const e = [...document.querySelectorAll('#panel_settings [data-key]')].find((x) => x.dataset.key === k);
  if (!e) return null; const row = e.closest('.setrow') || e;
  return { text: row.textContent.trim(), visible: !!row.getClientRects().length }; }"""
TABS_IDB_JS = """(op) => new Promise((res) => { const rq = indexedDB.open('mkb-dwarf', 1);
  rq.onupgradeneeded = () => rq.result.createObjectStore('kv');
  rq.onerror = () => res(-1);
  rq.onsuccess = () => { const db = rq.result; const tx = db.transaction('kv', op ? 'readwrite' : 'readonly');
    const s = tx.objectStore('kv'); if (op) s.put(new Uint8Array(8), op);
    const n = s.count(); tx.oncomplete = () => { db.close(); res(n.result); }; }; })"""


def tabs_settings(page, problems, r, shots):
    """The Settings tab: every remembered key shown with an explanation,
    changing a preference takes effect and is remembered, forget and reset
    clear them (hello booted at 1280 px)."""
    other = "mkb-map-" + "0" * 64
    page.evaluate("""(other) => { localStorage.setItem('mkb-watch:hello', JSON.stringify(['msg']));
      localStorage.setItem(other, JSON.stringify({ name: 'other.map', source: 'System.map', symbols: [], sections: [] }));
      localStorage.setItem('mkb-later-step', 'x'); }""", other)
    page.evaluate(TABS_IDB_JS, other + ":index")
    page.reload()
    wait_for(lambda: "Running hello " in (page.text_content("#status") or "") and ALIVE in page.inner_text("#screen_container"), 60)
    show_tab(page, "machine")
    page.fill("#cmdline", "console=tty0 u4=1")
    page.press("#cmdline", "Enter")
    wait_for(lambda: any(k.startswith("mkb-cmdline-") for k in page.evaluate(TABS_STORED_JS)), 10)
    wait_for(lambda: ALIVE in page.inner_text("#screen_container"), 60)
    if not page.locator("#tab_settings").count():
        problems.append("no Settings tab")
        return
    # IndexedDB can be slow (CI run 42704392268: the list was still empty
    # when read): the localStorage rows must not wait for it.
    page.evaluate("""() => { const orig = dbgIdbOp;
      dbgIdbOp = async (...a) => { await new Promise((r) => setTimeout(r, 1500)); return orig(...a); }; }""")
    show_tab(page, "settings")
    stored = page.evaluate(TABS_STORED_JS)
    r["stored_keys"] = stored
    for want in ("mkb-watch:hello", other, "mkb-later-step", "mkb-ref-hello-10"):
        if want not in stored:
            problems.append(f"setup: {want} not in localStorage ({stored})")
    if not any(k.startswith("mkb-cmdline-") for k in stored):
        problems.append(f"setup: no remembered cmdline after Boot with one ({stored})")
    for k in stored:
        row = page.evaluate(TABS_ROW_JS, k)
        if not row or not row["visible"]:
            problems.append(f"Settings does not show the remembered {k}")
        elif len(row["text"]) < 25:
            problems.append(f"Settings row of {k} has no explanation: {row['text']!r}")
    wait_for(lambda: page.evaluate(TABS_ROW_JS, "idb:mkb-dwarf"), 10)
    row = page.evaluate(TABS_ROW_JS, "idb:mkb-dwarf")
    if not row or "1" not in row["text"]:
        problems.append(f"Settings does not show the imported DWARF kept in IndexedDB: {row}")
    for key, ctl, default in TABS_PREFS:
        if not page.locator(f"#{ctl}").count():
            problems.append(f"no #{ctl} in Settings for {key}")
        elif page.get_attribute(f"#{ctl}", "data-key") != key:
            problems.append(f"#{ctl}: data-key {page.get_attribute(f'#{ctl}', 'data-key')!r}, want {key}")
    page.screenshot(path=str(shots / "tabs-1280-settings.png"), full_page=True)

    # Each preference takes effect and is remembered.
    if page.locator("#set_speed").count():
        page.select_option("#set_speed", "100")
        if page.input_value("#dbg_speed") != "100" or page.evaluate("localStorage.getItem('mkb-speed')") != "100":
            problems.append(f"Settings speed 100/s: Machine tab {page.input_value('#dbg_speed')}, "
                            f"stored {page.evaluate('localStorage.getItem(\'mkb-speed\')')}")
        show_tab(page, "machine")
        machine_do(page, "select_option", "#dbg_speed", "10")
        show_tab(page, "settings")
        if page.input_value("#set_speed") != "10":
            problems.append(f"after the Machine tab's speed 10/s Settings shows {page.input_value('#set_speed')}")
    if page.locator("#set_mips").count():
        page.select_option("#set_mips", "100")
        if page.input_value("#clock_mips") != "100" or page.evaluate("localStorage.getItem('mkb-mips')") != "100":
            problems.append(f"Settings clock 100/us: Machine tab {page.input_value('#clock_mips')}")
    if page.locator("#set_count").count():
        page.uncheck("#set_count")
        if page.is_checked("#count") or page.evaluate("localStorage.getItem('mkb-count')") != "0":
            problems.append("Settings count off: the Machine tab's count stays on or is not remembered")
    page.reload()
    wait_for(lambda: page.locator("#tab_settings").count() and "Running hello " in (page.text_content("#status") or ""), 60)
    after = {"speed": page.evaluate("prefSpeed"), "clock": page.input_value("#clock_mips"), "count": page.is_checked("#count")}
    r["after_reload"] = after
    if after != {"speed": "10", "clock": "100", "count": False}:
        problems.append(f"preferences after a reload: {after}, want speed 10, clock 100, count off")
    show_tab(page, "settings")
    if page.locator("#set_count").count() and page.is_checked("#set_count"):
        problems.append("Settings count shows on after a reload with count off")

    # Forget single entries: each goes, with its effect.
    def forget(k):
        b = page.locator("#panel_settings .setrow").filter(has=page.locator(f'[data-key="{k}"]')).locator("button.forget")
        if not b.count():
            problems.append(f"no Forget button for {k}")
            return False
        b.first.click()
        if not wait_for(lambda: k not in page.evaluate(TABS_STORED_JS) and not page.evaluate(TABS_ROW_JS, k), 10):
            problems.append(f"Forget {k}: still stored or shown")
            return False
        return True
    show_tab(page, "debugger")  # loads the kernel's watch list
    show_tab(page, "settings")
    if page.evaluate("watchList") != ["msg"]:
        problems.append(f"setup: the watch list of hello is {page.evaluate('watchList')}")
    if forget("mkb-watch:hello") and page.evaluate("watchList") != []:
        problems.append(f"after Forget the watch list of hello is {page.evaluate('watchList')}")
    cmd = next((k for k in page.evaluate(TABS_STORED_JS) if k.startswith("mkb-cmdline-")), None)
    if cmd and forget(cmd) and page.input_value("#cmdline") != "nokaslr cryptomgr.notests":
        problems.append(f"after Forget the command line is {page.input_value('#cmdline')!r}, want the default")
    forget(other)
    forget("mkb-later-step")
    if page.evaluate(TABS_IDB_JS, "") != 0:
        problems.append("Forget the other bzImage's symbols left its DWARF in IndexedDB")

    # Reset all settings: nothing of ours stays, the defaults are back. The
    # boot (10/s after the reload) ends first: its "Still alive" count is a
    # new measurement, remembered again when it arrives.
    page.select_option("#set_speed", "1000")
    if not wait_for(lambda: ALIVE in page.inner_text("#screen_container"), 60):
        problems.append("hello did not reach Still alive at 1k/s set in Settings")
    page.evaluate("(k) => localStorage.setItem(k, '{}')", other)
    page.evaluate(TABS_IDB_JS, other + ":index")
    page.check("#compact_tabs")
    if not page.locator("#settings_reset").count():
        problems.append("no Reset all settings button")
        return
    page.click("#settings_reset")
    time.sleep(0.5)
    left = page.evaluate(TABS_STORED_JS)
    r["after_reset"] = left
    if left:
        problems.append(f"Reset all settings left {left}")
    if page.evaluate(TABS_IDB_JS, "") != 0:
        problems.append("Reset all settings left the IndexedDB of imported DWARF")
    now = {"compact": page.is_checked("#compact_tabs"), "speed": page.evaluate("prefSpeed"),
           "clock": page.input_value("#clock_mips"), "count": page.is_checked("#count"),
           "words": all(t["wordShown"] for t in tabs_look(page))}
    if now != {"compact": False, "speed": "1000", "clock": "10", "count": True, "words": True}:
        problems.append(f"after Reset all settings: {now}, want the defaults")
    if page.locator("#panel_settings .setrow button.forget").count():
        problems.append("after Reset all settings Settings still lists remembered entries")


def tabs_mini(page):
    return page.evaluate(TABS_MINI_JS)


def tabs_u5(browser, base, shots, problems, r, fresh, linux):
    """U5: only the title, the tab bar and the run status above the tabs;
    the Machine tab opens first and holds the screen; the VM runs on and
    its screen changes while another tab is shown."""
    def selected(pg):
        return [t for t in TABS_WORDS if pg.locator(f"#tab_{t}").count()
                and pg.get_attribute(f"#tab_{t}", "aria-selected") == "true"]

    def header_check(pg, tag):
        extra = pg.evaluate(TABS_HEADER_EXTRA_JS)
        if extra:
            problems.append(f"{tag}: above the tabs besides the title and the tab bar: {extra[:8]}")
        mini = tabs_mini(pg)
        if not mini or not mini["shown"]:
            problems.append(f"{tag}: no run status (the menu toggle #runbadge)")
        h = round(pg.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))
        r[f"u5_header_{tag}"] = h
        if h > 140:
            problems.append(f"{tag}: header {h} px high (want the title, the tab bar and one status line)")
        return mini

    for scheme in ("light", "dark"):
        for phone in (False, True):
            tag = f"{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=hello")
            ok = wait_for(lambda: ALIVE in (page.text_content("#screen_text") or ""), 60)
            if not ok:
                problems.append(f"{tag}: hello did not reach {ALIVE!r} on the first load")
            if selected(page) != ["machine"] or page.evaluate("location.hash") != "#machine":
                problems.append(f"{tag}: first load shows {selected(page)} ({page.evaluate('location.hash')!r}), want Machine")
            if not page.is_visible("#screen_container") or HELLO not in page.inner_text("#screen_container"):
                problems.append(f"{tag}: the Machine tab does not show the screen with {HELLO!r}")
            time.sleep(0.5)
            mini = header_check(page, tag)
            if mini and (mini["state"] != mini["vmState"] or "paused" not in mini["text"] and "ended" not in mini["text"]
                         and "halt" not in mini["text"]):
                problems.append(f"{tag}: run status {mini} does not match the VM ({mini and mini['vmState']})")
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"{tag}: horizontal scroll")
            page.screenshot(path=str(shots / f"tabs-u5-{tag}.png"))
            show_tab(page, "debugger")
            time.sleep(0.3)
            page.screenshot(path=str(shots / f"tabs-u5-{tag}-debugger.png"))
            c.close()

    # The hash still selects a tab.
    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello#memory")
    if not wait_for(lambda: selected(page) == ["memory"], 10):
        problems.append(f"#memory on the first load shows {selected(page)}")
    c.close()

    # hello at 10/s: leave Machine after the kernel entry, before "Hello, World!".
    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello&speed=10")
    wait_for(lambda: page.text_content("#entry_info"), 60)
    left_early = HELLO not in (page.text_content("#screen_text") or "")
    r["u5_left_before_hello"] = left_early
    show_tab(page, "page")
    m1 = tabs_mini(page)
    time.sleep(2)
    m2 = tabs_mini(page)
    r["u5_page_10"] = [m1, m2]
    if not m1 or not m2 or m2["steps"] <= m1["steps"] or m2["state"] != "stepped":
        problems.append(f"hello at 10/s on the Page tab: the run status does not count on: {m1} -> {m2}")
    elif m2["state"] != m2["vmState"] or "10/s" not in m2["text"]:
        problems.append(f"hello at 10/s: the run status {m2} does not match the VM")
    if page.is_visible("#screen_container"):
        problems.append("the screen is shown on the Page tab")
    if not page.evaluate("document.getElementById('screen_container').isConnected"):
        problems.append("the screen was taken out of the page")
    # The screen element is hidden, not stopped: the text arrives meanwhile.
    if not wait_for(lambda: HELLO in (page.text_content("#screen_text") or ""), 60):
        problems.append(f"{HELLO!r} did not reach the hidden screen while the Page tab was shown")
    show_tab(page, "debugger")
    a = tabs_mini(page)
    if a:
        nav_press(page, "#runbadge_pause")
        if not wait_for(lambda: (tabs_mini(page) or {}).get("state") == "paused", 10):
            problems.append(f"Pause in the run status did not pause: {tabs_mini(page)}")
        p1 = tabs_mini(page)
        time.sleep(1)
        p2 = tabs_mini(page)
        if p1["steps"] != p2["steps"] or "paused" not in p2["text"]:
            problems.append(f"paused from the run status, it still counts: {p1} -> {p2}")
        nav_press(page, "#runbadge_pause")
        if not wait_for(lambda: (tabs_mini(page) or {}).get("state") == "stepped", 10):
            problems.append(f"Continue in the run status did not run on: {tabs_mini(page)}")
        r["u5_pause_continue"] = [p1, p2, tabs_mini(page)]
    if page.locator("#runbadge_state").count():
        nav_press(page, "#runbadge_state")
    if selected(page) != ["machine"]:
        problems.append(f"a click on the run status shows {selected(page)}, want Machine")
    if not page.is_visible("#screen_container") or HELLO not in page.inner_text("#screen_container"):
        problems.append(f"back on Machine the screen does not show {HELLO!r}")
    page.screenshot(path=str(shots / "tabs-u5-back-on-machine.png"))
    c.close()

    # The minified Linux at 1k/s on the Debugger tab: the count rises with the VM's.
    if linux:
        c, page = fresh()
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000")
        wait_for(lambda: page.text_content("#entry_info"), 60)
        show_tab(page, "debugger")
        time.sleep(0.5)
        m1 = tabs_mini(page)
        time.sleep(2)
        m2 = tabs_mini(page)
        r["u5_linux_1k"] = [m1, m2]
        if not m1 or not m2 or m2["steps"] - m1["steps"] < 500 or m2["state"] != "stepped" or "1k/s" not in m2["text"]:
            problems.append(f"minified Linux at 1k/s on the Debugger tab: run status {m1} -> {m2}")
        c.close()


def tabs_case(browser, base, shots, linux=False):
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
    if [t["id"] for t in look] != want_ids:
        problems.append(f"tab ids {[t['id'] for t in look]}, want {want_ids}")
    for t in look:
        want = TABS_EMOJI.get(t["id"][4:])
        if want and t["emoji"] != want:
            problems.append(f"{t['id']}: emoji {t['emoji']!r}, want {want!r}")
    drawn = page.evaluate(TABS_EMOJI_DRAW_JS, {"tab font": "", "serif": "serif"})
    r["emoji_drawn"] = drawn
    for d in drawn:
        for stack in ("tab font", "serif"):
            ink, magenta = d[stack]["ink"], d[stack]["magenta"]
            if ink < 100 or magenta > ink * 0.2:
                problems.append(f"{d['id']} emoji in the {stack} stack: {ink} px drawn, {magenta} in the text colour "
                                f"(want a colour emoji)")
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
    where = tabs_where(page, TABS_MACHINE_IDS + TABS_RUN_IDS)
    r["where"] = where
    for i in TABS_MACHINE_IDS:
        if where.get(i) != "panel_machine":
            problems.append(f"#{i} is in {where.get(i)}, want the Machine tab")
    for i in TABS_RUN_IDS:
        if where.get(i) != "panel_machine":
            problems.append(f"#{i} is in {where.get(i)}, want the Machine tab")
    for i in TABS_SETTINGS_IDS:
        w = tabs_where(page, [i])[i]
        if w != "panel_settings":
            problems.append(f"#{i} is in {w}, want the Settings tab")
    heads = page.evaluate("[...document.querySelectorAll('#panel_machine h2, #panel_machine h3, #panel_machine legend')]"
                          ".map((e) => e.textContent.trim())")
    if any("Settings" in h for h in heads) or page.locator("#panel_machine input[type=checkbox]:not(#count)").count():
        problems.append(f"the Machine tab still has a Settings section: {heads}")
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
    for url in (f"{base}/index.html?kernel=hello&tab=settings", f"{base}/index.html?kernel=hello#settings"):
        page.goto(url)
        if not wait_for(lambda: page.locator("#tab_settings").count()
                        and page.get_attribute("#tab_settings", "aria-selected") == "true"
                        and page.evaluate("location.hash") == "#settings", 10):
            problems.append(f"{url[len(base):]} does not open the Settings tab")
    page.goto(f"{base}/index.html?kernel=hello#machine")
    booted(page)
    if page.get_attribute("#tab_machine", "aria-selected") != "true" if page.locator("#tab_machine").count() else True:
        problems.append("#machine does not open the Machine tab")
    # A hash the browser has navigated to but whose hashchange has not run
    # yet: the page's own hash writes (writeHash, e.g. following EIP to
    # another page) must not overwrite it. CI run 43424964028 lost #coverage
    # this way and stayed on #calls.
    page.goto(f"{base}/index.html?kernel=hello#calls")
    wait_for(lambda: page.evaluate("location.hash") == "#calls", 10)
    page.evaluate("() => { location.hash = '#coverage'; writeHash(); }")
    if not wait_for(lambda: page.get_attribute("#tab_source", "aria-selected") == "true"
                    and page.evaluate("location.hash") == "#source", 10):
        problems.append(f"#coverage with a hash write before its hashchange: hash {page.evaluate('location.hash')!r}, "
                        f"Source tab selected {page.get_attribute('#tab_source', 'aria-selected')!r}")

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
        # (before U4 the setting was in the Machine tab: the case reports, not crashes)
        home = "settings" if page.locator("#tab_settings").count() else "machine"
        show_tab(page, home)
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
        # The header grows while hello boots and moves the tab bar under a
        # resting mouse (pointerout closes the popover): hover after the boot.
        booted(page)
        time.sleep(0.5)
        page.hover("#tab_calls")
        if not wait_for(lambda: page.is_visible("#tip_pop") and page.text_content("#tip_pop") == "Calls", 3):
            problems.append(f"hovering the compact Calls tab shows {page.text_content('#tip_pop')!r}")
        page.click("#tab_calls")
        if page.get_attribute("#tab_calls", "aria-selected") != "true":
            problems.append("a click on the compact Calls tab does not open it")
        show_tab(page, home)
        page.uncheck("#compact_tabs")
        page.reload()
        wait_for(lambda: page.locator("#compact_tabs").count(), 10)
        if page.is_checked("#compact_tabs") or not all(t["wordShown"] for t in tabs_look(page)):
            problems.append("Compact tabs off does not stay off after a reload")
    c.close()

    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello")
    if booted(page):
        tabs_settings(page, problems, r, shots)
    c.close()

    # A browser that keeps no data (site data blocked: localStorage throws,
    # no IndexedDB): Settings says nothing is remembered and its controls
    # still work for this page.
    c, page = fresh()
    c.add_init_script("""Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('blocked by the test', 'SecurityError'); } });
        Object.defineProperty(window, 'indexedDB', { get() { return undefined; } });""")
    page.goto(f"{base}/index.html?kernel=hello")
    if booted(page):
        show_tab(page, "settings")
        time.sleep(0.5)
        none = page.text_content("#settings_stored") or ""
        page.select_option("#set_speed", "100")
        r["no storage"] = [none[:80], page.input_value("#dbg_speed")]
        if "Nothing remembered for any kernel yet." not in none or page.locator("#settings_stored .setrow").count():
            problems.append(f"Settings without storage: {none[:200]!r}")
        if page.input_value("#dbg_speed") != "100":
            problems.append(f"Settings without storage: speed 100/s set, the Machine tab shows {page.input_value('#dbg_speed')}")
    else:
        problems.append("without storage: hello did not boot")
    c.close()

    tabs_u5(browser, base, shots, problems, r, fresh, linux)

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
        for t in ["machine", "memory", "calls", "source", "settings", "about"]:
            if page.locator(f"#tab_{t}").count():
                page.tap(f"#tab_{t}")
                time.sleep(0.3)
                if page.get_attribute(f"#tab_{t}", "aria-selected") != "true":
                    problems.append(f"390 px {scheme}: a tap on {t} does not open it")
                if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                    problems.append(f"390 px {scheme}: horizontal scroll on the {t} tab")
                if t in ("machine", "settings"):
                    page.screenshot(path=str(shots / f"tabs-390-{scheme}-{t}.png"), full_page=True)
        if page.locator("#compact_tabs").count():
            page.tap("#tab_settings" if page.locator("#tab_settings").count() else "#tab_machine")
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
    return report("tabs", *tabs_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries))


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
    for d in ("kernels", "v86", "iced", "lz4"):
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
            pwa_after_update(page, base, state, timeout, problems, r)

    finally:
        if errors:
            problems.append(f"page errors: {errors[:3]}")
        c.close()
        if tmp:
            httpd.shutdown()
            tmp.cleanup()
    return problems, r


PWA_RANGE_JS = """async ([path, range]) => {
    const resp = await fetch(withV(path), { headers: { Range: range } });
    const b = new Uint8Array(await resp.arrayBuffer());
    return { status: resp.status, range: resp.headers.get('content-range'), bytes: Array.from(b.slice(0, 16)), n: b.length };
}"""


def pwa_after_update(page, base, state, timeout, problems, r):
    """The new deploy's service worker (block coverage from its start, the
    first one's often begins late): a Range request for a cached file gets
    its bytes from the cache (206, a suffix range, 416 past the end); a
    POST is not answered by it; offline again the page loads from its
    cache, hello boots, and a file never fetched gets the offline note."""
    data = (SITE / kernel_entries()["hello"]["path"]).read_bytes()
    n = len(data)
    got = {}
    for rng, want in (("bytes=0-9", (206, f"bytes 0-9/{n}", list(data[:10]), 10)),
                      ("bytes=-4", (206, f"bytes {n - 4}-{n - 1}/{n}", list(data[-4:]), 4)),
                      (f"bytes={n}-", (416, f"bytes */{n}", [], 0))):
        g = page.evaluate(PWA_RANGE_JS, [kernel_entries()["hello"]["path"], rng])
        got[rng] = [g["status"], g["range"], g["n"]]
        if (g["status"], g["range"], g["bytes"], g["n"]) != want:
            problems.append(f"after the update, Range {rng} of kernels/hello/bzImage: {g}, want {want[:2]} and {want[3]} bytes of the file")
    r["ranges"] = got
    post = page.evaluate("fetch('version.json', { method: 'POST' }).then((x) => x.status)")
    r["post_status"] = post
    if post != 501:
        problems.append(f"a POST answered {post}, want the server's 501 (the service worker leaves non-GET requests alone)")
    state["down"] = True
    page.context.set_offline(True)
    try:
        page.goto(f"{base}/index.html?kernel=hello")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append(f"offline after the update: hello did not boot: {page.inner_text('#status')!r}")
        page.goto(f"{base}/index.html?kernel=debian-6.1-686")
        wait_for(lambda: "not available offline yet" in (page.text_content("#pwa_notes") or ""), 30)
        r["offline_note_after_update"] = (page.text_content("#pwa_notes") or "")[:120]
        if "not available offline yet" not in r["offline_note_after_update"]:
            problems.append(f"offline after the update, a file never fetched: no note ({r['offline_note_after_update']!r})")
    finally:
        page.context.set_offline(False)
        state["down"] = False


def pwa_edges(browser, shots, timeout):
    """Two cases the main one does not reach (local only, they need their
    own deploys). No service worker can be registered (sw.js missing): the
    page says so in the console and boots as a plain page. Another tab
    activates a newer deploy's worker: a page of the old deploy, now
    controlled by it, shows the new-version notice."""
    problems, r, errors = [], {}, []
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        root = pathlib.Path(t)
        pwa_deploy(root, PWA_V[0], "A")
        (root / "sw.js").unlink()
        httpd, _ = pwa_server(root)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        c = browser.new_context(viewport={"width": 1280, "height": 900})
        try:
            page = c.new_page()
            page.on("pageerror", lambda e: errors.append(str(e)[:200]))
            warns = []
            page.on("console", lambda m: m.type == "warning" and warns.append(m.text[:200]))
            page.goto(f"{base}/index.html?kernel=hello")
            booted = wait_for(lambda: gate_on_screen(page), timeout)
            wait_for(lambda: any("service worker:" in w for w in warns), 10)
            r["no sw.js"] = [booted, warns[:2], page.evaluate("!!navigator.serviceWorker.controller"),
                             (page.text_content("#pwa_notes") or "")[:80]]
            if not booted or not any("service worker:" in w for w in warns):
                problems.append(f"without sw.js: booted {booted}, console warnings {warns[:3]}")
            if page.evaluate("!!navigator.serviceWorker.controller") or (page.text_content("#pwa_notes") or "").strip():
                problems.append(f"without sw.js: a controller or a note: {r['no sw.js']}")
        finally:
            c.close()
            httpd.shutdown()

    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        root = pathlib.Path(t)
        pwa_deploy(root, PWA_V[0], "A")
        httpd, _ = pwa_server(root)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        c = browser.new_context(viewport={"width": 1280, "height": 900})
        try:
            old = c.new_page()
            old.on("pageerror", lambda e: errors.append(str(e)[:200]))
            old.goto(f"{base}/index.html?kernel=hello")
            if not wait_for(lambda: gate_on_screen(old), timeout) or not pwa_controlled(old):
                problems.append("another tab: the first page did not boot under its worker")
                return problems, r
            pwa_deploy(root, PWA_V[1], "B")
            new = c.new_page()
            new.on("pageerror", lambda e: errors.append(str(e)[:200]))
            new.goto(f"{base}/index.html?kernel=hello")
            wait_for(lambda: new.evaluate(PWA_SW_VERSION_JS) == PWA_V[1], 60)
            shown = wait_for(lambda: old.locator("#pwa_update").count() and old.is_visible("#pwa_update"), 30)
            note = old.text_content("#pwa_update") if old.locator("#pwa_update").count() else ""
            r["another tab"] = [new.evaluate(PWA_SW_VERSION_JS), old.evaluate(PWA_SW_VERSION_JS), note[:120],
                                new.locator("#pwa_update").count()]
            if not shown or "build " + PWA_V[1] not in note:
                problems.append(f"another tab activated deploy B: the deploy A page shows {note!r}")
            if new.locator("#pwa_update").count() and new.is_visible("#pwa_update"):
                problems.append("another tab: the new deploy's own page shows the new-version notice")
            old.bring_to_front()
            old.screenshot(path=str(shots / "pwa-1280-light-other-tab.png"))
        finally:
            c.close()
            httpd.shutdown()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_pwa(ctx):
    base = ctx.base if ctx.args.base else None
    failed = report("pwa", *pwa_case(ctx.browser, base, ctx.shots, ctx.args.timeout))
    if not base:
        failed |= report("pwa edges", *pwa_edges(ctx.browser, ctx.shots, ctx.args.timeout))
    return failed


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
    machine_do(page, "select_option", "#dbg_speed", "10")
    phase("10/s", 4)
    # 1k/s for 4 s or a few instructions: hello's run ends about 450 after
    # its entry (and a click takes a while at 4x).
    s0 = page.evaluate("stepper.steps")
    machine_do(page, "select_option", "#dbg_speed", "1000")
    wait_for(lambda: page.evaluate("stepper.steps") - s0 > (20 if kernel.startswith("hello") else 100), 4, 0.02)
    phase("1000/s", 0)

    def act(sel):
        # From either tab (Step N and Run to are in the Debugger tab); a
        # control the end of the run has disabled is left alone.
        page.evaluate("s => { const b = document.querySelector(s); if (!b.disabled) b.click(); }", sel)

    act("#dbg_pause")
    phase("Pause", 2)
    machine_do(page, "select_option", "#dbg_speed", "1")
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
    machine_do(page, "select_option", "#dbg_speed", "full")
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
        machine_do(page, "select_option", "#dbg_speed", v)
        return "speed " + v
    if r < 0.45:
        return click(rng.choice(["#dbg_pause", "#dbg_continue", "#dbg_step", "#dbg_stepn", "#vm_continue", "#runbadge_toggle", "#runbadge_pause"]))
    if r < 0.50:
        v = rng.choice(FOLLOW_FUZZ_TARGETS)
        page.evaluate("v => { document.getElementById('dbg_addr').value = v; }", v)
        return "Run to " + v + ": " + click("#dbg_runto")
    if r < 0.60:
        return click(rng.choice([".srclink", ".srclink", "#src_close"]))
    if r < 0.70:
        return click(rng.choice(["#nav_back", "#nav_fwd", "#runbadge_toggle"]))
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
RUN_FUZZ_CONTROLS = ["#dbg_pause", "#dbg_continue", "#vm_continue", "#runbadge_toggle", "#runbadge_pause", "#dbg_step", "#dbg_stepn", "#dbg_runto", "#boot",
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
        machine_do(page, "select_option", "#dbg_speed", v)
        return "speed " + v
    if r < 0.25:
        v = rng.choice(["10", "100", "1000", "real"])
        machine_do(page, "select_option", "#clock_mips", v)
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
    machine_do(page, "select_option", "#dbg_speed", other)
    time.sleep(3)
    machine_do(page, "select_option", "#dbg_speed", rate)
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
    memory: Number(document.getElementById('pages').dataset.draws || 0),
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
    if tab == "memory":
        page.evaluate(MEMEIP_JS)
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
    eip = page.evaluate("window.mkbEip ? [mkbEip.checks, mkbEip.viol] : null") if tab == "memory" else None
    if eip and (eip[1] or eip[0] < min(ran, secs) / 2):
        problems.append({"EIP mark (M1)": f"drawn {eip[0]} times for {ran} instructions; {eip[1][:3]}"})
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
                  ("hello", "page", 4, "1"), ("minified-linux-os", "memory", 6, "10")]


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
# ---- bookmarks: named bookmarks from many places (K1) ------------------------
# The operator: "chcę móc w różnych miejscach dodawać nazwane zakładki".
# Bookmarks (not tabs: the tab bar has tabs) are added from a listing line,
# Code at EIP, a page of the Memory grid, a line of the Source panel, a
# file of the Source tab, a box of the Calls tab, a frame of the call stack
# and the machine's position, each with a ☆ button, a context menu (right
# click) or long press, and the B key; a dialog asks for the name with a
# default. They are listed in the menu toggle's menu (H1), kept per kernel
# identity (S1's bzImage SHA-256) in localStorage across a reload, jump to
# their place as a history entry (Back returns), carry a ★ mark with a
# popover where they point, export to and import from a JSON file and open
# from a link (#bm=...).
BOOKMARKS_POINTS = {"hello": ("hello_main", 40, "boot/hello.c"),
                    "minified-linux": ("start_kernel", 3000, "init/main.c")}

BM_STATE_JS = """() => { const rows = [...document.querySelectorAll('#bm_list .bmrow')];
  return { rows: rows.map((e) => ({ id: e.dataset.id, kind: e.dataset.kind, group: e.dataset.group,
      name: (e.querySelector('.bmgo') || {}).textContent || '', what: (e.querySelector('.bmwhat') || {}).textContent || '',
      link: (e.querySelector('a.bmlink') || {}).href || '', sw: !!e.querySelector('.bmswitch'), shown: e.getClientRects().length > 0 })),
    count: Number((document.getElementById('bm_list') || {dataset: {}}).dataset.count || 0),
    store: (() => { try { return JSON.parse(localStorage.getItem('mkb-bookmarks') || 'null'); } catch (e) { return 'error ' + e; } })(),
    status: (document.getElementById('bm_status') || {}).textContent || '',
    list: typeof BM === 'undefined' ? null : BM.list.map((b) => ({ ...b })) }; }"""

BM_DIALOG_JS = """() => { const d = document.getElementById('bm_dialog'); if (!d || d.hidden) return null;
  const b = d.getBoundingClientRect(), n = document.getElementById('bm_name');
  return { name: n.value, what: document.getElementById('bm_what').textContent, kind: d.dataset.kind || '',
    focus: document.activeElement === n, box: [b.left, b.top, b.right, b.bottom], view: [innerWidth, innerHeight] }; }"""

BM_WHERE_JS = """() => { const sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
  const L = document.getElementById('listing'), lb = L.getBoundingClientRect();
  const vis = (a) => { const e = L.querySelector('.ins[data-addr="' + a + '"]'); if (!e) return false;
    const r = e.getBoundingClientRect(); return r.top >= lb.top - 1 && r.bottom <= lb.bottom + 1 && r.bottom > 0 && r.top < innerHeight; };
  return { tab, page: selectedPage, listingPage: Number(L.dataset.page ?? -1), srcOpen: !sp.hidden, file: sb.dataset.file || '',
    title: document.getElementById('src_title').textContent, pick: Number((sb.querySelector('.pick') || {dataset: {}}).dataset.line || 0),
    hit: jumpHit, hash: location.hash,
    jhit: [...L.querySelectorAll('.ins.jhit[data-addr]')].map((e) => Number(e.dataset.addr)),
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, insVisible: (() => {
      // On screen: inside the listing, under the top bar, above the Source panel's sheet (a phone).
      const top = Math.max(lb.top, document.querySelector('.top').getBoundingClientRect().bottom, 0);
      const pr = sp.hidden ? null : sp.getBoundingClientRect();
      const bottom = Math.min(lb.bottom, innerHeight, pr && pr.left <= 0 && pr.top > 0 ? pr.top : Infinity);
      return [...L.querySelectorAll('.ins[data-addr]')].filter((e) => { const r = e.getBoundingClientRect();
        return r.top >= top - 1 && r.bottom <= bottom + 1; }).map((e) => Number(e.dataset.addr)); })() }; }"""


def bm_state(page):
    return page.evaluate(BM_STATE_JS)


def bm_where(page):
    return page.evaluate(BM_WHERE_JS)


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


def bm_press(page, sel, phone, cdp, nth=0):
    """A context menu (right click) at 1280, a long press at 390."""
    loc = page.locator(sel).nth(nth)
    loc.scroll_into_view_if_needed()
    b = loc.bounding_box()
    x, y = b["x"] + min(b["width"] / 2, 60), b["y"] + b["height"] / 2
    if phone:
        tip_long_press(page, cdp, x, y)
    else:
        page.mouse.click(x, y, button="right")


def bm_press_xy(page, x, y, phone, cdp):
    if phone:
        tip_long_press(page, cdp, x, y)
    else:
        page.mouse.click(x, y, button="right")


def bm_tap(page, sel, phone):
    page.tap(sel) if phone else page.click(sel)


def bm_shots(page, shots, stem, scheme):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))
    page.emulate_media(color_scheme=scheme)


def bm_star_tip(page, sel, name, phone, problems, tag):
    """The ★ at sel exists and its popover names the bookmark."""
    star = page.locator(sel)
    if not star.count():
        problems.append(f"{tag}: no ★ mark at {sel}")
        return
    star.first.scroll_into_view_if_needed()
    if phone:
        star.first.tap()
    else:
        star.first.hover()
    if not wait_for(lambda: page.is_visible("#tip_pop"), 3):
        problems.append(f"{tag}: the ★ at {sel} shows no popover")
        return
    t = page.inner_text("#tip_pop")
    if name not in t or "ookmark" not in t:
        problems.append(f"{tag}: the ★ popover at {sel} does not name the bookmark {name!r}: {t[:160]!r}")
    # Closed as a tap elsewhere would (Escape in the Source panel also closes the panel).
    page.evaluate("hideTip()")
    page.mouse.move(2, 300)


def bm_jump_check(page, b, F, syms, problems, tag, phone):
    """Jump to bookmark b from the list; check the place independently, then Back."""
    if not bm_open_list(page, phone):
        problems.append(f"{tag}: the bookmarks list does not open")
        return
    before = nav_at(page)
    where0 = bm_where(page)
    sel = f'#bm_list .bmrow[data-id="{b["id"]}"] .bmgo'
    if not page.locator(sel).count():
        problems.append(f"{tag}: bookmark {b['name']!r} is not listed")
        return
    bm_tap(page, sel, phone)
    time.sleep(1.4)
    k, t = b["kind"], b["t"]
    ok = True
    if k in ("code", "pos"):
        pa = t["pa"]
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == pa // 4096 and pa in w["insVisible"])(bm_where(page)), 15)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the {k} bookmark {b['name']!r} did not show {hex(pa)} in the Page listing: tab {w['tab']}, "
                            f"page {w['listingPage']}, lines on screen {[hex(x) for x in w['insVisible'][:3]]}..")
    elif k == "page":
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == t["page"])(bm_where(page)), 15)
        if not ok:
            problems.append(f"{tag}: the page bookmark {b['name']!r} did not open page {hex(t['page'])}: {bm_where(page)['listingPage']}")
    elif k == "fn":
        s = syms.get(t["name"])
        want = s["paddr"] if s else t["pa"]
        if want != t["pa"]:
            problems.append(f"{tag}: the function bookmark {t['name']} holds {hex(t['pa'])}, symbols.json says {hex(want)}")
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == want // 4096 and want in w["insVisible"])(bm_where(page)), 15)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the function bookmark {b['name']!r} did not show {t['name']} at {hex(want)}: tab {w['tab']} page {w['listingPage']}")
    elif k == "line":
        ok = wait_for(lambda: (lambda w: w["srcOpen"] and w["file"] == t["path"] and w["pick"] == t["line"])(bm_where(page)), 20)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the line bookmark {b['name']!r} did not open {t['path']}:{t['line']}: {w['file']}:{w['pick']} open {w['srcOpen']}")
        else:
            vis = page.evaluate("""(l) => { const sb = document.getElementById('src_body'), e = sb.querySelector('[data-line="' + l + '"]');
                if (!e) return false; const a = e.getBoundingClientRect(), b = sb.getBoundingClientRect(); return a.top >= b.top - 1 && a.bottom <= b.bottom + 1; }""", t["line"])
            if not vis:
                problems.append(f"{tag}: the line bookmark's line {t['line']} is not in view in the Source panel")
    elif k == "file":
        ok = wait_for(lambda: (lambda w: w["srcOpen"] and w["file"] == t["path"] and "(whole file)" in w["title"])(bm_where(page)), 20)
        if not ok:
            problems.append(f"{tag}: the file bookmark {b['name']!r} did not open {t['path']} (whole file): {bm_where(page)['title']!r}")
    after = nav_at(page)
    if after[0] != before[0] + 1:
        problems.append(f"{tag}: the jump to {b['name']!r} is not one history entry: {before} -> {after}")
    if bm_where(page)["hscroll"]:
        problems.append(f"{tag}: horizontal scroll after the jump to {b['name']!r}")
    # Back returns to where the jump started.
    bm_close_menu(page)
    page.keyboard.press("Alt+ArrowLeft") if not phone else page.go_back()
    if not wait_for(lambda: nav_at(page)[0] == before[0] and bm_where(page)["tab"] == where0["tab"], 8):
        problems.append(f"{tag}: Back after the jump to {b['name']!r} did not return: {nav_at(page)} tab {bm_where(page)['tab']}, want {before} {where0['tab']}")
    time.sleep(0.5)
    return ok


def bookmarks_run(browser, base, shots, name, scheme, phone, other=None, problems=None):
    fn, steps, src_path = BOOKMARKS_POINTS[name]
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    problems, r = [] if problems is None else problems, {}
    errors = []
    width = 390 if phone else 1280
    tag = f"{name} {width} {scheme}"
    vp = {"width": 390, "height": 844} if phone else {"width": 1280, "height": 1000}
    ctx = browser.new_context(viewport=vp, has_touch=True, is_mobile=True, accept_downloads=True) if phone else \
        browser.new_context(viewport=vp, accept_downloads=True)
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(syms[fn]["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {syms[fn]["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{tag}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)
    eip = int(page.get_attribute("#regs", "data-phys"))
    icount = page.evaluate("emulator.v86.cpu.instruction_counter[0] >>> 0")  # v86's count: Restore puts it back
    steps = page.evaluate("stepper.steps")  # the count the Machine tab shows, and the bookmark's name (X2)
    r["eip"] = [hex(eip), steps, icount]
    show_tab(page, "debugger")
    added = {}

    # 1. Code at EIP: its ☆ button; the default names EIP's symbol.
    if not page.locator("#bm_add_eip").count():
        ctx.close()
        return [f"{tag}: no ☆ button by Code at EIP (#bm_add_eip)"], r
    bm_tap(page, "#bm_add_eip", phone)
    if wait_for(lambda: page.evaluate(BM_DIALOG_JS) is not None, 5):
        bm_shots(page, shots, f"bookmarks-{name}-{width}-dialog", scheme)
    b = bm_dialog_save(page, problems, tag, "code", want_default=None)
    if b:
        added["eip"] = b
        if b["t"]["pa"] != eip:
            problems.append(f"{tag}: the Code at EIP bookmark holds {hex(b['t']['pa'])}, EIP is at {hex(eip)}")
    # ... and the line shows the ★ (in Code at EIP).
    wait_for(lambda: page.locator(f'#context .ins[data-addr="{eip}"] .bmstar').count() > 0, 5)
    if b:
        bm_star_tip(page, f'#context .ins[data-addr="{eip}"] .bmstar', b["name"], phone, problems, f"{tag} Code at EIP")

    # 2. A frame of the call stack: its ☆; a function bookmark of that frame's function.
    frames = json.loads(page.get_attribute("#call_stack", "data-frames") or "[]")
    r["frames"] = [f["name"] for f in frames]
    k = next((i for i, f in enumerate(frames) if f["name"] in syms and not f["user"]), None)
    if k is None or not page.locator("#call_stack .cframe .bmadd").count():
        problems.append(f"{tag}: no call stack frame with a ☆ button (frames {r['frames']})")
    else:
        bm_tap(page, f"#call_stack .cframe >> nth={k} >> .bmadd", phone)
        b = bm_dialog_save(page, problems, tag, "fn", name="frame " + frames[k]["name"], want_default=frames[k]["name"])
        if b:
            added["frame"] = b
            if b["t"]["name"] != frames[k]["name"] or b["t"]["pa"] != syms[frames[k]["name"]]["paddr"]:
                problems.append(f"{tag}: the call stack bookmark holds {b['t']}, want {frames[k]['name']} at {hex(syms[frames[k]['name']]['paddr'])}")
            wait_for(lambda: page.locator("#call_stack .cframe .bmstar").count() > 0, 5)
            if not page.locator("#call_stack .cframe .bmstar").count():
                problems.append(f"{tag}: the bookmarked call stack frame has no ★")

    # 3. A listing line in the Page tab: context menu / long press, a typed name.
    target = syms[fn]["paddr"]
    page.evaluate(f"jumpTo({target})")  # the Page tab at the function, as a symbol link does
    time.sleep(1.4)
    addrs = page.evaluate("[...document.querySelectorAll('#listing .ins[data-addr]')].map((e) => Number(e.dataset.addr))")
    line_pa = next((a for a in addrs if a > target + 4), None)
    if line_pa is None:
        problems.append(f"{tag}: the Page listing has no instruction after {fn}")
    else:
        bm_press(page, f'#listing .ins[data-addr="{line_pa}"]', phone, cdp)
        b = bm_dialog_save(page, problems, tag, "code", name="my line", key="Enter", want_default=fn + "+0x")
        if b:
            added["line"] = b
            if b["t"]["pa"] != line_pa:
                problems.append(f"{tag}: the listing line bookmark holds {hex(b['t']['pa'])}, the line is {hex(line_pa)}")
            bm_star_tip(page, f'#listing .ins[data-addr="{line_pa}"] .bmstar', "my line", phone, problems, f"{tag} listing")
        # The B key with the pointer over a line (a mouse).
        if not phone:
            other_pa = next((a for a in addrs if a > line_pa), None)
            page.hover(f'#listing .ins[data-addr="{other_pa}"]')
            page.keyboard.press("b")
            b = bm_dialog_save(page, problems, tag + " B key", "code")
            if b and b["t"]["pa"] != other_pa:
                problems.append(f"{tag}: B over {hex(other_pa)} bookmarked {hex(b['t']['pa'])}")
            if b:
                added["bkey"] = b
            # Escape cancels the dialog.
            page.hover(f'#listing .ins[data-addr="{other_pa}"]')
            page.keyboard.press("b")
            if wait_for(lambda: page.evaluate(BM_DIALOG_JS) is not None, 3):
                n0 = len(bm_state(page)["list"])
                page.keyboard.press("Escape")
                if not wait_for(lambda: page.evaluate(BM_DIALOG_JS) is None, 3) or len(bm_state(page)["list"]) != n0:
                    problems.append(f"{tag}: Escape did not cancel the dialog")
            else:
                problems.append(f"{tag}: the B key opened no dialog the second time")
        # The function's label line above its first instruction: a bookmark
        # of the function, by a right click or a long press. At 390 px a long
        # press on the minified Linux's start_kernel label opened no dialog
        # (CI run 43509415990): Chromium gave its touch events to the whole
        # listing (#listing), not to the label under the finger.
        lbl = f'#listing .lbl[data-addr="{target}"]'
        if not page.locator(lbl).count():
            problems.append(f"{tag}: the Page listing has no label line of {fn}")
        else:
            bm_press(page, lbl, phone, cdp)
            b = bm_dialog_save(page, problems, tag + " label line", "fn", name="label " + fn, want_default=fn)
            if b:
                added["label"] = b
                if b["t"].get("pa") != target or b["t"].get("name") != fn:
                    problems.append(f"{tag}: the label line's bookmark holds {b['t']}, want {fn} at {hex(target)}")
    bm_shots(page, shots, f"bookmarks-{name}-{width}-listing", scheme)

    # 4. A page of the Memory grid: context menu / long press on its square.
    mem_p = eip // 4096 + 1
    x, y = page_center(page, mem_p)
    bm_press_xy(page, x, y, phone, cdp)
    b = bm_dialog_save(page, problems, tag, "page", want_default=hex(mem_p))
    if b:
        added["page"] = b
        if b["t"]["page"] != mem_p:
            problems.append(f"{tag}: the Memory page bookmark holds page {hex(b['t']['page'])}, the square is {hex(mem_p)}")
        if shown_tab(page) != "memory":
            problems.append(f"{tag}: bookmarking a page left the Memory tab ({shown_tab(page)})")
        # B3: only the click that ends the long press is dropped. The next
        # click, here a tab's right after the dialog closed, still works (it
        # was dropped for 1 s after the press, so CI's next step, the
        # Debugger tab, sometimes stayed hidden).
        if phone:
            r["click_after_long_press_ms"] = round(page.evaluate("realNow() - BM.longAt"))
            page.click("#tab_debugger")
            if not wait_for(lambda: shown_tab(page) == "debugger", 3):
                problems.append(f"{tag}: a click on the Debugger tab {r['click_after_long_press_ms']} ms after the long press "
                                f"that opened the dialog did nothing (tab {shown_tab(page)})")
            show_tab(page, "memory")
        wait_for(lambda: page.locator(f'#pages_box .bmpage[data-page="{mem_p}"]').count() > 0, 5)
        bm_star_tip(page, f'#pages_box .bmpage[data-page="{mem_p}"]', b["name"], phone, problems, f"{tag} Memory")
    # The Memory tab's ☆ (the selected page, else EIP's).
    if not page.locator("#bm_add_mem").count():
        problems.append(f"{tag}: no ☆ button in the Memory tab (#bm_add_mem)")
    bm_shots(page, shots, f"bookmarks-{name}-{width}-memory", scheme)

    # 5. A line of the Source panel: pick it, its ☆.
    show_tab(page, "debugger")
    if not wait_for(lambda: shown_tab(page) == "debugger", 3):
        problems.append(f"{tag}: the Debugger tab did not open (tab {shown_tab(page)})")
    if wait_for(lambda: page.locator("#context .src:not(.nol)").first.is_visible(), 30):
        page.locator("#context .src:not(.nol)").first.scroll_into_view_if_needed()
        bm_tap(page, "#context .src:not(.nol) >> nth=0", phone)
        if not wait_for(lambda: page.evaluate("!document.getElementById('src_panel').hidden && document.getElementById('src_body').dataset.state === 'ok'"), 25):
            problems.append(f"{tag}: a C line of Code at EIP did not open the Source panel")
        time.sleep(0.5)
        rows = page.evaluate("[...document.querySelectorAll('#src_body .exec:not(.cont)')].map((e) => Number(e.dataset.line))")
        file = page.get_attribute("#src_body", "data-file")
        if rows:
            ln = rows[len(rows) // 2]
            page.locator(f'#src_body [data-line="{ln}"]').scroll_into_view_if_needed()
            bm_tap(page, f'#src_body [data-line="{ln}"]', phone)
            time.sleep(0.3)
            if not page.locator("#bm_add_src").count():
                problems.append(f"{tag}: no ☆ button in the Source panel (#bm_add_src)")
            else:
                bm_tap(page, "#bm_add_src", phone)
                b = bm_dialog_save(page, problems, tag, "line", want_default=file.rsplit("/", 1)[-1] + ":" + str(ln))
                if b:
                    added["src"] = b
                    if b["t"] != {"path": file, "line": ln}:
                        problems.append(f"{tag}: the Source panel bookmark holds {b['t']}, want {file}:{ln}")
                    bm_star_tip(page, f'#src_body [data-line="{ln}"] .bmstar', b["name"], phone, problems, f"{tag} Source panel")
            bm_shots(page, shots, f"bookmarks-{name}-{width}-srcpanel", scheme)
        else:
            problems.append(f"{tag}: the Source panel shows no executed line")
        if page.is_visible("#src_close"):
            bm_tap(page, "#src_close", phone)
        else:
            problems.append(f"{tag}: the Source panel is not open after bookmarking a line of it")
    else:
        problems.append(f"{tag}: Code at EIP shows no C line")

    # 6. A file of the Source tab: the row's ☆.
    show_tab(page, "source")
    page.fill("#cov_filter", src_path.rsplit("/", 1)[-1])
    sel = f'#cov_tree .covrow[data-kind="file"][data-path="{src_path}"] .bmadd'
    if not wait_for(lambda: page.locator(sel).count() > 0, 20):
        problems.append(f"{tag}: the Source tab's row of {src_path} has no ☆ button")
    else:
        bm_tap(page, sel, phone)
        b = bm_dialog_save(page, problems, tag, "file", want_default=src_path)
        if b:
            added["file"] = b
            if b["t"] != {"path": src_path}:
                problems.append(f"{tag}: the Source tab bookmark holds {b['t']}, want {src_path}")
            wait_for(lambda: page.locator(f'#cov_tree .covrow[data-path="{src_path}"] .bmstar').count() > 0, 5)
            if not page.locator(f'#cov_tree .covrow[data-path="{src_path}"] .bmstar').count():
                problems.append(f"{tag}: the bookmarked file row of the Source tab has no ★")
        bm_fit(page, tag + " Source tab", problems)
        bm_shots(page, shots, f"bookmarks-{name}-{width}-source", scheme)
    page.fill("#cov_filter", "")

    # 7. A box of the Calls tab: selected, then the Calls ☆ (1280) or a long press (390).
    show_tab(page, "calls")
    if not wait_for(lambda: page.evaluate("G.rects.length > 0"), 20):
        problems.append(f"{tag}: the Calls timeline drew no box")
    else:
        xy = graph_box_xy(page, fn)
        if not xy:
            problems.append(f"{tag}: no {fn} box in the Calls tab")
        else:
            if phone:
                tip_long_press(page, cdp, xy["x"], xy["y"])
            else:
                page.mouse.click(xy["x"], xy["y"])
                time.sleep(0.7)
                if not page.locator("#bm_add_calls").count():
                    problems.append(f"{tag}: no ☆ button in the Calls tab (#bm_add_calls)")
                else:
                    page.click("#bm_add_calls")
            b = bm_dialog_save(page, problems, tag, "fn", want_default=fn)
            if b:
                added["calls"] = b
                if b["t"] != {"pa": syms[fn]["paddr"], "name": fn}:
                    problems.append(f"{tag}: the Calls bookmark holds {b['t']}, want {fn} at {hex(syms[fn]['paddr'])}")
                time.sleep(1.2)
                marked = page.evaluate("Number(document.getElementById('graph_canvas').dataset.bmarks || 0)")
                if not marked:
                    problems.append(f"{tag}: no box of the Calls tab shows the bookmark's ★ (data-bmarks {marked})")
            if shown_tab(page) != "calls":
                problems.append(f"{tag}: bookmarking a box left the Calls tab ({shown_tab(page)})")
        bm_shots(page, shots, f"bookmarks-{name}-{width}-calls", scheme)

    # 8. The machine's position: the menu's ☆.
    bm_open_list(page, phone)
    if not page.locator("#bm_here").count():
        problems.append(f"{tag}: the menu has no ☆ for the machine's position (#bm_here)")
    else:
        bm_tap(page, "#bm_here", phone)
        # At 1280 with a snapshot saved along (S1), so the bookmark can restore the machine.
        def tick():
            if wait_for(lambda: page.is_visible("#bm_snap"), 3):
                page.check("#bm_snap")
                page.focus("#bm_name")
            else:
                problems.append(f"{tag}: the position dialog offers no snapshot (#bm_snap)")
        b = bm_dialog_save(page, problems, tag, "pos", want_default="@" + f"{steps:,}", then=None if phone else tick)
        if b:
            added["pos"] = b
            if b["t"]["pa"] != eip or b["t"].get("steps") != steps or b["t"]["icount"] != icount:
                problems.append(f"{tag}: the position bookmark holds {b['t']}, want EIP {hex(eip)} at {steps} (v86 {icount})")
            if not phone:
                if not wait_for(lambda: (bm_state(page)["list"][-1]["t"].get("snap") or "") != "", 30):
                    problems.append(f"{tag}: the position bookmark has no snapshot tied to it: {bm_state(page)['list'][-1]['t']}")
                else:
                    added["pos"] = bm_state(page)["list"][-1]

    # 9. Rename one in the list (inline), check the list and the storage.
    st = bm_state(page)
    r["added"] = {k: [v["kind"], v["name"]] for k, v in added.items()}
    if len(st["rows"]) != len(added):
        problems.append(f"{tag}: the list shows {len(st['rows'])} bookmarks, {len(added)} were added: {[x['name'] for x in st['rows']]}")
    ren = added.get("eip")
    if ren:
        bm_open_list(page, phone)
        bm_tap(page, f'#bm_list .bmrow[data-id="{ren["id"]}"] .bmren', phone)
        inp = f'#bm_list .bmrow[data-id="{ren["id"]}"] input.bmedit'
        if not wait_for(lambda: page.locator(inp).count() > 0, 3):
            problems.append(f"{tag}: ✎ does not make the name editable")
        else:
            page.fill(inp, "EIP here")
            page.press(inp, "Enter")
            wait_for(lambda: any(x["name"] == "EIP here" for x in bm_state(page)["rows"]), 3)
            ren["name"] = "EIP here"
    st = bm_state(page)
    if not st["store"] or len(st["store"].get("bookmarks", [])) != len(added):
        problems.append(f"{tag}: localStorage mkb-bookmarks holds {st['store'] and len(st['store'].get('bookmarks', []))}, want {len(added)}")
    if any(x["group"] != "this" for x in st["rows"]):
        problems.append(f"{tag}: bookmarks of this kernel listed as another's: {[(x['name'], x['group']) for x in st['rows']]}")
    bm_fit(page, tag, problems)
    bm_shots(page, shots, f"bookmarks-{name}-{width}-list", scheme)
    bm_close_menu(page)

    # 10. Reload: every bookmark is listed with its name; each jump lands; Back returns.
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: len(bm_state(page)["rows"]) == len(added) and all(x["group"] == "this" for x in bm_state(page)["rows"]), 20)
    st = bm_state(page)
    names = sorted(x["name"] for x in st["rows"])
    want = sorted(v["name"] for v in added.values())
    if names != want:
        problems.append(f"{tag}: after the reload the list has {names}, want {want}")
    if any(x["group"] != "this" for x in st["rows"]):
        problems.append(f"{tag}: after the reload bookmarks are not this kernel's: {[(x['name'], x['group']) for x in st['rows']]}")
    show_tab(page, "debugger")
    for key in ("eip", "line", "page", "src", "file", "calls", "frame", "pos"):
        if key in added:
            bm_jump_check(page, added[key], F, syms, problems, f"{tag} jump {key}", phone)
    # The position's snapshot restores the machine there (after the reload: from IndexedDB).
    if not phone and added.get("pos", {}).get("t", {}).get("snap"):
        bm_open_list(page, phone)
        rs = f'#bm_list .bmrow[data-id="{added["pos"]["id"]}"] .bmrestore'
        if not wait_for(lambda: page.locator(rs).count() > 0, 15):
            problems.append(f"{tag}: the position bookmark with a snapshot has no Restore")
        else:
            page.click(rs)
            if not wait_for(lambda: page.evaluate("(emulator.v86.cpu.instruction_counter[0] >>> 0)") == icount and
                            int(page.get_attribute("#regs", "data-phys") or -1) == eip, 30):
                problems.append(f"{tag}: Restore did not bring the machine to EIP {hex(eip)} after {icount}: "
                                f"{page.evaluate('emulator.v86.cpu.instruction_counter[0] >>> 0')} at {page.get_attribute('#regs', 'data-phys')}")
        bm_close_menu(page)
    # Keyboard: Tab to the first bookmark in the list, Enter jumps.
    if not phone and "page" in added:
        bm_open_list(page, phone)
        page.focus("#bm_filter")
        page.fill("#bm_filter", added["page"]["name"][:6])
        time.sleep(0.3)
        shown = [x for x in bm_state(page)["rows"] if x["shown"]]
        if not shown or any(added["page"]["name"][:6].lower() not in x["name"].lower() for x in shown):
            problems.append(f"{tag}: the filter {added['page']['name'][:6]!r} shows {[x['name'] for x in shown]}")
        page.keyboard.press("Tab")
        foc = page.evaluate("document.activeElement && document.activeElement.className")
        before = nav_at(page)
        page.keyboard.press("Enter")
        if not wait_for(lambda: nav_at(page)[0] == before[0] + 1 and bm_where(page)["listingPage"] == added["page"]["t"]["page"], 8):
            problems.append(f"{tag}: Tab and Enter from the filter did not jump to the filtered bookmark (focus on {foc!r}): {nav_at(page)}")
        bm_open_list(page, phone)
        page.fill("#bm_filter", "")
        bm_close_menu(page)

    # 11. Delete one; it stays deleted after a reload.
    gone = added.get("bkey") or added.get("pos")
    if gone:
        bm_open_list(page, phone)
        bm_tap(page, f'#bm_list .bmrow[data-id="{gone["id"]}"] .bmdel', phone)
        if not wait_for(lambda: not any(x["id"] == gone["id"] for x in bm_state(page)["rows"]), 3):
            problems.append(f"{tag}: ✕ did not delete {gone['name']!r}")
        added = {k: v for k, v in added.items() if v is not gone}

    # 12. Export, then import into a fresh context.
    bm_open_list(page, phone)
    exported = None
    if not page.locator("#bm_export").count():
        problems.append(f"{tag}: no Export button")
    else:
        with page.expect_download() as dl:
            bm_tap(page, "#bm_export", phone)
        path = dl.value.path()
        try:
            exported = json.loads(pathlib.Path(path).read_text())
        except Exception as e:
            problems.append(f"{tag}: the export is no JSON: {e}")
        if exported:
            got = sorted(x["name"] for x in exported.get("bookmarks", []))
            if got != sorted(v["name"] for v in added.values()):
                problems.append(f"{tag}: the export has {got}, the list {sorted(v['name'] for v in added.values())}")
            r["export"] = {"format": exported.get("format"), "n": len(exported.get("bookmarks", []))}
            ex = pathlib.Path(shots) / f"bookmarks-{name}-{width}.json"
            ex.write_text(json.dumps(exported))
    link = next((x["link"] for x in bm_state(page)["rows"] if added.get("calls") and x["id"] == added["calls"]["id"]), "")
    bm_close_menu(page)
    if errors:
        problems.append(f"{tag}: page errors {errors}")
    ctx.close()

    if exported:
        ctx2 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p2 = ctx2.new_page()
        p2.emulate_media(color_scheme=scheme)
        p2.goto(f"{base}/index.html?kernel={name}&paused=1#machine")
        wait_for(lambda: p2.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p2, phone)
        if not len(bm_state(p2)["rows"]) == 0:
            problems.append(f"{tag}: a fresh context lists bookmarks before the import")
        if not p2.locator("#bm_import_file").count():
            problems.append(f"{tag}: no import file input (#bm_import_file)")
        else:
            p2.set_input_files("#bm_import_file", str(ex))
            wait_for(lambda: len(bm_state(p2)["rows"]) == len(exported["bookmarks"]), 5)
            got = sorted(x["name"] for x in bm_state(p2)["rows"])
            if got != sorted(x["name"] for x in exported["bookmarks"]):
                problems.append(f"{tag}: the import restored {got}, the file has {sorted(x['name'] for x in exported['bookmarks'])}")
            if any(x["group"] != "this" for x in bm_state(p2)["rows"]):
                problems.append(f"{tag}: imported bookmarks are not this kernel's")
            bm_fit(p2, tag + " import", problems)
        ctx2.close()

    # 13. A link with a bookmark opens its place (a fresh context: nothing stored).
    if not link or "#bm=" not in link:
        problems.append(f"{tag}: the Calls bookmark has no link with #bm= ({link!r})")
    else:
        r["link"] = link[-80:]
        ctx3 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p3 = ctx3.new_page()
        p3.emulate_media(color_scheme=scheme)
        p3.goto(link)
        want_pa = syms[fn]["paddr"]
        # The machine runs (no paused=1): the listing may follow EIP afterwards, so
        # the check is the jump itself: the Page tab at the function, its line outlined.
        if not wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == want_pa // 4096 and want_pa in w["jhit"])(bm_where(p3)), 90):
            w = bm_where(p3)
            problems.append(f"{tag}: the link did not open {fn} in the Page tab: tab {w['tab']}, page {w['listingPage']}")
        else:
            time.sleep(1)
            bm_shots(p3, shots, f"bookmarks-{name}-{width}-link", scheme)
        ctx3.close()

    # 14. Another kernel: listed apart, with a switch to the kernel they are for.
    if other:
        ctx4 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p4 = ctx4.new_page()
        p4.emulate_media(color_scheme=scheme)
        p4.goto(f"{base}/index.html?kernel={name}&paused=1#machine")
        wait_for(lambda: p4.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p4, phone)
        p4.set_input_files("#bm_import_file", str(ex))
        wait_for(lambda: len(bm_state(p4)["rows"]) > 0, 5)
        n = len(bm_state(p4)["rows"])
        p4.goto(f"{base}/index.html?kernel={other}&paused=1#machine")
        wait_for(lambda: p4.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p4, phone)
        if not wait_for(lambda: len(bm_state(p4)["rows"]) == n and all(x["group"] == "other" for x in bm_state(p4)["rows"]), 15):
            problems.append(f"{tag}: on {other} the {name} bookmarks are not listed as another kernel's: "
                            f"{[(x['name'], x['group']) for x in bm_state(p4)['rows']]}")
        sw = p4.locator("#bm_list .bmrow .bmswitch")
        if not sw.count():
            problems.append(f"{tag}: no switch action for another kernel's bookmarks")
        else:
            bm_shots(p4, shots, f"bookmarks-{name}-{width}-other", scheme)
            sw.first.tap() if phone else sw.first.click()
            if not wait_for(lambda: p4.input_value("#kernel") == name and all(x["group"] == "this" for x in bm_state(p4)["rows"]), 60):
                problems.append(f"{tag}: the switch did not load {name} with its bookmarks as this kernel's: "
                                f"{p4.input_value('#kernel')} {[(x['name'], x['group']) for x in bm_state(p4)['rows']]}")
        ctx4.close()
    return problems, r


def bm_fit(page, tag, problems):
    got = page.evaluate("""() => { const box = (id) => { const e = document.getElementById(id); if (!e || e.hidden || !e.getClientRects().length) return null;
        const b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
        const l = document.getElementById('bm_list');
        return { view: [innerWidth, innerHeight], menu: box('runbadge_menu'), list: box('bm_list'),
                 hdoc: document.documentElement.scrollWidth > innerWidth + 1, hlist: !!l && l.scrollWidth > l.clientWidth + 1 }; }""")
    for k in ("menu", "list"):
        b = got[k]
        if b and (b[0] < -1 or b[1] < -1 or b[2] > got["view"][0] + 1 or b[3] > got["view"][1] + 1):
            problems.append(f"{tag}: the bookmarks {k} {b} is outside the viewport {got['view']}")
    if got["hdoc"] or got["hlist"]:
        problems.append(f"{tag}: horizontal scroll with the bookmarks list open (page {got['hdoc']}, list {got['hlist']})")


# Storage that fails on demand: setItem throws a QuotaExceededError while
# window.__bmFail is set; every access throws (a browser keeping no data
# for the page) when the URL has nostore=1.
BM_STORAGE_JS = """(() => {
  if (new URLSearchParams(location.search).get('nostore') === '1') {
    Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('no storage here', 'SecurityError'); } });
    return;
  }
  const set = Storage.prototype.setItem;
  Storage.prototype.setItem = function (k, v) {
    if (window.__bmFail) throw new DOMException('full', 'QuotaExceededError');
    return set.call(this, k, v);
  };
})();"""


def bm_status(page):
    return page.text_content("#bm_status") or ""


def bookmarks_edges(browser, base, shots):
    """Bookmarks off the main path, on hello paused at 1280: B in the
    About, Page and Memory tabs; import of a file that is no JSON, of
    JSON without bookmarks, and of a list with one new, one already there
    and one unreadable entry; delete and Undo; another page of the same
    site sees a change at once (the storage event); a link that cannot be
    read; a link from another build of hello (goes there, says so); the
    switch for a kernel that is not listed; storing failing (quota) and a
    browser that keeps no data: the list says so."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script(BM_STORAGE_JS)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel=hello&paused=1"
    page.goto(url + "#machine")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    tmp = tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP"))
    try:
        # B in three tabs: the machine's position (About: no place of its own;
        # in the Machine tab B is the VM's key), the Page tab's page, the
        # Memory tab's page.
        kinds = {}
        for tab, kind in (("about", "pos"), ("page", None), ("memory", "page")):
            show_tab(page, tab)
            page.click(f'.tabbar [role=tab][data-tab="{tab}"]')
            if tab == "page":
                page.evaluate("selectPage(0x100000 / 4096)")
                time.sleep(0.5)
            page.mouse.move(5, 5)
            page.evaluate("document.activeElement && document.activeElement.blur && document.activeElement.blur()")
            page.keyboard.press("b")
            got = bm_dialog_save(page, problems, "edges", kind or (page.evaluate(BM_DIALOG_JS) or {}).get("kind", "?"))
            kinds[tab] = got and got["kind"]
        r["b_key"] = kinds
        if kinds.get("about") != "pos" or kinds.get("page") not in ("code", "page") or kinds.get("memory") != "page":
            problems.append(f"B in the About, Page and Memory tabs: {kinds}, want pos, code or page, page")
        st = bm_state(page)
        pos = next((b for b in st["list"] if b["kind"] == "pos"), None)
        if not pos:
            problems.append(f"no machine position bookmark to go on with: {st['list']}")
            return problems, r
        bm_open_list(page, False)
        # Import: no JSON, no bookmarks, a mixed list.
        d = pathlib.Path(tmp.name)
        files = {"junk.json": "this is not JSON", "nolist.json": json.dumps({"format": "x"}),
                 "mixed.json": json.dumps({"bookmarks": [pos, {"name": 3, "kind": "nope"}, {**pos, "id": "bnew1", "name": "another one"}]})}
        want = {"junk.json": "junk.json is not a bookmarks file", "nolist.json": "nolist.json has no bookmarks",
                "mixed.json": "Imported 1 bookmarks from mixed.json, 1 were here already, 1 unreadable."}
        for n, text in files.items():
            (d / n).write_text(text)
            page.set_input_files("#bm_import_file", str(d / n))
            ok = wait_for(lambda: want[n] in bm_status(page), 5)
            r["import_" + n] = bm_status(page)[:100]
            if not ok:
                problems.append(f"import {n}: status {bm_status(page)!r}, want {want[n]!r}")
        n0 = len(bm_state(page)["list"])
        # Another page of the site (same storage) follows.
        other = ctx.new_page()
        other.goto(url + "#machine")
        wait_for(lambda: other.evaluate("typeof BM === 'object' && BM.list.length") == n0, 30)
        # Delete, Undo.
        page.click('#bm_list .bmrow[data-id="bnew1"] .bmdel')
        wait_for(lambda: len(bm_state(page)["list"]) == n0 - 1, 3)
        seen = wait_for(lambda: other.evaluate("BM.list.length") == n0 - 1, 5)
        r["other_page_follows"] = seen
        if not seen:
            problems.append(f"another page of the site still lists {other.evaluate('BM.list.length')} bookmarks after a delete, want {n0 - 1}")
        page.click("#bm_status button")
        if not wait_for(lambda: len(bm_state(page)["list"]) == n0 and "“another one” is back." in bm_status(page), 3):
            problems.append(f"Undo of a delete: {len(bm_state(page)['list'])} bookmarks, status {bm_status(page)!r}")
        other.close()
        # A bookmark of a kernel the menu does not list: its switch says so.
        (d / "alien.json").write_text(json.dumps([{**pos, "id": "balien", "name": "alien", "kernel": "no-such-kernel", "kernelKey": "f" * 64}]))
        page.set_input_files("#bm_import_file", str(d / "alien.json"))
        wait_for(lambda: page.locator('#bm_list .bmrow[data-id="balien"] .bmswitch').count() > 0, 5)
        page.click('#bm_list .bmrow[data-id="balien"] .bmswitch')
        if "“no-such-kernel” is not one of the listed kernels" not in bm_status(page):
            problems.append(f"switch to a kernel not listed: {bm_status(page)!r}")
        # Storing fails: the list says the bookmarks are not kept.
        page.evaluate("window.__bmFail = true")
        page.click('#bm_list .bmrow[data-id="balien"] .bmdel')
        wait_for(lambda: page.is_visible("#bm_store"), 3)
        r["store_fail"] = page.text_content("#bm_store")
        if "storing them failed (QuotaExceededError)" not in (r["store_fail"] or ""):
            problems.append(f"storing failing: #bm_store {r['store_fail']!r}")
        page.evaluate("window.__bmFail = false")
        page.screenshot(path=str(shots / "bookmarks-edges-1280.png"))
        page.close()
        # Links: one that cannot be read, one from another build of hello.
        p2 = ctx.new_page()
        p2.on("pageerror", lambda e: errors.append(str(e)[:200]))
        p2.goto(url + "#bm=@@not-base64@@")
        if not wait_for(lambda: "The link's bookmark could not be read." in bm_status(p2), 30):
            problems.append(f"a link that cannot be read: status {bm_status(p2)!r}")
        data = {"n": "elsewhere", "k": pos["kind"], "t": pos["t"], "kn": "hello", "kk": "0" * 64}
        b64 = base64.urlsafe_b64encode(json.dumps(data).encode()).decode().rstrip("=")
        p2.goto(url + "#bm=" + b64)
        ok = wait_for(lambda: "“elsewhere” was made with another build of hello" in bm_status(p2), 60)
        r["other_build"] = bm_status(p2)[:120]
        if not ok:
            problems.append(f"a link from another build of hello: status {bm_status(p2)!r}")
        p2.close()
        # A browser that keeps no data for the page.
        p3 = ctx.new_page()
        p3.on("pageerror", lambda e: errors.append(str(e)[:200]))
        p3.goto(f"{base}/index.html?kernel=hello&paused=1&nostore=1#machine")
        wait_for(lambda: p3.evaluate("typeof BM === 'object'"), 30)
        bm_open_list(p3, False)
        r["no_storage"] = p3.text_content("#bm_store")
        if "this browser keeps no data for the page (SecurityError)" not in (r["no_storage"] or "") or not p3.is_visible("#bm_store"):
            problems.append(f"no storage: #bm_store {r['no_storage']!r}")
        p3.screenshot(path=str(shots / "bookmarks-nostore-1280.png"))
    finally:
        tmp.cleanup()
        if errors:
            problems.append(f"page errors: {errors[:3]}")
        ctx.close()
    return problems, r


def bookmarks_case(browser, base, shots, linux):
    out = []
    names = kernel_entries()
    runs = [("hello", "light", False, "hello-os" if "hello-os" in names else None), ("hello", "dark", True, None)]
    if linux:
        runs += [("minified-linux", "dark", False, None), ("minified-linux", "light", True, None)]
    for name, scheme, phone, other in runs:
        t0 = time.monotonic()
        problems = []
        try:
            problems, r = bookmarks_run(browser, base, shots, name, scheme, phone, other, problems)
        except Exception as e:  # a missing control times out in Playwright: report it (and where), go on
            tb = traceback.extract_tb(e.__traceback__)
            at = ([f for f in tb if f.filename == __file__] or tb)[-1]
            problems, r = problems + [f"{name} {390 if phone else 1280} {scheme}: {type(e).__name__} at line {at.lineno} "
                                      f"({at.line}): {str(e)[:300]}"], {}
        out.append(report("bookmarks", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    out.append(report("bookmarks edges", *bookmarks_edges(browser, base, shots)))
    for name, scheme, phone in [("hello", "light", False)] + ([("minified-linux", "dark", True)] if linux else []):
        problems, r = snapdiff_guard(onecount_run, browser, base, shots, name, scheme, phone)
        out.append(report("bookmarks one count", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    return any(out)


def suite_bookmarks(ctx):
    return bookmarks_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)


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
JITDET_MIN_RATIO = 0.9
JITDET_RUNNING_AT = 1_500_000_000  # count at which the running page is checked
# The speed: a 10 ms watcher times the run from Continue to v86's count cap
# (or to an earlier stop: on real time Debian can reach its panic before the
# cap). stop: also stop there (on real time: Pause), else the virtual-clock
# run goes on to the page's own stop at the panic's last line.
JITDET_WATCH_JS = """([cap, stop]) => { const st = stepper, w = window.__jd = { t0: realNow(), c0: ffCounter(st), cap, at: null, c: null, done: false };
  const iv = setInterval(() => { if (stepper !== st) { clearInterval(iv); return; }
    const c = ffCounter(st), stopped = runState === 'paused' || runState === 'off';
    if (!w.ran && runState === 'jit') Object.assign(w, { ran: true, t0: realNow(), c0: c });
    if (w.ran && !w.at && (c >= cap || stopped)) { w.at = realNow(); w.c = c; if (stop && runState === 'jit') pause(); }
    if (w.at && (stop || stopped)) { w.done = true; clearInterval(iv); } }, 10); }"""
JITDET_STATE_JS = """async () => { const st = stepper, w = window.__jd || {}, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const c = ffCounter(st);
  return { steps: st.steps, counter: c, eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    chain: st.jitChain === undefined ? null : (st.jitChain >>> 0).toString(16), irq0: st.irq0, end: st.end ? st.end.kind : null,
    exact: st.end ? st.end.exact === true : null, compiles: st.jitCompiles || 0, waits: st.jitWaits || 0,
    checkpoints: REV.cps.filter((c) => c.jit).length, save_ms: Math.round(REV.cps.reduce((a, c) => a + (c.jit ? c.saveMs : 0), 0)),
    mips: w.at ? +(((w.c - w.c0) >>> 0) / (w.at - w.t0) / 1000).toFixed(1) : null }; }"""
JITDET_RUNNING_JS = """() => ({ clock: document.getElementById('vclock').textContent, virtual: document.getElementById('vclock').dataset.virtual,
  badge: document.getElementById('runbadge').dataset.state, face: document.getElementById('runbadge_speed').textContent,
  status: document.getElementById('dbg_status').textContent, runState })"""


def jitdet_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def jitdet_count_run(browser, base, shots, name, mips, scheme, phone, tag, problems):
    """From the reset vector (paused) at full speed: on the virtual clock to
    the page's stop at the end, on real time to v86's count JITDET_CAP."""
    ctx = jitdet_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips={mips}", timeout=120000)
        if not wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && runState === 'paused'"), 180, 0.5):
            problems.append(f"{tag}: not paused at the reset vector")
            return None
        page.evaluate(JITDET_WATCH_JS, [JITDET_CAP, mips == "real"])
        machine_do(page, "click", "#dbg_continue")
        # Look at the running page well before the cap: a fast runner (CI,
        # 650 MIPS) reaches it about 4 s after Continue.
        wait_for(lambda: page.evaluate(f"ffCounter(stepper) >= {JITDET_RUNNING_AT} || !!window.__jd.at"), 120, 0.05)
        running = page.evaluate(JITDET_RUNNING_JS)
        page.screenshot(path=str(shots / f"jitdet-{tag}-running.png"))
        if not wait_for(lambda: page.evaluate("window.__jd.done && runState === 'paused'"), 400, 0.5):
            problems.append(f"{tag}: did not stop (counter {page.evaluate('ffCounter(stepper)'):,})")
            return None
        out = page.evaluate(JITDET_STATE_JS)
        out["running"] = running
        page.screenshot(path=str(shots / f"jitdet-{tag}-stopped.png"))
        want_virtual = "false" if mips == "real" else "true"
        if running["virtual"] != want_virtual or ("virtual time" if want_virtual == "true" else "real time") not in running["clock"]:
            problems.append(f"{tag}: while running at full speed the clock line says {running['clock']!r} (virtual={running['virtual']})")
        if running["runState"] != "jit" or running["face"] != "full":
            problems.append(f"{tag}: while running: runState {running['runState']}, run badge face {running['face']!r}")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def jitdet_end_run(browser, base, shots, name, scheme, phone, tag, problems):
    """The default load at full speed, count off: fast-forward to the
    entry, then the JIT to the kernel's end."""
    ctx = jitdet_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 240, 0.5):
            problems.append(f"{tag}: no end in 240 s")
            return None
        time.sleep(0.5)
        out = page.evaluate(JITDET_STATE_JS)
        out["virtual"] = page.get_attribute("#vclock", "data-virtual")
        page.screenshot(path=str(shots / f"jitdet-{tag}-end.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def jitdet_case(browser, base, shots, entries, debian):
    problems, r = [], {}
    keys = ("counter", "steps", "eip", "chain", "ram")
    # hello and the minified Linux: two full-speed boots end in the same state.
    for name, scheme, phone in (("hello", "light", False), ("minified-linux", "dark", True)):
        if name not in entries:
            continue
        runs = [jitdet_end_run(browser, base, shots, name, scheme, phone, f"{name}-{i}", problems) for i in range(2)]
        r[name] = runs
        if None in runs:
            continue
        a, b = runs
        for k in keys + ("irq0", "end"):
            if a[k] != b[k]:
                problems.append(f"{name}: end state differs in {k}: {a[k]} vs {b[k]}")
        if a["chain"] is None:
            problems.append(f"{name}: no chunk-chain hash (the JIT did not run from the page)")
    if not debian:
        return problems, r
    # Debian to its end twice on the virtual clock, to count JITDET_CAP once on real time.
    name = debian[0]
    det = [jitdet_count_run(browser, base, shots, name, "10", scheme, phone, f"debian-det{i}-{scheme}-{390 if phone else 1280}", problems)
           for i, (scheme, phone) in enumerate((("light", False), ("dark", True)))]
    rt = jitdet_count_run(browser, base, shots, name, "real", "light", False, "debian-realtime", problems)
    r["debian"] = det
    r["debian_realtime"] = rt
    if None not in det:
        a, b = det
        for k in keys + ("irq0", "end"):
            if a[k] != b[k]:
                problems.append(f"debian to its end: {k} differs: {a[k]} vs {b[k]}")
        if a["chain"] is None:
            problems.append("debian: no chunk-chain hash (the JIT did not run from the page)")
        for x in det:
            if x["end"] != "panic-text" or not x["exact"]:
                problems.append(f"debian: stopped at count {x['counter']:,} with end {x['end']} (exact {x['exact']}), "
                                "not at the instruction that wrote the panic's last line")
        # R6: the det JIT compiles a block where v86 queues it (fastforward.js
        # jitDetInstall), so the next slice never waits for it. Waiting for
        # WebAssembly.instantiate() there took about 1.4 s of an 8 s run on
        # d-claude, and came back after every JIT checkpoint's reset.
        if browser.browser_type.name == "chromium":
            for x in det:
                if x["waits"]:
                    problems.append(f"debian: the det JIT waited for {x['waits']} of {x['compiles']} compiled blocks")
        if rt and rt.get("mips") and a.get("mips"):
            best = max(x["mips"] for x in det)
            r["speed_ratio"] = round(best / rt["mips"], 3)
            if best < JITDET_MIN_RATIO * rt["mips"]:
                problems.append(f"debian: {best} MIPS on the virtual clock, below {JITDET_MIN_RATIO} x {rt['mips']} MIPS of v86's own loop")
    return problems, r


def suite_jitdet(ctx):
    return report("jitdet", *jitdet_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx)))


# ---- guestin: the guest's date and random numbers behind the virtual clock (J2)
# D0 (2026-11-10): on the virtual clock the stepped machine is the same in
# every run except for two inputs from the browser. v86's RTC takes its date
# from Date.now() (SeaBIOS turns it into the BIOS tick count at 0x46C, the
# only RAM difference for the small kernels) and RDRAND takes crypto's random
# numbers (Debian uses it: ~50 pages differ 30 M instructions after its
# entry). With the virtual clock the RTC runs from a start date (?rtc=,
# default GUESTIN_DEFAULT_RTC) on the virtual time, RDRAND and the NIC's MAC
# come from a seeded generator (?seed=), and a snapshot keeps both. No
# init script freezes Date or crypto here.
GUESTIN_DEFAULT_RTC = "2026-01-01T00:00:00Z"
GUESTIN_RTC = "2031-02-03T04:05:06Z"
GUESTIN_DEBIAN_STEPS = 30_000_000
GUESTIN_STATE_JS = """async () => { const m = emulator.read_memory(0, emuRam), c = emulator.v86.cpu;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const el = document.getElementById('guest_in');
  return { ram: sha.slice(0, 16), bda: Array.from(m.subarray(0x46c, 0x470)), ticks: (m[0x46c] | m[0x46d] << 8 | m[0x46e] << 16 | m[0x46f] << 24) >>> 0,
    eip: (c.instruction_pointer[0] >>> 0).toString(16), steps: stepper ? stepper.steps : null, clock: CLOCK.now(), virtual: CLOCK.virtual,
    rtcTime: c.devices.rtc.rtc_time, browserNow: Date.now(), line: el ? el.textContent : null, lineShown: !!el && el.offsetParent !== null,
    guest: typeof GUEST === 'object' ? { start: GUEST.rtcStart, seed: GUEST.seed, rng: GUEST.rng } : null }; }"""
GUESTIN_DATE_RE = re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +\d+ \d\d:\d\d:\d\d UTC \d{4}\b")
# BusyBox hwclock: "Mon Feb  3 04:11:14 2031  0.000000 seconds"
GUESTIN_HWCLOCK_RE = re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +\d+ \d\d:\d\d:\d\d \d{4}(?= +-?[\d.]+ seconds)")


def iso_ms(iso):
    return int(datetime.datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp() * 1000)


def bios_ticks(iso):
    """SeaBIOS's tick count at 0x46C for an RTC time of day (clock_setup:
    seconds since midnight x PIT_TICK_RATE / PIT_TICK_INTERVAL)."""
    t = datetime.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    return (t.hour * 3600 + t.minute * 60 + t.second) * 1193180 // 65536


def guestin_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def guestin_hello(browser, base, shots, query, scheme, phone, tag, problems):
    """hello booted on the given query to its halt; the machine state."""
    ctx = guestin_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello{query}", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end in 120 s")
            return None
        time.sleep(0.5)
        out = page.evaluate(GUESTIN_STATE_JS)
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"guestin-{tag}.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def guestin_debian_stepped(browser, base, shots, name, scheme, phone, tag, problems):
    """Debian: JIT fast-forward to startup_32, then GUESTIN_DEBIAN_STEPS
    stepped exactly; the machine state."""
    ctx = guestin_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1&mips=10#debugger", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no fast-forward to the kernel entry in 300 s")
            return None
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 30)
        target = page.evaluate("stepper.sinceEntry") + GUESTIN_DEBIAN_STEPS
        for _ in range(400):
            left = target - page.evaluate("stepper.steps")
            if left <= 0:
                break
            page.evaluate(f"doStep({min(left, 2_000_000)})")
        out = page.evaluate(GUESTIN_STATE_JS)
        if out["steps"] != target:
            problems.append(f"{tag}: stepped to {out['steps']:,}, not {target:,}")
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"guestin-{tag}.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def guestin_snapshot(browser, base, shots, problems, r):
    """A snapshot keeps the start date and the generator's state: saved
    with ?rtc=GUESTIN_RTC&seed=5, restored after a reload with another
    date and seed, both come back."""
    tag = "snapshot"
    ctx = guestin_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&rtc={GUESTIN_RTC}&seed=5", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end in 120 s")
            return
        st = snap_save(page, 1, False)
        row = st["rows"][0]["id"]
        saved = page.evaluate("(() => { const m = snapshots[0].meta; return { rtcStart: m.rtcStart, seed: m.seed, rng: m.rng }; })()")
        before = page.evaluate(GUESTIN_STATE_JS)
        r["snapshot_meta"] = saved
        if saved["rtcStart"] != iso_ms(GUESTIN_RTC) or saved["seed"] != 5 or not isinstance(saved["rng"], int):
            problems.append(f"{tag}: snapshot meta has no start date / seed / generator state: {saved}")
        page.goto(f"{base}/index.html?kernel=hello&rtc=2040-06-07T08:09:10Z&seed=9", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end after the reload")
            return
        other = page.evaluate(GUESTIN_STATE_JS)
        if not other["guest"] or other["guest"]["start"] != iso_ms("2040-06-07T08:09:10Z") or other["guest"]["seed"] != 9:
            problems.append(f"{tag}: after the reload ?rtc= / ?seed= not applied: {other['guest']}")
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
                 "failed" in page.inner_text("#snap_status"), 60)
        if page.get_attribute("#snap_status", "data-restored") != row:
            problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
            return
        after = page.evaluate(GUESTIN_STATE_JS)
        r["snapshot_restored"] = after["guest"]
        if after["guest"] != {"start": saved["rtcStart"], "seed": 5, "rng": saved["rng"]}:
            problems.append(f"{tag}: after Restore the guest inputs are {after['guest']}, saved {saved}")
        if after["rtcTime"] != before["rtcTime"]:
            problems.append(f"{tag}: the RTC after Restore reads {after['rtcTime']}, at the save {before['rtcTime']}")
        if "2031-02-03" not in (after["line"] or ""):
            problems.append(f"{tag}: the Machine tab does not show the restored start date: {after['line']!r}")
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / "guestin-snapshot-restored.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
    finally:
        ctx.close()


def guestin_shell_dates(browser, base, shots, name, problems, r):
    """Debian with its initramfs at full speed on ?rtc=GUESTIN_RTC: at the
    (initramfs) prompt `hwclock -u` (the guest reads the CMOS clock) and
    `date -u` (the kernel's time, set from it at boot) read the start date
    plus the virtual time at that moment, and a second hwclock a few seconds
    later has moved as far as the virtual clock."""
    tag = "shell-date"
    ctx = guestin_context(browser, "dark", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10&rtc={GUESTIN_RTC}", timeout=120000)
        if not wait_for(lambda: "(initramfs)" in page.inner_text("#screen_container"), 400, 1):
            problems.append(f"{tag}: no (initramfs) shell")
            return
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        if page.is_visible("#vm_continue"):
            machine_do(page, "click", "#vm_continue")
        page.click("#screen_container")
        time.sleep(0.5)
        start = iso_ms(GUESTIN_RTC) / 1000
        r["shell"] = []

        def read(cmd, rx, fmt):
            """The guest's date printed by cmd, and the virtual clock (s)
            just before Enter and just after the line appeared."""
            n = len(rx.findall(page.inner_text("#screen_container")))
            page.keyboard.type(cmd)
            c0 = page.evaluate("CLOCK.now()") / 1000
            page.keyboard.press("Enter")
            ok = wait_for(lambda: len(rx.findall(page.inner_text("#screen_container"))) > n, 60, 0.05)
            c1 = page.evaluate("CLOCK.now()") / 1000
            if not ok:
                problems.append(f"{tag}: `{cmd}` printed no date: {page.inner_text('#screen_container')[-200:]!r}")
                return None
            text = re.sub(" +", " ", rx.findall(page.inner_text("#screen_container"))[-1])
            got = datetime.datetime.strptime(text, fmt).replace(tzinfo=datetime.timezone.utc).timestamp()
            r["shell"].append({"cmd": cmd, "printed": text, "minus_start": got - start, "clock": [round(c0, 2), round(c1, 2)]})
            # The guest prints whole seconds.
            if not (c0 - 1 <= got - start <= c1 + 1):
                problems.append(f"{tag}: `{cmd}` printed {text!r}: {got - start:.0f} s after {GUESTIN_RTC}, "
                                f"the virtual clock was at {c0:.1f}..{c1:.1f} s")
            return got, c0, c1

        a = read("hwclock -u", GUESTIN_HWCLOCK_RE, "%a %b %d %H:%M:%S %Y")
        read("date -u", GUESTIN_DATE_RE, "%a %b %d %H:%M:%S UTC %Y")
        time.sleep(3)
        b = read("hwclock -u", GUESTIN_HWCLOCK_RE, "%a %b %d %H:%M:%S %Y")
        page.screenshot(path=str(shots / "guestin-shell-date.png"))
        if a and b and not (b[1] - a[2] - 1 <= b[0] - a[0] <= b[2] - a[1] + 1):
            problems.append(f"{tag}: between two hwclock readings the guest's date moved {b[0] - a[0]:.0f} s, "
                            f"the virtual clock {b[1] - a[2]:.1f}..{b[2] - a[1]:.1f} s")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
    finally:
        ctx.close()


def guestin_case(browser, base, shots, entries, debian, initrd):
    problems, r = [], {}
    # hello twice: the same machine at its halt, page 0 included.
    runs = [guestin_hello(browser, base, shots, "&mips=10", scheme, phone, f"hello-{scheme}-{390 if phone else 1280}", problems)
            for scheme, phone in (("light", False), ("dark", True))]
    r["hello"] = [x and {k: x[k] for k in ("ram", "bda", "ticks", "line")} for x in runs]
    if None not in runs:
        a, b = runs
        if a["bda"] != b["bda"]:
            problems.append(f"hello: the BIOS tick count at 0x46C differs: {a['bda']} vs {b['bda']}")
        if a["ram"] != b["ram"]:
            problems.append(f"hello: RAM differs: {a['ram']} vs {b['ram']}")
        if not (bios_ticks(GUESTIN_DEFAULT_RTC) <= a["ticks"] <= bios_ticks(GUESTIN_DEFAULT_RTC) + 20):
            problems.append(f"hello: BIOS tick count {a['ticks']}, not the default start date's {bios_ticks(GUESTIN_DEFAULT_RTC)}")
        for x in runs:
            if not x["line"] or "2026-01-01 00:00:00" not in x["line"] or "seed 1" not in x["line"] or not x["lineShown"]:
                problems.append(f"hello: the Machine tab does not show the default start date and seed: {x['line']!r}")
    # ?rtc= and ?seed=: SeaBIOS reads that time of day from the RTC.
    x = guestin_hello(browser, base, shots, f"&mips=10&rtc={GUESTIN_RTC}&seed=7", "light", False, "hello-rtc", problems)
    if x:
        r["hello_rtc"] = {k: x[k] for k in ("ticks", "line", "guest")}
        want = bios_ticks(GUESTIN_RTC)
        if not (want <= x["ticks"] <= want + 20):
            problems.append(f"hello ?rtc={GUESTIN_RTC}: BIOS tick count {x['ticks']}, want {want} (+ a few)")
        if not x["line"] or "2031-02-03 04:05:06" not in x["line"] or "seed 7" not in x["line"]:
            problems.append(f"hello ?rtc= ?seed=: the Machine tab line {x['line']!r}")
    # Real time keeps the browser's date.
    x = guestin_hello(browser, base, shots, "&mips=real", "dark", True, "hello-realtime", problems)
    if x:
        r["hello_real"] = {"rtc_minus_browser_s": round((x["rtcTime"] - x["browserNow"]) / 1000, 1), "line": x["line"]}
        if abs(x["rtcTime"] - x["browserNow"]) > 120_000:
            problems.append(f"hello on real time: the RTC reads {x['rtcTime']}, the browser {x['browserNow']}")
        if not x["line"] or "browser" not in x["line"]:
            problems.append(f"hello on real time: the Machine tab line {x['line']!r}")
    guestin_snapshot(browser, base, shots, problems, r)
    if debian:
        runs = [guestin_debian_stepped(browser, base, shots, debian[0], scheme, phone, f"debian-{scheme}-{390 if phone else 1280}", problems)
                for scheme, phone in (("light", False), ("dark", True))]
        r["debian"] = [x and {k: x[k] for k in ("steps", "eip", "ram", "clock")} for x in runs]
        if None not in runs:
            a, b = runs
            for k in ("steps", "eip", "ram", "clock"):
                if a[k] != b[k]:
                    problems.append(f"debian {GUESTIN_DEBIAN_STEPS:,} stepped after the entry: {k} differs: {a[k]} vs {b[k]}")
    if initrd:
        guestin_shell_dates(browser, base, shots, initrd[0], problems, r)
    return problems, r


def suite_guestin(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n]]
    return report("guestin", *guestin_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx), initrd))


# ---- exactstop: Debian's end stops at an exact instruction (J3) ------------
# D0 (2026-11-10): the virtual-clock machine is deterministic, yet Debian's
# "---[ end Kernel panic - not syncing: ... ]---" stop and the initramfs
# shell's "(initramfs) " prompt were found by polling the screen every
# 250 ms, so the count at the stop and the RAM there varied by up to ~1 M
# instructions. Now the stepper's VGA hook sees the line complete: stepping
# stops at the instruction that writes the panic line's last character, and
# at the prompt where the CPU first halts after it (the shell waits for
# input). Under J1's JIT the page steps exactly from the slice in which
# "Kernel panic - not syncing" appears, and stops at the prompt at the
# first slice that ends halted after it. Each kernel and mode three times
# (1280 light, 390 dark, 1280 dark): the same counts, EIP and RAM.
EXACTSTOP_RUNS = (("light", False), ("dark", True), ("dark", False))
EXACTSTOP_STATE_JS = """async () => { const st = stepper, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const pick = (s) => s && { steps: s.steps, sinceEntry: s.sinceEntry, unique: s.unique, phys: s.phys, kind: s.kind || null, exact: s.exact === undefined ? null : s.exact };
  return { steps: st.steps, counter: ffCounter(st), eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    chain: st.jitChain === undefined ? null : (st.jitChain >>> 0).toString(16), irq0: st.irq0, clock: CLOCK.now(),
    end: pick(st.end), prompt: pick(st.prompt), runState, hlt: st.halted(),
    banner: document.getElementById('end_banner').textContent, status: document.getElementById('dbg_status').textContent }; }"""


def exactstop_run(browser, base, shots, name, mode, scheme, phone, tag, problems, prompt):
    """One boot of name in mode ("stepped": counted at full stepping speed;
    "jit": J1's full speed) to its stop; the state there."""
    q = "speed=full&count=1&mips=10" if mode == "stepped" else "speed=full&count=0&mips=10"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        t0 = time.monotonic()
        page.goto(f"{base}/index.html?kernel={name}&{q}", timeout=120000)
        want = "!!stepper && !!stepper.prompt" if prompt else "!!stepper && !!stepper.end"
        if not wait_for(lambda: page.evaluate(f"{want} && runState === 'paused'"), 900, 1):
            problems.append(f"{tag}: no {'prompt' if prompt else 'end'} pause in 900 s "
                            f"({page.evaluate('stepper ? stepper.steps : null')} steps, {page.evaluate('runState')})")
            page.screenshot(path=str(shots / f"exactstop-{tag}-timeout.png"))
            return None
        time.sleep(0.5)
        out = page.evaluate(EXACTSTOP_STATE_JS)
        out["seconds"] = round(time.monotonic() - t0)
        show_tab(page, "machine")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"exactstop-{tag}.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def exactstop_kernel(browser, base, shots, name, mode, prompt, problems, r):
    runs = []
    for scheme, phone in EXACTSTOP_RUNS:
        tag = f"{name}-{mode}-{scheme}-{390 if phone else 1280}"
        runs.append(exactstop_run(browser, base, shots, name, mode, scheme, phone, tag, problems, prompt))
    key = "prompt" if prompt else "end"
    r[f"{name}-{mode}"] = [x and {k: x[k] for k in ("seconds", "counter", "eip", "ram", "irq0", key)} for x in runs]
    got = [x for x in runs if x]
    if len(got) < len(runs):
        return
    for x in got:
        if prompt and x["prompt"] is None:
            problems.append(f"{name} {mode}: paused without a prompt row")
        if not prompt and (x["end"] is None or x["end"]["kind"] != "panic-text"):
            problems.append(f"{name} {mode}: the end is {x['end']}, not the panic text")
        if not prompt and x["end"] and x["end"]["exact"] is False:
            problems.append(f"{name} {mode}: the panic text was found only at the end of a JIT slice")
        if prompt and not x["hlt"]:
            problems.append(f"{name} {mode}: stopped at the prompt with the CPU not halted (EIP {x['eip']})")
    fields = ("counter", "steps", "eip", "ram", "irq0", "clock", key) + (("chain",) if mode == "jit" else ())
    for k in fields:
        vals = [x[k] for x in got]
        if any(v != vals[0] for v in vals[1:]):
            problems.append(f"{name} {mode}: {k} at the stop differs over {len(got)} runs: {vals}")


# J2 residual: the count line's rate divided every instruction since reset
# (the fast-forward's JIT part too) by the stepping time only: right after
# the fast-forward to the entry it read "0.00 s of JavaScript stepping time
# (3918091.6 M/s)". The rate is the stepped instructions over that time.
COUNTRATE_RE = re.compile(r"([\d.]+) s of JavaScript stepping time \(([\d.]+) M/s\)")


def countrate_case(browser, base, shots, name, scheme, phone, problems, r):
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    tag = f"{name}-{scheme}-{390 if phone else 1280}"
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1000&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"countrate {tag}: no kernel entry")
            return
        time.sleep(1.5)
        if page.is_enabled("#dbg_pause"):  # hello has ended by now
            machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        time.sleep(0.5)
        text = page.inner_text("#count_text")
        st = page.evaluate("({ steps: stepper.steps, ms: countMs, jit: stepper.jitBeforeEntry })")
        r[f"countrate-{tag}"] = {"text": text, **st}
        page.eval_on_selector("#count_info", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"countrate-{tag}.png"))
        m = COUNTRATE_RE.search(text)
        if not m:
            problems.append(f"countrate {tag}: no stepping time and rate in the count line: {text!r}")
            return
        # JavaScript steps a few million instructions a second at most.
        if float(m.group(2)) > 100:
            problems.append(f"countrate {tag}: {m.group(2)} M/s for {m.group(1)} s of stepping: {text!r}")
    finally:
        ctx.close()


def exactstop_case(browser, base, shots, entries, debian, initrd, modes):
    problems, r = [], {}
    countrate_case(browser, base, shots, "hello", "light", False, problems, r)
    if debian:
        countrate_case(browser, base, shots, debian[0], "dark", True, problems, r)
    for mode in modes:
        if debian:
            exactstop_kernel(browser, base, shots, debian[0], mode, False, problems, r)
        if initrd:
            exactstop_kernel(browser, base, shots, initrd[0], mode, True, problems, r)
    return problems, r


def suite_exactstop(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n]]
    modes = [m for m in os.environ.get("MKB_EXACTSTOP_MODES", "jit,stepped").split(",") if m]
    return report("exactstop", *exactstop_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx), initrd, modes))


# ---- replay: record and replay guest input (J4) -------------------------------
# On the virtual clock (J1, J2, J3) a run repeats exactly until someone types:
# the keyboard reached the PS/2 controller at a moment of wall time. Now a key
# typed on the virtual clock waits for the next boundary between two JIT
# slices or two stepper slices and goes in there, logged with the count and
# the virtual time. A run (boot settings, J2's start date and seed, the log,
# where it ended) is downloadable, saved with snapshots and position
# bookmarks, and Replay boots the same kernel and puts the keys in at the
# same counts: the same count, EIP, RAM and screen at the end.
REPLAY_STATE_JS = """async () => { flushScreen(); const st = stepper, m = st.cpu.mem8;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const I = typeof INPUT === 'object' ? INPUT : null, keys = I ? I.log.filter((e) => e.keys) : [];
  const rs = document.getElementById('run_status');
  return { steps: st.steps, counter: ffCounter(st), eip: (st.cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha.slice(0, 16),
    clock: CLOCK.now(), screen: Array.from(document.getElementById('screen_text').children, (e) => e.textContent.replace(/\\s+$/, '')),
    entries: I ? I.log.length : null, keyEntries: keys.length, scancodes: keys.reduce((n, e) => n + e.keys.length, 0),
    firstKeyAt: keys.length ? keys[0].at : null, modes: I ? I.log.filter((e) => e.mode).map((e) => e.mode) : null,
    pending: I ? I.pending.length : null, taint: I ? I.taint : null, prompt: st.prompt ? st.prompt.steps : null, runState,
    replay: I && I.replay ? { done: I.replay.done, ok: I.replay.ok === undefined ? null : I.replay.ok, late: I.replay.late.length } : null,
    status: rs ? rs.textContent : null, verdict: rs ? rs.dataset.replay || null : null }; }"""
REPLAY_SAME = ("steps", "counter", "eip", "ram", "clock", "screen")


def replay_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, accept_downloads=True,
                               **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def replay_state(page):
    return page.evaluate(REPLAY_STATE_JS)


def replay_wait(page, timeout):
    """Until a replay has finished and the machine is paused there."""
    return wait_for(lambda: page.evaluate("typeof INPUT === 'object' && !!INPUT.replay && INPUT.replay.done && runState === 'paused'"),
                    timeout, 1)


def replay_same(a, b, tag, problems):
    for k in REPLAY_SAME:
        if a[k] != b[k]:
            problems.append(f"{tag}: {k} differs from the recorded run: {str(b[k])[:300]} vs {str(a[k])[:300]}")


def replay_lines(page):
    return [l.strip() for l in page.inner_text("#screen_container").splitlines()]


def replay_fit(page, tag, problems):
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")


def replay_shots(page, shots, stem):
    show_tab(page, "snapshots")
    page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"{stem}-snapshots.png"))
    show_tab(page, "machine")
    page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"{stem}-machine.png"))


def replay_initrd(browser, base, shots, name, tmp, problems, r):
    """Debian+initrd at J1's full speed: keys typed at the (initramfs) prompt
    while paused (held until Continue), a Step there, a command typed while
    it runs; then Replay in the page. Returns (run file, recorded state)."""
    tag = f"replay {name} jit 1280 light"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.evaluate("!!stepper && !!stepper.prompt && runState === 'paused'"), 900, 1):
            problems.append(f"{tag}: no pause at the (initramfs) prompt")
            return None, None
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.3)
        page.keyboard.type("echo j4")
        held = page.evaluate("typeof INPUT === 'object' ? INPUT.pending.length : -1")
        r["held_while_paused"] = held
        if held != 14:
            problems.append(f"{tag}: 7 keys typed while paused: {held} scancodes held for the next slice, want 14")
        # A Step at the prompt: the run goes on stepped for a moment.
        show_tab(page, "debugger")
        page.fill("#dbg_n", "3")
        page.click("#dbg_stepn")
        time.sleep(0.5)
        machine_do(page, "click", "#vm_continue")
        if not wait_for(lambda: any("echo j4" in l for l in replay_lines(page)), 60):
            problems.append(f"{tag}: the keys typed while paused did not reach the shell")
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.3)
        page.keyboard.type(" ok; uname -m")
        page.keyboard.press("Enter")
        if not wait_for(lambda: "j4 ok" in replay_lines(page) and any(l in ("i686", "i586", "i386") for l in replay_lines(page)), 90):
            problems.append(f"{tag}: the typed command did not run: {replay_lines(page)[-6:]}")
        time.sleep(2)
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 10)
        a = replay_state(page)
        r["recorded"] = {k: a[k] for k in ("steps", "counter", "eip", "ram", "entries", "keyEntries", "scancodes", "modes", "firstKeyAt", "prompt")}
        if a["keyEntries"] is None or a["scancodes"] < 2 * len("echo j4 ok; uname -m") + 2:
            problems.append(f"{tag}: the log holds {a['scancodes']} scancodes in {a['keyEntries']} entries, want all typed")
        if a["modes"] != ["jit", "stepped", "jit"]:
            problems.append(f"{tag}: the run's modes are {a['modes']}, want JIT, stepped (Step at the prompt), JIT")
        if a["pending"]:
            problems.append(f"{tag}: {a['pending']} scancodes still held after the run")
        if a["taint"]:
            problems.append(f"{tag}: the run is marked not replayable: {a['taint']}")
        # Download the run.
        show_tab(page, "snapshots")
        path = None
        try:
            with page.expect_download(timeout=30000) as d:
                page.click("#run_dl")
            path = tmp / d.value.suggested_filename
            d.value.save_as(path)
            run = json.loads(path.read_text())
            r["file"] = {"name": path.name, "bytes": path.stat().st_size, "entries": len(run.get("log", []))}
            if run.get("format") != "mkb-run" or run.get("kernel") != name or len(run.get("log", [])) != a["entries"] or \
                    (run.get("end") or {}).get("at") != a["steps"]:
                problems.append(f"{tag}: the downloaded run does not match the page: "
                                f"{ {k: run.get(k) for k in ('format', 'kernel', 'end')} }, {len(run.get('log', []))} entries")
        except (PlaywrightError, ValueError, OSError) as e:
            problems.append(f"{tag}: Download run: {e}")
        # A snapshot keeps the run.
        snap_save(page, 1, False)
        sr = page.evaluate("(() => { const s = snapshots[snapshots.length - 1]; return s.meta && s.meta.run ? "
                           "{ n: s.meta.run.log.length, steps: s.meta.run.end && s.meta.run.end.at } : null; })()")
        r["snapshot_run"] = sr
        if not sr or sr["n"] != a["entries"] or sr["steps"] != a["steps"]:
            problems.append(f"{tag}: the snapshot's run is {sr}, want {a['entries']} entries ending at {a['steps']}")
        if not page.locator("#snapshots button.snapreplay").count():
            problems.append(f"{tag}: the snapshot row has no Replay")
        # Replay here.
        page.click("#run_replay")
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish ({page.evaluate('runState')}, "
                            f"{page.evaluate('stepper && stepper.steps')} steps)")
        b = replay_state(page)
        r["replayed"] = {k: b[k] for k in ("steps", "eip", "ram", "verdict", "replay", "status")}
        replay_same(a, b, tag, problems)
        if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
            problems.append(f"{tag}: the page's replay verdict: {b['verdict']} {b['replay']} {b['status']!r}")
        if not any("j4 ok" == l for l in b["screen"]):
            problems.append(f"{tag}: the replayed screen lacks the command's output")
        replay_shots(page, shots, "replay-initrd-1280-light")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
        return path, a
    finally:
        ctx.close()


def replay_from_file(browser, base, shots, path, a, problems, r):
    """The downloaded run in a fresh page of another kernel (390 px, dark):
    Replay a file switches kernels and ends where the run ended."""
    tag = "replay file 390 dark"
    ctx = replay_context(browser, "dark", True)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&mips=10", timeout=120000)
        wait_for(lambda: page.evaluate("!!stepper && runState !== 'off'"), 60)
        show_tab(page, "snapshots")
        page.set_input_files("#run_file", str(path))
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish: {page.inner_text('#run_status')[:200]!r}")
        c = replay_state(page)
        r["file_replayed"] = {k: c[k] for k in ("steps", "eip", "ram", "verdict", "status")}
        replay_same(a, c, tag, problems)
        if c["verdict"] != "match":
            problems.append(f"{tag}: the page's replay verdict: {c['verdict']} {c['status']!r}")
        replay_shots(page, shots, "replay-file-390-dark")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


def replay_stepped(browser, base, shots, name, problems, r):
    """Stepped at 1k/s (390 px, light): keys at moments of wall time; a
    snapshot, more keys, a position bookmark; the snapshot's Replay ends at
    the snapshot, the bookmark's at the bookmark."""
    tag = f"replay {name} stepped 390 light"
    ctx = replay_context(browser, "light", True)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1000&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no kernel entry")
            return
        show_tab(page, "machine")
        page.tap("#screen_container")
        for keys in ("ab", "1", "cd"):
            time.sleep(0.6)
            page.keyboard.type(keys)
        time.sleep(0.6)
        machine_do(page, "tap", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 10)
        s1 = replay_state(page)
        snap_save(page, 1, True)
        row = snap_rows(page)["rows"][0]["id"]
        # Continue must not rebase the virtual clock (base + count since
        # baseSteps): a rebase rounds the float, and a replay that does not
        # pause there reads another clock (CI 42945681996: 141.214 vs
        # 141.21399999999997 at the bookmark, both inputs "late").
        ab = page.evaluate("async () => { const a = [CLOCK.base, CLOCK.baseSteps, CLOCK.mips]; await resume(true); "
                           "return [a, [CLOCK.base, CLOCK.baseSteps, CLOCK.mips]]; }")
        r["clock_continue"] = ab
        if ab[0] != ab[1]:
            problems.append(f"{tag}: Continue rebased the virtual clock: {ab[0]} -> {ab[1]}")
        for _ in range(3):  # more pauses than the replay has
            time.sleep(0.4)
            machine_do(page, "tap", "#dbg_pause")
            wait_for(lambda: page.evaluate("runState") == "paused", 10)
            machine_do(page, "tap", "#dbg_continue")
        show_tab(page, "machine")
        page.tap("#screen_container")
        for keys in ("x", "yz"):
            time.sleep(0.6)
            page.keyboard.type(keys)
        time.sleep(0.6)
        machine_do(page, "tap", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 10)
        s2 = replay_state(page)
        r["stepped"] = {"s1": {k: s1[k] for k in ("steps", "keyEntries", "modes")}, "s2": {k: s2[k] for k in ("steps", "keyEntries", "modes")}}
        if s1["keyEntries"] < 3 or s2["keyEntries"] <= s1["keyEntries"]:
            problems.append(f"{tag}: key entries {s1['keyEntries']} then {s2['keyEntries']}")
        if s2["modes"] != ["stepped"]:
            problems.append(f"{tag}: modes {s2['modes']}, want stepped only")
        if not bm_open_list(page, True) or not page.locator("#bm_here").count():
            problems.append(f"{tag}: no bookmarks list with the position ☆")
        else:
            bm_tap(page, "#bm_here", True)
            b = bm_dialog_save(page, problems, tag, "pos")
            if b and not (b["t"].get("run") and b["t"]["run"]["end"]["at"] == s2["steps"]):
                problems.append(f"{tag}: the position bookmark has no run ending at {s2['steps']}: {str(b['t'])[:200]}")
            bm_close_menu(page)
        # The snapshot's Replay: boots again and ends at the snapshot.
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.snapreplay", True)
        if not replay_wait(page, 600):
            problems.append(f"{tag}: the snapshot's replay did not finish")
        t1 = replay_state(page)
        replay_same(s1, t1, tag + " (snapshot)", problems)
        if t1["verdict"] != "match":
            problems.append(f"{tag}: snapshot replay verdict {t1['verdict']} {t1['status']!r}")
        replay_shots(page, shots, "replay-stepped-390-light-snapshot")
        # The bookmark's Replay: ends at the bookmark.
        if bm_open_list(page, True) and page.locator("#bm_list .bmreplay").count():
            bm_tap(page, "#bm_list .bmreplay", True)
            bm_close_menu(page)
            if not replay_wait(page, 600):
                problems.append(f"{tag}: the bookmark's replay did not finish")
            t2 = replay_state(page)
            replay_same(s2, t2, tag + " (bookmark)", problems)
            if t2["verdict"] != "match":
                problems.append(f"{tag}: bookmark replay verdict {t2['verdict']} {t2['status']!r}")
            r["stepped"]["replays"] = [{k: t[k] for k in ("steps", "ram", "verdict")} for t in (t1, t2)]
        else:
            problems.append(f"{tag}: the position bookmark has no Replay")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


# R3: Stepper.run(n) whose n ends on the instruction that halts after the
# (initramfs) prompt must stop there with "prompt", as a longer run does. A
# replay clamps its stepping to the next recorded input; when that input is
# due at the prompt's hlt, run() returned "count", the recorded keys went in
# and woke the CPU, and the stepper ran past the prompt (and past the run's
# switch back to the JIT) to the next halt. hello's cli; hlt stands in for
# the shell's idle halt; the prompt's text is marked seen.
REPLAY_EDGE_JS = """async (mode) => {
  const st = stepper;
  if (mode === 'find') {
    for (let i = 0; i < 100 && !st.cpu.in_hlt[0]; i++) st.run(1e6, -1, 2000);
    return { steps: st.steps, hlt: st.cpu.in_hlt[0] };
  }
  st.textSeen |= TEXT_PROMPT;
  st.prompt = null;
  const r = st.run(mode);
  return { r, steps: st.steps, hlt: st.cpu.in_hlt[0], prompt: st.prompt && st.prompt.steps };
}"""


def replay_prompt_edge(browser, base, shots, problems, r):
    tag = "replay prompt at the end of a clamped run (hello 1280 light)"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        def start():
            page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10", timeout=120000)
            return wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && runState === 'paused'"), 120, 0.5)
        if not start():
            problems.append(f"{tag}: did not start paused")
            return
        h = page.evaluate(REPLAY_EDGE_JS, "find")
        if not h["hlt"]:
            problems.append(f"{tag}: hello did not halt (test setup): {h}")
            return
        out = {}
        for n in (1, 2):
            if not start():
                problems.append(f"{tag}: did not start paused again")
                return
            left = h["steps"] - n
            for _ in range(100):
                d = left - page.evaluate("stepper.steps")
                if d <= 0:
                    break
                page.evaluate(f"stepper.run({d}, -1, 2000)")
            if page.evaluate("stepper.steps") != left:
                problems.append(f"{tag}: could not step to {left} (test setup)")
                return
            out[n] = page.evaluate(REPLAY_EDGE_JS, n)
        r["prompt_edge"] = {"hlt_at": h["steps"], "run": out}
        for n, o in out.items():
            if o["r"] != "prompt" or o["prompt"] != h["steps"] or o["steps"] != h["steps"]:
                problems.append(f"{tag}: run({n}) ending on the hlt at {h['steps']:,} returned {o['r']!r} at {o['steps']:,} "
                                f"(prompt {o['prompt']}), want 'prompt' there")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


def replay_refused(browser, base, shots, problems, r):
    """What a replay refuses, and a Step that ends one (the minified Linux at
    1280 light, stepped at 1M/s to its end): a file that is not JSON, a run
    of another format, of format version 0, of a kernel not in the list and
    on a clock the page does not offer each say why and leave the page as
    it was; the run's own replay (at a 20x CPU throttle, so it is still
    stepping when looked at), paused there and stepped, stops following
    its run and says so."""
    tag = "replay refused (minified-linux 1280 light)"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    tmp = pathlib.Path(tempfile.mkdtemp())
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000000&mips=10")
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no kernel entry")
            return
        entry = int(page.get_attribute("#entry_info", "data-steps"))
        # It stops by itself (init exits: a panic) a few seconds later.
        wait_for(lambda: page.evaluate("runState") == "paused", 120)
        run = page.evaluate("runNow()")
        url = page.url

        def refused(name, body, want):
            f = tmp / name
            f.write_text(body)
            show_tab(page, "snapshots")
            page.set_input_files("#run_file", str(f))
            wait_for(lambda: want in page.text_content("#run_status"), 10)
            st = page.text_content("#run_status")
            if want not in st or page.get_attribute("#run_status", "data-replay") != "error" or \
                    page.url.split("#")[0] != url.split("#")[0]:
                problems.append(f"{tag}: {name}: status {st!r} ({page.get_attribute('#run_status', 'data-replay')}), "
                                f"URL {page.url} (want {want!r}, the page as it was)")
            return st[:120]

        r["refused"] = [
            refused("junk.json", "this is no JSON", "Cannot replay junk.json: not JSON"),
            refused("other.json", json.dumps({"format": "something else"}), "Cannot replay: not a run of this page"),
            refused("v0.json", json.dumps({**run, "version": 0}), "Cannot replay: run format version 0"),
            refused("gone.json", json.dumps({**run, "kernel": "gone-kernel"}),
                    "Cannot replay: “gone-kernel” is not one of the listed kernels"),
            refused("clock.json", json.dumps({**run, "clock": "7"}),
                    "Cannot replay: this page has no clock of 7 instructions per microsecond"),
        ]
        # The run itself, slowed down: pause it while it steps after the
        # kernel entry, then Step.
        page.context.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": 20})
        show_tab(page, "snapshots")
        page.click("#run_replay")
        stepping = wait_for(lambda: page.evaluate(f"replayActive() && runState === 'stepped' && stepper.steps > {entry}"), 300, 0.2)
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 30)
        at = page.evaluate("stepper.steps")
        machine_do(page, "click", "#dbg_step")
        wait_for(lambda: "Replay stopped by Step" in page.text_content("#run_status"), 30)
        st = page.text_content("#run_status")
        taint = page.evaluate("INPUT.taint")
        r["stopped by Step"] = [stepping, at, st, page.get_attribute("#run_status", "data-replay"), taint]
        if not stepping or st != f"Replay stopped by Step at instruction {at:,}." or \
                page.get_attribute("#run_status", "data-replay") != "stopped" or \
                not taint or taint.get("why") != "replay stopped by Step" or taint.get("at") != at:
            problems.append(f"{tag}: a Step during the replay: {r['stopped by Step']}")
        page.screenshot(path=str(shots / "replay-1280-light-stopped.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()
        shutil.rmtree(tmp, ignore_errors=True)


def replay_case(browser, base, shots, entries, initrd):
    problems, r = [], {}
    tmp = pathlib.Path(tempfile.mkdtemp())
    def guard(fn, *args):
        try:
            return fn(*args)
        except (PlaywrightError, IndexError, KeyError, TypeError) as e:
            problems.append(f"{fn.__name__}: {type(e).__name__}: {(str(e).splitlines() or [''])[0][:300]}")
            return None
    guard(replay_prompt_edge, browser, base, shots, problems, r)
    if "minified-linux" in entries:
        guard(replay_refused, browser, base, shots, problems, r)
        guard(replay_stepped, browser, base, shots, "minified-linux", problems, r)
    if initrd:
        path, a = guard(replay_initrd, browser, base, shots, initrd[0], tmp, problems, r) or (None, None)
        if path and a:
            guard(replay_from_file, browser, base, shots, path, a, problems, r)
    shutil.rmtree(tmp, ignore_errors=True)
    return problems, r


def suite_replay(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n]]
    return report("replay", *replay_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, initrd))


# ---- reverse: reverse execution in stepped mode (R2) ---------------------------
# While the page steps the CPU on the virtual clock it keeps checkpoints (R1
# deltas in memory); Step back and Back to (reverse continue) restore the last
# one before the target and step forward to it. Checked against the machine
# as the forward run had it at the same counts: registers, EFLAGS, CR, v86's
# counter, clock, IRQ0 count, RAM SHA-256 and the VGA text; stepping forward
# again gives the same state and does not count the recordings (coverage,
# paths, calls) twice; keys typed in the stepped-over stretch go in again at
# their counts; typing or the JIT after stepping back drops what was ahead.
REV_STATE_JS = """async () => { const st = stepper, c = st.cpu, m = c.mem8.slice(0, emuRam);
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  return { steps: st.steps, phys: st.nextPhys(), eip: c.instruction_pointer[0] >>> 0, regs: Array.from(c.reg32, (x) => x >>> 0),
    sregs: Array.from(c.sreg), eflags: c.get_eflags() >>> 0, cr: [c.cr[0] >>> 0, c.cr[2] >>> 0, c.cr[3] >>> 0, c.cr[4] >>> 0],
    icount: c.instruction_counter[0] >>> 0, clock: CLOCK.now(), irq0: st.irq0, hlt: c.in_hlt[0], ram: sha, vga: runScreenHash(st) }; }"""
REV_SAME = ("steps", "phys", "eip", "regs", "sregs", "eflags", "cr", "icount", "clock", "irq0", "hlt", "ram", "vga")
REV_REC_JS = """() => { const st = stepper; let ns = 0; for (const x of st.nodeSteps) ns += x;
  return { unique: st.unique, uniqueKernel: st.uniqueKernel, jumps: st.jumpCount, jumpKeys: st.jumps.size, frames: st.fStart.length,
    nodeSteps: ns, edges: st.edges.size, history: st.history.length, switches: st.swStep.length,
    end: st.end ? st.end.steps : null, milestones: st.milestones.map((m) => m.found ? m.found.steps : null) }; }"""
# One pass over [steps, to) stopping at every function's first instruction:
# (count, address, registers) of each call, the way Back to must find them.
REV_ORACLE_JS = """({ to, hits }) => { const st = stepper, c = st.cpu;
  if (!window.mkbRevSet) window.mkbRevSet = new Set(codeSymbols().map((s) => s.paddr));
  let leave = -1;
  while (st.steps < to) {
    const r = st.run(to - st.steps, window.mkbRevSet, 5000, leave);
    leave = -1;
    if (r === 'addr') { hits.push([st.steps, st.nextPhys(), Array.from(c.reg32, (x) => x >>> 0), c.get_eflags() >>> 0]); leave = st.nextPhys(); }
    else if (r === 'hlt' && st.dead()) break;
  }
  return hits; }"""
REV_CASES = {
    # kernel: (URL params, Run to first, how far past it, how far the deep step back goes).
    # hello and the minified Linux are stepped from the reset vector with a
    # checkpoint every 20,000 instructions (so the 32 kept do not reach
    # back to the start); Debian runs the JIT to startup_32 and is stepped
    # from there with the page's own interval (half a second of stepping).
    "hello": ("fastforward=0&revk=20000", "0x100000", 400, 300000),
    "minified-linux": ("fastforward=0&revk=20000", "0x100000", 300000, 250000),
    "debian-6.1-686": ("", "startup_32", 8000000, 5000000),
}


def rev_state(page):
    return page.evaluate(REV_STATE_JS)


def rev_diff(got, want):
    return {k: (str(got.get(k))[:70], str(want.get(k))[:70]) for k in REV_SAME if got.get(k) != want.get(k)}


def rev_click(page, sel, phone, timeout=120):
    """Click a reverse button; wait for its result. (wall ms, the page's ms, status, state)."""
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


def rev_step_to(page, target):
    """Step forward with the Debugger's Step (doStep) until stepper.steps == target."""
    for _ in range(4000):
        left = target - page.evaluate("stepper.steps")
        if left <= 0:
            break
        page.evaluate(f"doStep({min(left, 2000000)})")
    return page.evaluate("stepper.steps")


def rev_nav(page):
    return page.evaluate("[navList.length, navAt]")


def reverse_kernel(browser, base, shots, name, scheme, phone):
    """Forward from the kernel entry, noting the machine at E-deep, E-1000,
    E-1 and E (and every call in the last 20,000 instructions); then Step
    back 1 (the run bar), Step back 999 and Step back to E-deep (the
    Debugger), each the machine as it was there, 1 and 1,000 within a
    second; forward to E again: the same machine, the recordings not
    counted twice; Back to the most called function twice: its last two
    calls; the history keeps the last 32 checkpoints."""
    params, first, past, deep = REV_CASES[name]
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"reverse {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def shot(s):
        page.screenshot(path=str(shots / f"reverse-{name}-{w}-{scheme}-{s}.png"), full_page=False)

    def start():
        page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10&{params}#debugger", timeout=120000)
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator && !!symbols"), 60)
        show_tab(page, "debugger")
        return ok

    try:
        if not start():
            return [f"{tag}: did not start paused"], r
        page.fill("#dbg_addr", first)
        page.click("#dbg_runto")
        want_at = int(first, 16) if first.startswith("0x") else page.evaluate(f"symbols.symbols.find((s) => s.name === '{first}' && s.paddr >= 0x100000).paddr")
        ok, at, status = run_to_wait(page, {want_at}, 600)
        if not ok:
            return [f"{tag}: Run to {first} stopped at {at:#x}: {status[:200]}"], r
        s0 = page.evaluate("stepper.steps")
        e = s0 + past
        if e - max(deep, 20000) < s0:
            # Stepped from the reset vector (fastforward=0): the same run
            # again, from the start, to note the machine before the entry.
            page.goto("about:blank")
            if not start():
                return [f"{tag}: did not start paused again"], r
        t0 = time.monotonic()
        if rev_step_to(page, e - deep) != e - deep:
            problems.append(f"{tag}: could not step to {e - deep} (test setup)")
        p_deep = rev_state(page)
        rev_step_to(page, e - 20000)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e - 1000, "hits": []})
        p1000 = rev_state(page)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e - 1, "hits": hits})
        p1 = rev_state(page)
        hits = page.evaluate(REV_ORACLE_JS, {"to": e, "hits": hits})
        pe = rev_state(page)
        rec_e = page.evaluate(REV_REC_JS)
        page.evaluate("afterStop()")  # the views at E (the passes above ran the stepper directly)
        r["forward"] = {"entry": s0, "E": e, "seconds": round(time.monotonic() - t0, 1), "calls in the last 20,000": len(hits)}
        if pe["steps"] != e:
            problems.append(f"{tag}: the forward run ended at {pe['steps']}, want {e} (halted early?)")
        cps = page.evaluate("[REV.cps.length, REV.cps.length ? REV.cps[0].steps : -1, REV.kFixed || REV.k]")
        r["history"] = {"checkpoints": cps[0], "from": cps[1], "k": cps[2], "info": page.inner_text("#rev_info")[:200]}
        if cps[0] < 2 or cps[0] > 32:
            problems.append(f"{tag}: {cps[0]} checkpoints kept (want 2 to 32)")
        if cps[1] > e - deep:
            problems.append(f"{tag}: the history starts at {cps[1]}, after E-deep {e - deep}")
        nav0 = rev_nav(page)
        shot("forward")

        # Step back 1 from the run bar (Machine tab).
        show_tab(page, "machine")
        wall, ms, text, st = rev_click(page, "#dbg_back", phone)
        got = rev_state(page)
        r["back 1"] = {"wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p1):
            problems.append(f"{tag}: Step back 1: {st} {text[:160]!r}; differs from E-1: {rev_diff(got, p1)}")
        if wall > 1000:
            problems.append(f"{tag}: Step back 1 took {wall} ms (want <= 1000)")
        if page.evaluate(REV_REC_JS) != rec_e:
            problems.append(f"{tag}: the recordings changed by stepping back: {page.evaluate(REV_REC_JS)} vs {rec_e}")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        shot("back1-machine")

        # Step back 999 more (the Debugger's Step back with N).
        show_tab(page, "debugger")
        page.fill("#dbg_n", "999")
        wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
        got = rev_state(page)
        r["back 1000"] = {"wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p1000):
            problems.append(f"{tag}: Step back 999 more: {st} {text[:160]!r}; differs from E-1000: {rev_diff(got, p1000)}")
        if wall > 1000:
            problems.append(f"{tag}: Step back 1,000 took {wall} ms (want <= 1000)")
        info = page.inner_text("#rev_info")
        if "stepped back" not in info:
            problems.append(f"{tag}: the reverse line does not say it is stepped back: {info[:200]!r}")
        prev = page.inner_text("#prev_sym") if page.locator("#prev_sym").count() else ""
        if "stepped back" not in prev:
            problems.append(f"{tag}: 'previous' while stepped back: {prev[:120]!r}")
        page.evaluate("window.scrollTo(0, 0)")
        shot("back1000-debugger")

        # Deep: across checkpoints.
        page.fill("#dbg_n", str(deep - 1000))
        wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
        got = rev_state(page)
        r["back deep"] = {"n": deep, "wall ms": wall, "page ms": ms, "status": text[:160]}
        if st != "done" or rev_diff(got, p_deep):
            problems.append(f"{tag}: Step back to E-{deep}: {st} {text[:160]!r}; differs: {rev_diff(got, p_deep)}")
        # The Calls timeline while stepped back: drawn to the furthest count, now marked.
        show_tab(page, "calls")
        time.sleep(1)
        cv = page.evaluate("(() => { const c = document.querySelector('#panel_calls canvas'); return c ? { now: c.dataset.nowMarker, drawn: c.dataset.drawn } : null; })()")
        r["calls while back"] = cv
        shot("back-deep-calls")

        # Forward to E again: the same machine, the recordings not counted twice.
        show_tab(page, "debugger")
        page.fill("#dbg_n", str(deep))
        page.click("#dbg_stepn")
        rev_step_to(page, e)
        got = rev_state(page)
        if rev_diff(got, pe):
            problems.append(f"{tag}: forward to E again differs: {rev_diff(got, pe)}")
        rec = page.evaluate(REV_REC_JS)
        if rec != rec_e:
            problems.append(f"{tag}: the recordings at E after stepping back and forward: {rec} vs {rec_e}")

        # Back to (reverse continue) the function called most in the last 20,000.
        calls = {}
        for h in hits:
            calls.setdefault(h[1], []).append(h)
        if calls:
            target, hl = max(calls.items(), key=lambda kv: (len(kv[1]), kv[1][-1][0]))
            r["back to"] = {"target": hex(target), "calls": len(hl)}
            page.fill("#dbg_addr", hex(target))
            for k in range(min(2, len(hl))):
                want = hl[-1 - k]
                wall, ms, text, st = rev_click(page, "#dbg_backto", phone)
                got = rev_state(page)
                r[f"back to {k + 1}"] = {"wall ms": wall, "page ms": ms, "steps": got["steps"], "want": want[0], "status": text[:200]}
                if st != "done" or got["steps"] != want[0] or got["phys"] != target or got["regs"] != want[2] or got["eflags"] != want[3]:
                    problems.append(f"{tag}: Back to {hex(target)} #{k + 1}: {st} at {got['steps']} {got['phys']:#x} "
                                    f"(want {want[0]}); {text[:200]!r}")
        else:
            problems.append(f"{tag}: no call in the last 20,000 instructions (test setup)")
        page.eval_on_selector("#dbg_status", "e => e.scrollIntoView()")
        shot("backto")
        # For comparison: a Step forward of 1 with its redrawing, as Step back has it.
        page.fill("#dbg_n", "1")
        t1 = time.monotonic()
        page.click("#dbg_stepn")
        wait_for(lambda: page.inner_text("#dbg_status").startswith("Stepped 1 "), 30, 0.05)
        r["step 1 wall ms"] = round((time.monotonic() - t1) * 1000)
        if rev_nav(page) != nav0:
            problems.append(f"{tag}: stepping back added jump history entries: {nav0} -> {rev_nav(page)}")
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")

        # Keys typed after stepping back: another way from here.
        if name != "debian-6.1-686":
            page.fill("#dbg_n", "50")
            rev_click(page, "#dbg_backn", phone)
            now = page.evaluate("stepper.steps")
            show_tab(page, "machine")
            page.click("#screen_container")
            page.keyboard.press("a")
            show_tab(page, "debugger")
            page.fill("#dbg_n", "1")
            page.click("#dbg_stepn")
            time.sleep(0.5)
            b = page.evaluate("[stepper.behind(), stepper.future.length, INPUT.redo.length, REV.cps.every((c) => c.steps <= %d),"
                              " stepper.fStart.every((s) => s <= stepper.steps), INPUT.log.filter((e) => e.keys).length]" % now)
            r["branch"] = b
            if b[0] or b[1] or b[2] or not b[3] or not b[4] or not b[5]:
                problems.append(f"{tag}: after a key typed while stepped back: behind {b[0]}, future {b[1]}, redo {b[2]}, "
                                f"checkpoints ahead dropped {b[3]}, calls ahead dropped {b[4]}, keys logged {b[5]}")
            s1 = page.evaluate("stepper.steps")
            page.fill("#dbg_n", "5")
            wall, ms, text, st = rev_click(page, "#dbg_backn", phone)
            if st != "done" or page.evaluate("stepper.steps") != s1 - 5:
                problems.append(f"{tag}: Step back after the new way: {st} {text[:160]!r}")
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


def reverse_jit(browser, base, shots):
    """The JIT after stepping back (hello at full speed past its entry):
    what was ahead is dropped (another way from there), and the history goes
    on with the JIT's checkpoints (R5: before it, the JIT dropped it all)."""
    problems, r = [], {}
    tag = "reverse jit hello 1280 dark"
    ctx = browser.new_context(color_scheme="dark", viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10&speed=full&count=0&revk=50#debugger", timeout=120000)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 60)
        page.fill("#dbg_addr", "0x100000")
        page.click("#dbg_runto")
        run_to_wait(page, {0x100000}, 300)
        rev_step_to(page, page.evaluate("stepper.steps") + 200)
        page.fill("#dbg_n", "100")
        rev_click(page, "#dbg_backn", False)
        r["back"] = page.evaluate("[stepper.steps, stepper.ahead, REV.cps.length]")
        machine_do(page, "select_option", "#dbg_speed", "full")
        machine_do(page, "click", "#dbg_continue")
        wait_for(lambda: page.evaluate("runState") == "paused" or page.evaluate("!!stepper.end"), 60)
        time.sleep(1)
        b = page.evaluate("""[stepper.behind(), REV.cps.filter((c) => c.jit).length, document.getElementById('dbg_back').disabled,
          document.getElementById('rev_info').textContent, REV.cps.filter((c) => !c.jit).every((c) => c.steps <= REV.cps.find((x) => x.jit).steps)]""")
        r["after jit"] = b
        if b[0] or not b[1] or b[2] or "at JIT speed" not in b[3] or not b[4]:
            problems.append(f"{tag}: after the JIT: behind {b[0]}, {b[1]} JIT checkpoints, Step back disabled {b[2]}, "
                            f"stepped ones before the branch {b[4]}, {b[3][:160]!r}")
        page.screenshot(path=str(shots / "reverse-jit-hello-1280-dark.png"))
    finally:
        ctx.close()
    return problems, r


def reverse_keys(browser, base, shots, name):
    """Debian+initrd at its (initramfs) prompt (J1): stepped, "echo r2" typed
    and run; Step back to before the keys: the keys wait to go in again,
    the screen is as before them; forward again: the same machine and log
    at the count where "r2" appeared."""
    problems, r = [], {}
    tag = f"reverse keys {name} 390 light"
    ctx = browser.new_context(color_scheme="light", **PHONE)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    rows = "(() => { const t = stepper.screenText(VGA_WINDOW_BYTES); const out = []; for (let i = 0; i < t.length; i += 80) out.push(t.slice(i, i + 80).trim()); return out; })()"
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.evaluate("!!stepper && !!stepper.prompt && runState === 'paused'"), 900, 1):
            return [f"{tag}: no pause at the (initramfs) prompt"], r
        show_tab(page, "debugger")
        page.fill("#dbg_n", "100000")
        page.tap("#dbg_stepn")
        time.sleep(0.5)
        s0 = page.evaluate("[stepper.steps, REV.cps.length ? REV.cps[0].steps : -1]")
        show_tab(page, "machine")
        page.tap("#screen_container", position={"x": 4, "y": 4})  # not the overlay's Continue
        page.keyboard.type("echo r2")
        page.keyboard.press("Enter")
        held = page.evaluate("[runState, INPUT.pending.length]")
        if held != ["paused", 16]:
            problems.append(f"{tag}: typing at the prompt: {held} (want paused, 16 scancodes held)")
        show_tab(page, "debugger")
        e = None
        for _ in range(600):
            page.evaluate("doStep(200000)")
            if "r2" in page.evaluate(rows):
                e = page.evaluate("stepper.steps")
                break
        if e is None:
            return [f"{tag}: the shell did not print r2 while stepping ({page.evaluate('stepper.steps')} steps)"], r
        pe = rev_state(page)
        log_e = page.evaluate("JSON.stringify(INPUT.log)")
        key_at = page.evaluate("INPUT.log.filter((x) => x.keys)[0].at")
        start = page.evaluate("REV.cps[0].steps")
        r["run"] = {"history from": start, "keys at": key_at, "r2 at": e, "checkpoints": page.evaluate("REV.cps.length")}
        if start > key_at:
            problems.append(f"{tag}: the history starts at {start}, after the keys at {key_at}")
        page.fill("#dbg_n", str(e - start))
        wall, ms, text, st = rev_click(page, "#dbg_backn", True, 300)
        r["back"] = {"wall ms": wall, "page ms": ms, "status": text[:200]}
        b = page.evaluate(f"[stepper.steps, INPUT.redo.filter((x) => x.keys).length, {rows}.includes('r2')]")
        if st != "done" or b[0] != start or not b[1] or b[2]:
            problems.append(f"{tag}: back to {start}: {st} at {b[0]}, {b[1]} key entries waiting, r2 on the screen {b[2]}; {text[:160]!r}")
        show_tab(page, "machine")
        page.evaluate("flushScreen()")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"reverse-keys-{name}-390-back.png"))
        show_tab(page, "debugger")
        # R5: the history starts where the JIT ran (its checkpoints): one
        # Step goes forward on the recorded run from the checkpoint before e.
        page.evaluate(f"doStep({e} - stepper.steps)")
        rev_step_to(page, e)
        got = rev_state(page)
        if rev_diff(got, pe):
            problems.append(f"{tag}: forward to {e} again differs: {rev_diff(got, pe)}")
        # R4: the log also holds the resets of v86's hidden JIT state the
        # restores did, at checkpoints' counts (a replay resets there too;
        # R5: a JIT checkpoint's own reset is there already); the rest is the
        # recorded log.
        log_g = json.loads(page.evaluate("JSON.stringify(INPUT.log)"))
        new_resets = [x["at"] for x in log_g if x.get("reset")][len([x for x in json.loads(log_e) if x.get("reset")]):]
        if [x for x in log_g if not x.get("reset")] != [x for x in json.loads(log_e) if not x.get("reset")] or \
                page.evaluate("INPUT.redo.length"):
            problems.append(f"{tag}: the input log after going forward again differs from the recorded one")
        cps = page.evaluate("REV.cps.map((c) => c.steps)")
        if any(x not in cps for x in new_resets):
            problems.append(f"{tag}: the step back logged the JIT resets {new_resets}, not all at checkpoints {cps}")
        show_tab(page, "machine")
        page.evaluate("flushScreen()")
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        page.screenshot(path=str(shots / f"reverse-keys-{name}-390-again.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()
    return problems, r


def reverse_case(browser, base, shots, entries):
    out = []
    runs = [("hello", "dark", True), ("hello", "light", False)]
    if "minified-linux" in entries:
        runs += [("minified-linux", "light", False), ("minified-linux", "dark", True)]
    if "debian-6.1-686" in entries:
        runs += [("debian-6.1-686", "dark", False)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(reverse_kernel, browser, base, shots, name, scheme, phone)
        out.append(report("reverse", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(reverse_jit, browser, base, shots)
    out.append(report("reverse jit", problems, r))
    if "debian-6.1-686-initrd" in entries:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(reverse_keys, browser, base, shots, "debian-6.1-686-initrd")
        out.append(report("reverse keys", problems, r, seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_reverse(ctx):
    only = os.environ.get("MKB_REVERSE_ONLY")
    entries = {k: v for k, v in ctx.entries.items() if not only or k in only.split(",")}
    return reverse_case(ctx.browser, ctx.base, ctx.shots, entries)



# ---- jitrev: reverse execution at JIT speed (R5) ----------------------------------
# While the JIT runs on the virtual clock the page keeps checkpoints too (a
# v86 snapshot and the reset of v86's hidden state, logged with the run, at
# the start of every JIT stretch and every second or so of JIT running).
# Step back in a JIT stretch restores the last checkpoint before the target
# and runs the JIT again to the target itself: v86-det.patch's instruction
# budget stops compiled code before a basic block that would cross it and
# interprets the rest with the counter compiled code reads the clock with.
# Checked against the run itself: Step forward from there again to the pause
# (the live state there, registers, RAM and clock), and a Replay from boot to
# the count stepped back to (J4, no restore of any checkpoint on the way)
# ends with the same EIP, RAM, screen, clock and registers. Debian (kernel
# code), Debian+initrd in initramfs userspace (R3: 0 of 150 JIT slices
# reproduced by the stepper there) and at the (initramfs) prompt (the last
# stretch stepped by the exact stop).
JITREV_RUNS = [  # kernel, scheme, phone, pause past this count (None: the prompt), ?revjitk=
    ("debian-6.1-686", "light", False, 2.5e9, 40000000),
    ("debian-6.1-686-initrd", "dark", True, 3.2e9, None),  # the initramfs scripts
    ("debian-6.1-686-initrd", "light", False, None, None),
]
JITREV_REGS_JS = """() => { const c = stepper.cpu; return { regs: Array.from(c.reg32, (x) => x >>> 0), sregs: Array.from(c.sreg),
  eflags: c.get_eflags() >>> 0, cr: [c.cr[0] >>> 0, c.cr[2] >>> 0, c.cr[3] >>> 0, c.cr[4] >>> 0] }; }"""
JITREV_LATENCY_MS = 10000
JITREV_STI_NOTE = "the instruction after an sti runs with it"


def jitrev_back(page, phone, sel, n, tag, problems, r, key):
    if n is not None:
        page.fill("#dbg_n", str(n))
    wall, ms, text, st = rev_click(page, sel, phone, 300)
    r[key] = {"wall_ms": wall, "page_ms": ms, "status": text[:300]}
    if st != "done":
        problems.append(f"{tag}: {key}: {st}: {text[:300]!r}")
    elif wall > JITREV_LATENCY_MS:
        problems.append(f"{tag}: {key} took {wall} ms")
    return page.evaluate(REV_STATE_JS)


def jitrev_replay(page, run, want, regs, tag, problems, r, key, ui):
    """Replay run (ending at the count stepped back to) from boot: the page's
    verdict, and the registers there."""
    page.evaluate("() => { INPUT.replay = null; delete document.getElementById('run_status').dataset.replay; }")
    if ui:
        show_tab(page, "snapshots")
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        page.click("#run_replay")
    else:
        page.evaluate("(run) => { replayRun(run); }", run)
    wait_for(lambda: page.evaluate("!!INPUT.replay || document.getElementById('run_status').dataset.replay === 'error'"), 60)
    if page.evaluate("document.getElementById('run_status').dataset.replay") == "error":
        problems.append(f"{tag}: {key}: {page.inner_text('#run_status')[:300]!r}")
        return
    if not replay_wait(page, 900):
        problems.append(f"{tag}: {key}: the replay did not finish ({page.evaluate('[runState, stepper && stepper.steps]')})")
        return
    b = replay_state(page)
    got = page.evaluate(JITREV_REGS_JS)
    r[key] = {"steps": b["steps"], "verdict": b["verdict"], "status": b["status"][:300], "late": (b["replay"] or {}).get("late")}
    if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
        problems.append(f"{tag}: {key}: the replay from boot: {b['verdict']} {b['replay']} {b['status'][:300]!r}")
    if b["steps"] != want["steps"]:
        problems.append(f"{tag}: {key}: the replay ended at {b['steps']:,}, stepped back to {want['steps']:,}")
    for k in ("regs", "sregs", "eflags", "cr"):
        if got[k] != regs[k]:
            problems.append(f"{tag}: {key}: {k} after the replay {got[k]}, stepped back {regs[k]}")


def jitrev_run(browser, base, shots, name, scheme, phone, past, k):
    problems, r = [], {}
    w = 390 if phone else 1280
    tag = f"jitrev {name} {w} {scheme} {'prompt' if past is None else f'{past:.2e}'}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10" + (f"&revjitk={k}" if k else ""), timeout=180000)
        if past is None:
            if not wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!stepper.prompt && runState === 'paused'"), 900, 1):
                return [f"{tag}: no pause at the (initramfs) prompt"], r
        else:
            if not wait_for(lambda: page.evaluate(f"typeof stepper !== 'undefined' && !!stepper && stepper.steps > {past} && runState === 'jit'"), 900, 0.2):
                return [f"{tag}: the JIT did not pass {past:.2e}"], r
            machine_do(page, "tap" if phone else "click", "#dbg_pause")
            wait_for(lambda: page.evaluate("runState") == "paused", 30)
        show_tab(page, "debugger")
        time.sleep(0.5)
        sn = page.evaluate(REV_STATE_JS)
        N = sn["steps"]
        r["paused_at"] = N
        r["history"] = page.evaluate("""() => ({ text: document.getElementById('rev_info').textContent,
          cps: typeof REV === 'object' ? REV.cps.map((c) => [c.steps, !!c.jit]) : null,
          segs: typeof REV === 'object' && REV.segs ? REV.segs.map((s) => [s.at, s.kind]) : null,
          back: !document.getElementById('dbg_back').disabled })""")
        if not r["history"]["back"]:
            problems.append(f"{tag}: Step back is disabled after the JIT ran: {r['history']['text'][:200]!r}")
            return problems, r
        show_tab(page, "machine")
        s1 = jitrev_back(page, phone, "#dbg_back", None, tag, problems, r, "back 1")
        # R6: v86 runs the instruction after an sti in the sti's step, so
        # from the hlt of "sti; hlt" (the initramfs prompt's halt) Step back
        # 1 lands on the sti, 2 back, and says why (it said "Stepped back 0
        # instructions" and stayed at the hlt).
        e1 = N - 2 if JITREV_STI_NOTE in r["back 1"]["status"] else N - 1
        r["back 1 expected"] = e1
        if s1["steps"] != e1:
            problems.append(f"{tag}: Step back 1 from {N:,} landed at {s1['steps']:,}, not {e1:,}")
        if e1 == N - 2:
            op = page.evaluate("emulator.read_memory(stepper.nextPhys(), 1)[0]")
            r["back 1 opcode"] = op
            if op != 0xFB:
                problems.append(f"{tag}: Step back 1 says the target is inside an sti's step, but the instruction there is {op:#x}")
        run1 = page.evaluate("runNow()")
        regs1 = page.evaluate(JITREV_REGS_JS)
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-back1.png"))
        show_tab(page, "debugger")
        s1000 = jitrev_back(page, phone, "#dbg_backn", 999, tag, problems, r, "back 1000")
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-back1000.png"))
        e1000 = e1 - 999 - (1 if JITREV_STI_NOTE in r["back 1000"]["status"] else 0)
        if s1000["steps"] != e1000:
            problems.append(f"{tag}: Step back 999 more from {e1:,} landed at {s1000['steps']:,}, not {e1000:,}")
        run1000 = page.evaluate("runNow()")
        regs1000 = page.evaluate(JITREV_REGS_JS)
        # Forward again on the run: at N - 1 and at the pause, as they were.
        page.fill("#dbg_n", str(e1 - e1000))
        t0 = time.monotonic()
        page.locator("#dbg_stepn").tap() if phone else page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("stepper.steps") == e1 and page.evaluate("!REV.busy && runState === 'paused'"), 300, 0.1)
        r["forward 999 ms"] = round((time.monotonic() - t0) * 1000)
        f1 = page.evaluate(REV_STATE_JS)
        for k in REV_SAME:
            if f1[k] != s1[k]:
                problems.append(f"{tag}: forward to {e1:,} again: {k} {str(f1[k])[:120]} vs stepped back {str(s1[k])[:120]}")
        page.evaluate("doStep(1)")
        wait_for(lambda: page.evaluate("stepper.steps") == N and page.evaluate("!REV.busy && runState === 'paused'"), 300, 0.1)
        fn = page.evaluate(REV_STATE_JS)
        for k in REV_SAME:
            if fn[k] != sn[k]:
                problems.append(f"{tag}: forward to the pause {N:,} again: {k} {str(fn[k])[:120]} vs the run {str(sn[k])[:120]}")
        # Where the windows are: user mode (CS & 3) in the last 1,000 of them.
        r["user mode"] = [s1000["sregs"][1] & 3, s1["sregs"][1] & 3]
        r["states"] = {"N": {k: sn[k] for k in ("steps", "eip", "ram", "clock")}, "N-1": {k: s1[k] for k in ("steps", "eip", "ram", "clock")},
                       "N-1000": {k: s1000[k] for k in ("steps", "eip", "ram", "clock")}}
        # Back to looks in the stretches it can watch per instruction: one
        # the JIT ran is crossed, and the status says so.
        page.fill("#dbg_addr", "schedule")
        wall, ms, text, st = rev_click(page, "#dbg_backto", phone, 300)
        r["back to"] = {"wall_ms": wall, "status": text[:300], "state": st}
        if st != "done" or "JIT" not in text:
            problems.append(f"{tag}: Back to schedule over a JIT stretch: {st} {text[:300]!r}")
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
        # Forward on the run again to N - 1 (from the history's start, where
        # Back to stopped), and Replay that run from boot there.
        page.evaluate(f"doStep({e1} - stepper.steps)")
        wait_for(lambda: page.evaluate("stepper.steps") == e1 and page.evaluate("!REV.busy && runState === 'paused'"), 300, 0.1)
        g1 = page.evaluate(REV_STATE_JS)
        r["forward to N-1 again"] = page.inner_text("#dbg_status")[:300]
        for k_ in REV_SAME:
            if g1[k_] != s1[k_]:
                problems.append(f"{tag}: forward from the history's start to {e1:,}: {k_} {str(g1[k_])[:120]} vs stepped back {str(s1[k_])[:120]}")
        jitrev_replay(page, run1, s1, regs1, tag, problems, r, "replay to N-1", True)
        page.screenshot(path=str(shots / f"jitrev-{name}-{w}-{scheme}-replayed.png"))
        jitrev_replay(page, run1000, s1000, regs1000, tag, problems, r, "replay to N-1000", False)
    finally:
        ctx.close()
    return problems, r


def jitrev_case(browser, base, shots, entries):
    out = []
    only = os.environ.get("MKB_JITREV_ONLY")
    for i, (name, scheme, phone, past, k) in enumerate(JITREV_RUNS):
        if name not in entries or (only and str(i) not in only.split(",")):
            continue
        t0 = time.monotonic()
        problems, r = snapdiff_guard(jitrev_run, browser, base, shots, name, scheme, phone, past, k)
        out.append(report("jitrev", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_jitrev(ctx):
    return jitrev_case(ctx.browser, ctx.base, ctx.shots, ctx.entries)


# ---- jitsnap: a snapshot repeats the JIT run (R3, R4) ------------------------------
# v86's JIT keeps state that save_state() does not (compiled code, hotness,
# hash maps whose iteration order decided what one compiled function holds,
# the TLB, the TSC's extrapolation), and that state decides where main_loop's
# slices end and so where timer interrupts land. The page's v86.wasm is built
# with site/v86-det.patch (site/gen-v86.sh): fixed-key hash maps, set_tsc from
# the clock, and jit_reset_hidden(), which the page calls after every restore
# and after every snapshot it saves, logging it with the run (J4). From a
# snapshot the JIT then runs as it did from that point the first time: X1 the
# original run on, X2 restored in the same page, X3 in a reloaded page (from
# IndexedDB), X4 a fresh page that boots to the point itself, saves and
# restores. Every slice end (count, EIP, registers, IRQ0) equal (R3's
# r3replay). And the run is no longer tainted by "the JIT after a snapshot
# restore": Restore at a pause under the JIT, the (initramfs) prompt comes at
# the same count, and Replay from boot matches.
JITSNAP_SHA256 = "c4e8e92eab6da3d11c7f62e17230307ac260818030fb83ac0160a22fa8395257"
# kernel name filter -> (A, B): save at the first slice end at or past A, run to B.
JITSNAP_WINDOWS = [("debian", False, 1.0e9, 2.55e9), ("debian", True, 2.5e9, 3.3e9)]
JITSNAP_DRV_JS = """() => { window.JS4 = {
  async run(cap, rec) {
    const st = stepper, cpu = st.cpu;
    const ch = new MessageChannel();
    const y = () => new Promise((r) => { ch.port1.onmessage = () => r(); ch.port2.postMessage(0); });
    const t0 = performance.timeOrigin, w0 = Date.now();
    let n = 0;
    const irq = st.irq0 || 0; // the page's count, not part of the machine: from the run's start
    if (rec) this.trace = [];
    while (st.steps < cap && !st.dead()) {
      await jitStep(st);
      n++;
      if (rec) {
        let h = 0x811c9dc5;
        for (const x of cpu.reg32) h = Math.imul(h ^ x, 0x01000193);
        h = Math.imul(h ^ cpu.get_eflags(), 0x01000193);
        this.trace.push([st.steps, cpu.instruction_pointer[0] >>> 0, h >>> 0, (st.irq0 || 0) - irq]);
      }
      if (n % 8 === 0) await y();
    }
    await jitFlush(st);
    return { steps: st.steps, slices: n, ms: Date.now() - w0 };
  },
  async state() {
    const cpu = stepper.cpu, m = cpu.mem8;
    const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m.slice()))).map((b) => b.toString(16).padStart(2, '0')).join('').slice(0, 16);
    return { steps: stepper.steps, icount: cpu.instruction_counter[0] >>> 0, eip: (cpu.instruction_pointer[0] >>> 0).toString(16), ram: sha,
      regs: Array.from(cpu.reg32).map((x) => (x >>> 0).toString(16)).join(','), clock: CLOCK.now(), taint: INPUT.taint };
  },
  // The guest's TSC against the virtual clock (offset 0 from power-on): read twice at the same count.
  tsc() {
    const c = stepper.cpu, rd = () => { c.store_current_tsc(); return c.current_tsc[0] + c.current_tsc[1] * 2 ** 32 - Math.round(CLOCK.now() * 1e6); };
    return [rd(), rd()];
  },
  async save() {
    const n = snapshots.length;
    await saveSnapshot();
    if (snapshots.length !== n + 1) throw new Error('no snapshot: ' + document.getElementById('snap_status').textContent);
    const s = snapshots[snapshots.length - 1];
    return { id: s.id, steps: s.meta.steps, status: document.getElementById('snap_status').textContent };
  },
  async restore(id) {
    const s = snapById(id);
    if (!s) throw new Error('no snapshot ' + id);
    await restoreSnapshot(s);
    if (document.getElementById('snap_status').dataset.restored !== id) throw new Error(document.getElementById('snap_status').textContent);
    return stepper.steps;
  },
}; }"""


def jitsnap_first_diff(t1, t2):
    for i, (x, y) in enumerate(zip(t1, t2)):
        if x != y:
            return i, x, y
    if len(t1) != len(t2):
        return min(len(t1), len(t2)), None, None
    return None


def jitsnap_window(browser, base, shots, name, a, b, problems, r):
    """X1-X4 over [a, b] on the det JIT, from the page's own Save and Restore."""
    tag = f"jitsnap {name} {a:.2e} -> {b:.2e}"
    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    url = f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips=10"
    out = {}
    try:
        def fresh():
            page.goto(url, timeout=180000)
            if not wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && runState === 'paused' && emuReady"), 300, 0.5):
                raise PlaywrightError("did not start paused")
            page.evaluate(JITSNAP_DRV_JS)

        def run(cap, rec=True):
            x = page.evaluate("([c, rec]) => JS4.run(c, rec)", [cap, rec])
            x["state"] = page.evaluate("() => JS4.state()")
            if rec:
                x["trace"] = page.evaluate("() => JS4.trace")
            return x

        fresh()
        run(a, False)
        sv = page.evaluate("() => JS4.save()")
        out["save"] = sv
        out["X1"] = run(b)
        tscs = {"X1 end": page.evaluate("() => JS4.tsc()")}
        page.evaluate("(id) => JS4.restore(id)", sv["id"])
        tscs["X2 restored"] = page.evaluate("() => JS4.tsc()")
        out["X2"] = run(b)
        fresh()
        wait_for(lambda: page.evaluate("(id) => !!snapById(id)", sv["id"]), 30)
        page.evaluate("(id) => JS4.restore(id)", sv["id"])
        tscs["X3 restored"] = page.evaluate("() => JS4.tsc()")
        out["X3"] = run(b)
        fresh()
        run(a, False)
        s4 = page.evaluate("() => JS4.save()")
        if s4["steps"] != sv["steps"]:
            problems.append(f"{tag}: X4 saved at {s4['steps']:,}, X1 at {sv['steps']:,}: the boot to A differs")
        page.evaluate("(id) => JS4.restore(id)", s4["id"])
        tscs["X4 restored"] = page.evaluate("() => JS4.tsc()")
        out["X4"] = run(b)
        tscs["X4 end"] = page.evaluate("() => JS4.tsc()")
        out["tsc"] = tscs
        # A restore puts the TSC back as saved: it stays on the virtual
        # clock, twice in a row at the same count too (v86's extrapolation
        # of repeated readings once doubled it after a reset: CI run
        # 43013630565, "TSC off the virtual clock by 232084897999 ticks").
        for k, v in tscs.items():
            if any(abs(x) > 1_000_000 for x in v):
                problems.append(f"{tag}: {k}: the TSC is off the virtual clock by {v} ticks (1 ms = 10^6)")
        page.screenshot(path=str(shots / f"jitsnap-{name}-1280-light.png"))
    finally:
        ctx.close()
    t1 = out.get("X1", {}).get("trace")
    res = {"saved_at": out.get("save", {}).get("steps"), "tsc": out.get("tsc")}
    for k in ("X1", "X2", "X3", "X4"):
        if k not in out:
            problems.append(f"{tag}: {k} did not run")
            continue
        x = out[k]
        d = jitsnap_first_diff(t1, x["trace"]) if k != "X1" else None
        res[k] = {"steps": x["steps"], "slices": x["slices"], "ms": x["ms"], "eip": x["state"]["eip"], "ram": x["state"]["ram"],
                  "first_diff": d}
        if d:
            problems.append(f"{tag}: {k} differs from the original run at slice {d[0]} of {len(t1)}: {d[1]} vs {d[2]}")
        for f in ("steps", "eip", "ram", "regs", "clock"):
            if x["state"][f] != out["X1"]["state"][f]:
                problems.append(f"{tag}: {k} ends with {f} {x['state'][f]}, the original run {out['X1']['state'][f]}")
    if t1 is not None and len(t1) < 1000:
        problems.append(f"{tag}: only {len(t1)} slices from A to B (test setup)")
    if errs:
        problems.append(f"{tag}: page errors {errs[:3]}")
    r[name] = res


JITSNAP_RUN_JS = """() => ({ steps: stepper.steps, prompt: stepper.prompt ? stepper.prompt.steps : null, runState, taint: INPUT.taint,
  resets: INPUT.log.filter((e) => e.reset).map((e) => e.at) })"""


def jitsnap_ui(browser, base, shots, name, scheme, phone, problems, r):
    """Debian+initrd at full speed: Pause past 2.5e9, Save, Continue to the
    (initramfs) prompt; Restore, Continue to the prompt again: the same
    count and machine, the run not tainted; Replay from boot matches."""
    w = 390 if phone else 1280
    tag = f"jitsnap ui {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    res = {}
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=180000)
        page.evaluate(JITSNAP_DRV_JS) if wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper"), 120) else None
        if not wait_for(lambda: page.evaluate("!!stepper && stepper.steps > 2.5e9 && runState === 'jit'"), 900, 0.2):
            problems.append(f"{tag}: the JIT did not pass 2.5e9 ({page.evaluate('[runState, stepper && stepper.steps]')})")
            return
        machine_do(page, "tap" if phone else "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("runState") == "paused", 20)
        page.evaluate(JITSNAP_DRV_JS)
        sv = page.evaluate("() => JS4.save()")
        res["saved_at"] = sv["steps"]
        resets = page.evaluate(JITSNAP_RUN_JS)["resets"]
        # R5: the reverse history's JIT checkpoints reset it too (logged).
        if not resets or resets[-1] != sv["steps"]:
            problems.append(f"{tag}: after Save the log has the JIT resets {resets}, want the save's at {sv['steps']:,} last")
        machine_do(page, "tap" if phone else "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("!!stepper.prompt && runState === 'paused'"), 600, 0.5):
            problems.append(f"{tag}: no pause at the (initramfs) prompt after Save")
            return
        first = page.evaluate("() => JS4.state()")
        res["first"] = {k: first[k] for k in ("steps", "eip", "ram")}
        first_resets = page.evaluate(JITSNAP_RUN_JS)["resets"]
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{sv['id']}'] button.restore", phone)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == sv["id"] or "failed" in page.inner_text("#snap_status"), 60)
        if page.get_attribute("#snap_status", "data-restored") != sv["id"]:
            problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
            return
        machine_do(page, "tap" if phone else "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("!!stepper.prompt && runState === 'paused'"), 600, 0.5):
            problems.append(f"{tag}: no pause at the (initramfs) prompt after Restore")
            return
        again = page.evaluate("() => JS4.state()")
        run = page.evaluate(JITSNAP_RUN_JS)
        res["again"] = {k: again[k] for k in ("steps", "eip", "ram")}
        res["run"] = run
        for k in ("steps", "eip", "ram", "regs", "clock"):
            if again[k] != first[k]:
                problems.append(f"{tag}: at the prompt after Restore {k} is {again[k]}, the first time {first[k]}")
        if run["taint"]:
            problems.append(f"{tag}: the run is marked not replayable: {run['taint']}")
        if run["resets"] != first_resets:
            problems.append(f"{tag}: after Restore the log has the JIT resets {run['resets']}, the first time {first_resets}")
        show_tab(page, "snapshots")
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jitsnap-ui-{w}-{scheme}-restored.png"))
        snap_click(page, "#run_replay", phone)
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish ({page.evaluate('[runState, stepper && stepper.steps]')})")
            return
        b = replay_state(page)
        res["replayed"] = {k: b[k] for k in ("steps", "eip", "ram", "verdict", "replay", "status")}
        if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
            problems.append(f"{tag}: the page's replay verdict: {b['verdict']} {b['replay']} {b['status']!r}")
        if b["steps"] != again["steps"]:
            problems.append(f"{tag}: the replay ended at {b['steps']:,}, the run at {again['steps']:,}")
        page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"jitsnap-ui-{w}-{scheme}-replayed.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        r[f"ui {w} {scheme}"] = res
        ctx.close()


def jitsnap_wasm(browser, base, problems, r):
    """The page loads the pinned build: its SHA-256 and the export."""
    ctx = browser.new_context()
    page = ctx.new_page()
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10", timeout=120000)
        wait_for(lambda: page.evaluate("typeof stepper !== 'undefined' && !!stepper && !!emulator"), 120)
        w = page.evaluate("""async () => { const b = await (await fetch(withV('v86/v86.wasm'))).arrayBuffer();
          const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', b))).map((x) => x.toString(16).padStart(2, '0')).join('');
          return { sha, reset: typeof emulator.v86.cpu.wm.exports.jit_reset_hidden, patchError: wasmPatchError }; }""")
        r["wasm"] = w
        if w["sha"] != JITSNAP_SHA256:
            problems.append(f"jitsnap: the page's v86.wasm has SHA-256 {w['sha']}, want the gen-v86.sh build {JITSNAP_SHA256}")
        if w["reset"] != "function":
            problems.append(f"jitsnap: v86.wasm exports no jit_reset_hidden ({w['reset']})")
        if w["patchError"]:
            problems.append(f"jitsnap: stepper.js could not add its step exports: {w['patchError']}")
    finally:
        ctx.close()


def jitsnap_case(browser, base, shots, entries):
    problems, r = [], {}

    def guard(fn, *args):
        try:
            return fn(*args)
        except (PlaywrightError, IndexError, KeyError, TypeError) as e:
            problems.append(f"{fn.__name__}: {type(e).__name__}: {(str(e).splitlines() or [''])[0][:300]}")
            return None
    guard(jitsnap_wasm, browser, base, problems, r)
    only = os.environ.get("MKB_JITSNAP_ONLY")
    for prefix, with_initrd, a, b in JITSNAP_WINDOWS:
        names = [n for n in entries if n.startswith(prefix) and ("initrd" in entries[n]) == with_initrd]
        if names and (not only or names[0] in only.split(",")):
            guard(jitsnap_window, browser, base, shots, names[0], a, b, problems, r)
    initrd = [n for n in entries if n.startswith("debian") and "initrd" in entries[n]]
    if initrd and (not only or "ui" in only.split(",")):
        guard(jitsnap_ui, browser, base, shots, initrd[0], "dark", True, problems, r)
    return problems, r


def suite_jitsnap(ctx):
    return report("jitsnap", *jitsnap_case(ctx.browser, ctx.base, ctx.shots, ctx.entries))


# ---- CFI parsing and unwinding on a hand-written fixture ---------------------
# The kernels' CFI is .debug_frame with the rules compilers emit; this
# fixture (llvm-mc from assembly, linked by ld.lld at 0xc1000000) has the
# rest: .eh_frame (CIE "zR", pc-relative FDE pointers) and .debug_frame,
# DW_CFA_offset_extended(_sf), restore_extended, undefined, same_value,
# register, val_offset(_sf), def_cfa_sf, def_cfa_offset_sf,
# val_expression, def_cfa_expression, advance_loc4, set_loc (.debug_frame
# only: in .eh_frame its operand is in the FDE pointer encoding) and a CFA
# expression of DW_OP const2s, const4s, const1u, drop, neg, abs, mod, shr,
# xor (it evaluates to ESP+12). f2's return address is undefined (the
# outermost frame).
CFI_FIXTURE = """%(sections)s
	.text
	.globl f1
	.type f1,@function
f1:
	.cfi_startproc
	pushl %%ebp
	.cfi_def_cfa_offset 8
	.cfi_offset %%ebp, -8
	movl %%esp, %%ebp
	.cfi_def_cfa_register %%ebp
	pushl %%ebx
	.cfi_offset %%ebx, -12
	.cfi_register %%esi, %%edi
	.cfi_undefined %%eax
	.cfi_same_value %%ecx
	nop
	.cfi_remember_state
	.cfi_restore %%ebx
	.cfi_escape 0x11, 0x07, 0x7d
	.cfi_escape 0x14, 0x06, 0x02
	.cfi_escape 0x15, 0x02, 0x7e
	nop
	.cfi_restore_state
	.cfi_escape 0x05, 0x06, 0x04
	.cfi_escape 0x06, 0x06
	.cfi_escape 0x12, 0x04, 0x10
	nop
	.cfi_escape 0x13, 0x08
	nop
	.cfi_escape 0x16, 0x03, 0x02, 0x75, 0x00
	.cfi_escape 0x0f, 0x02, 0x74, 0x08
	nop
	.cfi_escape 0x04, 0x02, 0x00, 0x00, 0x00
	.cfi_escape 0x0f, 0x1c, 0x74, 0x00, 0x0b, 0xf8, 0xff, 0x22, 0x0d, 0x10, 0x00, 0x00, 0x00, 0x22, 0x08, 0x07, 0x13, 0x35, 0x1f, 0x19, 0x33, 0x1d, 0x31, 0x25, 0x31, 0x27, 0x22, 0x08, 0x04, 0x22
%(setloc)s	nop
	nop
	nop
	ret
	.cfi_endproc
	.size f1, .-f1
	.globl f2
	.type f2,@function
f2:
	.cfi_startproc
	.cfi_undefined %%eip
	nop
	ret
	.cfi_endproc
	.size f2, .-f2
"""
CFI_BASE, CFI_OFFSET = 0xc1000000, 0xc0000000


def cfi_fixture(tmp, kind):
    """The fixture as an ELF with .eh_frame or .debug_frame (and DWARF for
    the import: llvm-mc -g)."""
    s = CFI_FIXTURE % {"sections": "\t.cfi_sections .debug_frame" if kind == "debug_frame" else "",
                       # DW_CFA_set_loc 0xc100000c, then DW_CFA_def_cfa ESP+20.
                       "setloc": "\t.cfi_escape 0x01, 0x0c, 0x00, 0x00, 0xc1, 0x0c, 0x04, 0x14\n" if kind == "debug_frame" else ""}
    src, obj, elf = (tmp / f"cfi-{kind}.{x}" for x in ("s", "o", "elf"))
    src.write_text(s)
    subprocess.run(["llvm-mc", "-g", "-triple=i386-linux-gnu", "-filetype=obj", str(src), "-o", str(obj)], check=True)
    subprocess.run(["ld.lld", "-m", "elf_i386", "-e", "f1", f"-Ttext=0x{CFI_BASE:x}", str(obj), "-o", str(elf)], check=True)
    return elf


# In the page: the ELF through dwarfworker.js (as "From disk / Symbols"
# opens a vmlinux), its CFI table back, then unwind.js on it.
CFI_TABLE_JS = """async ([b64, offset]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search);
    const ask = (m) => new Promise((res) => { w.onmessage = (e) => res(e.data); w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'cfi.elf'), offset });
    const c = o.ok ? await ask({ type: 'cfi' }) : null;
    w.terminate();
    window.__cfi = c && c.table;
    return { open: o.ok ? 'ok' : o.why, source: c && c.source, why: c && c.why };
}"""

CFI_ROWS_JS = """(pas) => pas.map((pa) => { const r = cfiRow(window.__cfi, pa); return r === null ? null : r.why ? 'why: ' + r.why : cfiRowText(r); })"""

# unwind() from regs over a memory of { address: value }; the code is the fixture's.
CFI_UNWIND_JS = """([regs, mem, lo, hi, offset]) => {
    const env = { rowAt: (pa) => cfiRow(window.__cfi, pa), read32: (va) => mem[va >>> 0],
                  toPhys: (va) => (va >= lo && va < hi ? va - offset : null), symAt: () => null, isEntry: () => false };
    const u = unwind(regs, env);
    return { state: u.state, why: u.why, frames: u.frames.map((f) => ({ pc: f.pc, method: f.method, cfa: f.cfa, regs: f.regs })) };
}"""


def cfi_unwind_checks(page, hi, problems, r):
    """unwind() through the fixture's rows from registers and a made-up
    stack: the return address and saved registers are where the row says,
    each kind of rule gives the caller's value, and the walk ends or stops
    with its reason."""
    B = CFI_BASE
    ra_f2 = B + 0xe  # the return lands after f2's first byte: f2's row, EIP undefined: the outermost frame

    def walk(eip, regs, mem):
        return page.evaluate(CFI_UNWIND_JS, [{"eip": eip, **regs}, {str(k): v for k, v in mem.items()}, B, hi, CFI_OFFSET])

    def caller(u):
        return {int(k): v for k, v in u["frames"][1]["regs"].items()} if len(u["frames"]) > 1 else {}
    cases = []
    # Row 0xc1000001: CFA=ESP+8, EBP=[CFA-8], EIP=[CFA-4].
    u = walk(B + 1, {"esp": 0x1000, "ebp": 0x1}, {0x1000: 0x2222, 0x1004: ra_f2})
    cases.append(("push ebp", u["state"] == "ok" and len(u["frames"]) == 2 and u["frames"][1]["pc"] == ra_f2 and
                  caller(u).get(5) == 0x2222 and caller(u).get(4) == 0x1008 and u["frames"][1]["method"] == "cfi", u))
    u = walk(B + 1, {"esp": 0x1000}, {0x1000: 0x2222})
    cases.append(("return address unreadable", u["state"] == "stopped" and u["why"] == "the return address at CFA-4 cannot be read", u))
    # Row 0xc1000003: CFA=EBP+8 with EBP unknown.
    u = walk(B + 3, {"esp": 0x1000}, {})
    cases.append(("CFA register unknown", u["why"] == "CFA = EBP+8, and EBP is not known here", u))
    # Row 0xc1000004: ESI=EDI (register rule), EBX=[CFA-12], EAX undefined, ECX same.
    u = walk(B + 4, {"esp": 0x2ff0, "ebp": 0x3000, "edi": 0x99, "ecx": 0x11, "eax": 0x55},
             {0x3000: 0x5555, 0x3004: ra_f2, 0x2ffc: 0x3333})
    c = caller(u)
    cases.append(("register, same, undefined", u["state"] == "ok" and c.get(6) == 0x99 and c.get(1) == 0x11 and 0 not in c and
                  c.get(3) == 0x3333 and c.get(5) == 0x5555, u))
    # Row 0xc1000005: EDX=CFA+8 and ESI=CFA-8 (val_offset), EDI=[CFA+12].
    u = walk(B + 5, {"esp": 0x2ff0, "ebp": 0x3000}, {0x3000: 0x5555, 0x3004: ra_f2, 0x3014: 0x7777})
    c = caller(u)
    cases.append(("val_offset", u["state"] == "ok" and c.get(2) == 0x3010 and c.get(6) == 0x3000 and c.get(7) == 0x7777, u))
    # Row 0xc1000009: CFA=DW_OP_breg4 ESP+8, EBX=DW_OP_breg5 EBP+0 (val_expression).
    u = walk(B + 9, {"esp": 0x4000, "ebp": 0x4444}, {0x4000: 0x6000, 0x4004: ra_f2})
    c = caller(u)
    cases.append(("expressions", u["state"] == "ok" and u["frames"][0]["cfa"] == 0x4008 and c.get(3) == 0x4444 and c.get(5) == 0x6000, u))
    u = walk(B + 9, {"ebp": 0x4444}, {})
    cases.append(("CFA expression without ESP", u["state"] == "stopped" and (u["why"] or "").startswith("CFA (DW_OP_breg4 ESP+8): "), u))
    # Row 0xc100000b: the long CFA expression, ESP+12.
    u = walk(B + 0xb, {"esp": 0x5000, "ebp": 0x1}, {0x5000: 0x1, 0x5004: 0x6000, 0x5008: ra_f2})
    cases.append(("CFA expression ops", u["state"] == "ok" and u["frames"][0]["cfa"] == 0x500c, u))
    # The caller's row (0xc1000006: CFA=ESP-64) puts its CFA below this frame's: a corrupt stack.
    u = walk(B + 1, {"esp": 0x1000}, {0x1000: 0x2222, 0x1004: B + 7, 0xfc4: ra_f2})
    cases.append(("stack does not grow", u["state"] == "stopped" and (u["why"] or "").startswith("the stack does not grow back up"), u))
    u = walk(0x1234, {"esp": 0x1000}, {})
    cases.append(("not code", u["why"] == "0x1234 is not code of this kernel", u))
    r["unwind"] = {name: ok for name, ok, _ in cases}
    for name, ok, u in cases:
        if not ok:
            problems.append(f"unwind on the CFI fixture, {name}: {json.dumps(u)[:400]}")


def cfi_case(browser, base, shots):
    """The fixture's CFI, .eh_frame and .debug_frame, opened in the page
    like an imported vmlinux: every row equals llvm-dwarfdump's (the long
    CFA expression's row: its value, as dwExprText abbreviates the rarer
    ops), no row outside the FDEs, the frame summary of f1, then unwind()."""
    problems, r = [], {}
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1")
    wait_for(lambda: page.evaluate("typeof cfiRow === 'function' && typeof unwind === 'function'"), 30)
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        for kind in ("eh_frame", "debug_frame"):
            elf = cfi_fixture(pathlib.Path(tmp), kind)
            fdes = dwarfdump_cfi(str(elf))
            got = page.evaluate(CFI_TABLE_JS, [base64.b64encode(elf.read_bytes()).decode(), CFI_OFFSET])
            r[kind] = got
            if got["open"] != "ok" or got["source"] != "." + kind:
                problems.append(f"{kind}: the fixture did not open with its CFI: {got}")
                continue
            hi = max(f[1] for f in fdes)
            rows = [(va, text) for f in fdes for va, text in f[2]]
            want = {}
            for va in range(CFI_BASE, hi + 2):
                f = next((f for f in fdes if f[0] <= va < f[1]), None)
                prev = [t for a, t in (f[2] if f else []) if a <= va]
                want[va] = prev[-1] if prev else None
            have = dict(zip(want, page.evaluate(CFI_ROWS_JS, [va - CFI_OFFSET for va in want])))
            bad = [(hex(va), want[va], have[va]) for va in want
                   if want[va] != have[va] and not (want[va] and "DW_OP_const2s" in want[va])]
            r[kind + "_rows"] = f"{len(want) - len(bad)}/{len(want)} addresses, {len(rows)} rows"
            if bad or len(rows) < 10:
                problems.append(f"{kind}: rows differ from llvm-dwarfdump: {bad[:4]} ({len(rows)} rows)")
            if kind == "debug_frame" and "CFA=ESP+20" not in (want.get(CFI_BASE + 0xc) or ""):
                problems.append(f"debug_frame: llvm-dwarfdump has no DW_CFA_set_loc row at 0xc100000c: {want.get(CFI_BASE + 0xc)}")
            info = page.evaluate("(pa) => cfiFrameText(cfiFrameInfo(window.__cfi, pa))", CFI_BASE + 4 - CFI_OFFSET)
            r[kind + "_frame"] = info
            # .debug_frame's last row (DW_CFA_set_loc) is CFA=ESP+20.
            want_info = (f"frame pointer: CFA = EBP+8 from +0x3 (before that at most ESP+{20 if kind == 'debug_frame' else 8}); the call frame information does not "
                         "say how far below EBP the frame goes; saves EDI at CFA+12, EBP at CFA-8, EBX at CFA-12; a CFA expression somewhere")
            if info != want_info:
                problems.append(f"{kind}: f1's frame summary {info!r}, want {want_info!r}")
            if kind == "eh_frame":
                cfi_unwind_checks(page, hi, problems, r)
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    page.close()
    return problems, r


def suite_cfi(ctx):
    return report("cfi", *cfi_case(ctx.browser, ctx.base, ctx.shots))


# ---- DWARF across compile units: a GCC LTO fixture --------------------------
# The shipped kernels' DWARF never refers from one compile unit into
# another; GCC's link-time optimization does it everywhere: the code's
# unit ("<artificial>") has a concrete DIE per function and variable whose
# DW_AT_abstract_origin (DW_FORM_ref_addr) is the early-debug DIE in the
# unit of its source file, with the name, file, line and type. Two files:
# a.c's helper is inlined into b.c's entry. Through dwarfworker.js like
# an import, every fact the page reads through those references
# (DwarfReader subprogramAt, declAt, typeAt, nameAt, foreignKids) must
# equal gdb's and llvm-dwarfdump's.
LTO_FIXTURE = {
    "a.c": "int counter;\nstatic int scale = 3;\n\nint helper(int x)\n{\n\tint y = x * scale;\n\n\tcounter += y;\n\treturn y + 1;\n}\n",
    "b.c": "int helper(int x);\n\nint entry(int n)\n{\n\tint s = 0;\n\n\tfor (int i = 0; i < n; i++)\n\t\ts += helper(i);\n\treturn s;\n}\n",
}
LTO_BASE, LTO_OFFSET = 0xc1000000, 0xc0000000

LTO_OPEN_JS = """async ([b64, offset, pa]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search);
    // The answer of type t (the types answer comes after progress messages).
    const ask = (m, t) => new Promise((res) => { w.onmessage = (e) => { if (e.data.type === t || e.data.type === 'error') res(e.data); };
        w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'lto.elf'), offset }, 'open');
    if (!o.ok) { w.terminate(); return { open: o.why }; }
    const T = (await ask({ type: 'types' }, 'types')).types;
    const groups = [];
    for (let i = 0; i < o.units; i++) {
        const l = await ask({ type: 'locals', i }, 'locals');
        if (l.type === 'error') { w.terminate(); return { open: 'ok', error: l.message }; }
        T.types.push(...l.added);
        groups.push(...l.groups);
    }
    w.terminate();
    const name = (v) => [v[0], dwTypeName(T, v[1]), v[3] === null ? 'optimized out' : 'has a location'];
    return { open: 'ok', units: o.units,
        funcs: T.funcs.fns.map((e) => ({ name: e[0], at: T.funcs.files[e[1]] + ':' + e[2], proto: dwFuncProto(T, e),
            inlined: e[8].map((x) => x + offset) })),
        vars: T.vars.map((v) => ({ name: v[0], at: dwVarDeclText(T, v), type: dwTypeName(T, v[2]), va: v[1] + offset })),
        frames: dwarfLocalFrames(dwarfScopesAt(groups, pa)).map((f) => ({ func: f.func, inlined: f.inlined,
            params: f.params.map(name), locals: f.locals.map(name) })) };
}"""


def lto_fixture(tmp):
    """a.c and b.c compiled with gcc -flto -g -O2 for i386 and linked at
    LTO_BASE; None (and why) when gcc cannot."""
    for n, t in LTO_FIXTURE.items():
        (tmp / n).write_text(t)
    flags = ["-m32", "-flto", "-g", "-O2", "-ffreestanding", "-fno-pic", "-fno-asynchronous-unwind-tables"]
    elf = tmp / "lto.elf"
    try:
        for n in LTO_FIXTURE:
            subprocess.run(["gcc", *flags, "-c", n, "-o", n[:-2] + ".o"], cwd=tmp, check=True, capture_output=True, text=True)
        subprocess.run(["gcc", *flags, "-nostdlib", "-no-pie", "-Wl,-e,entry", f"-Wl,-Ttext=0x{LTO_BASE:x}", "a.o", "b.o",
                        "-o", str(elf)], cwd=tmp, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return None, f"gcc: {getattr(e, 'stderr', '') or e}"[:300]
    return elf, None


def lto_dies(dump):
    """llvm-dwarfdump's DIEs: [{"tag", "DW_AT_...": value as printed}]."""
    out = []
    for block in re.split(r"\n(?=0x[0-9a-f]+:)", dump):
        m = re.match(r"0x[0-9a-f]+:\s+(DW_TAG_\w+)", block)
        if m:
            out.append({"tag": m.group(1), **{a: v for a, v in re.findall(r"^\s+(DW_AT_\w+)\s+\((.*)\)$", block, re.M)}})
    return out


def dwarf_lto_case(browser, base, shots):
    problems, r = [], {}
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        tmp = pathlib.Path(tmp)
        elf, why = lto_fixture(tmp)
        if not elf:
            return [f"the GCC LTO fixture did not build: {why}"], r
        r["ref_addr"] = subprocess.run(["llvm-dwarfdump", "-v", "--debug-info", str(elf)], capture_output=True,
                                       text=True).stdout.count("[DW_FORM_ref_addr]")
        if not r["ref_addr"]:
            problems.append("the fixture's DWARF has no reference into another unit (DW_FORM_ref_addr): it tests nothing here")
        dies = lto_dies(subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout)
        # helper is only inlined: at its call gdb's innermost scope is helper's.
        inl = next((d for d in dies if d["tag"] == "DW_TAG_inlined_subroutine" and d.get("DW_AT_abstract_origin", "").endswith('"helper"')), None)
        if not inl or "DW_AT_low_pc" not in inl:
            return problems + [f"the fixture has no inlined call of helper: {dies}"[:600]], r
        call = int(inl["DW_AT_low_pc"], 16)
        r["helper_call"] = hex(call)
        gdb = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info functions ^entry$", "-ex", "info variables ^counter$",
                              "-ex", f"info scope *{call:#x}", "-ex", "info scope entry", "-ex", "ptype counter", str(elf)],
                             capture_output=True, text=True).stdout
        nm = {l.split()[2]: int(l.split()[0], 16) for l in
              subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
        page = browser.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        page.goto(f"{base}/index.html?kernel=hello&paused=1")
        wait_for(lambda: page.evaluate("typeof dwFuncProto === 'function' && typeof dwarfLocalFrames === 'function'"), 30)
        got = page.evaluate(LTO_OPEN_JS, [base64.b64encode(elf.read_bytes()).decode(), LTO_OFFSET, call - LTO_OFFSET])
        page.close()
    r["page"] = got
    if got.get("open") != "ok" or got.get("error"):
        return problems + [f"the fixture did not open: {got}"], r
    fn = {f["name"]: f for f in got["funcs"]}
    # entry: gdb's "info functions" line (b.c, "3:\tint entry(int);").
    m = re.search(r"File [^\n]*/b\.c:\n(\d+):\tint entry\(int\);", gdb)
    want = f"b.c:{m.group(1)}" if m else None
    e = fn.get("entry")
    if not m or not e or e["at"] != want or e["proto"] != "int entry(int n)":
        problems.append(f"entry: the page says {e}, gdb {want} int entry(int) ({gdb[:300]!r})")
    # helper (inlined only, so not in gdb's function list): the abstract
    # DIE's decl_file/decl_line in llvm-dwarfdump, its call in gdb's scope.
    h = fn.get("helper")
    d = next((d for d in dies if d["tag"] == "DW_TAG_subprogram" and d.get("DW_AT_name") == '"helper"' and
              "DW_AT_declaration" not in d), {})
    want = f"{pathlib.Path(d['DW_AT_decl_file'].strip(chr(34))).name}:{d['DW_AT_decl_line']}" if "DW_AT_decl_line" in d else None
    if not want or not h or h["at"] != want or h["proto"] != "int helper(int x)" or call not in h["inlined"]:
        problems.append(f"helper: the page says {h}, llvm-dwarfdump {want}, inlined at {call:#x}")
    if f"Symbol helper is a function at address {call:#x}" not in gdb:
        problems.append(f"gdb does not put helper's inlined call at {call:#x}: {gdb[:400]!r}")
    # counter: gdb's "info variables" (a.c, "1:\tint counter;"), ptype, nm.
    m = re.search(r"File [^\n]*/a\.c:\n(\d+):\tint counter;", gdb)
    v = next((v for v in got["vars"] if v["name"] == "counter"), None)
    if not m or not v or v["at"] != f"a.c:{m.group(1)}" or v["type"] != "int" or "type = int" not in gdb or v["va"] != nm.get("counter"):
        problems.append(f"counter: the page says {v}, gdb a.c:{m and m.group(1)} int, nm {nm.get('counter')}")
    # Locals at the inlined call: helper's x and y, then entry's; names as
    # gdb's info scope lists them, x optimized out as gdb says.
    fr = got["frames"]
    scope = re.findall(r"Symbol (\w+) is ([^\n]*)", gdb.split("Scope for entry:")[0])
    inner = [n for n, what in scope if not what.startswith("a function")]
    r["gdb_scope"] = scope
    if len(fr) != 2 or fr[0]["func"] != "helper" or not fr[0]["inlined"] or fr[1]["func"] != "entry" or fr[1]["inlined"]:
        problems.append(f"frames at {call:#x}: {fr}, want helper (inlined) in entry")
    else:
        mine = [v[0] for v in fr[0]["params"] + fr[0]["locals"]]
        if sorted(mine) != sorted(inner) or any(v[1] != "int" for v in fr[0]["params"] + fr[0]["locals"]):
            problems.append(f"helper's variables at {call:#x}: {fr[0]}, gdb lists {inner}")
        for n, what in scope:
            v = next((v for v in fr[0]["params"] + fr[0]["locals"] if v[0] == n), None)
            if v and (what.startswith("optimized out")) != (v[2] == "optimized out"):
                problems.append(f"{n} at {call:#x}: the page says {v[2]}, gdb {what!r}")
        outer = re.findall(r"Symbol (\w+) is", gdb.split("Scope for entry:")[1].split("type =")[0]) if "Scope for entry:" in gdb else []
        theirs = [v[0] for v in fr[1]["params"] + fr[1]["locals"]]
        if not outer or any(n not in theirs for n in outer) or any(v[1] != "int" for v in fr[1]["params"] + fr[1]["locals"]):
            problems.append(f"entry's variables at {call:#x}: {fr[1]}, gdb's scope of entry {outer}")
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_dwarf_lto(ctx):
    return report("dwarf_lto", *dwarf_lto_case(ctx.browser, ctx.base, ctx.shots))


# ---- DWARF forms the shipped kernels lack: a compiled fixture ----------------
# The kernels are GCC or clang DWARF 5; an imported vmlinux can be DWARF
# 2 to 4 (.debug_ranges, .debug_loc, the old line program header, a member
# offset as a DW_OP_plus_uconst block, DW_AT_bit_offset bit fields), with
# GCC's DW_OP_GNU_* operations and SSE registers. One C file (bit fields,
# an enum, a union, a varargs and a noreturn function, a declaration and
# its definition, an inlined call with a block, a static local, long long
# and double values) built five ways, each opened through dwarfworker.js
# like an import and compared byte by byte with llvm-symbolizer, gdb and
# llvm-dwarfdump.
DWF_SOURCE = """typedef int (*binop)(int, int);
enum color { RED = -2, GREEN = 7, BLUE = 300 };
struct flags {
	unsigned a : 3;
	signed b : 5;
	unsigned c : 9;
	char tag;
	unsigned long long big;
};
union word { int i; unsigned char b[4]; float f; };
extern int shared;
int shared = 5;
struct flags gflags = { 1, -3, 100, 'x', 0x123456789aULL };
union word gword;
enum color gcolor = BLUE;
const volatile int cv[2][3];
binop gop;
double gdbl = 1.5;
long long gll = -7;

__attribute__((noinline)) int sum(int n, ...)
{
	__builtin_va_list ap;
	int s = 0;

	__builtin_va_start(ap, n);
	while (n--)
		s += __builtin_va_arg(ap, int);
	__builtin_va_end(ap);
	return s;
}

__attribute__((noreturn, noinline)) void stop(void)
{
	for (;;)
		__asm__ volatile("hlt");
}

static inline __attribute__((always_inline)) int twice(int v)
{
	int r = v * 2;

	{
		int t = r + shared;

		gword.i = t;
	}
	return r;
}

__attribute__((noinline)) long long mix(long long a, int k, double d)
{
	static int calls;
	long long acc = a;

	calls++;
	for (int i = 0; i < k; i++) {
		acc += twice(i) * (long long)d;
		if (acc > 1000000)
			stop();
	}
	gll = acc;
	return acc + calls;
}

__attribute__((noinline)) int use(struct flags *f, int x)
{
	int y = f->a + f->b + x;

	gflags.c = y;
	return twice(y) + sum(3, x, y, f->c);
}

int entry(int n)
{
	gop = (binop)0;
	return use(&gflags, n) + (int)mix(gll, n, gdbl);
}
"""
# C++: a method defined outside its class is a DIE with DW_AT_specification
# (its name, the varargs) pointing at the declaration in the class, a
# static data member's definition too.
DWF_CXX = """namespace ns {
struct Counter {
	static int total;
	int v;
	int add(int d, ...);
	__attribute__((noinline)) int get(int) const;
};
int Counter::total = 1;
}

__attribute__((noinline)) int ns::Counter::add(int d, ...)
{
	v += d;
	total += v;
	return v;
}

__attribute__((noinline)) int ns::Counter::get(int k) const
{
	return v * k + total;
}

static inline int helper(ns::Counter &c, int x)
{
	return c.get(x) + 1;
}

ns::Counter gc;

extern "C" int entry(int n)
{
	gc.add(n, 1, 2);
	return helper(gc, n);
}
"""
# name: (compiler, flags, DWARF version the page must report, source)
DWF_BUILDS = {
    "gcc-dwarf2": ("gcc", ["-gdwarf-2", "-mregparm=3"], 2, "m.c"),
    "gcc-dwarf3": ("gcc", ["-gdwarf-3", "-O0"], 3, "m.c"),
    "gcc-dwarf4": ("gcc", ["-gdwarf-4", "-mregparm=3", "-msse2", "-mfpmath=sse"], 4, "m.c"),
    "clang-dwarf4": ("clang", ["-gdwarf-4"], 4, "m.c"),
    "clang-dwarf5": ("clang", ["-gdwarf-5", "-mregparm=3"], 5, "m.c"),
    "g++-dwarf4": ("g++", ["-gdwarf-4", "-fno-exceptions", "-fno-rtti"], 4, "c.cc"),
}
DWF_FUNCS = ("sum", "stop", "mix", "use", "entry")
DWF_VARS = ("shared", "gflags", "gword", "gcolor", "cv", "gop", "gdbl", "gll")

DWF_JS = """async ([b64, offset, lo, hi, src]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search);
    const ask = (m, t) => new Promise((res) => { w.onmessage = (e) => { if (e.data.type === t || e.data.type === 'error') res(e.data); };
        w.postMessage(m); });
    const o = await ask({ type: 'open', file: new File([bytes], 'fixture.elf'), offset }, 'open');
    if (!o.ok) { w.terminate(); return { open: o.why }; }
    const out = { open: 'ok', versions: o.versions, units: o.units };
    // Asked before the types: the Worker's error.
    out.early = await ask({ type: 'locals', i: 0 }, 'locals');
    const tables = [];
    for (let i = 0; i < o.units; i++) {
        const u = await ask({ type: 'unit', i }, 'unit');
        if (u.type === 'error') { w.terminate(); return { ...out, error: 'unit: ' + u.message }; }
        tables.push(u.table);
    }
    const at = (pa) => { for (const t of tables) { const f = dwarfFrames(t, pa); if (f) return f; } return null; };
    out.frames = [];
    for (let pa = lo; pa < hi; pa++)
        out.frames.push(at(pa));
    const T = (await ask({ type: 'types' }, 'types')).types;
    const groups = [];
    for (let i = 0; i < o.units; i++) {
        const l = await ask({ type: 'locals', i }, 'locals');
        if (l.type === 'error') { w.terminate(); return { ...out, error: 'locals: ' + l.message }; }
        T.types.push(...l.added);
        groups.push(...l.groups);
    }
    const c = await ask({ type: 'cfi' }, 'cfi');
    const lines = await ask({ type: 'lines', q: src }, 'lines');
    out.nofile = (await ask({ type: 'lines', q: 'nosuch.c' }, 'lines')).why;
    w.terminate();
    out.code = lines.code ? [...lines.code].map(([line, e]) => [line, e.rows.map((r) => [r[0], r[1]]), e.calls.map((x) => [x[0], x[2]])]) :
        lines.why || lines.message;
    out.cfi = c.why || c.source;
    out.rows = [];
    for (let pa = lo; pa < hi && c.table; pa++) {
        const r = cfiRow(c.table, pa);
        out.rows.push(r === null ? null : r.why ? 'why: ' + r.why : cfiRowText(r));
    }
    out.funcs = T.funcs.fns.map((e) => ({ name: e[0], at: T.funcs.files[e[1]] + ':' + e[2], type: dwFuncProto(T, e, false),
        noret: !!(e[3] & 4), varargs: !!(e[3] & 2) }));
    out.vars = T.vars.map((v) => ({ name: v[0], va: v[1] + offset, size: v[3], at: dwVarDeclText(T, v), type: dwTypeName(T, v[2]),
        where: [...Array(v[3]).keys()].map((k) => { const x = dwWhere(T, v[1] + k); return x ? x.path + (x.note ? ' ' + x.note : '') : null; }) }));
    out.flags = T.types.filter((t) => t[0] === DWT.struct && t[1] === 'flags' && t[3]).map((t) => t[3])[0] || null;
    out.enums = T.types.filter((t) => t[0] === DWT.enum && t[1] === 'color').map((t) => t[3])[0] || null;
    // The variables at each address: name and kind of location there, as
    // gen-dwarf.js --locals classifies them for test_dwarf.py's gdb_scopes.
    const pieces = (e) => { for (let p = 0; p >= 0 && p < e.length; p = dwOpEnd(e, p)) if (e[p] === 0x93 || e[p] === 0x9d) return true; return false; };
    const cls = (loc, pa) => {
        const l = dwLocAt(loc, pa);
        if (l.why) return 'out';
        if (l.c !== undefined || l.cb) return 'const';
        const e = l.expr;
        if (pieces(e)) return 'pieces';
        if (e.length === 1 && e[0] >= 0x50 && e[0] <= 0x6f) return 'reg:' + (CFI_REGS[e[0] - 0x50] || e[0] - 0x50);
        return e.length ? 'expr' : 'out';
    };
    out.locals = [];
    for (let pa = lo; pa < hi; pa++) {
        const fr = dwarfLocalFrames(dwarfScopesAt(groups, pa));
        out.locals.push(fr.length ? fr.map((f) => ({ func: f.func, vars: [...f.params, ...f.locals].map((v) => [v[0], cls(v[3], pa)]) })) : null);
    }
    // Every location expression as text, per variable: [name, lo, hi, text].
    const hex = (h) => Uint8Array.from(h.match(/../g) || [], (x) => parseInt(x, 16));
    out.exprs = [];
    for (const g of groups)
        for (const s of g)
            for (const v of s[5]) {
                if (typeof v[3] === 'string')
                    out.exprs.push([v[0], 0, 0, dwExprText(hex(v[3]))]);
                else if (Array.isArray(v[3]))
                    for (const [a, n, e] of v[3])
                        out.exprs.push([v[0], a + offset, a + n + offset, dwExprText(hex(e))]);
            }
    return out;
}"""


def dwf_build(tmp, name):
    """DWF_SOURCE built as DWF_BUILDS[name] for i386, linked at CFI_BASE (-O2 unless the flags say otherwise)."""
    cc, flags, _, src = DWF_BUILDS[name]
    (tmp / src).write_text(DWF_CXX if src == "c.cc" else DWF_SOURCE)
    obj, elf = tmp / f"{name}.o", tmp / f"{name}.elf"
    common = ["-m32", "-O2", "-ffreestanding", "-fno-pic", *flags]
    ld = ["-fuse-ld=lld"] if cc == "clang" else []
    try:
        subprocess.run([cc, *common, "-c", src, "-o", str(obj)], cwd=tmp, check=True, capture_output=True, text=True)
        subprocess.run([cc, "-m32", *ld, "-nostdlib", "-no-pie", "-Wl,-e,entry", f"-Wl,-Ttext=0x{CFI_BASE:x}", str(obj), "-o", str(elf)],
                       cwd=tmp, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return None, f"{cc}: {getattr(e, 'stderr', '') or e}"[:300]
    return elf, None


def dwf_dwarfdump_exprs(elf):
    """llvm-dwarfdump's location of every named (or abstract-origin named)
    parameter and variable inside a function: {(name, lo, hi, text)}, lo =
    hi = 0 for a single expression, text None where it cannot print it;
    DW_OP_GNU_entry_value spelled as DWARF 5's."""
    dump = subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout
    out = {}
    for block in re.split(r"\n(?=0x[0-9a-f]+:)", dump):
        if not re.match(r"0x[0-9a-f]+:\s+DW_TAG_(formal_parameter|variable)", block):
            continue
        m = re.search(r'DW_AT_(?:name|abstract_origin)\s+\((?:0x[0-9a-f]+ )?"([^"]+)"\)', block)
        loc = re.search(r"DW_AT_location\s+\((.*?)\)\n(?=\s+DW_AT|\s*$)", block + "\n", re.S)
        if not m or not loc:
            continue
        text = loc.group(1).replace("DW_OP_GNU_entry_value", "DW_OP_entry_value")
        entries = re.findall(r"\[(0x[0-9a-f]+), (0x[0-9a-f]+)\): (.*)", text)
        if entries:
            for a, b, e in entries:
                e = e.strip()
                if e.count("(") < e.count(")"):
                    e = e[:-1]
                if int(b, 16) > int(a, 16):
                    out[(m.group(1), int(a, 16), int(b, 16))] = None if "error" in e else e
        elif "\n" not in text:
            out[(m.group(1), 0, 0)] = None if "error" in text else text.strip()
    return out


def dwf_gdb(elf):
    """gdb's answers: ptype of each function, whatis of each variable,
    ptype/o of struct flags, the enumerators' values, info functions /
    variables (file:line)."""
    ex = []
    for f in DWF_FUNCS:
        ex += ["-ex", f"echo @@func {f}\\n", "-ex", f"ptype {f}"]
    for v in DWF_VARS:
        ex += ["-ex", f"echo @@var {v}\\n", "-ex", f"whatis {v}", "-ex", f"info line {v}"]
    ex += ["-ex", "echo @@flags\\n", "-ex", "ptype/o struct flags", "-ex", "echo @@enum\\n",
           "-ex", "print/d (int)RED", "-ex", "print/d (int)GREEN", "-ex", "print/d (int)BLUE",
           "-ex", "echo @@info\\n", "-ex", "info functions", "-ex", "info variables"]
    out = subprocess.run(["gdb", "-nx", "-batch", "-ex", "set width 0", *ex, str(elf)], capture_output=True, text=True).stdout
    parts = {}
    for p in out.split("@@")[1:]:
        head, _, body = p.partition("\n")
        parts[head.strip()] = body
    return parts


def dwarf_forms_case(browser, base, shots, builds=DWF_BUILDS):
    from test_dwarf import symbolize, gdb_scopes
    problems, r = [], {}
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1")
    wait_for(lambda: page.evaluate("typeof dwFuncProto === 'function' && typeof cfiRow === 'function' && typeof dwExprText === 'function'"), 30)
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        tmp = pathlib.Path(tmp)
        for name in builds:
            elf, why = dwf_build(tmp, name)
            if not elf:
                problems.append(f"{name}: the fixture did not build: {why}")
                continue
            nm = {l.split()[2]: int(l.split()[0], 16) for l in
                  subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
            m = re.search(r"\.text\s+PROGBITS\s+([0-9a-f]+) [0-9a-f]+ ([0-9a-f]+)",
                          subprocess.run(["readelf", "-SW", str(elf)], capture_output=True, text=True).stdout)
            lo, hi = int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)
            got = page.evaluate(DWF_JS, [base64.b64encode(elf.read_bytes()).decode(), CFI_OFFSET, lo - CFI_OFFSET, hi - CFI_OFFSET, DWF_BUILDS[name][3]])
            if got.get("open") != "ok" or got.get("error"):
                problems.append(f"{name}: the fixture did not open: {str(got)[:300]}")
                continue
            p = dwf_checks(name, elf, got, lo, hi, nm, symbolize, gdb_scopes)
            r[name] = "ok" if not p else f"{len(p)} problems"
            problems += p
    page.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def dwf_locals_checks(name, elf, got, vas, gdb_scopes):
    """The parameters and locals at every byte (gdb's info scope, by kind
    of location) and the text of every location expression (llvm-dwarfdump)."""
    problems = []
    # Locals at every byte: gdb's info scope, by kind of location.
    scopes = gdb_scopes(str(elf), vas)
    bad = []
    for va, l in zip(vas, got["locals"]):
        w = scopes.get(va)
        if w is None or l is None:
            continue
        if sorted(map(tuple, l[0]["vars"])) != sorted(w):
            bad.append((hex(va), l, w))
    r_checked = sum(1 for va, l in zip(vas, got["locals"]) if l and scopes.get(va) is not None)
    if bad or r_checked < len(vas) // 4:
        problems.append(f"{name}: locals differ from gdb's info scope at {len(bad)}/{r_checked} addresses: {bad[:2]}")
    # Every location expression: llvm-dwarfdump's text.
    # (The page turns clang's DW_OP_addrx into DW_OP_addr with the address.)
    mine = {(n, a, b): t for n, a, b, t in got["exprs"]}
    theirs = {k: v for k, v in dwf_dwarfdump_exprs(elf).items() if k[0] not in DWF_VARS + ("gc", "total") and not (v or "").startswith("DW_OP_addrx")}
    differ = [(k, mine.get(k), theirs.get(k)) for k in sorted(set(mine) | set(theirs), key=str)
              if theirs.get(k) is not None and mine.get(k) != theirs.get(k)]
    if differ or len(theirs) < 5:
        problems.append(f"{name}: location expressions differ from llvm-dwarfdump's ({len(theirs)}): {differ[:4]}")
    return problems


def dwf_cxx_checks(name, elf, got, nm):
    """The C++ fixture: the methods' and variables' names, file:line and
    addresses through DW_AT_specification (gdb's info functions /
    variables, llvm-nm), add's varargs."""
    problems = []
    gdb = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info functions", "-ex", "info variables", str(elf)],
                         capture_output=True, text=True).stdout
    fn = {f["name"]: f for f in got["funcs"]}
    for f, decl in (("add", "ns::Counter::add(int, ...)"), ("get", "ns::Counter::get(int) const"), ("entry", "entry(int)")):
        m = re.search(r"^(\d+):\t[^\n]*" + re.escape(decl) + ";$", gdb, re.M)
        if not m or not fn.get(f) or fn[f]["at"] != "c.cc:" + m.group(1):
            problems.append(f"{name}: {f} at {fn.get(f)}, gdb's info functions {m and m.group(0)!r}")
    if not fn.get("add", {}).get("varargs") or fn.get("get", {}).get("varargs"):
        problems.append(f"{name}: varargs: add {fn.get('add')}, get {fn.get('get')}")
    vs = {v["name"]: v for v in got["vars"]}
    for v, decl, sym in (("total", "int ns::Counter::total", "_ZN2ns7Counter5totalE"), ("gc", "ns::Counter gc", "gc")):
        m = re.search(r"^(\d+):\t" + re.escape(decl) + ";$", gdb, re.M)
        if not m or not vs.get(v) or vs[v]["at"] != "c.cc:" + m.group(1) or vs[v]["va"] != nm.get(sym):
            problems.append(f"{name}: {v}: {vs.get(v)}, gdb {m and m.group(0)!r}, nm {nm.get(sym)}")
    return problems


def dwf_checks(name, elf, got, lo, hi, nm, symbolize, gdb_scopes):
    problems = []
    want_ver = DWF_BUILDS[name][2]
    if got["versions"] != [want_ver]:
        problems.append(f"{name}: DWARF versions {got['versions']}, want [{want_ver}]")
    if "not read yet" not in str(got["early"]):
        problems.append(f"{name}: locals before types answered {got['early']}")
    if got["nofile"] != "no source file nosuch.c with code in this kernel's line table":
        problems.append(f"{name}: lines of an unknown file: {got['nofile']!r}")
    # Frames at every byte: llvm-symbolizer --inlines.
    vas = list(range(lo, hi))
    want = symbolize(str(elf), vas, [str(elf.parent) + "/"])
    bad = [(hex(va), g, w) for va, g, w in zip(vas, got["frames"], want) if not frames_agree(g or [], w)]
    if bad:
        problems.append(f"{name}: frames differ from llvm-symbolizer at {len(bad)}/{len(vas)} addresses: {bad[:2]}")
    # The code of each line of the source: its rows' first and last bytes are that line.
    sym = dict(zip(vas, want))
    if not isinstance(got["code"], list) or not got["code"]:
        problems.append(f"{name}: no code for {DWF_BUILDS[name][3]}: {got['code']!r}")
    else:
        for line, rows, calls in got["code"]:
            for a, b in rows:
                for va in (a + CFI_OFFSET, b - 1 + CFI_OFFSET):
                    w = sym.get(va)
                    if w and w[0][2] != line:
                        problems.append(f"{name}: {DWF_BUILDS[name][3]}:{line} lists {va:#x}, llvm-symbolizer says {w[0]}")
    # CFI rows.
    fdes = dwarfdump_cfi(str(elf))
    rows = {}
    for va in vas:
        f = next((f for f in fdes if f[0] <= va < f[1]), None)
        prev = [t for a, t in (f[2] if f else []) if a <= va]
        rows[va] = prev[-1] if prev else None
    bad = [(hex(va), rows[va], g) for va, g in zip(vas, got["rows"]) if rows[va] != g]
    if bad or not fdes:
        problems.append(f"{name}: CFI rows ({got['cfi']}) differ from llvm-dwarfdump: {bad[:3]} ({len(fdes)} FDEs)")
    if DWF_BUILDS[name][3] == "c.cc":
        return problems + dwf_cxx_checks(name, elf, got, nm) + dwf_locals_checks(name, elf, got, vas, gdb_scopes)
    # Functions and variables against gdb.
    g = dwf_gdb(elf)
    fn = {f["name"]: f for f in got["funcs"]}
    for f in DWF_FUNCS:
        want_t = (g.get(f"func {f}", "").strip().partition("type = ")[2])
        have = fn.get(f)
        if not have or have["type"] != want_t:
            problems.append(f"{name}: {f}'s type {have and have['type']!r}, gdb's ptype {want_t!r}")
        m = re.search(rf"^(\d+):\t[^\n]*\b{f}\(", g.get("info", ""), re.M)
        if have and m and have["at"] != f"m.c:{m.group(1)}":
            problems.append(f"{name}: {f} at {have['at']}, gdb's info functions m.c:{m.group(1)}")
    # DW_AT_noreturn is DWARF 5's (an extension before).
    noret = "DW_AT_noreturn" in subprocess.run(["llvm-dwarfdump", "--debug-info", str(elf)], capture_output=True, text=True).stdout
    if fn.get("stop", {}).get("noret") != noret or fn.get("sum", {}).get("noret") or not fn.get("sum", {}).get("varargs"):
        problems.append(f"{name}: noreturn/varargs flags {fn.get('stop')} {fn.get('sum')}")
    vs = {v["name"]: v for v in got["vars"]}
    for v in DWF_VARS:
        want_t = g.get(f"var {v}", "").strip().partition("type = ")[2].split("\n")[0]
        have = vs.get(v)
        if not have or have["type"] != want_t or have["va"] != nm.get(v):
            problems.append(f"{name}: {v}: {have and (have['type'], hex(have['va']))}, gdb {want_t!r}, nm {nm.get(v) and hex(nm[v])}")
        m = re.search(rf"^(\d+):\t[^\n]*\b{v}\b[^\n]*;$", g.get("info", ""), re.M)
        if have and m and have["at"] != f"m.c:{m.group(1)}":
            problems.append(f"{name}: {v} at {have['at']!r}, gdb's info variables m.c:{m.group(1)}")
    # struct flags as gdb's ptype/o lays it out: [name, byte, bits, bit in byte].
    lay = []
    for m in re.finditer(r"/\*\s+(\d+)(?:\s*:\s*(\d+))?\s*\|\s*\d+\s*\*/\s+[^;]*?(\w+)(?:\s*:\s*(\d+))?;", g.get("flags", "")):
        off, bit, mem, bits = int(m.group(1)), int(m.group(2) or 0), m.group(3), m.group(4)
        lay.append([mem, off, int(bits), bit] if bits else [mem, off])
    have = [[m[0], m[1], m[3], m[4]] if len(m) > 3 else [m[0], m[1]] for m in got["flags"] or []]
    # gdb counts a bit field's offset from the start of its storage
    # unit's byte; the page from the byte holding its lowest bit.
    norm = lambda l: [[x[0], x[1] * 8 + x[3], x[2]] if len(x) > 2 else x for x in l]
    if norm(lay) != norm(have) or len(lay) != 5:
        problems.append(f"{name}: struct flags {have}, gdb's ptype/o {lay}")
    enums = re.findall(r"= (-?\d+)", g.get("enum", ""))
    if [e[1] if e[1] < 2 ** 31 else e[1] - 2 ** 32 for e in got["enums"] or []] != [int(x) for x in enums]:
        problems.append(f"{name}: enum color {got['enums']}, gdb {enums}")
    # The member at every byte of gflags (dwWhere) is one gdb's ptype/o
    # puts there (bit fields share bytes), else padding.
    gf = vs.get("gflags")
    sizes = {"tag": 1, "big": 8}
    for k, path in enumerate(gf["where"] if gf else []):
        there = [x[0] for x in lay if (x[1] + x[3] // 8 <= k <= x[1] + (x[3] + x[2] - 1) // 8 if len(x) > 2 else
                                       x[1] <= k < x[1] + sizes.get(x[0], 0))]
        mem = re.match(r"gflags\.(\w+)", path or "")
        if (mem.group(1) not in there if mem else there or path != "gflags padding"):
            problems.append(f"{name}: gflags byte {k}: the page says {path!r}, gdb's ptype/o has {there or 'padding'}")
            break
    problems += dwf_locals_checks(name, elf, got, vas, gdb_scopes)
    return problems


def suite_dwarf_forms(ctx):
    return report("dwarf_forms", *dwarf_forms_case(ctx.browser, ctx.base, ctx.shots))


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
    "runbadge": (suite_runbadge, False),
    "memeip": (suite_memeip, False),
    "graphopen": (suite_graphopen, False),
    "srcfiles": (suite_srcfiles, False),
    "snapdiff": (suite_snapdiff, False),
    "deltasnap": (suite_deltasnap, False),
    "bookmarks": (suite_bookmarks, False),
    "tabs": (suite_tabs, False),
    "pwa": (suite_pwa, False),
    "followeip": (suite_followeip, False),
    "followfuzz": (suite_followfuzz, False),
    "nopath": (suite_nopath, False),
    "cfi": (suite_cfi, False),
    "dwarf_lto": (suite_dwarf_lto, False),
    "dwarf_forms": (suite_dwarf_forms, False),
    "tipmarks": (suite_tipmarks, False),
    "legend": (suite_legend, True),
    "jitdet": (suite_jitdet, False),
    "guestin": (suite_guestin, False),
    "exactstop": (suite_exactstop, False),
    "replay": (suite_replay, False),
    "reverse": (suite_reverse, False),
    "jitsnap": (suite_jitsnap, False),
    "jitrev": (suite_jitrev, False),
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
    # With MKB_JSCOV set: V8 coverage of the site's scripts (site/jscov.py).
    jscov.install("browser-" + re.sub(r"[^a-z0-9]+", "-", ",".join(names))[:60])
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
