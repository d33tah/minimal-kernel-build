"""Browser test suite tipmarks (test_browser.py --suites)."""

import os
import time

from tests.browser.common import (
    kernel_entries, PHONE, report, run_to_wait, show_tab, tip_fits, TIP_STATE_JS, tip_tap_away, tip_wait, wait_for,
)
from tests.browser.bookmarks_run import tip_long_press
from tests.browser.nopath import NOPATH_WALKS


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
    wait_for(lambda: page.evaluate("!!MKB.disasm && !!MKB.symbols && !!MKB.stepper"), 60)
    fn = NOPATH_WALKS[name][0]
    s = page.evaluate(f"MKB.symbols.symbols.find((s) => s.name === '{fn}')")
    page.fill("#dbg_addr", hex(s["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
    if not ok:
        problems.append(f"{name}: Run to {fn} did not stop there: {status}")
        return None
    steps = page.evaluate("MKB.stepper.steps")
    page.fill("#dbg_n", "30")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and page.evaluate("MKB.stepper.steps") >= steps + 30, 30)
    pa = page.evaluate("""() => { const S = MKB.jumpsFor(MKB.disasm.insns);
      const x = MKB.disasm.insns.find(([a]) => MKB.jumpPaths(S, MKB.stepper, a).noDirect); return x ? x[0] : null; }""")
    if pa is None:
        problems.append(f"{name}: no instruction with ⊘ in the disassembly")
        return None
    page.evaluate(f"MKB.jumpTo({pa})")
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
    page.evaluate("MKB.renderContext()")
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
        page.evaluate("MKB.renderContext()")
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
            page.evaluate("MKB.renderContext()")
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
    wait_for(lambda: page.evaluate("typeof window.MKB?.symbols !== 'undefined' && !!MKB.symbols && MKB.symbols.symbols.length > 0"), 120)
    sk = page.evaluate("MKB.symbols.symbols.find((s) => s.name === 'start_kernel')")
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
