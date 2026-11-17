#!/usr/bin/env python3
"""UI interaction benchmark: the same user actions on several builds of the site, interleaved.

usage: site/bench_ui.py [--reps N] [--json OUT] LABEL=SITEDIR [LABEL=SITEDIR ...]
       site/bench_ui.py --redraw [--reps N] LABEL=SITEDIR [LABEL=SITEDIR ...]

A SITEDIR is a built site with its kernels (the CI artifact "site"); the first LABEL is the
baseline the others are compared with. Runs alternate between the sites (A B, B A, ...), one
browser at a time. Needs: pip install playwright && playwright install chromium.

Per run (a fresh context, service workers blocked, 1280x1000): minified Linux opened paused with
the Events tab (it records from then on), Run to start_kernel, then:
  tab      a click on a tab (8 tabs, 4 rounds)
  chip     a click on an Events filter chip (4 Who and 3 Access chips, 4 rounds)
  zoom     a wheel step on the Calls timeline (24 steps, in and out)
  srcopen  a click on the Run to status's file:line link with the Source panel closed (10 times)
  typing   one key typed in the Source editor (40 keys)
Each action is real input (CDP mouse/keyboard); positions are read before the measured window.
work: the renderer main thread's CPU time (CDP Performance TaskDuration, threadTicks) in a fixed
window after the action (tab 0.3 s, chip 0.25, zoom 0.25 (the Calls tab redraws on a 100 ms
timer), srcopen 0.5, typing 0.12), minus the median of 5 idle windows of the same length just
before, in the same state. lat: from the input's capture listener on window to the end of the
next frame (a rAF, then a setTimeout 0), so it moves in steps of a frame.
Summary per action: for each target (tab, chip, key...) the median of its samples over all runs,
then the mean over targets.

--redraw: the Calls timeline's zoom and redraw alone, without the window's noise: after Run to
start_kernel, in the page, a wheel event on #graph_canvas (in, then out at the same point; the
page's own handler) and the Calls tick (graphTick, which redraws because the zoom set G.dirty),
100 per batch, 7 batches, on the real clock (clock.js replaces performance.now). Per run: the
median batch's ms per zoom and redraw.
"""

import argparse
import json
import pathlib
import statistics
import time

from playwright.sync_api import sync_playwright

from tests.browser.common import serve, wait_for

INIT = """(() => {
  // clock.js replaces performance.now (guest time): keep the real one.
  const now = window.__now = performance.now.bind(performance);
  const raf = window.requestAnimationFrame.bind(window);
  const st = window.setTimeout.bind(window);
  const B = window.__b = { armed: false, lat: [] };
  for (const type of ["click", "keydown", "wheel"])
    window.addEventListener(type, () => {
      if (!B.armed) return;
      const t = now();
      raf(() => st(() => B.lat.push(now() - t), 0));
    }, true);
})();"""

REDRAW_JS = """(n) => {
  const tick = (window.MKB && MKB.graphTick) || graphTick;
  const c = document.getElementById('graph_canvas');
  const r = c.getBoundingClientRect();
  const x = r.left + r.width / 2, y = r.top + r.height / 2;
  const t0 = __now();
  for (let i = 0; i < n; i++) {
    c.dispatchEvent(new WheelEvent('wheel', { deltaY: i % 2 ? 240 : -240, clientX: x, clientY: y, bubbles: true, cancelable: true }));
    tick();
  }
  return (__now() - t0) / n;
}"""

TABS = ["page", "memory", "calls", "events", "source", "machine", "snapshots", "debugger"]
ACTIONS = ("tab", "chip", "zoom", "srcopen", "typing")


