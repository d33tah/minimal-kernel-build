#!/usr/bin/env python3
"""Boot kernels on the page in headless Chromium and check the VGA screen.

Serves site/ with python's http.server on a free port, opens
index.html?kernel=NAME&mem=BYTES, waits until v86's text-mode screen (DOM
text inside #screen_container) shows "Hello, World!" and then "Still alive",
scans guest memory with the page's button and checks the non-zero page
count is above 0 and below the RAM size. Saves screenshots.

Usage:
  site/test_browser.py                      hello, plus minified-linux if present
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


def default_cases():
    names = [k["name"] for k in json.loads((SITE / "kernels.json").read_text())["kernels"]]
    cases = [("hello", None)]
    if "minified-linux" in names:
        cases.append(("minified-linux", 18 * 1024 * 1024))
    return cases


def run_case(browser, base, name, mem, timeout, shots):
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
    if not page.is_disabled("#scan"):
        page.click("#scan")
        nonzero = int(page.get_attribute("#footprint", "data-nonzero"))
        total = int(page.get_attribute("#footprint", "data-total"))
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
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for name, mem in cases:
            r = run_case(browser, base, name, mem, args.timeout, shots)
            print(json.dumps(r))
            good = r["ok"] and r["nonzero_pages"] and 0 < r["nonzero_pages"] < r["total_pages"]
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
