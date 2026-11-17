"""Browser test suite srclinks (test_browser.py --suites)."""

import json
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import (
    hover_byte, kernel_entries, no_hscroll, open_page, PHONE, report, show_tab, shown_tab, SITE, wait_for,
)
from tests.browser.types import watch


# Where the srclinks suite pauses: Run to this function, then step until
# EIP is in code inlined into it (the source row has an "inlined into" line).
SRCLINK_POINT = ("minified-linux", "start_kernel")


def srclink_opens(page, sel, how="click"):
    """srclink_open_here in the tab that holds sel (U5: the state line and
    the "VM paused" overlay are in the Machine tab), then back to the tab
    shown before."""
    tab = page.evaluate("""(s) => { const e = document.querySelector(s.split(' >> ')[0]);
        const p = e && e.closest('[role=tabpanel]'); return p && p.hidden ? p.id.slice(6) : null; }""", sel)
    if not tab:
        return srclink_open_here(page, sel, how)
    back = shown_tab(page)
    show_tab(page, tab)
    try:
        return srclink_open_here(page, sel, how)
    finally:
        show_tab(page, back)


def srclink_open_here(page, sel, how="click"):
    """Activate the first visible .srclink under sel (click, Enter or tap)
    with the Source panel closed; the panel must show the link's file with
    its line picked (.pick) inside the visible part of #src_body. Returns
    (link's file:line, None) or (that, problem)."""
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    link = page.locator(sel).locator(".srclink").first
    if not link.count():
        return None, f"no .srclink in {sel}"
    # A listing can be drawn again (a scan, new DWARF) between finding the
    # link and acting on it: try again on the new copy.
    for attempt in range(5):
        try:
            link.scroll_into_view_if_needed()
            path, line = link.get_attribute("data-file"), int(link.get_attribute("data-line"))
            if how == "enter":
                link.focus()
                page.keyboard.press("Enter")
            elif how == "tap":
                link.tap()
            else:
                link.click()
            break
        except PlaywrightError:
            if attempt == 4:
                raise
            time.sleep(0.5)
    want = f"{path}:{line}"
    row = f'#src_body [data-line="{line}"].pick'
    ok = wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-file") == path
                  and page.get_attribute("#src_body", "data-state") in ("ok", "none")
                  and (page.get_attribute("#src_body", "data-state") == "none" or page.locator(row).count() > 0), 60)
    if not ok:
        return want, f"{sel}: {how} on {want} did not open it in the Source panel ({page.get_attribute('#src_body', 'data-file')}, " \
                     f"{page.get_attribute('#src_body', 'data-state')}, {page.inner_text('#src_title')!r})"
    if page.get_attribute("#src_body", "data-state") == "none":
        why = page.inner_text("#src_body").strip()
        return want, None if why else f"{sel}: {want} opened without text and without saying why"
    p = srclink_line_shown(page, line)
    return want, p and f"{sel}: {want}: {p}"


def srclink_line_shown(page, line):
    """None when line's row in the Source panel is picked and lies inside
    #src_body's visible rect and the viewport, a second after the panel
    drew (the address list above it and late DWARF or text change the
    layout); else what is wrong."""
    time.sleep(1)
    got = page.evaluate("""(line) => { const e = document.querySelector('#src_body [data-line="' + line + '"]');
        if (!e) return null;
        const r = e.getBoundingClientRect(), b = document.getElementById('src_body').getBoundingClientRect();
        return { pick: e.classList.contains('pick'), top: r.top, bottom: r.bottom, boxTop: b.top, boxBottom: b.bottom,
                 vh: window.innerHeight, first: document.querySelector('#src_body [data-line]').dataset.line,
                 scroll: document.getElementById('src_body').scrollTop }; }""", line)
    if not got:
        return f"no row for line {line} in the Source panel"
    if not got["pick"]:
        return f"line {line} is not picked"
    if not (got["top"] >= got["boxTop"] - 1 and got["bottom"] <= got["boxBottom"] + 1):
        return f"line {line} is outside the Source panel's visible rows: {got}"
    if not (got["top"] >= 0 and got["bottom"] <= got["vh"]):
        return f"line {line} is outside the viewport: {got}"
    # The texts use plain separators (the operator's rule: no em-dashes).
    title = page.inner_text("#src_title")
    if "\u2014" in title:
        return f"the Source panel's title has an em-dash: {title!r}"
    return None


