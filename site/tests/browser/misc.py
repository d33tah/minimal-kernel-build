"""Browser test suite misc (test_browser.py --suites)."""

import json
import os
import re
import time

from tests.browser.common import counters, gate_on_screen, kernel_entries, report, show_canvas, show_tab, SITE, wait_for
from tests.browser.misc_import import import_case, import_edges
from tests.browser.misc_initrd import header_check_case, initrd_case, third_party_case
from tests.browser.misc_panic import distro_panic_case, panic_text_case


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


def follow_case(browser, base, shots):
    """Slow motion, transposed timeline on a phone: "follow" keeps the
    running frame's column on screen; after a drag, ticking "follow" again
    slides the window with the run, and the call tree is rebuilt every 2 s;
    the cmdline default is shown."""
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
            scrolled = max(scrolled, page.evaluate("MKB.G.xOff"))
    r["visible"], r["rows"], r["scrolled_px"] = seen, rows, scrolled
    if not seen or any(v != "true" for v in seen):
        problems.append(f"the running frame left the view while following: {seen} rows {rows}")
    # At 390 px, three 120 px columns fit; a call in column 2 or deeper is
    # outside the band and must have scrolled the view.
    if not rows or max(rows) < 2 or scrolled <= 0:
        problems.append(f"following never had to scroll: rows {rows}, scrolled {scrolled}px")
    page.screenshot(path=str(shots / "follow-transposed-390.png"))
    page.close()
    # At 1k/s: a drag stops following (the window stays behind the run);
    # ticking "follow" again slides the window along with the run, its span kept.
    page = browser.new_page(viewport={"width": 1100, "height": 900})
    page.goto(f"{base}/index.html?kernel=minified-linux#calls")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return problems + ["1k/s: no kernel entry"], r
    time.sleep(2)
    show_canvas(page)
    box = page.locator("#graph_canvas").bounding_box()
    cx, cy = box["x"] + box["width"] / 2, box["y"] + 150
    page.mouse.move(cx, cy)
    page.mouse.down()
    page.mouse.move(cx + 100, cy, steps=4)
    page.mouse.up()
    time.sleep(1)
    left = page.evaluate("[MKB.G.follow, document.getElementById('graph_follow').checked, MKB.G.x0, MKB.G.x1, MKB.G.stepper.steps, MKB.runState]")
    page.check("#graph_follow")
    time.sleep(1)
    back = page.evaluate("[MKB.G.follow, MKB.G.x0, MKB.G.x1, MKB.G.stepper.steps, MKB.runState]")
    r["follow again"] = [left, back]
    if left[0] or left[1] or not left[4] > left[3] or left[5] != "stepped":
        problems.append(f"after a drag: [follow, ticked, x0, x1, steps, run state] {left}, want not following with the run past the window")
    elif not back[0] or not back[2] > left[3] or back[3] - back[2] > 300 or \
            abs((back[2] - back[1]) - (left[3] - left[2])) > 1e-6 * (left[3] - left[2]):
        problems.append(f"follow ticked again: [follow, x0, x1, steps] {back[:4]}, want the window's end moved from {left[3]:.0f} "
                        f"to within 300 instructions of the run, its span {left[3] - left[2]:.0f} kept")
    # The call tree while stepping: rebuilt at most every 2 s.
    page.click("#gview_tree")
    t0 = time.monotonic()
    incl = []
    for at in (0.4, 1.5, 2.8):
        time.sleep(max(0, t0 + at - time.monotonic()))
        incl.append(int(page.get_attribute("#graph_tree", "data-root-incl") or -1))
    r["tree while stepping"] = incl
    if not 0 < incl[0] == incl[1] < incl[2]:
        problems.append(f"the call tree's root count at 0.4, 1.5 and 2.8 s while stepping at 1k/s: {incl}, want a rebuild only after 2 s")
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


