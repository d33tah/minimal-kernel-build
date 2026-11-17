"""Browser test suite memtabs (test_browser.py --suites)."""

import time

from tests.browser.common import machine_do, mev_page, report, show_tab, shown_tab, wait_for
from tests.browser.tabs import tabs_look


# ---- memtabs: Events and Quests as tabs of their own (M4) -------------------
# Operator 2026-11-15: "quests powinien być nową zakładką, events też i jest
# nieczytelne". The memory events list and the quests leave the Memory tab
# (which keeps the grid and the live memory map) for an Events tab and a
# Quests tab with an emoji and a word like the others. Checked: deep links
# (#events, #quests, ?tab=, ?memevents=1 and ?quests=1 open those tabs, a
# hash wins over them); the live memory map goes with the Quests tab (its
# quests are answered on it) and back to the Memory tab; recording goes on
# while the Events tab is hidden, and a Record box the user cleared stays
# cleared; a kernel switch starts a new log in the table and the quests
# count for the new kernel. Readable: a table with plain column names (When,
# Who, What, Where, Region, Code), repeated accesses grouped into one row
# with their count (the text a run of writes put into text memory spelled
# out), filters by initiator, read/write and region, cells at 13 px or more
# with a contrast of 4.5 or more (WCAG AA) in light and dark; quest cards
# say their state in words, at full opacity. At 390 px the eleven emoji
# share one row, the table's rows become cards, no horizontal scroll.
# The Events and Quests tabs are mounted when first shown (shell.jsx): their
# text is null until then.
MT_LOOK_JS = """() => { const shown = document.querySelector('.tabbar [role=tab][aria-selected=true]');
  const rows = [...document.querySelectorAll('#mev_rows tr')];
  const text = (id) => { const e = document.getElementById(id); return e ? e.textContent : null; };
  return { tab: shown && shown.dataset.tab, hash: location.hash, on: MKB.MEV.on, n: MKB.MEV.n, gen: MKB.MEV.gen, quests: MKB.QUESTS.open,
    status: text('mev_status'), record: document.getElementById('mev_on') ? document.getElementById('mev_on').checked : null,
    heads: [...document.querySelectorAll('#mev_table thead th')].map((t) => t.textContent.trim()),
    rows: rows.map((tr) => ({ n: Number(tr.dataset.n || 0), cells: Object.fromEntries([...tr.cells].map((td) => [td.dataset.col, td.textContent.trim()])) })),
    regions: document.querySelectorAll('#mev_regions tr').length, facts: document.querySelectorAll('#mev_facts li').length,
    regionOptions: [...document.querySelectorAll('#mev_regionf button')].map((o) => o.textContent),
    mapIn: (document.getElementById('mm').closest('[role=tabpanel]') || {}).id,
    progress: text('qs_progress'), badge: text('qs_badge'),
    hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""
# Font size and contrast (WCAG 2 relative luminance) of each element with
# text of its own under sel, against the first opaque background above it;
# opacity of it and its ancestors multiplies into the text's alpha.
MT_READ_JS = """(sel) => { const ch = (c) => (c.match(/[\\d.]+/g) || []).map(Number);
  const lin = (x) => { x /= 255; return x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4; };
  const lum = (v) => 0.2126 * lin(v[0]) + 0.7152 * lin(v[1]) + 0.0722 * lin(v[2]);
  const bgOf = (e) => { for (; e; e = e.parentElement) { const b = ch(getComputedStyle(e).backgroundColor); if (b.length === 3 || b[3] > 0.5) return b; }
    return ch(getComputedStyle(document.body).backgroundColor); };
  const out = [];
  for (const e of document.querySelectorAll(sel)) {
    const own = [...e.childNodes].filter((n) => n.nodeType === 3).map((n) => n.textContent).join('').trim();
    if (!own || !e.getClientRects().length) continue;
    const cs = getComputedStyle(e); let a = 1;
    for (let p = e; p; p = p.parentElement) a *= Number(getComputedStyle(p).opacity);
    const bg = bgOf(e), fg0 = ch(cs.color); const fa = (fg0.length > 3 ? fg0[3] : 1) * a;
    const fg = fg0.slice(0, 3).map((x, k) => x * fa + bg[k] * (1 - fa));
    const l1 = lum(fg), l2 = lum(bg);
    out.push({ text: own.slice(0, 30), cls: e.className, px: parseFloat(cs.fontSize), ratio: +((Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05)).toFixed(2) });
    if (out.length >= 60) break;
  }
  return out; }"""


def mt_look(page):
    page.evaluate("MKB.MEV.render()")
    return page.evaluate(MT_LOOK_JS)


def mt_readable(page, sel, minpx, problems, label):
    got = page.evaluate(MT_READ_JS, sel)
    if not got:
        problems.append(f"{label}: no text under {sel}")
    for g in got:
        if g["px"] < minpx or g["ratio"] < 4.5:
            problems.append(f"{label}: {g['text']!r} ({g['cls']}) {g['px']} px, contrast {g['ratio']} (want >= {minpx} px, >= 4.5)")
            break
    return got


def mt_end(page, base, q, timeout=600):
    page.goto(f"{base}/index.html?{q}", timeout=180000)
    return wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), timeout, 0.5)


def mt_links_case(browser, base, shots, problems, r):
    """Deep links, the map's place, the hash of a tab click (hello, 1280 light)."""
    ctx, page, errs = mev_page(browser)
    k = "kernel=hello&speed=full&count=0&mips=10"
    try:
        for q, tab, rec, qs in ((f"{k}&memevents=1", "events", True, None), (f"{k}#events", "events", True, None),
                                (f"{k}&quests=1", "quests", None, True), (f"{k}#quests", "quests", None, True),
                                (f"{k}&quests=1&memevents=1", "quests", True, True), (f"{k}&tab=events", "events", True, None),
                                (f"{k}&tab=quests", "quests", None, True), (f"{k}&memevents=1#memory", "memory", True, None)):
            page.goto("about:blank")
            if not mt_end(page, base, q, 120):
                problems.append(f"links {q}: hello did not end")
                continue
            time.sleep(0.6)
            s = mt_look(page)
            r[f"link {q}"] = {k2: s[k2] for k2 in ("tab", "hash", "on", "quests", "mapIn")}
            if s["tab"] != tab or s["hash"] != "#" + tab:
                problems.append(f"links {q}: tab {s['tab']} hash {s['hash']}, want {tab}")
            if rec is not None and s["on"] != rec:
                problems.append(f"links {q}: recording {s['on']}, want {rec}")
            if qs is not None and s["quests"] != qs:
                problems.append(f"links {q}: quests on {s['quests']}, want {qs}")
            if tab == "events" and (not s["rows"] or s["n"] < 5):
                problems.append(f"links {q}: the Events tab shows {len(s['rows'])} rows of {s['n']} events")
            want_map = "panel_quests" if tab == "quests" else "panel_memory"
            if s["mapIn"] != want_map:
                problems.append(f"links {q}: the live memory map is in {s['mapIn']}, want {want_map}")
        for tab, panel in (("events", "panel_memory"), ("quests", "panel_quests"), ("memory", "panel_memory"), ("events", "panel_memory")):
            page.click(f"#tab_{tab}")
            time.sleep(0.3)
            s = mt_look(page)
            if s["tab"] != tab or s["hash"] != "#" + tab or s["mapIn"] != panel:
                problems.append(f"click {tab}: tab {s['tab']} hash {s['hash']} map in {s['mapIn']}")
        if errs:
            problems.append(f"links: page errors {errs[:3]}")
    finally:
        ctx.close()


