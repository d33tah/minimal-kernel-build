"""Browser test suite pwa (test_browser.py --suites)."""

import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import gate_on_screen, HELLO, kernel_entries, report, serve, show_tab, SITE, wait_for
from tests.browser.touch import CachingHandler, stamp


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
# The page's registration: each worker's state and script.
PWA_REG_STATE_JS = """() => Object.fromEntries(['installing', 'waiting', 'active'].map((k) => [k,
    MKB.PWA.reg && MKB.PWA.reg[k] ? [MKB.PWA.reg[k].state, MKB.PWA.reg[k].scriptURL] : null]))"""
# The deploy version.json names now, as the page's fetch gets it (a query the cache never saw).
PWA_VERSION_NOW_JS = """() => fetch('version.json?now=' + Date.now(), { cache: 'no-store' })
    .then((r) => (r.ok ? r.json() : null)).then((j) => (j && j.sha ? j.sha.slice(0, 12) : null), () => null)"""
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
        with open(root / "app.bundle.js", "a") as f:
            f.write(f'\nwindow.deployMarker = "{marker}";\n')


def pwa_server(root):
    """CachingHandler on root; state["down"] makes it answer nothing (offline),
    state["hold"] = (path, threading.Event()) holds the answer for that file
    until the event is set (at most 60 s); state["log"] lists the sw.js
    requests as (path, If-Modified-Since, mtime of the file then)."""
    state = {"down": False, "hold": None, "log": []}

    class Handler(CachingHandler):
        extensions_map = {**CachingHandler.extensions_map, ".webmanifest": "application/manifest+json"}

        def do_GET(self):
            if state["down"]:
                self.close_connection = True
                return
            # A deploy is a full answer at once: http.server compares
            # If-Modified-Since in whole seconds, so on a fast machine a
            # deploy within the second of the last one answered 304 to the
            # worker's update check (CI run 43566350950, case edges: deploy
            # B's sw.js never seen).
            del self.headers["If-Modified-Since"]
            hold = state["hold"]
            if hold and self.path.split("?")[0] == "/" + hold[0]:
                hold[1].wait(60)
            if self.path.split("?")[0].endswith("/sw.js"):
                f = root / "sw.js"
                state["log"].append([round(time.time(), 2), self.headers.get("If-Modified-Since"),
                                     round(f.stat().st_mtime, 2) if f.exists() else None])
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
    page.evaluate("MKB.openSourceFile('boot/hello.c')")
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
        if not live:
            # The new worker cannot finish installing (one file of its
            # precache is held until Reload has been pressed): Reload is
            # pressed while it installs, whatever the machine's speed.
            state["hold"] = ("kernels/minified-linux-os/disasm.json", threading.Event())
        c.set_offline(False)
        # The page checks version.json on the online event, but the
        # emulated network can reach the service worker a moment later
        # (CI run 43566350950: the worker's fetch failed, it answered the
        # cached version.json, no notice): once the worker gets the new
        # version.json, the page's check runs again as on that event.
        newer = (PWA_V[1] if not live else "f" * 12)
        reach = wait_for(lambda: page.evaluate(PWA_VERSION_NOW_JS) == newer, 30)
        if reach and not (page.locator("#pwa_update").count() and page.is_visible("#pwa_update")):
            page.evaluate("window.dispatchEvent(new Event('online'))")
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
            r["installing_at_reload"] = wait_for(lambda: page.evaluate("!!(MKB.PWA.reg && MKB.PWA.reg.installing)"), 30)
            if not r["installing_at_reload"]:
                problems.append("the new deploy's worker had installed before Reload (want Reload while it installs)")
            checks = []
            page.on("request", lambda q: "version.json" in q.url and checks.append(q.url))
            with page.expect_navigation(timeout=45000):
                page.click("#pwa_update button")
                # The tab shown again while it reloads: no update check then.
                page.evaluate("document.dispatchEvent(new Event('visibilitychange'))")
                time.sleep(1)
                r["checks_while_reloading"] = len(checks)
                state["hold"][1].set()
            state["hold"] = None
            if r["checks_while_reloading"]:
                problems.append(f"version.json fetched {len(checks)} times while the page reloads into the new deploy")
            if not wait_for(lambda: gate_on_screen(page), timeout):
                problems.append("after the update reload hello did not boot")
            r["after_reload"] = {
                "marker": page.evaluate("window.deployMarker"),
                "stamp": page.evaluate("new URL(document.querySelector('script[src*=\"app.bundle.js\"]').src).searchParams.get('v')"),
                "sw": page.evaluate(PWA_SW_VERSION_JS),
                "caches": page.evaluate(PWA_CACHES_JS),
            }
            a = r["after_reload"]
            if a["marker"] != "B" or a["stamp"] != PWA_V[1] or a["sw"] != PWA_V[1]:
                problems.append(f"after the update reload: app.bundle.js deploy {a['marker']}, stamp {a['stamp']}, service worker {a['sw']}")
            if any(PWA_V[0] in k for k in a["caches"]) or not any(PWA_V[1] in k for k in a["caches"]):
                problems.append(f"after the update the old deploy's caches are still there (or no new one): {a['caches']}")
            time.sleep(3)
            if page.locator("#pwa_update").count() and page.is_visible("#pwa_update"):
                problems.append("the update notice shows on the new deploy itself")
            pwa_after_update(page, base, state, timeout, problems, r)

    finally:
        if errors:
            problems.append(f"page errors: {errors[:3]}")
        if not live and state["hold"]:
            state["hold"][1].set()
        c.close()
        if tmp:
            httpd.shutdown()
            tmp.cleanup()
    return problems, r