def fetch_fail_case(browser, base, shots):
    """A kernel or its initrd the server does not have (404) is named with
    the HTTP status, and an error nothing else catches (a rejection, an
    exception) shows in the status line with the kernel and RAM (hello at 1280 light, service worker off
    so the server's answer reaches the page); a kernel picked in the
    Machine tab that the server does not have names its file too, and a
    kernel's own initrd goes when another kernel is picked."""
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
            # Its initrd (a small stand-in) goes with it when another
            # kernel is picked.
            ctx.unroute("**/" + kernel_entries()[initrds[0]]["initrd"] + "*")
            ctx.route("**/" + kernel_entries()[initrds[0]]["initrd"] + "*", lambda route: route.fulfill(status=200, body=bytes(512)))
            page.goto(f"{base}/index.html?kernel={initrds[0]}&paused=1")
            had = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                           and page.evaluate("!!MKB.initrd && MKB.initrd.entry") == initrds[0], 60)
            show_tab(page, "machine")
            page.select_option("#kernel", "hello-os")  # (hello's bzImage answers 404 here)
            wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == "hello-os" and "Running hello-os" in (page.text_content("#status") or ""), 60)
            r["initrd after the switch"] = [had, page.evaluate("MKB.initrd"), page.inner_text("#initrd_status")]
            if r["initrd after the switch"] != [True, None, ""]:
                problems.append(f"{initrds[0]} then hello-os picked: [had its initrd, initrd, note] {r['initrd after the switch']}")
            ctx.unroute("**/" + kernel_entries()[initrds[0]]["initrd"] + "*")
        ctx.unroute("**/kernels/hello/bzImage*")
        page.goto(f"{base}/index.html?kernel=hello&paused=1")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.evaluate("setTimeout(() => Promise.reject(new Error('nobody catches this')), 0)")
        wait_for(lambda: "nobody catches this" in (page.text_content("#status") or ""), 10)
        r["uncaught"] = page.text_content("#status")
        if not re.match(r"Emulator error: Error: nobody catches this \(hello, 2,048 KiB RAM\)", r["uncaught"] or ""):
            problems.append(f"an uncaught rejection: status {r['uncaught']!r}")
        page.evaluate("setTimeout(() => { throw new Error('nobody catches this throw'); }, 0)")
        wait_for(lambda: "nobody catches this throw" in (page.text_content("#status") or ""), 10)
        r["uncaught throw"] = page.text_content("#status")
        if r["uncaught throw"] != "Emulator error: Uncaught Error: nobody catches this throw (hello, 2,048 KiB RAM)":
            problems.append(f"an uncaught exception: status {r['uncaught throw']!r}")
        # A kernel picked in the Machine tab that the server does not have.
        ctx.route("**/kernels/hello-os/bzImage*", lambda route: route.fulfill(status=404, body="no"))
        show_tab(page, "machine")
        page.select_option("#kernel", "hello-os")
        wait_for(lambda: "HTTP 404" in (page.text_content("#status") or ""), 30)
        r["picked 404"] = page.text_content("#status")
        if r["picked 404"] != "Error: kernels/hello-os/bzImage: HTTP 404":
            problems.append(f"hello-os picked, its bzImage answered 404: status {r['picked 404']!r}")
        fetch_fail_lists(ctx, base, problems, r)
        other = [e for e in errors if "nobody catches this" not in e]
        if other:
            problems.append(f"page errors besides the test's own: {other[:3]}")
    finally:
        ctx.close()
    return problems, r


