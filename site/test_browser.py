#!/usr/bin/env python3
"""Boot kernels on the page in headless Chromium and check the VGA screen.

Serves site/ with python's http.server on a free port, opens
index.html?kernel=NAME&mem=BYTES, waits until v86's text-mode screen (DOM
text inside #screen_container) shows "Hello, World!" and then "Still alive",
scans guest memory with the page's button and checks the non-zero page
count is above 0 and below the RAM size. Then the memory lab:

  - clicks the page at 1 MiB (the first page of the kernel) in the grid and
    checks the hex dump has non-zero bytes, and, when the kernel has a
    symbols.json, that the page's symbol table and hover line are not empty;
  - turns on "live" and checks the dump is re-read;
  - saves a snapshot and checks gzip made it smaller and the ratio is shown;
  - blanks the screen's DOM, restores the snapshot and checks both lines
    come back.

Saves screenshots.

Usage:
  site/test_browser.py                      hello at 2 MB, plus minified-linux at
                                            18 MB and 2180 KiB if present
  site/test_browser.py hello:1048576 minified-linux:18874368
  site/test_browser.py --probe hello:655360 report only, never fail

Needs: pip install playwright && playwright install chromium
"""

import argparse
import functools
import http.server
import json
import pathlib
import socketserver
import sys
import threading
import time

from playwright.sync_api import sync_playwright

SITE = pathlib.Path(__file__).resolve().parent
HELLO = "Hello, World!"
ALIVE = "Still alive"


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(QuietHandler, directory=str(SITE))
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd


def kernel_entries():
    return {k["name"]: k for k in json.loads((SITE / "kernels.json").read_text())["kernels"]}


def default_cases():
    names = kernel_entries()
    cases = [("hello", None)]
    if "minified-linux" in names:
        cases.append(("minified-linux", 18 * 1024 * 1024))
        # The smallest RAM it boots with in v86 (measured: 2176 KiB fails).
        cases.append(("minified-linux", 2180 * 1024))
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
    cols = int(page.get_attribute("#pages", "data-cols"))
    cell = int(page.get_attribute("#pages", "data-cell"))
    width = int(page.eval_on_selector("#pages", "e => e.width"))
    page.locator("#pages").scroll_into_view_if_needed()
    box = page.locator("#pages").bounding_box()
    scale = box["width"] / width
    return (box["x"] + ((p % cols) * cell + cell / 2) * scale,
            box["y"] + ((p // cols) * cell + cell / 2) * scale)


def memory_lab(page, has_symbols, problems):
    """Exercise inspector and snapshots; append failures to problems."""
    r = {}
    kpage = 0x100000 // 4096
    x, y = page_center(page, kpage)
    page.mouse.move(x, y)
    r["hover"] = page.inner_text("#hover_info")
    page.mouse.click(x, y)
    if not wait_for(lambda: page.get_attribute("#hexdump", "data-page") == str(kpage), 5):
        problems.append("clicking the 1 MiB page did not fill the hex dump")
        return r
    r["dump_nonzero"] = int(page.get_attribute("#hexdump", "data-nonzero"))
    r["page_symbols"] = int(page.get_attribute("#page_symbols", "data-count") or 0)
    if r["dump_nonzero"] <= 0:
        problems.append("hex dump of the 1 MiB page is all zero")
    if has_symbols and r["page_symbols"] <= 0:
        problems.append("symbols.json exists but the 1 MiB page lists no symbols")
    if has_symbols and "startup_32" not in r["hover"]:
        problems.append("hover line does not name startup_32: " + r["hover"])

    reads = int(page.get_attribute("#hexdump", "data-reads"))
    page.check("#live")
    time.sleep(1.6)
    page.uncheck("#live")
    r["live_reads"] = int(page.get_attribute("#hexdump", "data-reads")) - reads
    if r["live_reads"] < 2:
        problems.append(f"live mode re-read the page {r['live_reads']} times in 1.6 s")

    page.click("#scan")
    r["changed_pages"] = page.get_attribute("#footprint", "data-changed")
    if r["changed_pages"] in (None, ""):
        problems.append("second scan did not report changed pages")

    page.click("#snap_save")
    if not wait_for(lambda: page.locator("#snapshots tbody tr").count() >= 1, 30):
        problems.append("Save snapshot added no row: " + page.inner_text("#snap_status"))
        return r
    row = page.locator("#snapshots tbody tr").first
    r["snapshot_raw"] = int(row.get_attribute("data-raw"))
    r["snapshot_gzip"] = int(row.get_attribute("data-stored"))
    r["snapshot_ratio"] = row.locator("td.ratio").inner_text()
    r["snapshot_status"] = page.inner_text("#snap_status")
    if not r["snapshot_gzip"] < r["snapshot_raw"]:
        problems.append("compressed snapshot is not smaller than the raw state")
    if ":" not in r["snapshot_ratio"]:
        problems.append("snapshot ratio not displayed: " + r["snapshot_ratio"])

    # Blank the text screen's DOM; only the restore can bring the lines back
    # (both kernels halt after printing).
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


def run_case(browser, base, name, mem, timeout, shots, has_symbols):
    page = browser.new_page(viewport={"width": 1100, "height": 1400})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel={name}" + (f"&mem={mem}" if mem else "")
    page.goto(url)
    start = time.monotonic()
    text = ""
    ok = False
    while time.monotonic() - start < timeout:
        text = page.inner_text("#screen_container")
        h = text.find(HELLO)
        if h >= 0 and text.find(ALIVE, h + len(HELLO)) >= 0:
            ok = True
            break
        time.sleep(0.5)
    elapsed = time.monotonic() - start
    ram = int(page.eval_on_selector("#memory", "e => e.value"))
    status = page.inner_text("#status")
    problems = page.inner_text("#problems")
    nonzero = total = None
    lab, lab_problems = {}, []
    if not page.is_disabled("#scan"):
        page.click("#scan")
        nonzero = int(page.get_attribute("#footprint", "data-nonzero"))
        total = int(page.get_attribute("#footprint", "data-total"))
        if ok:
            lab = memory_lab(page, has_symbols, lab_problems)
    tag = f"{name}-{ram // 1024}k"
    page.locator("#screen_container").screenshot(path=str(shots / f"screen-{tag}.png"))
    page.screenshot(path=str(shots / f"page-{tag}.png"), full_page=True)
    page.close()
    return {
        "kernel": name, "ram_kib": ram // 1024, "ok": ok,
        "seconds": round(elapsed, 1), "nonzero_pages": nonzero,
        "total_pages": total, "status": status, "problems": problems,
        "page_errors": errors,
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
        name, _, mem = c.partition(":")
        cases.append((name, int(mem) if mem else None))
    cases = cases or default_cases()
    shots = pathlib.Path(args.shots)
    shots.mkdir(parents=True, exist_ok=True)

    httpd = serve()
    base = f"http://127.0.0.1:{httpd.server_address[1]}"
    failed = False
    entries = kernel_entries()
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, mem in cases:
            has_symbols = "symbols" in entries.get(name, {})
            r = run_case(browser, base, name, mem, args.timeout, shots, has_symbols)
            print(json.dumps(r))
            good = (r["ok"] and r["nonzero_pages"] and 0 < r["nonzero_pages"] < r["total_pages"]
                    and not r["lab_problems"] and not r["page_errors"])
            if not good:
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
