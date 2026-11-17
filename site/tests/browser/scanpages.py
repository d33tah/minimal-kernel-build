"""Browser test suite scanpages (test_browser.py --suites)."""

import time

from tests.browser.common import open_page, report, show_tab, wait_for


# ---- scanpages: which pages a memory scan reports changed (P1a) -------------
# A scan (Scan, Auto-scan, every stop and every few frames while running)
# marks the 4 KiB pages that differ from the scan before, counts the non-zero
# ones, and the Page tab marks the selected page's bytes changed since the
# last scan (and, drawn by the scan itself, since the previous one). The
# machine stays paused at the reset vector and the test writes guest RAM
# itself, so it knows every change: one byte, the last word, a page zeroed,
# a zero page written, two words swapped, a byte written and put back, two
# words changed by amounts that cancel in a sum. Its own copies of RAM (not
# the page's) give the pages that must be reported.
SCANP_SETUP_JS = """() => { const P = 4096, pages = MKB.emuRam / P, T = [];
  for (let i = 0; i < 9; i++) T.push(pages - 24 + i);
  for (let i = 0; i < 8; i++) { const b = new Uint8Array(P); for (let k = 0; k < P; k++) b[k] = (i * 37 + k * 13 + (k >> 7)) & 0xff || 1;
    MKB.emulator.write_memory(b, T[i] * P); }
  MKB.emulator.write_memory(new Uint8Array(P), T[8] * P);
  return { pages, T }; }"""
SCANP_COPY_JS = "() => { window.__scanCopy = MKB.emulator.read_memory(0, MKB.emuRam).slice(); return MKB.emuRam; }"
# Pages that differ between the test's copy and RAM now, and the non-zero ones.
SCANP_WANT_JS = """() => { const P = 4096, a = window.__scanCopy, b = MKB.emulator.read_memory(0, MKB.emuRam), changed = [], used = [];
  for (let p = 0; p < MKB.emuRam / P; p++) { let c = false, u = false;
    for (let i = p * P; i < p * P + P; i++) { if (a[i] !== b[i]) c = true; if (b[i]) u = true; if (c && u) break; }
    if (c) changed.push(p); if (u) used.push(p); }
  return { changed, used }; }"""
SCANP_GOT_JS = """() => { const f = document.getElementById('footprint'), l = document.getElementById('listing');
  const idx = (a) => { const o = []; if (a) for (let p = 0; p < a.length; p++) if (a[p]) o.push(p); return o; };
  return { changed: idx(MKB.changedPages), used: idx(MKB.usedPages), fpChanged: f.dataset.changed, fpNonzero: f.dataset.nonzero,
    page: l.dataset.page, lchanged: l.dataset.changed, marks: l.querySelectorAll('mark').length,
    info: document.getElementById('dump_info').textContent }; }"""
# (marks: a changed byte is marked twice, in the hex and in the ASCII column.)
# A scan with the Page tab shown, read in the same task: a later drawing of
# the listing (the Page tab draws again once it has its width) says "since
# the last scan" instead of the scan's own "since the previous scan".
SCANP_SCAN_JS = "() => { MKB.scan(); return (" + SCANP_GOT_JS + ")(); }"
# The grid's cell of page p has a pixel of the "changed" mark's colour.
SCANP_MARK_JS = """(p) => { const c = document.getElementById('pages'), cell = Number(c.dataset.cell), cols = Number(c.dataset.cols);
  const px = c.getContext('2d').getImageData((p % cols) * cell, Math.floor(p / cols) * cell, cell, cell).data;
  const t = document.createElement('canvas').getContext('2d');
  t.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--changed').trim(); t.fillRect(0, 0, 1, 1);
  const want = t.getImageData(0, 0, 1, 1).data;
  for (let i = 0; i < px.length; i += 4)
    if (px[i + 3] === 255 && [0, 1, 2].every((k) => Math.abs(px[i + k] - want[k]) <= 2)) return true;
  return false; }"""