PWA_RANGE_JS = """async ([path, range]) => {
    const resp = await fetch(MKB.withV(path), { headers: { Range: range } });
    const b = new Uint8Array(await resp.arrayBuffer());
    return { status: resp.status, range: resp.headers.get('content-range'), bytes: Array.from(b.slice(0, 16)), n: b.length };
}"""


def pwa_after_update(page, base, state, timeout, problems, r):
    """The new deploy's service worker (block coverage from its start, the
    first one's often begins late): a Range request for a cached file gets
    its bytes from the cache (206, a suffix range, 416 past the end); a
    POST is not answered by it; a precached file fetched again is not
    kept twice; with the origin's storage nearly full (a quota override) a
    fetched file is not kept, with room it is; offline again the page loads
    from its cache, hello boots, a file never fetched gets the offline
    note, a request to another origin is left to the network (it fails,
    no 503 from the worker), and without index.html in the cache the
    worker's own offline page answers."""
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
    pwa_store_checks(page, base, problems, r)
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
        other = base.replace("127.0.0.1", "localhost") + "/version.json"
        r["offline_fetches"] = page.evaluate(PWA_ANSWERED_JS, [other, "kernels/hello/not-there.bin"])
        if r["offline_fetches"] != ["failed", 503]:
            problems.append(f"offline: another origin's fetch, a file of this site never fetched: {r['offline_fetches']}, "
                            "want failed (not the worker's) and the worker's 503")
        page.evaluate("async (v) => (await caches.open('mkb-shell-' + v)).delete(new URL('index.html', location.href).href)", PWA_V[1])
        resp = page.goto(f"{base}/index.html?kernel=hello")
        r["offline_no_page"] = [resp.status if resp else None, (page.text_content("body") or "")[:80]]
        if r["offline_no_page"] != [503, "Offline: index.html is not available offline yet."]:
            problems.append(f"offline without index.html in the cache: {r['offline_no_page']}, want the worker's 503 page")
    finally:
        page.context.set_offline(False)
        state["down"] = False


# [another origin, a path of this site]: "failed" when the fetch rejects, else its status.
PWA_ANSWERED_JS = """async ([a, b]) => Promise.all([fetch(a, { mode: 'no-cors' }), fetch(b)].map((p) =>
    p.then((x) => x.status, () => 'failed')))"""
PWA_CACHED_JS = """async ([cache, path]) => !!(await (await caches.open(cache)).match(new URL(MKB.withV(path), location.href).href))"""