def mt_hidden_case(browser, base, shots, entries, problems, r):
    """Recording while the Events tab is hidden, Record cleared by the user,
    a kernel switch, filters, grouping, readability (1280, light and dark)."""
    for scheme in ("light", "dark"):
        ctx, page, errs = mev_page(browser, scheme)
        try:
            page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0&mips=10&paused=1", timeout=180000)
            if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused'"), 120, 0.3):
                problems.append(f"hidden {scheme}: hello not paused at the reset vector")
                continue
            before = mt_look(page)
            show_tab(page, "events")
            time.sleep(0.3)
            opened = mt_look(page)
            show_tab(page, "machine")
            page.click("#dbg_continue")
            seen_on = []
            ok = wait_for(lambda: seen_on.append(page.evaluate("MKB.MEV.on")) or page.get_attribute("#end_banner", "data-kind"), 120, 0.2)
            hidden = page.evaluate("({ n: MKB.MEV.n, on: MKB.MEV.on, cpu: MKB.MEV.counts[0], tab: document.querySelector('.tabbar [aria-selected=true]').dataset.tab })")
            show_tab(page, "events")
            time.sleep(0.6)
            back = mt_look(page)
            r[f"hidden {scheme}"] = {"before": [before["on"], before["n"]], "opened": [opened["on"], opened["n"], opened["record"]],
                                     "hidden": hidden, "back rows": len(back["rows"]), "status": back["status"]}
            if before["on"]:
                problems.append(f"hidden {scheme}: recording before the Events tab was opened")
            if not opened["on"] or not opened["record"]:
                problems.append(f"hidden {scheme}: opening the Events tab did not start recording: {opened['status']!r}")
            if not ok or not all(seen_on) or hidden["cpu"] < 10 or hidden["tab"] != "machine":
                problems.append(f"hidden {scheme}: with the Machine tab shown: {hidden}, recording all the time {all(seen_on)}")
            if not back["rows"] or back["n"] != hidden["n"]:
                problems.append(f"hidden {scheme}: back on Events {len(back['rows'])} rows, {back['n']} events (recorded {hidden['n']})")
            # the table: plain names, grouped rows, readable
            if back["heads"] != ["Step", "Who", "Access", "Address", "Size", "Value", "Region (named by)", "Code"]:
                problems.append(f"hidden {scheme}: column names {back['heads']}")
            cpu_rows = [x for x in back["rows"] if x["cells"].get("who", "").startswith("CPU")]
            r[f"hello rows {scheme}"] = back["rows"][:6]
            if not cpu_rows or not any(x["n"] > 1 for x in cpu_rows):
                problems.append(f"hidden {scheme}: no grouped CPU row: {back['rows'][:4]}")
            if not any("Hello, World!" in x["cells"].get("value", "") for x in back["rows"]):
                problems.append(f"hidden {scheme}: no row spells the text hello wrote: {[x['cells'].get('value') for x in back['rows'][:8]]}")
            if not any("put_line" in x["cells"].get("code", "") and "hello.c:" in x["cells"]["code"] for x in cpu_rows):
                problems.append(f"hidden {scheme}: no CPU row names put_line hello.c: {[x['cells'].get('code') for x in cpu_rows[:5]]}")
            if not any(x["cells"].get("region", "").endswith("IBM PC memory map") for x in back["rows"]):
                problems.append(f"hidden {scheme}: no region names its standard: {[x['cells'].get('region') for x in back['rows'][:5]]}")
            if back["regions"] < 8:
                problems.append(f"hidden {scheme}: {back['regions']} regions listed")
            mt_readable(page, "#mev_table th, #mev_rows td, #mev_rows td *", 13, problems, f"events {scheme}")
            # reads and writes apart at a glance, not by colour alone: the
            # badge's marker, fill and border style, the access's weight
            rw = page.evaluate("""() => Object.fromEntries(['read', 'write'].map((k) => { const a = document.querySelector(`#mev_rows .mev-rw[data-rw=${k}]`);
              if (!a) return [k, null]; const b = a.querySelector('.mev-rwb'), cs = getComputedStyle(b);
              return [k, { mark: b.textContent, bg: cs.backgroundColor, border: cs.borderTopStyle, weight: getComputedStyle(a).fontWeight }]; }))""")
            r[f"read/write {scheme}"] = rw
            rd, wr = rw.get("read"), rw.get("write")
            if not rd or not wr or rd["mark"] == wr["mark"] or "R" not in rd["mark"] or "W" not in wr["mark"] or rd["bg"] == wr["bg"] or \
                    rd["border"] == wr["border"] or int(wr["weight"]) <= int(rd["weight"]):
                problems.append(f"read/write {scheme}: a read row and a write row do not differ in marker, fill, border and weight: {rw}")
            page.screenshot(path=str(shots / f"memtabs-events-1280-{scheme}.png"))
            line =page.evaluate("MKB.MEV.describe(MKB.MEV.event(MKB.MEV.query({ initiator: 'emulator' }).find((i) => MKB.MEV.event(i).source === 'kernel protected-mode part')))")
            # filter chips, the search box, grouping
            page.click("#mev_who [data-v=emulator]")
            emu = mt_look(page)["rows"]
            pressed = page.evaluate("[...document.querySelectorAll('#mev_who [aria-pressed=true]')].map((b) => b.dataset.v)")
            page.click("#mev_who [data-v='']")
            page.click("#mev_rw [data-v=read]")
            reads = mt_look(page)["rows"]
            page.click("#mev_rw [data-v='']")
            vga = next((o for o in back["regionOptions"] if "colour text" in o), None)
            if vga:
                page.locator("#mev_regionf button", has_text="colour text").first.click()
            inreg = mt_look(page)["rows"]
            page.click("#mev_regionf [data-v='']")
            page.fill("#mev_search", "put_line")
            found = mt_look(page)["rows"]
            page.fill("#mev_search", "")
            page.uncheck("#mev_group")
            flat = mt_look(page)["rows"]
            page.check("#mev_group")
            r[f"filters {scheme}"] = {"emulator": len(emu), "pressed": pressed, "reads": len(reads), "region": [vga, len(inreg)],
                                      "search": len(found), "flat": len(flat)}
            if pressed != ["emulator"]:
                problems.append(f"filters {scheme}: chips pressed {pressed}")
            if not found or any("put_line" not in x["cells"]["code"] for x in found):
                problems.append(f"filters {scheme}: search put_line: {[x['cells'].get('code') for x in found[:5]]}")
            # the standard explained on the region, the address to the Page
            # tab, map to the live memory map, rewind to the instruction
            tip = page.evaluate("(document.querySelector('#mev_rows .mev-std') || {}).dataset?.tip || ''")
            row = next((i for i, x in enumerate(back["rows"]) if "Hello, World!" in x["cells"].get("value", "")), None)
            r[f"actions {scheme}"] = {"tip": tip[:60]}
            if "IBM PC" not in tip and "E820" not in tip and "PCI" not in tip:
                problems.append(f"actions {scheme}: the standard's explanation: {tip!r}")
            if row is not None:
                tr = page.locator("#mev_rows tr").nth(row)
                tr.locator(".mev-addr .symlink").click()
                time.sleep(0.5)
                title = page.text_content("#page_title") or ""
                r[f"actions {scheme}"]["page"] = [shown_tab(page), title[:60]]
                if shown_tab(page) != "page" or "b8" not in title.lower():
                    problems.append(f"actions {scheme}: the address opens {r[f'actions {scheme}']['page']}")
                show_tab(page, "events")
                time.sleep(0.6)
                tr = page.locator("#mev_rows tr").nth(row)
                tr.locator(".mev-act", has_text="map").click()
                time.sleep(0.5)
                onmap = page.evaluate("({ tab: document.querySelector('.tabbar [aria-selected=true]').dataset.tab, open: document.getElementById('mm').open, pick: MKB.MMAP.pick, range: MKB.MMAP.pick >= 0 ? MKB.MMAP.cellRange(MKB.MMAP.pick) : null })")
                r[f"actions {scheme}"]["map"] = onmap
                if onmap["tab"] != "memory" or not onmap["open"] or not onmap["range"] or not (0xb8000 <= onmap["range"][1] - 1 and onmap["range"][0] < 0xc0000):
                    problems.append(f"actions {scheme}: map from the row: {onmap}")
                show_tab(page, "events")
                time.sleep(0.6)
                steps = page.evaluate("MKB.stepper.steps")
                page.locator("#mev_rows tr").nth(row).locator(".mev-act", has_text="rewind").click()
                wait_for(lambda: page.evaluate("!MKB.REV.busy && MKB.runState === 'paused'"), 120, 0.2)
                landed = page.evaluate("MKB.stepper.steps")
                r[f"actions {scheme}"]["steps"] = [steps, landed]
                if not landed < steps:
                    problems.append(f"actions {scheme}: rewind from {steps} landed at {landed}")
                # (M5) the rewind restored instruction 0's checkpoint, whose
                # state the loader made before the first instruction: its
                # events at 0 stay in the log and in the table
                loader = page.evaluate("""() => ({ cp0: MKB.REV.cps.length ? MKB.REV.cps[0].steps : null,
                  log: MKB.MEV.query({ initiator: 'emulator', to: 1 }).map((i) => MKB.MEV.event(i).source) })""")
                page.click("#mev_who [data-v=emulator]")
                loader["rows"] = [x["cells"].get("who", "") for x in mt_look(page)["rows"]]
                page.click("#mev_who [data-v='']")
                r[f"actions {scheme}"]["loader after rewind"] = loader
                if loader["cp0"] != 0 or "kernel protected-mode part" not in loader["log"] or \
                        not any("kernel protected-mode part" in w for w in loader["rows"]):
                    problems.append(f"actions {scheme}: after a rewind to instruction 0's checkpoint the loader's events are gone: {loader}")
                # the log was cut there: run to the end again for what follows
                machine_do(page, "click", "#dbg_continue")
                wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.3)
                show_tab(page, "events")
                time.sleep(0.6)
                again = page.evaluate("MKB.MEV.query({ initiator: 'emulator', to: 1 }).map((i) => MKB.MEV.event(i).source)")
                if again != loader["log"]:
                    problems.append(f"actions {scheme}: the loader's events at 0 after running on {again}, after the rewind {loader['log']}")
            else:
                problems.append(f"actions {scheme}: no Hello, World! row")
            if not emu or not all(x["cells"]["who"].startswith("Emulator") for x in emu) or not any("kernel protected-mode part" in x["cells"]["who"] for x in emu):
                problems.append(f"filters {scheme}: who = emulator: {[x['cells'].get('who') for x in emu[:6]]}")
            if any("read" not in x["cells"]["access"] or "write" in x["cells"]["access"] for x in reads):
                problems.append(f"filters {scheme}: reads only: {[x['cells'].get('access') for x in reads[:6]]}")
            if not vga or not inreg or not all("colour text" in x["cells"]["region"] for x in inreg):
                problems.append(f"filters {scheme}: region {vga}: {[x['cells'].get('region') for x in inreg[:6]]}")
            if not flat or any(x["n"] > 1 for x in flat) or len(flat) <= len(back["rows"]):
                problems.append(f"filters {scheme}: not grouped: {len(flat)} rows vs {len(back['rows'])} grouped")
            # Record cleared by the user stays cleared across tab changes
            # a device's DMA (no shipped kernel makes one: written through
            # libv86's device API, as mev_negative_case does), a search
            # that finds nothing, what each standard is
            page.evaluate("MKB.MEV.cpu.dma_write(0x1e << 3, new Uint8Array([1, 2, 3]), 0xa010)")
            page.evaluate("MKB.MEV.cpu.dma_write(0x12 << 3, new Uint8Array([4]), 0xa020)")
            # the live memory map (opened by map above) pulses them in: the IDE
            # controller's from the edge (it has no memory BAR region),
            # labelled; the display controller's from its BAR0 region
            show_tab(page, "memory")
            pulse = wait_for(lambda: page.evaluate("""MKB.MMAP.anims.some((a) => a.layer === 1 && a.edge && MKB.MEV.event(a.i).source === 'PCI 00:1e.0') &&
              MKB.MMAP.anims.some((a) => a.layer === 1 && !a.edge && MKB.MEV.event(a.i).source === 'PCI 00:12.0' && MKB.MMAP.segName(MKB.MMAP.segs[MKB.MMAP.cellSeg[a.from]]).startsWith('PCI 00:12.0 BAR0'))"""), 10, 0.05)
            page.screenshot(path=str(shots / f"memtabs-dma-pulse-1280-{scheme}.png"))
            show_tab(page, "events")
            if not pulse:
                problems.append(f"device {scheme}: no pulse of the DMA on the live memory map")
            page.click("#mev_who [data-v=device]")
            dev = mt_look(page)["rows"]
            page.click("#mev_who [data-v='']")
            page.fill("#mev_search", "no such thing here")
            none = page.evaluate("MKB.MEV.render(), [document.querySelectorAll('#mev_rows tr').length, document.getElementById('mev_more').textContent]")
            page.fill("#mev_search", "")
            stds = page.evaluate("""() => ['IBM PC memory map', 'E820 type 1', 'PCI configuration mechanism #1', 'MultiProcessor Specification table',
              'ACPI MADT', 'IA32_APIC_BASE power-up value (Intel SDM)', ''].map((s) => MKB.MEV.stdText(s))""")
            r[f"device {scheme}"] = {"rows": [x["cells"] for x in dev[:2]], "none": none, "stds": [t[:40] for t in stds]}
            ide = [x for x in dev if x["cells"]["who"].startswith("DevicePCI 00:1e.0, mass storage controller, IDE (PCI class 01:01)")]
            if len(dev) != 2 or len(ide) != 1 or "3 bytes" not in ide[0]["cells"]["size"] or "checksum" not in ide[0]["cells"]["value"] or \
                    not dev[0]["cells"]["who"].startswith("DevicePCI 00:12.0, display controller"):
                problems.append(f"device {scheme}: the DMA row {[x['cells'] for x in dev[:2]]}")
            if none[0] or "No event matches" not in none[1]:
                problems.append(f"device {scheme}: a search that finds nothing: {none}")
            for s_, word in zip(stds, ("IBM PC", "INT 15h", "0xCF8", "_MP_", "MADT", "0xFEE00000", "No standard")):
                if word not in s_:
                    problems.append(f"device {scheme}: the standard's text {s_[:60]!r} lacks {word!r}")
            # MEV.describe (one line per event, the memevents suite's checks) of
            # an emulator event names its reason; a click between the chips
            # (on the group's label) changes no filter
            page.click("#mev_who [data-v=cpu]")
            page.click("#mev_who .mev-chiplabel")
            kept = page.evaluate("MKB.MEV.filter.who")
            page.click("#mev_who [data-v='']")
            if not line.startswith("0  emulator  write  0x00100000") or not line.endswith("kernel protected-mode part"):
                problems.append(f"device {scheme}: describe() of the loader's event: {line!r}")
            if kept != "cpu":
                problems.append(f"device {scheme}: a click on the Who label changed the filter to {kept!r}")
            # (the live memory map opened above records for itself)
            page.uncheck("#mev_on")
            off = page.evaluate("MKB.MEV.needs.has('list')")
            show_tab(page, "memory")
            show_tab(page, "events")
            still = mt_look(page)
            still["list"] = page.evaluate("MKB.MEV.needs.has('list')")
            if off or still["list"] or still["record"]:
                problems.append(f"hidden {scheme}: Record cleared: the list records {off} then {still['list']}, box {still['record']}")
            page.check("#mev_on")
            if not page.evaluate("MKB.MEV.on"):
                problems.append(f"hidden {scheme}: Record ticked again, not recording")
            # a kernel switch: a new log, the quests count for the new kernel
            if scheme == "light" and "minified-linux" in entries:
                gen = page.evaluate("MKB.MEV.gen")
                machine_do(page, "select_option", "#kernel", "minified-linux")
                ok = wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == "minified-linux" and page.get_attribute("#end_banner", "data-kind"), 600, 0.5)
                time.sleep(0.6)
                s = mt_look(page)
                text = [x["cells"].get("value", "") for x in s["rows"]]
                r["switch"] = {"gen": [gen, s["gen"]], "tab": s["tab"], "on": s["on"], "rows": len(s["rows"]), "status": s["status"],
                               "text": [t for t in text if t and not t.startswith("0x")][:4]}
                if not ok or s["gen"] == gen or s["tab"] != "events" or not s["on"] or not s["rows"]:
                    problems.append(f"switch: {r['switch']}")
                if not any("Still alive" in t for t in text):
                    problems.append(f"switch: no row spells Still alive: {text[:8]}")
                if any("put_line" in x["cells"].get("code", "") for x in s["rows"]):
                    problems.append("switch: rows of hello's log after the switch")
                show_tab(page, "quests")
                time.sleep(0.6)
                q = mt_look(page)
                r["switch quests"] = [q["progress"], q["badge"], q["mapIn"]]
                if "on minified-linux" not in q["progress"] or q["mapIn"] != "panel_quests" or "/" not in q["badge"]:
                    problems.append(f"switch: quests {r['switch quests']}")
                mt_readable(page, "#qs_list .qs-task, #qs_list .qs-chip, #qs_list .qs-why, #qs_list h4", 14, problems, f"quests {scheme}")
                never = page.evaluate("[...document.querySelectorAll('#qs_list li[data-state=never]')].map((li) => [getComputedStyle(li).opacity, li.querySelector('.qs-chip').textContent])")
                r["never cards"] = never
                if not never or any(o != "1" or "Debian" not in c for o, c in never):
                    problems.append(f"switch: cards this kernel cannot give: {never}")
                page.screenshot(path=str(shots / f"memtabs-quests-1280-{scheme}.png"))
                # "Watch this run" cleared by the user stays cleared (and is remembered)
                page.uncheck("#qs_on")
                off = page.evaluate("[MKB.QUESTS.open, MKB.MEV.needs.has('quests'), localStorage.getItem('mkb-quests-open')]")
                show_tab(page, "memory")
                show_tab(page, "quests")
                still = page.evaluate("[MKB.QUESTS.open, document.getElementById('qs_on').checked]")
                page.check("#qs_on")
                again = page.evaluate("[MKB.QUESTS.open, MKB.MEV.needs.has('quests')]")
                r["watch box"] = [off, still, again]
                if off != [False, False, "0"] or still != [False, False] or again != [True, True]:
                    problems.append(f"switch: Watch this run cleared {off}, after a tab change {still}, ticked {again}")
            else:
                show_tab(page, "quests")
                time.sleep(0.6)
                mt_readable(page, "#qs_list .qs-task, #qs_list .qs-chip, #qs_list .qs-why, #qs_list h4", 14, problems, f"quests {scheme}")
                page.screenshot(path=str(shots / f"memtabs-quests-1280-{scheme}.png"))
            if errs:
                problems.append(f"hidden {scheme}: page errors {errs[:3]}")
        finally:
            ctx.close()