def scanpages_case(browser, base, shots, name, problems, r):
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#memory")
    if not wait_for(lambda: page.evaluate("typeof window.MKB?.emuReady !== 'undefined' && MKB.emuReady && MKB.runState === 'paused'"), 120):
        problems.append(f"{name}: not paused at the reset vector")
        page.close()
        return
    setup = page.evaluate(SCANP_SETUP_JS)
    T = setup["T"]
    A, B, N2, E, F, G, U, S, Z = T
    r[f"{name} pages"] = setup["pages"]
    show_tab(page, "memory")
    page.click("#scan")
    page.click("#scan")
    page.evaluate(SCANP_COPY_JS)

    def check(tag, extra=None, got=None):
        want, got = page.evaluate(SCANP_WANT_JS), got or page.evaluate(SCANP_GOT_JS)
        if extra is not None and sorted(want["changed"]) != sorted(extra):
            problems.append(f"{name} {tag}: test setup: RAM changed in {want['changed']}, the test wrote {extra}")
        if got["changed"] != want["changed"] or got["fpChanged"] != str(len(want["changed"])):
            problems.append(f"{name} {tag}: scan reports pages {got['changed'][:20]} ({got['fpChanged']}) changed, RAM changed in {want['changed'][:20]}")
        if got["used"] != want["used"] or got["fpNonzero"] != str(len(want["used"])):
            problems.append(f"{name} {tag}: scan reports {got['fpNonzero']} non-zero pages, RAM has {len(want['used'])}")
        return got

    got = check("a scan with nothing written", [])
    if got["fpChanged"] != "0":
        problems.append(f"{name}: two scans in a row of a paused machine: {got['fpChanged']} pages changed")
    def settle():
        # The listing is drawn again once it has its width: wait for the draws to stop.
        for _ in range(20):
            d0 = page.get_attribute("#listing", "data-draws")
            time.sleep(0.3)
            if page.get_attribute("#listing", "data-draws") == d0:
                break

    # A, selected before the scan: its bytes since the last scan, then since the previous one.
    if not open_page(page, A):
        problems.append(f"{name}: page {A:#x} did not open")
    settle()
    got = page.evaluate(SCANP_GOT_JS)
    if got["lchanged"] != "0" or "0 changed since the last scan" not in got["info"]:
        problems.append(f"{name}: page {A:#x} right after the scan: {got['lchanged']} changed, {got['info']!r}")
    page.evaluate(f"MKB.emulator.write_memory(new Uint8Array([0xee]), {A * 4096 + 100})")
    page.evaluate("MKB.inspect()")
    got = page.evaluate(SCANP_GOT_JS)
    if got["lchanged"] != "1" or got["marks"] != 2 or "1 changed since the last scan" not in got["info"]:
        problems.append(f"{name}: page {A:#x} with one byte written after the scan: {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    # Other pages: last word, zeroed, two words swapped, written and put back,
    # two words changed by +k and -k*31 (a 31-multiply sum would not see it),
    # and a zero page written.
    page.evaluate(f"""() => {{ const P = 4096, w = (b, a) => MKB.emulator.write_memory(new Uint8Array(b), a);
      w([1, 2, 3, 4], {B} * P + P - 4);
      MKB.emulator.write_memory(new Uint8Array(P), {N2} * P);
      const e = MKB.emulator.read_memory({E} * P, 8).slice(); w([...e.subarray(4, 8), ...e.subarray(0, 4)], {E} * P);
      const f = MKB.emulator.read_memory({F} * P + 7, 1)[0]; w([f ^ 0x55], {F} * P + 7); w([f], {F} * P + 7);
      const g = new DataView(MKB.emulator.read_memory({G} * P, 8).slice().buffer);
      g.setInt32(0, g.getInt32(0, true) + 31, true); g.setInt32(4, g.getInt32(4, true) - 1, true);
      w(new Uint8Array(g.buffer), {G} * P);
      w([9], {Z} * P + 4000); }}""")
    show_tab(page, "page")
    scanned = page.evaluate(SCANP_SCAN_JS)
    wanted = sorted([A, B, N2, E, G, Z])
    got = check("a scan after writes", wanted, scanned)
    if got["page"] != str(A) or got["lchanged"] != "1" or got["marks"] != 2 or "1 changed since the previous scan" not in got["info"]:
        problems.append(f"{name}: the scan's drawing of page {A:#x}: page {got['page']}, {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    show_tab(page, "memory")
    page.evaluate("MKB.drawGrid()")
    marks = {p: page.evaluate(SCANP_MARK_JS, p) for p in T}
    r[f"{name} grid marks"] = {hex(p): v for p, v in marks.items()}
    if any(marks[p] != (p in wanted) for p in T):
        problems.append(f"{name}: the grid marks changed pages { [hex(p) for p in T if marks[p]] }, want { [hex(p) for p in wanted] }")
    page.evaluate(SCANP_COPY_JS)
    # B, selected after the scan and not written since: its bytes as the scan saw them.
    open_page(page, B)
    settle()
    got = page.evaluate(SCANP_GOT_JS)
    if got["lchanged"] != "0" or "0 changed since the last scan" not in got["info"]:
        problems.append(f"{name}: page {B:#x} selected after the scan: {got['lchanged']} changed, {got['info']!r}")
    page.evaluate(f"MKB.emulator.write_memory(new Uint8Array([0x77, 0x78]), {B * 4096 + 2000})")
    page.evaluate("MKB.inspect()")
    got = page.evaluate(SCANP_GOT_JS)
    if got["lchanged"] != "2" or got["marks"] != 4 or "2 changed since the last scan" not in got["info"]:
        problems.append(f"{name}: page {B:#x} with two bytes written after its selection: {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    # U, written after the scan and selected then (code copied and then
    # jumped to): its byte changed since the scan is marked too.
    page.evaluate(f"MKB.emulator.write_memory(new Uint8Array([0x42]), {U * 4096 + 9})")
    open_page(page, U)
    settle()
    got = page.evaluate(SCANP_GOT_JS)
    r[f"{name} selected after a change"] = got["info"]
    if got["lchanged"] != "1" or got["marks"] != 2 or "1 changed since the last scan" not in got["info"]:
        problems.append(f"{name}: page {U:#x} written, then selected: {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    scanned = page.evaluate(SCANP_SCAN_JS)
    got = check("a scan after a write to the selected page", [B, U], scanned)
    if got["lchanged"] != "1" or got["marks"] != 2 or "1 changed since the previous scan" not in got["info"]:
        problems.append(f"{name}: the scan's drawing of page {U:#x}: {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    page.evaluate(SCANP_COPY_JS)
    page.evaluate(f"MKB.emulator.write_memory(new Uint8Array([0x43]), {U * 4096 + 10})")
    scanned = page.evaluate(SCANP_SCAN_JS)
    got = check("a scan after a second write to the selected page", [U], scanned)
    if got["lchanged"] != "1" or got["marks"] != 2 or "1 changed since the previous scan" not in got["info"]:
        problems.append(f"{name}: page {U:#x} written between two scans while selected: {got['lchanged']} changed, {got['marks']} marks, {got['info']!r}")
    page.evaluate(SCANP_COPY_JS)
    # One copy of RAM, the same buffer from scan to scan (P1a: no copy per scan).
    page.evaluate("window.__scanBuf = typeof window.MKB?.scanCopy === 'undefined' ? null : MKB.scanCopy")
    scanned = page.evaluate(SCANP_SCAN_JS)
    one = page.evaluate("!!window.__scanBuf && MKB.scanCopy === window.__scanBuf && MKB.scanCopy.length === MKB.emuRam && typeof scanPrev === 'undefined'")
    if not one:
        problems.append(f"{name}: a scan made a new copy of RAM or kept a second one")
    got = check("a scan with nothing written since", [], scanned)
    if got["lchanged"] != "0" or "0 changed since the previous scan" not in got["info"]:
        problems.append(f"{name}: page {U:#x} unchanged between two scans: {got['lchanged']} changed, {got['info']!r}")
    for w in (1280, 390):
        page.set_viewport_size({"width": w, "height": 1000 if w > 400 else 844})
        for tab in ("memory", "page"):
            show_tab(page, tab)
            time.sleep(0.4)
            page.screenshot(path=str(shots / f"scanpages-{name}-{tab}-{w}.png"))
    if errors:
        problems.append(f"{name}: page errors {errors[:3]}")
    page.close()


def suite_scanpages(ctx):
    problems, r = [], {}
    scanpages_case(ctx.browser, ctx.base, ctx.shots, "hello", problems, r)
    return report("scanpages", problems, r)