class Meter:
    def __init__(self, page, cdp):
        self.page, self.cdp = page, cdp

    def cpu(self):
        m = {x["name"]: x["value"] for x in self.cdp.send("Performance.getMetrics")["metrics"]}
        return m["TaskDuration"] * 1000.0

    def idle(self, window, n=5):
        out = []
        for _ in range(n):
            a = self.cpu()
            time.sleep(window)
            out.append(self.cpu() - a)
        return statistics.median(out)

    def act(self, fn, window, idle):
        self.page.evaluate("__b.lat = []; __b.armed = true")
        a = self.cpu()
        fn()
        time.sleep(window)
        b = self.cpu()
        lat = self.page.evaluate("__b.armed = false; __b.lat")
        return round(b - a - idle, 3), (round(lat[0], 2) if lat else None)


def center(page, sel):
    page.locator(sel).first.scroll_into_view_if_needed()
    bx = page.locator(sel).first.bounding_box()
    return bx["x"] + bx["width"] / 2, bx["y"] + bx["height"] / 2


def start(browser, base, vaddr, tab):
    """A fresh context paused at the reset vector with `tab` shown, then Run to start_kernel."""
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, service_workers="block")
    ctx.add_init_script(INIT)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#{tab}")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 90):
        raise SystemExit(f"{base}: not paused")
    page.click("#tab_debugger")
    page.fill("#dbg_addr", hex(vaddr))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.inner_text("#dbg_status").startswith("Stopped at"), 300, 0.5):
        raise SystemExit(f"{base}: Run to did not stop: {page.inner_text('#dbg_status')!r}")
    return ctx, page, errors


def run(browser, base, vaddr):
    ctx, page, errors = start(browser, base, vaddr, "events")
    cdp = ctx.new_cdp_session(page)
    cdp.send("Performance.enable", {"timeDomain": "threadTicks"})
    m = Meter(page, cdp)
    res = {k: [] for k in ACTIONS}
    wait_for(lambda: page.locator("#dbg_status .srclink").count() > 0, 30)
    time.sleep(3)

    def click_at(xy):
        return lambda: page.mouse.click(*xy)

    idle = m.idle(0.3)
    for _ in range(4):
        for t in TABS:
            res["tab"].append([t, *m.act(click_at(center(page, f"#tab_{t}")), 0.3, idle)])
    page.mouse.click(*center(page, "#tab_events"))
    time.sleep(1)
    chips = [f'#mev_who .mev-chip[data-v="{v}"]' for v in ("cpu", "device", "emulator", "")] + \
            [f'#mev_rw .mev-chip[data-v="{v}"]' for v in ("write", "read", "")]
    res["events_rows"] = page.locator("#mev_rows tr").count()
    idle = m.idle(0.25)
    for _ in range(4):
        for c in chips:
            res["chip"].append([c, *m.act(click_at(center(page, c)), 0.25, idle)])
    page.mouse.click(*center(page, "#tab_calls"))
    time.sleep(1.5)
    page.mouse.move(*center(page, "#graph_canvas"))
    idle = m.idle(0.25)
    for k in range(24):
        dy = -240 if k % 2 == 0 else 240
        res["zoom"].append(["in" if dy < 0 else "out", *m.act(lambda dy=dy: page.mouse.wheel(0, dy), 0.25, idle)])
    page.mouse.click(*center(page, "#tab_debugger"))
    time.sleep(1)
    for k in range(10):
        if page.is_visible("#src_panel"):
            page.click("#src_close")
            time.sleep(0.5)
        if k == 0:
            idle = m.idle(0.5)
        res["srcopen"].append(["link", *m.act(click_at(center(page, "#dbg_status .srclink")), 0.5, idle)])
        if not wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 20):
            raise SystemExit(f"{base}: the Source panel did not open")
    page.click("#src_edit")
    if not wait_for(lambda: page.is_visible("#ed_text"), 20):
        raise SystemExit(f"{base}: no editor")
    page.evaluate("() => { const t = document.getElementById('ed_text'); t.focus(); "
                  "const at = t.value.indexOf('\\n', t.value.length >> 1) + 1; t.setSelectionRange(at, at); }")
    time.sleep(1)
    idle = m.idle(0.12)
    for ch in "abcdefghij" * 4:
        res["typing"].append(["key", *m.act(lambda ch=ch: page.keyboard.press(ch), 0.12, idle)])
    res["errors"] = errors
    ctx.close()
    return res