def mt_phone_case(browser, base, shots, problems, r):
    """390 px, light and dark: one row of eleven emoji, Events rows as
    cards, Quests with the map, no horizontal scroll."""
    for scheme in ("light", "dark"):
        ctx, page, errs = mev_page(browser, scheme, phone=True)
        try:
            if not mt_end(page, base, "kernel=hello&speed=full&count=0&mips=10", 120):
                problems.append(f"390 {scheme}: hello did not end")
                continue
            look = tabs_look(page)
            widths = page.evaluate("[...document.querySelectorAll('.tabbar [role=tab]')].map((b) => [b.id, Math.round(b.getBoundingClientRect().width), b.scrollWidth <= b.clientWidth + 1])")
            r[f"390 {scheme} tabs"] = widths
            if len(look) != 11 or len({t["top"] for t in look}) != 1:
                problems.append(f"390 {scheme}: {len(look)} tabs in {len({t['top'] for t in look})} rows (want 11 in one)")
            if any(w < 28 or not fit for _, w, fit in widths):
                problems.append(f"390 {scheme}: tab buttons too narrow or cut: {widths}")
            page.tap("#tab_events")
            time.sleep(0.8)
            s = mt_look(page)
            cards = page.evaluate("""() => [...document.querySelectorAll('#mev_rows tr')].slice(0, 5).map((tr) => { const b = tr.getBoundingClientRect();
              return [Math.round(b.width), [...tr.cells].filter((td) => td.getClientRects().length).length, getComputedStyle(tr).display]; })""")
            r[f"390 {scheme} events"] = {"tab": s["tab"], "rows": len(s["rows"]), "cards": cards, "hscroll": s["hscroll"]}
            if s["tab"] != "events" or not s["on"] or not s["rows"] or s["hscroll"]:
                problems.append(f"390 {scheme}: Events tab {r[f'390 {scheme} events']}")
            if not cards or any(w > 390 - 16 or d == "table-row" for w, _, d in cards):
                problems.append(f"390 {scheme}: the rows are not cards that fit: {cards}")
            mt_readable(page, "#mev_rows td, #mev_rows td *", 13, problems, f"390 {scheme} events")
            # (M5) every filter chip whole inside its row and the screen: the
            # rows wrap, none hides chips past its right edge
            chips = page.evaluate("""() => [...document.querySelectorAll('#panel_events .mev-chip')].filter((b) => b.getClientRects().length).map((b) => {
              const c = b.getBoundingClientRect(), row = b.closest('.mev-chips').getBoundingClientRect();
              return [b.textContent.trim().slice(0, 32), Math.round(c.left), Math.round(c.right), Math.round(row.left), Math.round(row.right), b.scrollWidth <= b.clientWidth + 1]; })""")
            rows = page.evaluate("[...document.querySelectorAll('#panel_events .mev-chips')].map((d) => [d.getAttribute('aria-label'), d.scrollWidth, d.clientWidth])")
            cut = [c for c in chips if c[1] < c[3] - 1 or c[2] > c[4] + 1 or c[2] > 390 or not c[5]]
            r[f"390 {scheme} chips"] = {"n": len(chips), "cut": cut, "rows": rows}
            if len(chips) < 9 or cut or any(sw > cw + 1 for _, sw, cw in rows):
                problems.append(f"390 {scheme}: filter chips cut at the edge: {r[f'390 {scheme} chips']}")
            page.screenshot(path=str(shots / f"memtabs-events-390-{scheme}.png"))
            page.tap("#tab_quests")
            time.sleep(0.8)
            s = mt_look(page)
            fits = page.evaluate("[...document.querySelectorAll('#qs_list li')].every((li) => li.scrollWidth <= li.clientWidth + 1)")
            r[f"390 {scheme} quests"] = {"tab": s["tab"], "map": s["mapIn"], "fits": fits, "hscroll": s["hscroll"]}
            if s["tab"] != "quests" or s["mapIn"] != "panel_quests" or not fits or s["hscroll"]:
                problems.append(f"390 {scheme}: Quests tab {r[f'390 {scheme} quests']}")
            page.screenshot(path=str(shots / f"memtabs-quests-390-{scheme}.png"))
            page.evaluate("document.getElementById('mm').scrollIntoView()")
            page.screenshot(path=str(shots / f"memtabs-quests-map-390-{scheme}.png"))
            if errs:
                problems.append(f"390 {scheme}: page errors {errs[:3]}")
        finally:
            ctx.close()


def suite_memtabs(ctx):
    problems, r = [], {}
    mt_links_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    mt_hidden_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, problems, r)
    mt_phone_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    return report("memtabs", problems, r)
