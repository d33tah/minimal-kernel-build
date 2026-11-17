"""Browser test suite overlap (test_browser.py --suites)."""

import time

from tests.browser.common import gate_on_screen, report, show_tab, wait_for


# ---- overlap: nothing of the page under the Source panel (E5a) -------------
# E5's screenshots: at 1280 px with the Source panel open the page ran on
# under the panel (status and build log lines cut at its edge). Every tab,
# the window scrolled from top to end, at 390 and 1280 px, also with the
# Source sheet or panel open: the page never scrolls sideways. At 1280 px
# with the Source panel open no element of the header or the tabs runs past
# the panel's left edge (inside a scroll box: the box's visible part). The
# menu toggle floats over the page (the operator wants no bar under it):
# what it lies over is recorded, not checked.
OVERLAP_TOGGLE_JS = """() => {
  const t = document.getElementById('runbadge_toggle'), badge = document.getElementById('runbadge');
  if (!t || !t.getClientRects().length) return null;
  const b = t.getBoundingClientRect(), inBadge = (e) => !!e && badge.contains(e);
  const meets = (r) => r.width > 0.5 && r.height > 0.5 && Math.min(r.right, b.right) - Math.max(r.left, b.left) > 0.5
    && Math.min(r.bottom, b.bottom) - Math.max(r.top, b.top) > 0.5;
  const cand = new Set();
  for (let x = b.left + 2; x < b.right - 1; x += 5)
    for (let y = b.top + 2; y < b.bottom - 1; y += 5)
      for (const e of document.elementsFromPoint(x, y))
        if (!inBadge(e) && e !== document.body && e !== document.documentElement) cand.add(e);
  const pieces = [];
  for (const el of cand) {
    // Controls by their box (a select, a button); a summary or a link by its words.
    if (el.matches('button, input, select, textarea'))
      pieces.push([el.getBoundingClientRect(), '<' + el.tagName.toLowerCase() + (el.id ? '#' + el.id : '') + '>']);
    for (const n of el.childNodes) {
      if (n.nodeType !== 3) continue;
      for (const m of n.data.matchAll(/\\S+/g)) {
        const g = document.createRange(); g.setStart(n, m.index); g.setEnd(n, m.index + m[0].length);
        for (const rr of g.getClientRects()) pieces.push([rr, m[0]]);
      }
    }
  }
  const hits = [];
  for (const [rr, what] of pieces) {
    if (!meets(rr)) continue;
    const y = (Math.max(rr.top, b.top) + Math.min(rr.bottom, b.bottom)) / 2;
    const left = document.elementFromPoint(Math.max(1, b.left - 3), y), start = document.elementFromPoint(Math.max(1, rr.left + 1), y);
    if (!inBadge(left) || !inBadge(start)) hits.push(what.slice(0, 40));
  }
  return { hits: [...new Set(hits)].slice(0, 8), toggle: [b.left, b.top, b.right, b.bottom].map(Math.round),
           y: Math.round(scrollY), maxY: Math.round(document.documentElement.scrollHeight - innerHeight), vh: innerHeight,
           hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""

OVERLAP_PANEL_JS = """() => {
  const p = document.getElementById('src_panel');
  if (p.hidden || !p.getClientRects().length) return null;
  const edge = p.getBoundingClientRect().left, hits = [];
  for (const el of document.querySelectorAll('body > header, body > header *, body > main, body > main *')) {
    if (!el.getClientRects().length) continue;
    const r = el.getBoundingClientRect();
    if (r.width < 0.5 || r.height < 0.5 || r.right <= edge + 0.5) continue;
    let lo = r.left, hi = r.right;
    for (let a = el.parentElement; a && a !== document.body; a = a.parentElement)
      if (getComputedStyle(a).overflowX !== 'visible') {
        const ar = a.getBoundingClientRect();
        lo = Math.max(lo, ar.left); hi = Math.min(hi, ar.right);
      }
    if (hi > edge + 0.5 && hi > lo + 0.5 && getComputedStyle(el).visibility !== 'hidden')
      hits.push((el.id ? '#' + el.id : el.tagName.toLowerCase() + (el.className && typeof el.className === 'string' ? '.' + el.className.split(' ')[0] : ''))
                + ' to ' + Math.round(hi));
  }
  return { edge: Math.round(edge), hits: hits.slice(0, 8), n: hits.length, hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""

OVERLAP_TABS = ["machine", "memory", "events", "quests", "page", "debugger", "calls", "source", "snapshots", "settings", "about"]


def overlap_scroll(page, tag, problems, r):
    """Scroll the window from the top to the end; at each stop the menu
    toggle shows and the page does not scroll sideways. What the toggle
    lies over is recorded in r (it floats over the page)."""
    page.evaluate("window.scrollTo(0, 0)")
    time.sleep(0.2)
    seen, under, y = [], [], 0
    for _ in range(40):
        got = page.evaluate(OVERLAP_TOGGLE_JS)
        if not got:
            problems.append(f"{tag}: no menu toggle shown")
            return
        seen.append(got["y"])
        under += [w for w in got["hits"] if w not in under]
        if got["hscroll"]:
            problems.append(f"{tag}: the page scrolls sideways")
            break
        if got["y"] >= got["maxY"]:
            break
        y = min(got["maxY"], got["y"] + int(got["vh"] * 0.4))
        page.evaluate(f"window.scrollTo(0, {y})")
        time.sleep(0.12)
    r[tag] = [len(seen), under[:8]]


def overlap_case(browser, base, shots):
    problems, r, errors = [], {}, []
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello")
    if not wait_for(lambda: gate_on_screen(page), 120):
        ctx.close()
        return ["390: hello did not boot"], r
    for tab in OVERLAP_TABS:
        show_tab(page, tab)
        time.sleep(0.6)
        overlap_scroll(page, f"390 {tab}", problems, r)
    show_tab(page, "machine")
    page.evaluate("window.scrollTo(0, 0)")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"overlap-390-machine-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    # The Source sheet open (the toggle floats right above it).
    page.evaluate("(p) => MKB.openSourceFile(p)", "boot/hello.c")
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    for tab in ("machine", "source", "page"):
        show_tab(page, tab)
        time.sleep(0.6)
        overlap_scroll(page, f"390 {tab}, Source sheet open", problems, r)
    page.evaluate("window.scrollTo(0, 0)")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"overlap-390-sheet-{scheme}.png"))
    ctx.close()

    ctx = browser.new_context(viewport={"width": 1280, "height": 900})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello")
    if not wait_for(lambda: gate_on_screen(page), 120):
        ctx.close()
        return problems + ["1280: hello did not boot"], r
    for tab in OVERLAP_TABS:
        show_tab(page, tab)
        time.sleep(0.6)
        overlap_scroll(page, f"1280 {tab}", problems, r)
    # The Page listing keeps the line it shows when the panel opens and
    # closes (the page's column changes width, the listing is drawn again).
    page.evaluate("MKB.jumpTo(0x100000)")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == "256", 10)
    page.evaluate("() => { const b = document.getElementById('listing'); b.scrollTop = (b.scrollHeight - b.clientHeight) / 2; }")
    time.sleep(1.5)
    keep = page.evaluate("MKB.navListingAnchor()")
    line_js = """(a) => { const b = document.getElementById('listing'), br = b.getBoundingClientRect();
      const e = b.querySelector('.ins[data-addr="' + a + '"]'); if (!e) return null; const r = e.getBoundingClientRect();
      return { shown: r.top >= br.top - 1 && r.bottom <= br.bottom + 1, top: Math.round(r.top - br.top), h: Math.round(b.clientHeight),
               scroll: b.scrollHeight > b.clientHeight + 4, w: Math.round(br.width) }; }"""
    r["listing kept"] = [keep, page.evaluate(line_js, keep)]
    for what, js in (("opens", "(p) => MKB.openSourceFile(p)"), ("closes", "() => MKB.closeSource()")):
        page.evaluate(js, "boot/hello.c")
        time.sleep(1.5)
        got = page.evaluate(line_js, keep)
        r["listing kept"].append(got)
        if keep < 0 or not got or not got["shown"] or not got["scroll"]:
            problems.append(f"1280 Page tab: the listing's line {keep:#x} after the Source panel {what}: {got} (before {r['listing kept'][1]})")
    page.evaluate("(p) => MKB.openSourceFile(p)", "boot/hello.c")
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    for tab in OVERLAP_TABS:
        # A tab under the panel cannot be clicked: the check below names it.
        page.evaluate("(t) => document.getElementById('tab_' + t).click()", tab)
        time.sleep(0.6)
        got = page.evaluate(OVERLAP_PANEL_JS)
        r[f"1280 {tab} under the panel"] = got and got["n"]
        if not got:
            problems.append(f"1280 {tab}: the Source panel is not open")
        elif got["hits"] or got["hscroll"]:
            problems.append(f"1280 {tab}: {got['n']} elements run under the Source panel (edge {got['edge']}): {got['hits']}, sideways scroll {got['hscroll']}")
        overlap_scroll(page, f"1280 {tab}, Source panel open", problems, r)
    page.evaluate("document.getElementById('tab_machine').click()")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"overlap-1280-panel-{scheme}.png"))
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def suite_overlap(ctx):
    return report("overlap", *overlap_case(ctx.browser, ctx.base, ctx.shots))