def pwa_store_checks(page, base, problems, r):
    """What the worker keeps of what the page fetches (online, deploy B)."""
    data = "mkb-data-" + PWA_V[1]
    r["manifest_refetch"] = page.evaluate("fetch('manifest.webmanifest').then((x) => x.status)")
    time.sleep(1)
    if r["manifest_refetch"] != 200 or page.evaluate(PWA_CACHED_JS, [data, "manifest.webmanifest"]):
        problems.append(f"the precached manifest fetched again ({r['manifest_refetch']}) was kept in {data} too")
    # Two files no view of this case fetched: one with the storage nearly
    # full, one with room.
    fresh = [f for f in ("kernels/hello/meta.json", "kernels/hello/dwarf/types.json", "kernels/hello/dwarf/files.json",
                         "kernels/hello/src/index.json")
             if not page.evaluate("async (p) => !!(await caches.match(new URL(MKB.withV(p), location.href).href))", f)][:2]
    if len(fresh) < 2:
        problems.append(f"no two files left that were never fetched: {fresh}")
        return
    cdp = page.context.new_cdp_session(page)
    origin = base + "/"
    cdp.send("Storage.overrideQuotaForOrigin", {"origin": origin, "quotaSize": 1})
    try:
        full = page.evaluate("async (p) => (await fetch(MKB.withV(p))).status", fresh[0])
        time.sleep(1.5)
        kept_full = page.evaluate(PWA_CACHED_JS, [data, fresh[0]])
    finally:
        cdp.send("Storage.overrideQuotaForOrigin", {"origin": origin})
    room = page.evaluate("async (p) => (await fetch(MKB.withV(p))).status", fresh[1])
    kept_room = wait_for(lambda: page.evaluate(PWA_CACHED_JS, [data, fresh[1]]), 10)
    r["store"] = {fresh[0] + " (storage full)": [full, kept_full], fresh[1]: [room, kept_room]}
    if full != 200 or kept_full or room != 200 or not kept_room:
        problems.append(f"files fetched online, kept in {data}: {r['store']}, want only the one fetched with room kept")


def pwa_edges(browser, shots, timeout):
    """Cases the main one does not reach (local only, they need their own
    deploys). No service worker can be registered (sw.js missing): the
    page says so in the console and boots as a plain page; then a worker
    that never answers which deploy it is: the page takes it after waiting
    for the answer. An insecure origin (no service worker API): a plain
    page. The next deploy without its sw.js: the old worker serves the new
    page online but does not keep it (offline the old deploy's page comes
    back whole), and Reload still reaches the new deploy. Another tab
    activates a newer deploy's worker (installed and waiting when it
    opens): a page of the old deploy, now controlled by it, shows the
    new-version notice."""
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
            # A worker that never answers the version question (busy, or a
            # sw.js without it): no answer within 3 s, the page takes it.
            # (site/ may be stamped already, as in CI: any V is replaced.)
            sw, nv = re.subn(r'^const V = "[^"]*";$', f'const V = "{PWA_V[0]}";', (SITE / "sw.js").read_text(), flags=re.M)
            mute = sw.replace('if (m.type === "version" && event.ports[0])', "if (false)")
            if mute == sw or nv != 1:
                problems.append("sw.js: no version answer or no V to replace")
            (root / "sw.js").write_text(mute)
            page.reload()
            controlled = pwa_controlled(page, 30)
            r["mute worker"] = [controlled, page.evaluate(PWA_SW_VERSION_JS), gate_on_screen(page),
                                (page.text_content("#pwa_notes") or "")[:80]]
            if r["mute worker"] != [True, "no answer", True, ""]:
                problems.append(f"a worker that does not answer: [controlled, its answer, booted, notes] {r['mute worker']}")
            # version.json answered with something that is not JSON (a
            # captive portal's page): no notice, no error.
            (root / "version.json").write_text("<html>Sign in to the network</html>")
            checks = []
            page.on("response", lambda q: q.url.split("?")[0].endswith("/version.json") and checks.append(q.status))
            page.evaluate("window.dispatchEvent(new Event('online'))")
            wait_for(lambda: checks, 10)
            time.sleep(0.5)
            r["version.json not JSON"] = [checks, (page.text_content("#pwa_notes") or "")[:80]]
            if r["version.json not JSON"] != [[200], ""]:
                problems.append(f"version.json not JSON: [statuses, notes] {r['version.json not JSON']}")
        finally:
            c.close()
            httpd.shutdown()

    pwa_edge_insecure(browser, timeout, problems, r, errors)
    pwa_edge_next_without_sw(browser, timeout, problems, r, errors)

    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        root = pathlib.Path(t)
        pwa_deploy(root, PWA_V[0], "A")
        httpd, state = pwa_server(root)
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
            # The old page's update check (its 5 minute timer) finds the new
            # worker first: installed and waiting when the new tab opens.
            old.evaluate("MKB.PWA.reg.update()")
            if not wait_for(lambda: old.evaluate("!!MKB.PWA.reg.waiting"), 60):
                problems.append("another tab: deploy B's worker did not install and wait: " + json.dumps({
                    "workers": old.evaluate(PWA_REG_STATE_JS), "sw.js requests": state["log"]}))
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