def fetch_fail_lists(ctx, base, problems, r):
    """kernels.json missing or listing nothing: the status says to load a
    bzImage from disk. hello's symbols.json missing (404), not JSON, or not
    in its kernels.json entry, and its disasm.json not JSON or not listed:
    hello still runs, without symbols (Run to hello_main says the kernel
    has none) and the console names the file that did not parse; with
    version.json unreachable the header says "local build"; Boot without
    a kernel does nothing."""
    entries = json.loads((SITE / "kernels.json").read_text())
    warns = []

    def answer(status, body):
        return lambda route: route.fulfill(status=status, body=body, content_type="application/json")
    ctx.on("console", lambda m: m.type == "warning" and warns.append(m.text[:120]))
    for body, want in (("no", "No kernels.json ("), ('{"kernels": []}', "kernels.json lists no kernels. Load a bzImage from disk.")):
        ctx.route("**/kernels.json*", answer(404 if body == "no" else 200, body))
        page = ctx.new_page()
        page.goto(f"{base}/index.html?kernel=hello")
        wait_for(lambda: (page.text_content("#status") or "").startswith(want), 30)
        show_tab(page, "machine")
        page.select_option("#memory", str(4 << 20))  # no kernel: no header to check
        page.click("#boot")  # no kernel: nothing to boot
        time.sleep(0.5)
        st = page.text_content("#status") or ""
        r["kernels.json " + body[:8]] = st
        if not st.startswith(want) or not st.endswith("Load a bzImage from disk.") or page.evaluate("!!MKB.emulator"):
            problems.append(f"kernels.json {body!r}, Boot pressed: status {st!r}, want {want!r}... and no machine")
        page.close()
        ctx.unroute("**/kernels.json*")
    hello = next(e for e in entries["kernels"] if e["name"] == "hello")
    bare = dict(entries, kernels=[{k: v for k, v in hello.items() if k not in ("symbols", "disasm")}])
    for label, routes, warn, has_disasm in (
            ("symbols.json 404, disasm.json not JSON", {"symbols.json": (404, "no"), "disasm.json": (200, "{")}, "disasm:", False),
            ("symbols.json not JSON", {"symbols.json": (200, "{")}, "symbols:", True),
            ("not listed in kernels.json", {"kernels.json": (200, json.dumps(bare))}, None, False)):
        # version.json unreachable too (the request fails): "local build".
        ctx.route("**/version.json*", lambda route: route.abort())
        for f, (status, body) in routes.items():
            ctx.route(("**/kernels/hello/" if f != "kernels.json" else "**/") + f + "*", answer(status, body))
        del warns[:]
        page = ctx.new_page()
        page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.fill("#dbg_addr", "hello_main")
        page.click("#dbg_runto")
        want = "Run to hello_main: no symbol of that name (this kernel has no symbols). Run to takes a function name, a hex address or file:line."
        wait_for(lambda: page.inner_text("#dbg_status") == want, 10)
        got = [ok, page.inner_text("#dbg_status"), page.evaluate("!!MKB.disasm"), warns[:2], page.inner_text("#build_info")]
        r[label] = got
        if not ok or got[1] != want or got[2] != has_disasm or (warn and not any(w.startswith(warn) for w in warns)) or got[4] != "local build":
            problems.append(f"hello with {label}: {got}, want paused at the reset vector, {want!r}, "
                            f"disasm.json used {has_disasm}, a console warning {warn}")
        page.close()
        for f in routes:
            ctx.unroute(("**/kernels/hello/" if f != "kernels.json" else "**/") + f + "*")
        ctx.unroute("**/version.json*")


GLOBALS_JS = """() => {
  const f = document.createElement('iframe');
  document.body.appendChild(f);
  const blank = new Set(Object.getOwnPropertyNames(f.contentWindow));
  f.remove();
  let readOnly = null;
  try { MKB.hex = 1; } catch (e) { readOnly = e.message; }
  return { own: Object.getOwnPropertyNames(window).filter((k) => !blank.has(k)).sort(), readOnly,
    scripts: [...document.scripts].map((s) => (s.type || 'classic') + ' ' + s.getAttribute('src')) };
}"""
# The page's own globals: window.MKB and libv86.js's V86 (a classic script
# of v86); the rest are ES modules (R1).
GLOBALS_WANT = ["MKB", "V86"]