# Visible text outside a .srclink that reads like file:line, anywhere in
# the document (the Source panel shows source text: close it first).
PLAIN_SRCLOC_JS = r"""() => {
  const re = /[\w./-]+\.(?:c|h|S|lds|tbl):\d+\b/;
  const out = [];
  const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
  for (let n; (n = w.nextNode());) {
    const el = n.parentElement;
    if (!re.test(n.nodeValue) || !el || el.closest('script,style,.srclink') || !el.checkVisibility()) continue;
    const r = el.getBoundingClientRect();
    if (r.width === 0 && r.height === 0) continue;
    const anc = el.closest('[id]');
    out.push((anc ? '#' + anc.id : el.tagName) + ': ' + n.nodeValue.trim().slice(0, 100));
  }
  return out;
}"""


def srclink_plain_text(page, where):
    """A problem when a visible file:line anywhere on the page is plain
    text instead of a link; else None."""
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    plain = page.evaluate(PLAIN_SRCLOC_JS)
    return plain and f"{where}: file:line shown as plain text: {plain[:5]}"


def state_link_shown(page):
    """None when the state line's file:line link lies fully inside the
    viewport and inside the state line's own box (not cut off by its
    ellipsis) and is what a tap at its centre hits; else what is wrong."""
    got = page.evaluate("""() => { const st = document.getElementById('dbg_state'), e = st.querySelector('.srclink');
        if (!e) return null;
        const r = e.getBoundingClientRect(), b = st.getBoundingClientRect();
        const h = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return { text: st.textContent, rect: [r.left, r.right, r.top, r.bottom], box: [b.left, b.right], vw: window.innerWidth,
                 vh: window.innerHeight, hit: !!h && (h === e || e.contains(h)) }; }""")
    if not got:
        return "no link in the state line"
    left, right, top, bottom = got["rect"]
    if not (left >= -0.5 and right <= got["vw"] + 0.5 and top >= 0 and bottom <= got["vh"]):
        return f"state line link outside the viewport: {got}"
    if not (left >= got["box"][0] - 0.5 and right <= got["box"][1] + 0.5):
        return f"state line link cut off by the state line's box: {got}"
    if not got["hit"]:
        return f"a tap at the state line link's centre does not hit it: {got}"
    return None


