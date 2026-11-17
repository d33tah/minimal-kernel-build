"""Browser test suite touch (test_browser.py --suites)."""

import pathlib
import runpy
import shutil
import tempfile
import time

from tests.browser.common import gate_on_screen, QuietHandler, report, serve, show_tab, SITE, wait_for


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


def stamp(html, v):
    """What the workflow's site/stamp.py does to index.html: asset?v=<commit>
    (an asset the workflow stamped already gets the new stamp)."""
    return runpy.run_path(str(SITE / "stamp.py"))["stamp_html"](html, v)


def stale_cache_case(browser, timeout):
    """Deploy A, load it, deploy B over it, reload with A in the HTTP cache.

    The server sends max-age=600 like GitHub Pages. Deploy B's app.bundle.js
    (app.js and its modules, build.mjs) sets a marker; after the reload the
    page must run B's bundle (not A's from the cache), boot, and raise no
    errors.
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
        app = (SITE / "app.bundle.js").read_text()

        stamp_js = runpy.run_path(str(SITE / "stamp.py"))["stamp_js"]

        def deploy(v):
            (root / "index.html").write_text(stamp(html, v))
            (root / "app.bundle.js").write_text(app + f'\nwindow.deployMarker = "{v}";\n')
            # The modules' imports get the stamp too (site/stamp.py).
            for f in root.glob("*.js"):
                f.write_text(stamp_js(f.read_text(), v))

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
        stamp_v = page.evaluate("new URL(document.querySelector('script[src*=\"app.bundle.js\"]').src).searchParams.get('v')")
        if marker != "B" or stamp_v != "B":
            problems.append(f"after the reload index.html is deploy {stamp_v} but app.bundle.js is deploy {marker}")
        if not wait_for(lambda: gate_on_screen(page), timeout):
            problems.append("deploy B did not boot after the reload")
        if errors:
            problems.append("page errors: " + "; ".join(errors))
        ctx.close()
        httpd.shutdown()
    return problems


def suite_touch(ctx):
    failed = report("touch", touch_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("stale_cache", stale_cache_case(ctx.browser, ctx.args.timeout))
