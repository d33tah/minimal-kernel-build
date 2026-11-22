"""Browser test suite srcfind (test_browser.py --suites)."""

import time

from tests.browser.common import PHONE, gate_on_screen, report, show_tab, wait_for


# ---- srcfind: a file without code is found from the Source tab (E9b) --------
# Operator 2026-11-22 "ja tego pliku nasm nie [widzę]": the minified Linux's
# /init source, elo/init.nasm, has no DWARF (nasm -f bin, packed into the
# initramfs), so the Source tab's tree, made from the DWARF's files, did not
# list it; Files, which does, is in the Source panel, which shows only once
# some file is open. As a user from the default page: the Kernel menu's
# minified Linux (no ?kernel=, no page API), the Source tab, "init.nasm" in
# its filter, the row (marked "no code", no counts) opens the panel on it.
# At 390 px (touch) a tap on the middle of a directory's row, not on its
# name, opens and closes it, and the same tap on init.nasm's row opens it.
# Screenshots at 1280 and 390 px, light and dark.
SF_FILE = "elo/init.nasm"
SF_ROW_JS = """(p) => { const e = document.querySelector('#cov_tree .covrow[data-path="' + p + '"]');
  if (!e) return null; const b = e.getBoundingClientRect();
  return { kind: e.dataset.kind, code: e.dataset.code || '', text: e.innerText.replace(/\\s+/g, ' ').trim(),
    x: b.left + b.width / 2, y: b.top + b.height / 2, name: e.querySelector('.covname').getBoundingClientRect().right }; }"""
SF_PANEL_JS = """() => { const p = document.getElementById('src_panel'), b = document.getElementById('src_body');
  return { open: !p.hidden && p.getClientRects().length > 0, file: b.dataset.file || '', state: b.dataset.state || '',
    title: (document.getElementById('src_title') || {}).textContent || '', text: b.innerText.slice(0, 4000),
    hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""


def sf_shots(page, shots, stem):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))


def sf_press(page, phone, x, y):
    if phone:
        page.touchscreen.tap(x, y)
    else:
        page.mouse.click(x, y)


def sf_case(browser, base, shots, phone, scheme):
    tag = "390" if phone else "1280"
    problems, r, errors = [], {}, []
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html", timeout=180000)
        if not wait_for(lambda: gate_on_screen(page), 120):
            return [f"{tag}: the default kernel did not boot"], r
        # The Kernel menu, as a user picks it.
        page.select_option("#kernel", "minified-linux")
        if not wait_for(lambda: "minified-linux" in page.inner_text("#status"), 120):
            return [f"{tag}: picking minified Linux did not load it: {page.inner_text('#status')[:200]}"], r
        if phone:
            page.tap("#tab_source")
        else:
            page.click("#tab_source")
        if not wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 60):
            return [f"{tag}: the Source tab's tree did not load: {page.get_attribute('#cov_tree', 'data-state')}"], r
        if phone:
            # A tap on the middle of a directory's row (on its counts, not its name).
            row = page.evaluate(SF_ROW_JS, "init")
            r["init row"] = row
            if not row:
                problems.append(f"{tag}: no init/ row in the tree")
            else:
                if row["x"] <= row["name"]:
                    problems.append(f"{tag}: the middle of init/'s row is on its name ({row['x']} <= {row['name']})")
                kids = "#cov_tree .covrow[data-path^='init/']"
                n0 = page.locator(kids).count()
                sf_press(page, phone, row["x"], row["y"])
                opened = wait_for(lambda: page.locator(kids).count() > n0, 5)
                r["init kids"] = [n0, page.locator(kids).count()]
                if not opened:
                    problems.append(f"{tag}: a tap on the middle of init/'s row did not open it ({n0} rows below it)")
                else:
                    row = page.evaluate(SF_ROW_JS, "init")
                    sf_press(page, phone, row["x"], row["y"])
                    if not wait_for(lambda: page.locator(kids).count() == n0, 5):
                        problems.append(f"{tag}: a second tap on the middle of init/'s row did not close it")
        page.click("#cov_filter")
        page.keyboard.type("init.nasm")
        if not wait_for(lambda: page.evaluate(SF_ROW_JS, SF_FILE), 10):
            rows = page.eval_on_selector_all("#cov_tree .covrow[data-path]", "es => es.map(e => e.dataset.path)")
            problems.append(f"{tag}: the tree filtered by init.nasm has no {SF_FILE} row (rows: {rows[:12]})")
            sf_shots(page, shots, f"srcfind-{tag}-missing")
            return problems, r
        row = page.evaluate(SF_ROW_JS, SF_FILE)
        r["row"] = row
        if row["code"] != "none" or "no code" not in row["text"] or "%" in row["text"]:
            problems.append(f"{tag}: {SF_FILE}'s row is not marked as a file without code (no counts): {row}")
        sf_shots(page, shots, f"srcfind-{tag}-tree")
        if phone:
            sf_press(page, phone, row["x"], row["y"])
        else:
            page.click(f'#cov_tree .covrow[data-path="{SF_FILE}"] .covname')
        if not wait_for(lambda: page.evaluate(SF_PANEL_JS)["state"] == "ok" and page.evaluate(SF_PANEL_JS)["file"] == SF_FILE, 30):
            problems.append(f"{tag}: clicking {SF_FILE}'s row did not open it: {page.evaluate(SF_PANEL_JS)}")
        else:
            got = page.evaluate(SF_PANEL_JS)
            r["panel"] = [got["title"], got["text"][:120]]
            if not got["open"] or "init.nasm" not in got["title"] or "Hello, World!" not in got["text"]:
                problems.append(f"{tag}: the Source panel does not show {SF_FILE}: {got['title']!r} {got['text'][:200]!r}")
            if got["hscroll"]:
                problems.append(f"{tag}: the page scrolls sideways with the panel open")
        sf_shots(page, shots, f"srcfind-{tag}-panel")
        if errors:
            problems.append(f"{tag}: page errors: {errors[:3]}")
        return problems, r
    finally:
        ctx.close()


def suite_srcfind(ctx):
    failed = False
    for phone, scheme in ((False, "light"), (True, "dark")):
        problems, r = sf_case(ctx.browser, ctx.base, ctx.shots, phone, scheme)
        failed |= report("srcfind " + ("390" if phone else "1280"), problems, r)
    return failed