def globals_case(browser, base):
    """After a boot the page adds no global of its own but MKB (the tests'
    handle, debug.js) and v86's V86; MKB's names without a setter refuse
    assignments."""
    problems, r = [], {}
    page = browser.new_page()
    page.goto(base + "/index.html?kernel=hello")
    page.wait_for_function("!!window.MKB && !!MKB.stepper", timeout=60000)
    g = page.evaluate(GLOBALS_JS)
    r.update(g)
    if g["own"] != GLOBALS_WANT:
        problems.append(f"own globals {g['own']}, want {GLOBALS_WANT}")
    if g["readOnly"] != "MKB.hex has no setter":
        problems.append(f"MKB.hex = 1: {g['readOnly']!r}")
    if [s.split("?")[0] for s in g["scripts"]] != ["module clock.js", "classic v86/libv86.js", "module app.bundle.js"]:
        problems.append(f"index.html loads {g['scripts']}: the modules clock.js (before v86) and app.bundle.js (app.js "
                        "and its imports, build.mjs), and v86/libv86.js")
    page.close()
    return problems, r


STORE_JS = """async () => {
  const s = MKB.state, out = { told: [], keyed: [] };
  const snap0 = s.getSnapshot();
  out.sameSnap = s.getSnapshot() === snap0;
  out.snapRunState = snap0.runState;
  out.snapStepper = snap0.stepper === MKB.stepper;
  const off = s.subscribe((k) => out.told.push(k));
  const offKeyed = s.subscribe((k) => out.keyed.push(k + ':' + MKB.runState), ['runState']);
  const m0 = MKB.machine;
  document.getElementById('dbg_step').click();
  await new Promise((r) => setTimeout(r, 1500));
  out.machineMoved = MKB.machine > m0;
  out.snapMachine = s.getSnapshot().machine === MKB.machine;
  out.newSnap = s.getSnapshot() !== snap0;
  out.badgeSteps = Number(document.getElementById('runbadge').dataset.steps);
  out.steps = MKB.stepper.steps;
  document.getElementById('dbg_continue').click();
  await new Promise((r) => setTimeout(r, 300));
  document.getElementById('dbg_pause').click();
  await new Promise((r) => setTimeout(r, 300));
  off();
  const n = out.told.length;
  offKeyed();
  document.getElementById('dbg_step').click();
  await new Promise((r) => setTimeout(r, 1000));
  out.afterOff = out.told.length - n;
  out.storeThrows = null;
  try { s.set('nope', 1); } catch (e) { out.storeThrows = e.message; }
  out.provideThrows = null;
  try { MKB.provide({ nope: 1 }); } catch (e) { out.provideThrows = e.message; }
  out.api = Object.fromEntries(Object.entries(MKB.api).map(([g, o]) => [g, Object.keys(o).filter((k) => o[k] === undefined)]));
  out.apiFrozen = Object.isFrozen(MKB.api) && Object.isSealed(MKB.api.show);
  // a listener that throws: the others run, the error is reported afterwards
  const ran = [];
  const offBad = s.subscribe(() => { throw new Error('store test listener'); }, ['countNote']);
  const offGood = s.subscribe(() => ran.push(1), ['countNote']);
  MKB.countNote = 'store test';
  MKB.countNote = '';
  offBad();
  offGood();
  out.othersRan = ran.length;
  // removed by an earlier listener of the same change: not told
  const order = [];
  let offLate = null;
  const offFirst = s.subscribe(() => { order.push('first'); offLate(); }, ['countNote']);
  offLate = s.subscribe(() => order.push('late'), ['countNote']);
  MKB.countNote = 'store test 2';
  MKB.countNote = '';
  offFirst();
  out.order = order;
  out.get = s.get('runState') === MKB.runState && s.get('stepper') === MKB.stepper;
  return out;
}"""


