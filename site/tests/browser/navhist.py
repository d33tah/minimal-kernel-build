"""Browser test suite navhist (test_browser.py --suites)."""

import json
import re
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import (
    cov_open_all, kernel_entries, machine_do, nav_at, nav_menu, nav_press, nav_settle, page_center, PHONE, report,
    run_to_wait, show_tab, SITE, wait_for,
)


# ---- jump history: Back and Forward ----------------------------------------

# What a jump changes and Back must restore: the tab, the hash, the Page
# listing's page, scroll and marked instruction, the Source panel (open or
# closed, file, picked line, title, scroll) and the window's scroll.
NAV_VIEW_JS = """() => {
  const tab = [...document.querySelectorAll('.tabbar [role=tab]')].find((t) => t.getAttribute('aria-selected') === 'true').dataset.tab;
  const l = document.getElementById('listing'), sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
  const hit = l && l.querySelector('.jhit'), pick = sb.querySelector('.pick'); // no #listing before the Page tab is first shown
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
            # A list the page draws again (the Coverage tree after its
            # rows were opened) can drop the element just found (CI run
            # 43544246661: "Element is not attached to the DOM"): find it
            # again in the new drawing, as a user's next look would.
            for again in range(3):
                try:
                    target.scroll_into_view_if_needed()
                    break
                except PlaywrightError as e:
                    if "not attached" not in str(e) or again == 2:
                        raise
                    time.sleep(0.5)
                    target = prep()
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

    def panel_addr():
        if not wait_for(lambda: page.locator("#src_addrs button.symlink").count() > 0, 30):
            return None
        # The listing scrolled to its end first: the panel opening keeps the
        # listing's line (E5a), and hello's address is then already where a
        # jump puts it (no change of view, no entry).
        page.evaluate("document.getElementById('listing').scrollTop = 1e9")
        return nav_first(page, "#src_addrs button.symlink")

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
        ("Source panel address", panel_addr),
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
        label = page.evaluate("MKB.navList[MKB.navAt - 1].label")
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
    r["stepping"] = f"page {p0} -> {page.get_attribute('#listing', 'data-page')}, {page.evaluate('MKB.stepper.steps')} steps"
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
    page.evaluate("window.mkbEmu = MKB.emulator")
    machine_do(page, "select_option", "#dbg_speed", "10")  # hello_main ends in a few hundred instructions
    machine_do(page, "click", "#dbg_continue")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "stepped", 10)
    r["running"] = page.evaluate("({ state: MKB.runState, steps: MKB.stepper.steps })")
    url = page.url.split("#")[0]
    for _ in range(n + 1):
        page.go_back()
        time.sleep(0.3)
    s0 = page.evaluate("MKB.stepper.steps")
    time.sleep(2)
    got = page.evaluate("({ same: window.mkbEmu === MKB.emulator, steps: MKB.stepper.steps, state: MKB.runState, url: location.href })")
    r["after back past the start"] = got
    if not got["url"].startswith(url) or not got["same"] or got["steps"] <= s0 or got["state"] != "stepped" or nav_at(page)[0] != 0:
        problems.append(f"{name}: back past the first view: {got}, at {nav_at(page)} (want the page, the same VM running, at 0)")
    page.go_forward()
    page.go_forward()
    time.sleep(0.5)
    if not page.evaluate("window.mkbEmu === MKB.emulator && MKB.runState === 'stepped'") or nav_at(page)[0] != 1:
        problems.append(f"{name}: browser forward: at {nav_at(page)}, VM {page.evaluate('MKB.runState')}")
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