def pwa_edge_insecure(browser, timeout, problems, r, errors):
    """The site from an origin that is not a secure context (http on a
    host name: navigator.serviceWorker does not exist): hello boots, no
    worker, no note."""
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        root = pathlib.Path(t)
        pwa_deploy(root, PWA_V[0], "A")
        httpd, _ = pwa_server(root)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        c = browser.new_context(viewport={"width": 1280, "height": 900})
        host = "http://mkb.test"

        def proxy(route):
            try:
                route.fulfill(response=route.fetch(url=base + route.request.url[len(host):]))
            except PlaywrightError:
                pass  # the context closed while a request was on its way
        c.route(host + "/**", proxy)
        try:
            page = c.new_page()
            page.on("pageerror", lambda e: errors.append(str(e)[:200]))
            page.goto(host + "/index.html?kernel=hello")
            booted = wait_for(lambda: gate_on_screen(page), timeout)
            r["insecure origin"] = [booted, page.evaluate("[isSecureContext, 'serviceWorker' in navigator]"),
                                    (page.text_content("#pwa_notes") or "")[:80], (page.text_content("#status") or "")[:80]]
            if r["insecure origin"][:3] != [True, [False, False], ""]:
                problems.append(f"an insecure origin: [booted, [secure context, service worker API], notes, status] {r['insecure origin']}")
        finally:
            c.unroute_all(behavior="ignoreErrors")
            c.close()
            httpd.shutdown()


def pwa_edge_next_without_sw(browser, timeout, problems, r, errors):
    """Deploy B is live but its sw.js is not (missing): deploy A's worker
    gets B's page online and does not keep it (B's assets are not in A's
    caches), so offline A's whole page comes back. The A page's notice
    (version.json says B) and Reload: the update check fails, the page
    reloads into B anyway."""
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        root = pathlib.Path(t)
        pwa_deploy(root, PWA_V[0], "A")
        httpd, state = pwa_server(root)
        base = f"http://127.0.0.1:{httpd.server_address[1]}"
        c = browser.new_context(viewport={"width": 1280, "height": 900})
        try:
            a = c.new_page()
            a.on("pageerror", lambda e: errors.append(str(e)[:200]))
            a.goto(f"{base}/index.html?kernel=hello")
            if not wait_for(lambda: gate_on_screen(a), timeout) or not pwa_controlled(a):
                problems.append("next deploy without sw.js: the deploy A page did not boot under its worker")
                return
            pwa_deploy(root, PWA_V[1], "B")
            (root / "sw.js").unlink()
            b = c.new_page()
            b.on("pageerror", lambda e: errors.append(str(e)[:200]))
            b.goto(f"{base}/index.html?kernel=hello")
            online = [wait_for(lambda: gate_on_screen(b), timeout), b.evaluate("window.deployMarker"), b.evaluate(PWA_SW_VERSION_JS)]
            state["down"] = True
            try:
                b.goto(f"{base}/index.html?kernel=hello")
                offline = [wait_for(lambda: gate_on_screen(b), timeout), b.evaluate("window.deployMarker")]
            finally:
                state["down"] = False
            r["next deploy without sw.js"] = {"online": online, "offline": offline}
            if online != [True, "B", PWA_V[0]] or offline != [True, "A"]:
                problems.append(f"deploy B without its sw.js: online [booted, deploy, worker] {online}, "
                                f"offline [booted, deploy] {offline}; want B under A's worker, then A's cached page")
            b.close()
            a.evaluate("window.dispatchEvent(new Event('online'))")
            if not wait_for(lambda: a.locator("#pwa_update").count() and a.is_visible("#pwa_update"), 30):
                problems.append(f"deploy B without its sw.js: no notice on the A page: {a.text_content('#pwa_notes')!r}")
                return
            with a.expect_navigation(timeout=30000):
                a.click("#pwa_update button")
            r["reload without sw.js"] = [wait_for(lambda: gate_on_screen(a), timeout), a.evaluate("window.deployMarker"),
                                         a.evaluate(PWA_SW_VERSION_JS)]
            if r["reload without sw.js"] != [True, "B", PWA_V[0]]:
                problems.append(f"Reload with deploy B's sw.js missing: [booted, deploy, worker] {r['reload without sw.js']}")
        finally:
            c.close()
            httpd.shutdown()


def suite_pwa(ctx):
    base = ctx.base if ctx.args.base else None
    failed = report("pwa", *pwa_case(ctx.browser, base, ctx.shots, ctx.args.timeout))
    if not base:
        failed |= report("pwa edges", *pwa_edges(ctx.browser, ctx.shots, ctx.args.timeout))
    return failed