def store_case(browser, base):
    """The page's state store and API (R2, page.js): a snapshot is the same
    object until a value changes; a step publishes the machine's state
    (publishMachine) to the subscribed views (the run badge shows the new
    count); Continue and Pause change runState and tell its keyed listener
    with the new value already readable; an unsubscribed listener hears
    nothing; unknown keys and API entries throw; every API entry is
    provided after the boot; a throwing listener does not stop the others."""
    problems, r = [], {}
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(base + "/index.html?kernel=hello&paused=1#debugger")
    page.wait_for_function("document.getElementById('dbg_state').dataset.state === 'paused'", timeout=60000)
    g = page.evaluate(STORE_JS)
    page.wait_for_timeout(500)  # the listener's errors are reported in a later task
    r.update(g)
    r["errors"] = errors
    if not g["sameSnap"] or g["snapRunState"] != "paused" or not g["snapStepper"]:
        problems.append(f"snapshot: same {g['sameSnap']}, runState {g['snapRunState']!r}, stepper {g['snapStepper']}")
    if not g["machineMoved"] or not g["snapMachine"] or not g["newSnap"] or "machine" not in g["told"]:
        problems.append(f"a step: machine published {g['machineMoved']}, in the snapshot {g['snapMachine']}, "
                        f"new snapshot {g['newSnap']}, told {g['told'][:12]}")
    if g["badgeSteps"] != g["steps"] or g["steps"] < 1:
        problems.append(f"run badge {g['badgeSteps']} instructions, the stepper {g['steps']}")
    if not g["keyed"] or g["keyed"][-1] != "runState:paused" or \
            any(k not in ("runState:stepped", "runState:jit", "runState:paused") for k in g["keyed"]):
        problems.append(f"runState listener: {g['keyed']}, want the new values, paused last")
    if g["afterOff"] != 0:
        problems.append(f"unsubscribed listener told {g['afterOff']} times")
    if g["storeThrows"] != "store: no key nope" or g["provideThrows"] != "page API: no entry nope":
        problems.append(f"unknown key: {g['storeThrows']!r}, unknown API entry: {g['provideThrows']!r}")
    missing = {k: v for k, v in g["api"].items() if v}
    if missing or not g["apiFrozen"]:
        problems.append(f"API entries not provided: {missing}, frozen {g['apiFrozen']}")
    if g["othersRan"] != 2 or len([e for e in errors if "store test listener" in e]) != 2 or len(errors) != 2:
        problems.append(f"throwing listener: others ran {g['othersRan']} times, page errors {errors}")
    if g["order"] != ["first", "first"] or not g["get"]:
        problems.append(f"a listener removed by an earlier one: {g['order']}, want first, first; state.get {g['get']}")
    page.close()
    return problems, r


def suite_misc(ctx):
    """Symbol import, initrds, panic text, determinism, follow mode."""
    failed = report("globals", *globals_case(ctx.browser, ctx.base))
    failed |= report("store", *store_case(ctx.browser, ctx.base))
    failed |= report("import", *import_case(ctx.browser, ctx.base, ctx.shots))
    failed |= report("import_edges", *import_edges(ctx.browser, ctx.base, ctx.shots))
    failed |= report("initrd", *initrd_case(ctx.browser, ctx.base, ctx.shots))
    if os.environ.get("MKB_THIRD_PARTY"):
        failed |= report("third_party", *third_party_case(ctx.browser, ctx.base, ctx.shots, os.environ["MKB_THIRD_PARTY"]))
    failed |= report("header_check", *header_check_case(ctx.browser, ctx.base, ctx.shots))
    failed |= report("panic_text", *panic_text_case(ctx.browser, ctx.base, ctx.shots))
    if os.environ.get("MKB_DISTRO_CACHE"):
        failed |= report("distro_panic", *distro_panic_case(ctx.browser, ctx.base, os.environ["MKB_DISTRO_CACHE"]))
    failed |= report("determinism", *determinism_case(ctx.browser, ctx.base))
    failed |= report("follow", *follow_case(ctx.browser, ctx.base, ctx.shots))
    failed |= report("fetch_fail", *fetch_fail_case(ctx.browser, ctx.base, ctx.shots))
    return failed
