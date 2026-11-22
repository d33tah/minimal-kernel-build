"""Browser test suite panelfit (test_browser.py --suites)."""

import json
import time

from tests.browser.common import PHONE, SITE, gate_on_screen, kernel_entries, report, show_tab, snap_save, wait_for


# ---- panelfit: the tabs beside the Source panel (A1b) -----------------------
# The runner's check of A1 (1280 light, 390 dark): (1) after the Kernel menu
# switched hello to the minified Linux the Source panel stayed open on
# "boot/hello.c: not shipped"; it must close or show a file of the new
# kernel. (2) With the panel open the Machine tab's counter table (until,
# from 1 MiB, ...) and the run state line ran on past the room left of the
# panel (the table scrolled inside its box, the state's file:line was cut),
# and at 390 px the table's "from reset" column was cut. (3) With the panel
# open the Snapshots table squeezed the name to one or two characters a line
# and cut "compression". Checked: every element of the tab ends left of the
# panel (at 390 px: inside the viewport) and none runs past a box that clips
# or scrolls it sideways (the VGA screen's own box aside); a snapshot's name
# is at least 8 characters wide. Screenshots light and dark.
PF_FIT_JS = """(sel) => {
  const p = document.getElementById('src_panel'), open = !p.hidden && p.getClientRects().length > 0;
  const limit = open && innerWidth > 640 ? p.getBoundingClientRect().left : innerWidth;
  const root = document.querySelector(sel), hits = [];
  const name = (e) => e.id ? '#' + e.id : e.tagName.toLowerCase() + (typeof e.className === 'string' && e.className ? '.' + e.className.split(' ')[0] : '');
  for (const el of root.querySelectorAll('*')) {
    if (!el.getClientRects().length || el.closest('#screen_container') || getComputedStyle(el).position === 'fixed') continue;
    const r = el.getBoundingClientRect();
    if (r.width < 0.5 || r.height < 0.5 || getComputedStyle(el).visibility === 'hidden') continue;
    if (r.right > limit + 1) { hits.push(name(el) + ' ends at ' + Math.round(r.right) + ' > ' + Math.round(limit)); continue; }
    for (let a = el.parentElement; a && a !== root.parentElement; a = a.parentElement)
      if (getComputedStyle(a).overflowX !== 'visible' && r.right > a.getBoundingClientRect().right + 1) {
        hits.push(name(el) + ' runs past ' + name(a) + ' (' + Math.round(r.right) + ' > ' + Math.round(a.getBoundingClientRect().right) + ')');
        break;
      }
  }
  return { open, limit: Math.round(limit), n: hits.length, hits: hits.slice(0, 8), hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""
PF_SNAPNAME_JS = """() => [...document.querySelectorAll('#snapshots .snapname')].map((e) => {
  const fs = parseFloat(getComputedStyle(e).fontSize); return { w: Math.round(e.getBoundingClientRect().width), ch: +(e.getBoundingClientRect().width / (0.6 * fs)).toFixed(1) }; })"""
PF_PANEL_JS = """() => { const p = document.getElementById('src_panel'), b = document.getElementById('src_body');
  return { open: !p.hidden && p.getClientRects().length > 0, file: b ? b.dataset.file || '' : '', state: b ? b.dataset.state || '' : '',
    text: p.innerText.slice(0, 300) }; }"""


def pf_shots(page, shots, stem):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))


def pf_fit(page, sel, tag, problems, r):
    got = page.evaluate(PF_FIT_JS, sel)
    r[tag] = got
    if got["hits"] or got["hscroll"]:
        problems.append(f"{tag}: {got['n']} elements do not fit (right edge {got['limit']}): {got['hits']}, sideways scroll {got['hscroll']}")


def pf_boot_full(page):
    """Boot the kernel shown at full speed, counted, to its second line."""
    show_tab(page, "machine")
    page.select_option("#dbg_speed", "full")
    page.click("#boot")
    time.sleep(1)
    wait_for(lambda: gate_on_screen(page), 240)
    time.sleep(3)


def pf_open_from_tree(page, phone, path):
    """Open path in the Source panel from the Source tab's tree, as a user:
    the filter, then the file's name."""
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 60)
    page.fill("#cov_filter", path.rsplit("/", 1)[-1])
    sel = f'#cov_tree .covrow[data-path="{path}"] .covname'
    if not wait_for(lambda: page.locator(sel).count() > 0, 10):
        return False
    page.tap(sel) if phone else page.click(sel)
    page.fill("#cov_filter", "")
    return wait_for(lambda: page.evaluate(PF_PANEL_JS)["file"] == path and page.evaluate(PF_PANEL_JS)["state"] == "ok", 30)


