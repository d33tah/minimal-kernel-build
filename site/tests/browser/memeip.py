"""Browser test suite memeip (test_browser.py --suites)."""

import json
import re
import time

from tests.browser.common import (
    dbg_shown_once,
    gate_on_screen, kernel_entries, machine_do, nav_press, open_page, page_center, PHONE, report, run_to_wait,
    show_tab, shown_tab, SITE, tip_fits, tip_tap_away, tip_wait, wait_for,
)
from tests.browser.speedasm import MEMEIP_JS


def css_rgb(h):
    h = h.lstrip("#")
    return "rgb(" + ", ".join(str(int(h[i:i + 2], 16)) for i in (0, 2, 4)) + ")"


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
    tokens: Object.fromEntries([...Object.keys(MKB.REGION_NAMES), ...Object.keys(MKB.SECTION_NAMES), 'accent', 'bg']
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
    wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
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
    ended = page.evaluate("!!MKB.stepper.end")
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
        # The ESP link opens the stack's page (ESP from the CPU: the kernel's
        # direct map at 0xC0000000, or no paging).
        esp = page.evaluate("MKB.emulator.v86.cpu.reg32[4] >>> 0")
        esp_phys = esp - 0xC0000000 if esp >= 0xC0000000 else esp
        if page.locator("#mem_esp_go").count():
            page.locator("#mem_esp_go").tap() if phone else page.click("#mem_esp_go")
            ok = wait_for(lambda: shown_tab(page) == "page" and page.get_attribute("#listing", "data-page") == str(esp_phys // 4096), 10)
            r[f"{tag0} stack link -> Page"] = [ok, hex(esp), page.get_attribute("#listing", "data-page")]
            if not ok:
                problems.append(f"{tag0}: the stack link does not open ESP {esp:#x}'s page {esp_phys // 4096}: tab {shown_tab(page)}, "
                                f"listing page {page.get_attribute('#listing', 'data-page')}")
            nav_press(page, "#nav_back", "tap" if phone else "click")
            wait_for(lambda: shown_tab(page) == "memory", 5)
        else:
            problems.append(f"{tag0}: no stack link for ESP {esp:#x}: {page.inner_text('#mem_eip')!r}")
        memeip_clear_of_toggle(page, tag0, problems, r)
        shot("mark")
        # The end of the run: the minified Linux's panic, hello's halt, at full speed.
        machine_do(page, "select_option", "#dbg_speed", "full")
        if page.is_enabled("#dbg_continue"):
            machine_do(page, "click", "#dbg_continue")
        if not wait_for(lambda: page.evaluate("MKB.runState === 'paused' && !MKB.ffActive && !!MKB.stepper.end"), 180):
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
    wait_for(lambda: page.evaluate("typeof window.MKB?.entryPhase !== 'undefined' && MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 120)
    page.evaluate(MEMEIP_JS)
    time.sleep(3)
    for label, act in (("kernel switch to hello-os", lambda: machine_do(page, "select_option", "#kernel", "hello-os")),
                       ("Boot", lambda: machine_do(page, "click", "#boot"))):
        act()
        wait_for(lambda: page.evaluate("MKB.runState === 'stepped' && !MKB.entryPhase && !MKB.ffActive"), 120)
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


def memory_controls_case(browser, base, shots):
    """The Memory tab from the keyboard and the mouse (hello, 1280 px): Enter
    and Space on a legend entry highlight its pages and say how many
    (the grid's own count of that class), again turns it off; a click on a
    bar segment highlights its class; a "?" clicked twice opens and closes
    its popover; with 2 MiB + 64 KiB of RAM (?mem=,
    528 pages: half a row of the grid empty) the hover line names the page
    under the mouse and is empty over an empty cell; the Page tab's
    previous and next buttons move one page; Step after hello's halt says
    there is nothing to step; a #page~src= link that is not valid
    percent-encoding opens the Page tab without the Source panel."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    mem = (2 << 20) + (64 << 10)
    page.goto(f"{base}/index.html?kernel=hello&mem={mem}")
    wait_for(lambda: gate_on_screen(page), 60)
    dbg_shown_once(page)  # #dbg_status (read after Step below) is there once the Debugger tab has been shown
    show_tab(page, "memory")
    li = page.locator("#legend li[data-cls='r-conv']")
    n = page.evaluate("MKB.pageClass.filter((c) => c === 'r-conv').length")
    got = []
    for key in ("Enter", " "):
        li.focus()
        page.keyboard.press(key)
        got.append([li.get_attribute("aria-pressed"), page.inner_text("#focus_info")[:60]])
    r["legend keys"] = got
    if got[0][0] != "true" or f" {n} pages" not in got[0][1] or got[1] != ["false", ""]:
        problems.append(f"Enter then Space on conventional RAM's legend entry: {got}, want {n} pages highlighted, then none")
    bar = page.locator("#map_low > div", has_text="conventional").first
    if bar.count():
        bar.click()
        r["bar click"] = [li.get_attribute("aria-pressed"), page.inner_text("#focus_info")[:60]]
        if r["bar click"][0] != "true":
            problems.append(f"a click on the first MiB's conventional segment: legend entry {r['bar click']}")
        bar.click()
    else:
        problems.append("no conventional RAM segment in the first MiB's bar")
    # A "?" clicked opens its popover, clicked again closes it.
    tip = page.locator("#panel_memory .tip").first
    tip.click()
    shown = page.is_visible("#tip_pop")
    tip.click()
    r["tip twice"] = [shown, page.is_visible("#tip_pop")]
    if r["tip twice"] != [True, False]:
        problems.append(f"a Memory tab '?' clicked twice: popover [after one, after two] {r['tip twice']}")
    pages = mem // 4096
    hover = []
    for p in (5, pages + 4):
        x, y = page_center(page, p)
        page.mouse.move(x, y)
        time.sleep(0.2)
        hover.append(page.inner_text("#hover_info")[:40])
    r["hover"] = [page.get_attribute("#pages", "data-pages"), hover]
    if r["hover"][0] != str(pages) or not hover[0].startswith("page 0x5000, conventional RAM") or hover[1]:
        problems.append(f"hover over page 5, then an empty cell past the last page: {r['hover']}")
    open_page(page, 5)
    page.click("#page_prev")
    prev = page.get_attribute("#listing", "data-page")
    page.click("#page_next")
    page.click("#page_next")
    nxt = page.get_attribute("#listing", "data-page")
    r["prev, next next"] = [prev, nxt]
    if [prev, nxt] != ["4", "6"]:
        problems.append(f"page 5, previous, next twice: listing pages {[prev, nxt]}, want 4 and 6")
    # hello has halted (hlt with interrupts off): Step has nothing to step.
    wait_for(lambda: page.evaluate("!!MKB.stepper.end"), 30)
    machine_do(page, "click", "#dbg_step")
    want = "The CPU executed hlt with interrupts off: nothing to step."
    wait_for(lambda: page.inner_text("#dbg_status") == want, 10)
    r["step after the halt"] = page.inner_text("#dbg_status")
    if r["step after the halt"] != want:
        problems.append(f"Step after hello's halt: {r['step after the halt']!r}, want {want!r}")
    # A link whose ~src= part is not valid percent-encoding: the Page tab,
    # no Source panel, no error.
    page.goto(f"{base}/index.html?kernel=hello#page~src=%E0%A4%A")
    wait_for(lambda: gate_on_screen(page), 60)
    r["malformed src hash"] = [shown_tab(page), page.is_visible("#src_panel")]
    if r["malformed src hash"] != ["page", False]:
        problems.append(f"#page~src=%E0%A4%A: [tab, Source panel] {r['malformed src hash']}, want the Page tab without the panel")
    page.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_memeip(ctx):
    failed = report("memory controls", *memory_controls_case(ctx.browser, ctx.base, ctx.shots))
    return memeip_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries) or failed