def redraw_run(browser, base, vaddr):
    ctx, page, _ = start(browser, base, vaddr, "debugger")
    page.click("#tab_calls")
    time.sleep(2)
    page.evaluate(REDRAW_JS, 20)
    out = [page.evaluate(REDRAW_JS, 100) for _ in range(7)]
    ctx.close()
    return statistics.median(out)


def summary(runs, k, col):
    by = {}
    for r in runs:
        for s in r[k]:
            if s[col] is not None:
                by.setdefault(s[0], []).append(s[col])
    return statistics.mean(statistics.median(v) for v in by.values()) if by else None


def start_kernel(d):
    k = {x["name"]: x for x in json.loads((pathlib.Path(d) / "kernels.json").read_text())["kernels"]}["minified-linux"]
    syms = json.loads((pathlib.Path(d) / k["symbols"]).read_text())["symbols"]
    return next(s["vaddr"] for s in syms if s["name"] == "start_kernel")


def main():
    ap = argparse.ArgumentParser(description="UI interaction benchmark (see the docstring)")
    ap.add_argument("sites", nargs="+", help="LABEL=SITEDIR; the first is the baseline")
    ap.add_argument("--reps", type=int, default=12)
    ap.add_argument("--json", help="write every sample here")
    ap.add_argument("--redraw", action="store_true", help="only the Calls zoom + redraw loop")
    args = ap.parse_args()
    sites = [a.split("=", 1) for a in args.sites]
    servers = {lab: serve(pathlib.Path(d)) for lab, d in sites}
    vaddr = {lab: start_kernel(d) for lab, d in sites}
    allres = {lab: [] for lab, _ in sites}
    base = sites[0][0]
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for rep in range(args.reps):
            for lab, _ in (sites if rep % 2 == 0 else sites[::-1]):
                t0 = time.monotonic()
                url = f"http://127.0.0.1:{servers[lab].server_address[1]}"
                if args.redraw:
                    v = redraw_run(browser, url, vaddr[lab])
                    allres[lab].append(v)
                    print(f"rep {rep} {lab} {v:.3f} ms", flush=True)
                    continue
                r = run(browser, url, vaddr[lab])
                allres[lab].append(r)
                med = {k: round(summary([r], k, 1), 2) for k in ACTIONS}
                print(f"rep {rep} {lab} {time.monotonic() - t0:.0f} s work {med} events rows {r['events_rows']} "
                      f"page errors {r['errors'][:2]}", flush=True)
                if args.json:
                    pathlib.Path(args.json).write_text(json.dumps(allres))
        browser.close()
    if args.redraw:
        b = statistics.median(allres[base])
        for lab, _ in sites:
            v = statistics.median(allres[lab])
            print(f"{lab}: median {v:.3f} ms per zoom + redraw ({(v / b - 1) * 100:+.1f}%), runs {sorted(round(x, 3) for x in allres[lab])}")
        return
    print("per target median over all runs, mean over targets (work: CPU ms; lat: ms to the next frame's end)")
    for k in ACTIONS:
        line = [f"{k:8}"]
        w0, l0 = summary(allres[base], k, 1), summary(allres[base], k, 2)
        for lab, _ in sites:
            w, lt = summary(allres[lab], k, 1), summary(allres[lab], k, 2)
            line.append(f"{lab} work {w:6.2f}" + (f" lat {lt:5.1f}" if lt is not None else ""))
            if lab != base:
                line.append(f"({(w / w0 - 1) * 100:+.1f}% work" + (f", {(lt / l0 - 1) * 100:+.1f}% lat)" if lt and l0 else ")"))
        print("  ".join(line))


if __name__ == "__main__":
    main()