def pf_desktop_case(browser, base, shots):
    """1280 light: hello, its hello.c open from the tree, the Kernel menu's
    minified Linux; then the Machine tab (hello halted, the minified Linux
    counted) and the Snapshots tab with a snapshot, the panel open."""
    problems, r, errors = [], {}, []
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, color_scheme="light")
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html", timeout=180000)
        if not wait_for(lambda: gate_on_screen(page), 120):
            return ["1280: hello did not boot"], r
        if not pf_open_from_tree(page, False, "boot/hello.c"):
            return [f"1280: boot/hello.c did not open from the Source tab: {page.evaluate(PF_PANEL_JS)}"], r
        # hello halted, the panel open: the Machine tab and a snapshot.
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
        show_tab(page, "machine")
        time.sleep(0.8)
        pf_fit(page, "#panel_machine", "1280 hello Machine, panel open", problems, r)
        pf_shots(page, shots, "panelfit-1280-hello-machine")
        show_tab(page, "snapshots")
        snap_save(page, 1, False)
        time.sleep(0.5)
        pf_fit(page, "#panel_snapshots", "1280 Snapshots, panel open", problems, r)
        names = page.evaluate(PF_SNAPNAME_JS)
        r["snapshot names"] = names
        if not names or min(x["ch"] for x in names) < 8:
            problems.append(f"1280 Snapshots, panel open: a snapshot's name is narrower than 8 characters: {names}")
        pf_shots(page, shots, "panelfit-1280-snapshots")
        # The Kernel menu: the panel must not stay on hello's file.
        show_tab(page, "machine")
        page.select_option("#kernel", "minified-linux")
        if not wait_for(lambda: "minified-linux" in page.inner_text("#status"), 120):
            return problems + [f"1280: picking minified Linux did not load it: {page.inner_text('#status')[:200]}"], r
        time.sleep(2)
        entry = kernel_entries()["minified-linux"]
        known = {f[0] for f in json.loads((SITE / entry["dwarf"] / "files.json").read_text())["files"]}
        known |= set(json.loads((SITE / entry["src"] / "index.json").read_text())["files"])
        got = page.evaluate(PF_PANEL_JS)
        r["panel after the switch"] = got
        if got["open"] and (got["file"] not in known or "not shipped" in got["text"]):
            problems.append(f"1280: after switching to minified Linux the Source panel still shows {got['file']!r}: {got['text'][:120]!r}")
        pf_shots(page, shots, "panelfit-1280-switched")
        # The minified Linux counted to its end, its init/main.c open.
        pf_boot_full(page)
        if not pf_open_from_tree(page, False, "init/main.c"):
            problems.append(f"1280: init/main.c did not open from the Source tab: {page.evaluate(PF_PANEL_JS)}")
        show_tab(page, "machine")
        time.sleep(0.8)
        pf_fit(page, "#panel_machine", "1280 minified Machine, panel open", problems, r)
        pf_shots(page, shots, "panelfit-1280-minified-machine")
        if errors:
            problems.append(f"1280: page errors: {errors[:3]}")
        return problems, r
    finally:
        ctx.close()


def pf_phone_case(browser, base, shots):
    """390 dark, touch: hello halted and the minified Linux counted, the
    Machine tab with the sheet closed and open; the Snapshots tab with a
    snapshot."""
    problems, r, errors = [], {}, []
    ctx = browser.new_context(color_scheme="dark", **PHONE)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html", timeout=180000)
        if not wait_for(lambda: gate_on_screen(page), 120):
            return ["390: hello did not boot"], r
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
        time.sleep(0.8)
        pf_fit(page, "#panel_machine", "390 hello Machine", problems, r)
        pf_shots(page, shots, "panelfit-390-hello-machine")
        show_tab(page, "snapshots")
        snap_save(page, 1, True)
        time.sleep(0.5)
        pf_fit(page, "#panel_snapshots", "390 Snapshots", problems, r)
        pf_shots(page, shots, "panelfit-390-snapshots")
        show_tab(page, "machine")
        page.select_option("#kernel", "minified-linux")
        if not wait_for(lambda: "minified-linux" in page.inner_text("#status"), 120):
            return problems + ["390: picking minified Linux did not load it"], r
        pf_boot_full(page)
        pf_fit(page, "#panel_machine", "390 minified Machine", problems, r)
        pf_shots(page, shots, "panelfit-390-minified-machine")
        if not pf_open_from_tree(page, True, "init/main.c"):
            problems.append(f"390: init/main.c did not open from the Source tab: {page.evaluate(PF_PANEL_JS)}")
        show_tab(page, "machine")
        time.sleep(0.8)
        pf_fit(page, "#panel_machine", "390 minified Machine, sheet open", problems, r)
        if errors:
            problems.append(f"390: page errors: {errors[:3]}")
        return problems, r
    finally:
        ctx.close()


def suite_panelfit(ctx):
    failed = report("panelfit 1280", *pf_desktop_case(ctx.browser, ctx.base, ctx.shots))
    failed |= report("panelfit 390", *pf_phone_case(ctx.browser, ctx.base, ctx.shots))
    return failed
