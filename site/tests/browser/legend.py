"""Browser test suite legend (test_browser.py --suites)."""

import time

from tests.browser.common import (
    machine_do, nav_press, report, run_to_wait, show_tab, SITE, tip_fits, tip_tap_away, tip_wait, wait_for,
)
from tests.browser.memeip import css_rgb, stub_shift
from tests.browser.tipmarks import TIP_LAYOUT_JS


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
  return { mem: row('legend_mem'), graph: row('legend_graph'), regions: Object.keys(MKB.REGION_NAMES), sections: Object.keys(MKB.SECTION_NAMES),
    palette: Object.entries(MKB.PALETTE), tok: Object.fromEntries([...Object.keys(MKB.REGION_NAMES), ...Object.keys(MKB.SECTION_NAMES)].map((k) => [k, tok(k)])) }; }"""


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
    wait_for(lambda: page.evaluate("!!MKB.disasm && !!MKB.symbols && !!MKB.stepper"), 60)
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
    page.evaluate("MKB.renderContext()")
    seen = page.evaluate(LEGEND_LIVE_CLASSES_JS, "#context *, #regs *")
    show_tab(page, "memory")
    page.click("#scan")
    time.sleep(1)
    show_tab(page, "debugger")
    s = page.evaluate("MKB.symbols.symbols.find((s) => s.name === 'start_kernel')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    run_to_wait(page, {s["paddr"]}, 300)
    page.fill("#dbg_n", "300")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and page.evaluate("MKB.stepper.steps") > 0, 60)
    time.sleep(1)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_debugger *"))
    pa = s["paddr"]
    page.evaluate(f"MKB.jumpTo({pa})")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(pa // 4096), 20)
    time.sleep(0.5)
    page.click("#reread")
    time.sleep(0.5)
    seen.update(page.evaluate(LEGEND_LIVE_CLASSES_JS, "#panel_page *"))
    page.evaluate("MKB.openSourceFile('init/main.c')")
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


# The yellow lines drawn in a box: address, its tip, the build's bytes, and
# what guest memory holds there and at pa + shift.
YELLOW_LINES_JS = """([sel, shift]) => [...document.querySelectorAll(sel + ' .ins.bad')].map((e) => {
  const pa = Number(e.dataset.addr), ins = MKB.disasm.insns[MKB.insnIndex(pa)], n = ins[1].length / 2;
  const b = e.querySelector('.difb');
  return { pa, tip: e.dataset.tip || '', btip: b ? b.dataset.tip || '' : null, mark: b ? b.matches('.tipmark[tabindex="0"]') : false, raw: ins[1],
    got: [...MKB.emulator.read_memory(pa, n)].map((x) => x.toString(16).padStart(2, '0')).join(''),
    moved: [...MKB.emulator.read_memory(pa + shift, n)].map((x) => x.toString(16).padStart(2, '0')).join('') }; })"""


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
    wait_for(lambda: page.evaluate("!!MKB.disasm && !!MKB.symbols && !!MKB.stepper"), 60)
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        page.close()
        return [f"{name}: Run to 0x100000 did not stop there: {status}"], r
    page.evaluate("MKB.renderContext()")
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
    page.evaluate("MKB.jumpTo(0x100000)")
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
    wait_for(lambda: pp.evaluate("!!MKB.disasm && !!MKB.symbols && !!MKB.stepper"), 60)
    pp.fill("#dbg_addr", "0x100000")
    pp.click("#dbg_runto")
    run_to_wait(pp, {0x100000}, 300)
    pp.evaluate("MKB.jumpTo(0x100000)")
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
    s = page.evaluate("MKB.symbols.symbols.find((s) => s.name === 'start_kernel')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    run_to_wait(page, {s["paddr"]}, 300)
    page.evaluate("MKB.renderContext()")
    n_ctx = page.locator("#context .ins.bad").count()
    page.evaluate("MKB.jumpTo(0x100000)")
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
    tgt = page.evaluate("""() => { const [lo, hi] = MKB.codeRange(MKB.ffTarget().phys), S = MKB.jumpsFor(MKB.disasm.insns);
      const x = MKB.disasm.insns.find(([a]) => a >= lo + 0x1000 && a < hi && MKB.jumpPaths(S, MKB.stepper, a).noDirect && !MKB.stepper.executed(a));
      return x ? x[0] : null; }""")
    if tgt is None:
        problems.append(f"{name}: no instruction for the Run to breakpoint check")
    else:
        page.evaluate(f"MKB.jumpTo({tgt})")
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(tgt // 4096), 20)
        show_tab(page, "debugger")
        page.evaluate("""(t) => { const orig = MKB.jitChunk; window._bpSeen = null;
          MKB.jitChunk = function (...a) { if (!window._bpSeen && MKB.emulator.read_memory(t, 1)[0] === 0xf4) {
              MKB.showTab('page'); document.getElementById('reread').click();
              const e = document.querySelector('#listing .ins[data-addr="' + t + '"]');
              window._bpSeen = { cls: e ? e.className : '', tip: e ? e.dataset.tip || '' : '', lines: [] }; }
            return orig.apply(this, a); }; window._bpOrig = orig; }""", tgt)
        page.fill("#dbg_addr", hex(tgt + page.evaluate("MKB.symbols.symbols[0].vaddr - MKB.symbols.symbols[0].paddr")))
        page.click("#dbg_runto")
        wait_for(lambda: page.evaluate("!MKB.ffActive && !!window._bpSeen"), 120)
        page.evaluate("void (MKB.jitChunk = window._bpOrig)")
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