def srclinks_case(browser, base, shots):
    """Every file:line the Debugger shows is a link to the Source panel at
    that line, through one helper (srclinks.js srcLinkEl/srcLinkHtml): the
    state line, the Run to status, the source row and its inline chain,
    the call stack, the Code at EIP C lines, the locals' frame headers, the
    function facts (defined at, starts at), a watch's "defined at"; the
    other tabs too (Page listing, hovered byte, Graph detail, Coverage
    facts). Each opens its file with the line picked and in view; Enter
    opens one from the keyboard and moves the focus into the panel, Escape
    gives it back; a tap opens one on a 390 px phone. Light/dark at 1280
    and 390 px."""
    problems, r = [], {}
    name, fn = SRCLINK_POINT
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", fn)
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.inner_text("#dbg_status").startswith("Stopped at"), 240):
        page.close()
        return [f"{name}: Run to {fn} did not stop: {page.inner_text('#dbg_status')}"], r
    wait_for(lambda: page.locator("#dbg_status .srclink").count() > 0, 30)
    want, p = srclink_opens(page, "#dbg_status")
    r["Run to status"] = want
    if p:
        problems.append(p)
    # The "VM paused" overlay on the screen repeats the Run to status.
    want, p = srclink_opens(page, "#vm_reason")
    r["VM paused overlay"] = want
    if p:
        problems.append(f"{name} VM paused overlay: {p}")
    p = srclink_plain_text(page, f"{name} after Run to {fn}")
    if p:
        problems.append(p)
    # Paused at fn's first instruction, the state line names fn and links
    # the line at EIP, as the source row does (not the caller's call).
    wait_for(lambda: page.locator("#cur_src .srclink").count() > 0, 30)
    st = page.inner_text("#dbg_state")
    st_link = page.locator("#dbg_state .srclink")
    src_link = page.locator("#cur_src .frame:not(.inl) .srclink").first
    got = [(e.get_attribute("data-file"), e.get_attribute("data-line")) if e.count() else None for e in (st_link, src_link)]
    if f"in {fn}" not in st.replace("\n", " ") or got[0] != got[1]:
        problems.append(f"{name}: after Run to {fn} the state line {st!r} ({got[0]}) does not match the source row ({got[1]})")
    # Step into inlined code: the source row then has an "inlined into" line.
    for _ in range(60):
        if page.locator("#cur_src .frame.inl .srclink").count():
            break
        before = page.evaluate("MKB.stepper.steps")
        page.fill("#dbg_n", "5")
        page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") > before and page.get_attribute("#dbg_state", "data-state") == "paused", 30)
        wait_for(lambda: page.get_attribute("#cur_src", "data-state") not in (None, "loading"), 20)
    r["steps"] = page.evaluate("MKB.stepper.steps")
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    wait_for(lambda: page.locator("#context .src .srclink").count() > 0, 20)
    wait_for(lambda: page.locator("#locals .lhead .srclink").count() > 0, 30)
    wait_for(lambda: page.locator("#cur_fn .fnloc").count() > 0, 30)
    views = {
        "state line": "#dbg_state",
        "source row": "#cur_src .frame:not(.inl)",
        "inline chain": "#cur_src .frame.inl",
        "call stack (top)": "#call_stack .cframe >> nth=0",
        "call stack (caller)": "#call_stack .cframe >> nth=1",
        "Code at EIP": "#context .src",
        "locals frame header": "#locals .lhead",
        "function defined at": "#cur_fn .fnfacts >> nth=0",
        "function starts at": "#cur_fn [data-entry]",
    }
    for view, sel in views.items():
        want, p = srclink_opens(page, sel)
        r[view] = want
        if p:
            problems.append(f"{name} {view}: {p}")
    # Every file:line in the Debugger tab outside the Source panel is a link.
    plain = page.evaluate("""() => [...document.querySelectorAll('#panel_debugger .srcloc, #dbg_state .srcloc, #dbg_status .srcloc')]
        .filter((e) => !e.classList.contains('srclink')).map((e) => e.textContent)""")
    if plain:
        problems.append(f"{name}: file:line shown as plain text in the Debugger: {plain[:5]}")
    r["links in the Debugger"] = page.locator("#panel_debugger .srclink").count()
    # A watch's "defined at".
    watch(page, "init_task")
    want, p = srclink_opens(page, '#watches .watch[data-expr="init_task"] .wdecl')
    r["watch defined at"] = want
    if p:
        problems.append(f"{name} watch: {p}")
    # Keyboard: Enter on a focused link opens it and moves the focus into the panel; Escape gives it back.
    want, p = srclink_opens(page, "#cur_src .frame:not(.inl)", "enter")
    r["keyboard"] = want
    if p:
        problems.append(f"{name} keyboard: {p}")
    elif page.evaluate("document.activeElement.id") != "src_body":
        problems.append(f"{name} keyboard: the focus did not move into the Source panel: {page.evaluate('document.activeElement.outerHTML.slice(0, 80)')}")
    else:
        page.keyboard.press("Escape")
        if page.is_visible("#src_panel") or not page.evaluate("document.activeElement.classList.contains('srclink')"):
            problems.append(f"{name} keyboard: Escape did not close the panel and give the focus back to the link")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        if page.is_visible("#src_panel"):
            page.click("#src_close")
        page.locator("#sym_info").scroll_into_view_if_needed()
        time.sleep(0.2)
        page.screenshot(path=str(shots / f"srclinks-{name}-debugger-1280-{scheme}.png"))
        inl = page.locator("#cur_src .frame.inl .srclink")
        if inl.count():
            inl.first.click()
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 20)
        if inl.count():
            p = srclink_line_shown(page, int(inl.first.get_attribute("data-line")))
            if p:
                problems.append(f"{name} 1280 {scheme}: inline chain link: {p}")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"srclinks-{name}-panel-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    # Other tabs: the Page listing's C lines and a hovered byte's "defined at".
    T = json.loads((SITE / kernel_entries()[name]["dwarf"] / "types.json").read_text())
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "page")
    wait_for(lambda: page.locator("#listing .src .srclink").count() > 0, 20)
    want, p = srclink_opens(page, "#listing .src")
    r["Page listing"] = want
    if p:
        problems.append(f"{name} Page listing: {p}")
    p = srclink_plain_text(page, f"{name} Page tab")
    if p:
        problems.append(p)
    it = next(v for v in T["vars"] if v[0] == "init_task")
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    hover_byte(page, it[1] + 16, click=True)
    want, p = srclink_opens(page, "#var_info")
    r["hovered byte"] = want
    if p:
        problems.append(f"{name} hovered byte: {p}")
    # Graph detail: where the selected call came from and its entry.
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "calls")
    page.evaluate("""() => { const st = MKB.G.stepper; MKB.G.view = 'timeline';
        MKB.G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fr.n - 1; MKB.renderGraphInfo(); }""")
    wait_for(lambda: page.locator("#graph_src .srclink").count() > 0, 20)
    want, p = srclink_opens(page, "#graph_src")
    r["Graph detail"] = want
    if p:
        problems.append(f"{name} Graph detail: {p}")
    p = srclink_plain_text(page, f"{name} Calls tab")
    if p:
        problems.append(p)
    # Coverage: a function's facts (open its row's arrow).
    if page.is_visible("#src_panel"):
        page.click("#src_close")
    show_tab(page, "source")
    page.fill("#cov_filter", fn)
    frow = page.locator(f'#cov_tree .covrow[data-kind="func"][data-path$=":{fn}"]').first
    if wait_for(lambda: frow.count() > 0, 30):
        frow.locator(".ttog").click()
        wait_for(lambda: page.locator("#cov_tree .covfacts .fnloc").count() > 0, 20)
        want, p = srclink_opens(page, "#cov_tree .covfacts")
        r["Coverage facts"] = want
        if p:
            problems.append(f"{name} Coverage facts: {p}")
        p = srclink_plain_text(page, f"{name} Source tab")
        if p:
            problems.append(p)
    else:
        problems.append(f"{name}: no Coverage row for {fn}")
    page.close()
    # A 390 px phone: tap the source row's link.
    ctx = browser.new_context(**PHONE)
    ph = ctx.new_page()
    ph.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: ph.get_attribute("#dbg_state", "data-state") == "paused", 60)
    ph.fill("#dbg_addr", fn)
    ph.tap("#dbg_runto")
    wait_for(lambda: ph.get_attribute("#dbg_state", "data-state") == "paused"
             and ph.inner_text("#dbg_status").startswith("Stopped at"), 240)
    wait_for(lambda: ph.locator("#cur_src .srclink").count() > 0, 30)
    want, p = srclink_opens(ph, "#cur_src .frame:not(.inl)", "tap")
    r["phone tap"] = want
    if p:
        problems.append(f"{name} phone: {p}")
    # The state line ("paused, in <function> (file:line)") keeps its link
    # on the screen when the function name is long: step until a stop in a
    # function with a long name, checking every stop on the way.
    longest = ""
    for k in range(40):
        if ph.is_visible("#src_panel"):
            ph.click("#src_close")
        # The state line is in the Machine tab (U5), Step N in the Debugger tab.
        show_tab(ph, "machine")
        ph.locator("#dbg_state").scroll_into_view_if_needed()
        fname = ph.evaluate("MKB.stepper.curSym >= 0 ? MKB.stepper.symName[MKB.stepper.curSym] : ''")
        p = state_link_shown(ph)
        if p:
            problems.append(f"{name} phone, in {fname} after {ph.evaluate('MKB.stepper.steps')} steps: {p}")
            break
        if len(fname) > len(longest):
            longest = fname
        if len(fname) >= 20:
            want, p = srclink_opens(ph, "#dbg_state", "tap")
            r["phone state line"] = f"{fname}: {want}"
            if p:
                problems.append(f"{name} phone state line in {fname}: {p}")
            break
        before = ph.evaluate("MKB.stepper.steps")
        show_tab(ph, "debugger")
        ph.fill("#dbg_n", "5")
        ph.tap("#dbg_stepn")
        wait_for(lambda: ph.evaluate("MKB.stepper.steps") > before and ph.get_attribute("#dbg_state", "data-state") == "paused", 30)
        wait_for(lambda: ph.locator("#dbg_state .srclink").count() > 0, 20)
    else:
        problems.append(f"{name} phone: no stop in a function with a name of 20 or more characters (longest {longest!r})")
    if ph.is_visible("#src_panel"):
        ph.click("#src_close")
    show_tab(ph, "debugger")
    for scheme in ("light", "dark"):
        ph.emulate_media(color_scheme=scheme)
        if ph.is_visible("#src_panel"):
            p = srclink_line_shown(ph, int(ph.locator("#cur_src .srclink").first.get_attribute("data-line")))
            if p:
                problems.append(f"{name} 390 {scheme}: source row link: {p}")
        ph.screenshot(path=str(shots / f"srclinks-{name}-panel-390-{scheme}.png"))
        if ph.is_visible("#src_panel"):
            ph.click("#src_close")
        ph.locator("#sym_info").scroll_into_view_if_needed()
        time.sleep(0.2)
        ph.screenshot(path=str(shots / f"srclinks-{name}-debugger-390-{scheme}.png"))
        if ph.locator("#cur_src .srclink").count():
            ph.locator("#cur_src .srclink").first.tap()
        wait_for(lambda: ph.is_visible("#src_panel"), 10)
    if not no_hscroll(ph, "body"):
        problems.append(f"{name} phone: horizontal scroll with the links")
    ctx.close()
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    return problems, r


def suite_srclinks(ctx):
    return report("srclinks", *srclinks_case(ctx.browser, ctx.base, ctx.shots))
