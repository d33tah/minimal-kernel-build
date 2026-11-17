"""Browser test suite quests (test_browser.py --suites)."""

import json
import re
import time

from tests.browser.common import debian_names, machine_do, mev_page, report, show_tab, shown_tab, wait_for
from tests.browser.memmap import mm_frames


# ---- quests: challenges about the standards (M3) ---------------------------
# quests.js checks each quest itself from the memory events, the machine's
# state and the live memory map. Each case solves the quests as a user
# would, with what the page shows: the map's cell lists and story, the Page
# tab, "go there"; the expected answers come from outside the checker where
# there is such a source (the bzImage's and vgabios.bin's bytes, the
# kernel's own function names). minified Linux (1280 light): the loader's
# byte count (a wrong and a malformed answer first), the option ROM's
# length (from the Page tab, typed in hex), where E820 says usable RAM
# ends (a wrong cell first, then the right one), the kernel's first own
# write to the text memory (its story line's go there); the hint makes the
# region blink on the map; the PCI and I/O APIC quests say "try Debian";
# progress, the celebration, remembered after a reload, Start over. Opened
# late: the first-paint quest asks to boot again and works after. Without
# storage: solved, kept in the page, a note says so. Debian (1280 dark):
# the PCI class of the device the OS read, the first BAR the OS sized, the
# I/O APIC from the MP table (its hint blinks the table's pointer).
# Screenshots at 390 px light and dark: no horizontal scroll, the cards fit.
QS_STATE_JS = """() => ({
  cards: Object.fromEntries([...document.querySelectorAll('#qs_list li')].map((li) => [li.dataset.q, { state: li.dataset.state,
    why: li.querySelector('.qs-why').textContent, fb: li.querySelector('.qs-fb').textContent, won: li.querySelector('.qs-wontext').hidden ? '' : li.querySelector('.qs-wontext').textContent,
    mark: li.querySelector('.qs-mark').firstChild.textContent, task: li.querySelector('.qs-task').textContent,
    hint: !li.querySelector('.qs-hint').hidden, boot: !li.querySelector('.qs-boot').hidden,
    fits: li.scrollWidth <= li.clientWidth + 1 }])),
  progress: document.getElementById('qs_progress').textContent, badge: document.getElementById('qs_badge').textContent,
  toast: document.getElementById('qs_toast').textContent, note: document.getElementById('qs_note').textContent,
  wins: MKB.QUESTS.wins, bursts: MKB.QUESTS.burstBits || 0, since: MKB.MEV.since, open: MKB.QUESTS.open,
  meter: [document.getElementById('qs_meter').getAttribute('aria-valuenow'), document.getElementById('qs_meter').getAttribute('aria-valuemax')],
  hl: MKB.MMAP.hl && { ranges: MKB.MMAP.hl.ranges, label: MKB.MMAP.hl.label, cells: MKB.MMAP.hl.cells }, mapOpen: document.getElementById('mm').open,
  stored: (() => { try { return localStorage.getItem('mkb-quests'); } catch (e) { return 'no storage'; } })(),
  hscroll: document.documentElement.scrollWidth > innerWidth + 1, steps: MKB.stepper && MKB.stepper.steps, run: MKB.runState })"""


def qs_open(browser, base, name, scheme="light", phone=False, q="quests=1", storage=True):
    ctx, page, errs = mev_page(browser, scheme, phone)
    if not storage:
        ctx.add_init_script("Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('blocked by the test', 'SecurityError'); } });")
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10&{q}", timeout=180000)
    # ?quests=1 opens the Quests tab; with ?quests=0 the case opens it later
    if "quests=1" in q:
        show_tab(page, "quests")
    return ctx, page, errs


def qs_state(page):
    return page.evaluate(QS_STATE_JS)


def qs_card(page, qid, sel):
    return page.locator(f"#qs_list li[data-q={qid}] {sel}")


def qs_wait(page, qid, state, timeout=30):
    return wait_for(lambda: page.evaluate("(q) => (document.querySelector(`#qs_list li[data-q=${q}]`) || {}).dataset?.state", qid) == state, timeout, 0.2)


def qs_answer(page, qid, text):
    qs_card(page, qid, ".qs-form input").fill(text)
    qs_card(page, qid, ".qs-form input").press("Enter")
    time.sleep(0.2)


def qs_click_addr(page, addr):
    """Click the live memory map's cell of addr, the cell in the middle of the viewport."""
    d = page.evaluate("(a) => MKB.MMAP.cellOf(a)", addr)
    page.locator("#mm_map").scroll_into_view_if_needed()
    box = page.locator("#mm_map").bounding_box()
    x, y = page.evaluate("(d) => [MKB.MMAP.cellX[d], MKB.MMAP.cellY[d]]", d)
    cy = box["y"] + (y + 0.5) / 128 * box["height"]
    page.evaluate(f"window.scrollBy(0, {cy - page.viewport_size['height'] / 2})")
    box = page.locator("#mm_map").bounding_box()
    page.mouse.click(box["x"] + (x + 0.5) / 128 * box["width"], box["y"] + (y + 0.5) / 128 * box["height"])
    return d


def qs_hint_wait(page):
    """Until the map has drawn the hint (live, it draws about 20 frames a second)."""
    wait_for(lambda: page.evaluate("!!(MKB.MMAP.hl && MKB.MMAP.hl.cells > 0)"), 5, 0.05)


def qs_story(page):
    return page.evaluate("[...document.querySelectorAll('#mm_story li')].map((l) => l.textContent)")


def qs_loader_bytes(page):
    """The loader's byte count read as a user would: the events of the cells
    where the parts went (instruction 0, emulator writes)."""
    total, rows = 0, []
    for a in (0x8e000, 0x80000, 0x100000):
        qs_click_addr(page, a)
        for t in page.evaluate("[...document.querySelectorAll('#mm_info li')].map((l) => l.textContent)"):
            m = re.match(r"0 emulator write (0x[0-9a-f]+) ([\d,]+) bytes", t)
            if m and int(m.group(1), 16) == a:
                total += int(m.group(2).replace(",", ""))
                rows.append(t[:60])
    return total, rows


def qs_minified_case(browser, base, shots, entries, problems, r):
    name = "minified-linux"
    ctx, page, errs = qs_open(browser, base, name)
    try:
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
            problems.append("minified: no end")
            return
        qs_wait(page, "paint", "open")
        # The cards follow the quests' half-second look: the run's end (the
        # banner) shows as "try Debian" on the next one (run 43988655649
        # read them between the banner and that look: still "wait").
        for q in ("pciclass", "barsize", "ioapic"):
            qs_wait(page, q, "never", 10)
        s = qs_state(page)
        r["minified start"] = {k: v["state"] for k, v in s["cards"].items()}
        for q in ("loader", "rom", "ramend", "paint"):
            if s["cards"][q]["state"] != "open":
                problems.append(f"minified: {q} is {s['cards'][q]['state']}: {s['cards'][q]['why']}")
        for q in ("pciclass", "barsize", "ioapic"):
            if s["cards"][q]["state"] != "never" or "try Debian" not in s["cards"][q]["why"]:
                problems.append(f"minified: {q} should say try Debian: {s['cards'][q]}")
        if s["progress"] != "0 of 4 solved on minified-linux." or s["badge"] != "★ 0/4" or s["since"] != 0:
            problems.append(f"minified: progress {s['progress']!r} badge {s['badge']!r} since {s['since']}")
        # a hint of a quest this kernel cannot give: its text, nothing blinks
        qs_card(page, "pciclass", ".qs-hintbtn").click()
        s = qs_state(page)
        if not s["cards"]["pciclass"]["hint"] or s["hl"] or s["mapOpen"]:
            problems.append(f"minified: the hint of a quest minified cannot give: {s['cards']['pciclass']} {s['hl']} map {s['mapOpen']}")
        # the loader: a malformed and a wrong answer, the hint, then the bytes
        # the map's cells show; the bzImage and the command line say the same
        qs_answer(page, "loader", "lots")
        bad = qs_state(page)["cards"]["loader"]
        qs_answer(page, "loader", "1000")
        low = qs_state(page)["cards"]["loader"]
        if "Type a number" not in bad["fb"] or "Too small" not in low["fb"] or low["state"] != "open":
            problems.append(f"minified: wrong loader answers: {bad['fb']!r} {low}")
        qs_card(page, "loader", ".qs-hintbtn").click()
        qs_hint_wait(page)
        h = qs_state(page)
        r["loader hint"] = {"hl": h["hl"], "map": h["mapOpen"]}
        if not h["cards"]["loader"]["hint"] or not h["mapOpen"] or not h["hl"] or not any(a == 0x100000 for a, b in h["hl"]["ranges"]) or not h["hl"]["cells"]:
            problems.append(f"minified: the loader's hint does not blink the parts on the map: {h['hl']} open {h['mapOpen']}")
        page.screenshot(path=str(shots / "quests-hint-1280-light.png"))
        total, rows = qs_loader_bytes(page)
        bz = len(page.request.get(f"{base}/{entries[name]['path']}").body())
        cmdline = page.evaluate("document.getElementById('cmdline').value")
        r["loader"] = {"cells": rows, "total": total, "bzImage": bz, "cmdline": len(cmdline)}
        # the whole bzImage (real-mode and protected-mode parts) and the
        # command line with its terminating NUL (boot protocol: cmd_line_ptr)
        if total != bz + len(cmdline) + 1:
            problems.append(f"minified: the map's cells show {total} loader bytes, the bzImage and the command line are {bz} + {len(cmdline)} + 1")
        time.sleep(0.6)
        if qs_state(page)["cards"]["ramend"]["fb"]:
            problems.append("minified: cells clicked for the loader quest told the E820 quest 'not that one'")
        page.evaluate("document.getElementById('qs').scrollIntoView()")
        qs_answer(page, "loader", f"{total:,}")
        s = qs_state(page)
        c = s["cards"]["loader"]
        r["loader solved"] = {"won": c["won"], "toast": s["toast"], "wins": s["wins"], "bursts": s["bursts"]}
        if c["state"] != "solved" or c["mark"] != "★" or f"{total:,} bytes" not in c["won"]:
            problems.append(f"minified: loader not solved by {total:,}: {c}")
        if s["wins"] != 1 or not s["bursts"] or "Quest solved: The loader's delivery. 1 of 4" not in s["toast"] or s["meter"] != ["1", "4"]:
            problems.append(f"minified: no celebration: {s['wins']} wins, {s['bursts']} bursts, toast {s['toast']!r}, meter {s['meter']}")
        page.screenshot(path=str(shots / "quests-solved-1280-light.png"))
        # a solved quest takes no more answers
        again = page.evaluate("() => { MKB.QUESTS.answer(MKB.QUESTS.cards.get('loader'), 1); return [MKB.QUESTS.wins, document.querySelector('#qs_list li[data-q=loader]').dataset.state]; }")
        if again != [1, "solved"]:
            problems.append(f"minified: a solved quest answered again: {again}")
        # the option ROM: its hint blinks the video BIOS ROM; its first bytes
        # in the Page tab; vgabios.bin agrees
        qs_card(page, "rom", ".qs-hintbtn").click()
        qs_hint_wait(page)
        h = qs_state(page)["hl"]
        if not h or h["ranges"] != [[0xc0000, 0xc8000]] or not h["cells"]:
            problems.append(f"minified: the ROM's hint: {h}")
        page.evaluate("document.getElementById('qs').scrollIntoView()")
        qs_card(page, "rom", ".qs-page").click()
        wait_for(lambda: shown_tab(page) == "page", 10, 0.2)
        title = page.text_content("#page_title")
        listing = page.text_content("#listing") or ""
        # the listing shows hex rows, or decodes the bytes as code (one line
        # per instruction): the bytes after each address
        seen = {}
        for m in re.finditer(r"(?m)^000c00([0-9a-f]{2}) ((?:[0-9a-f]{2} {1,2})+)", listing):
            for k, byte in enumerate(m.group(2).split()):
                seen.setdefault(int(m.group(1), 16) + k, int(byte, 16))
        b = [seen.get(k) for k in range(3)]
        rom = page.request.get(f"{base}/v86/vgabios.bin").body()
        r["rom"] = {"title": title, "listing": listing[:120], "page bytes": b, "vgabios": [hex(rom[0]), hex(rom[1]), rom[2]]}
        if "c0000" not in title.lower() or b != [0x55, 0xaa, rom[2]]:
            problems.append(f"minified: the Page tab at 0xc0000 does not show 55 aa {rom[2]:02x}: {title!r} {listing[:200]!r}")
        want = (b[2] or 0) * 512
        r["rom"]["bytes"] = want
        show_tab(page, "quests")
        qs_answer(page, "rom", hex(want))
        c = qs_state(page)["cards"]["rom"]
        if c["state"] != "solved" or f"{want:,} bytes" not in c["won"]:
            problems.append(f"minified: rom not solved by {hex(want)}: {c}")
        # E820: its hint (the usable entries blink), a wrong cell, then the
        # last byte of the highest usable entry from the story's E820 lines
        qs_card(page, "ramend", ".qs-hintbtn").click()
        qs_hint_wait(page)
        h = qs_state(page)["hl"]
        if not h or h["label"] != "E820 usable RAM" or not h["cells"] or [0x100000, 0x1c0000] not in h["ranges"]:
            problems.append(f"minified: the E820 hint: {h}")
        qs_answer(page, "ramend", "0x100000")
        typed = qs_state(page)["cards"]["ramend"]["fb"]
        if "Lower than the end" not in typed:
            problems.append(f"minified: a typed address below the end: {typed!r}")
        qs_click_addr(page, 0x1000)
        time.sleep(0.8)
        wrong = qs_state(page)["cards"]["ramend"]
        ends = [int(m.group(2), 16) for t in qs_story(page) for m in [re.search(r"the guest learned E820: (0x[0-9a-f]+)-(0x[0-9a-f]+) usable RAM", t)] if m]
        r["ramend"] = {"wrong": wrong["fb"], "usable ends": [hex(e) for e in ends]}
        if wrong["state"] != "open" or "Not that one" not in wrong["fb"] or not ends:
            problems.append(f"minified: ramend after a wrong cell: {wrong}, E820 lines {ends}")
        else:
            qs_click_addr(page, max(ends))
            qs_wait(page, "ramend", "solved", 5)
            c = qs_state(page)["cards"]["ramend"]
            r["ramend"]["won"] = c["won"]
            if c["state"] != "solved" or hex(max(ends)) not in c["won"] or "the BIOS keeps the last 262,144" not in c["won"]:
                problems.append(f"minified: ramend not solved by the cell of {hex(max(ends))}: {c}")
        # first paint: its hint blinks the text memory; the story's line of
        # the kernel's code first touching the text memory, its go there
        qs_card(page, "paint", ".qs-hintbtn").click()
        qs_hint_wait(page)
        h = qs_state(page)["hl"]
        if not h or h["ranges"] != [[0xb8000, 0xc0000]] or not h["cells"]:
            problems.append(f"minified: the first paint's hint: {h}")
        target = page.evaluate("""() => { const li = [...document.querySelectorAll('#mm_story li')].find((l) => /the CPU running code in .*RAM/.test(l.textContent) &&
          l.textContent.includes('first touched colour text (CGA) video memory')); if (!li) return null; const b = li.querySelector('.mm-go');
          const pos = Number(b.title.match(/instruction ([\\d,]+)/)[1].replace(/,/g, '')); b.click(); return [pos, li.textContent]; }""")
        if not target:
            problems.append("minified: no story line of the kernel's code touching the text memory")
        else:
            ok = wait_for(lambda: page.evaluate("!MKB.REV.busy && MKB.runState === 'paused' && MKB.stepper.steps <= %d" % target[0]), 300, 0.2)
            landed = page.evaluate("MKB.stepper.steps")
            # the first access can be a read: Step on to the first write
            steps = 0
            while qs_state(page)["cards"]["paint"]["state"] != "solved" and steps < 40:
                machine_do(page, "click", "#dbg_step")
                wait_for(lambda: page.evaluate("MKB.runState === 'paused'"), 10, 0.05)
                time.sleep(0.6)
                steps += 1
            s = qs_state(page)
            c = s["cards"]["paint"]
            r["paint"] = {"story": target[1][:200], "landed": landed, "stepped": steps, "steps": s["steps"], "won": c["won"]}
            if not ok or landed != target[0] or c["state"] != "solved" or "vgacon" not in c["won"] or f"Instruction {s['steps']:,}" not in c["won"]:
                problems.append(f"minified: paint not solved from the story's go there to {target[0]:,} and Step: {r['paint']}, {c}")
        s = qs_state(page)
        r["minified end"] = {"progress": s["progress"], "toast": s["toast"], "badge": s["badge"]}
        if s["progress"] != "4 of 4 solved on minified-linux." or "all of them" not in s["toast"] or s["badge"] != "★ 4/4":
            problems.append(f"minified: all four solved: {s['progress']!r} {s['toast']!r} {s['badge']!r}")
        page.evaluate("document.getElementById('qs').scrollIntoView()")
        page.screenshot(path=str(shots / "quests-minified-1280-light.png"))
        # remembered: a reload shows them solved; Start over
        page.reload(timeout=180000)
        show_tab(page, "quests")
        qs_wait(page, "loader", "solved", 60)
        s = qs_state(page)
        r["reload"] = {k: v["state"] for k, v in s["cards"].items()}
        stored = json.loads(s["stored"] or "{}").get(name, {})
        if any(s["cards"][q]["state"] != "solved" for q in ("loader", "rom", "ramend", "paint")) or sorted(stored) != ["loader", "paint", "ramend", "rom"]:
            problems.append(f"minified: not remembered after a reload: {r['reload']} stored {s['stored']}")
        # per kernel: hello has its own count (no E820 call, no PCI, no MP
        # table), the minified Linux's stay
        machine_do(page, "select_option", "#kernel", "hello")
        wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 300, 0.5)
        wait_for(lambda: qs_state(page)["progress"].startswith("0 of 3 solved on hello"), 30, 0.3)
        s = qs_state(page)
        r["hello"] = {"progress": s["progress"], **{k: v["state"] for k, v in s["cards"].items()}}
        if s["progress"] != "0 of 3 solved on hello (4 on all kernels)." or s["cards"]["ramend"]["state"] != "never" or s["cards"]["paint"]["state"] != "open":
            problems.append(f"minified: switched to hello: {r['hello']}")
        machine_do(page, "select_option", "#kernel", name)
        wait_for(lambda: qs_state(page)["progress"].startswith("4 of 4"), 120, 0.3)
        page.click("#qs_reset")
        time.sleep(0.6)
        s = qs_state(page)
        if s["cards"]["loader"]["state"] == "solved" or not s["progress"].startswith("0 of"):
            problems.append(f"minified: Start over: {s['progress']!r} {s['cards']['loader']}")
        if errs:
            problems.append(f"minified: page errors {errs[:3]}")
    finally:
        ctx.close()


def qs_late_case(browser, base, shots, entries, problems, r):
    """Quests opened after the boot: first paint asks to boot again."""
    ctx, page, errs = qs_open(browser, base, "minified-linux", q="quests=0")
    try:
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
            problems.append("late: no end")
            return
        show_tab(page, "quests")
        qs_wait(page, "paint", "boot", 10)
        s = qs_state(page)
        c = s["cards"]["paint"]
        r["late"] = {"paint": c["state"], "why": c["why"], "since": s["since"], "loader": s["cards"]["loader"]["state"]}
        if c["state"] != "boot" or not c["boot"] or "Boot again" not in c["why"] or s["cards"]["loader"]["state"] != "open":
            problems.append(f"late: first paint opened late: {c}")
        qs_card(page, "paint", ".qs-boot").click()
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind") and page.evaluate("MKB.MEV.since === 0"), 600, 0.5):
            problems.append(f"late: no end after Boot again, page errors {errs[:3]}")
            return
        ok = qs_wait(page, "paint", "open", 20)
        s = qs_state(page)
        r["late again"] = {"paint": s["cards"]["paint"]["state"], "since": s["since"]}
        if not ok:
            problems.append(f"late: after Boot again first paint is {s['cards']['paint']}")
        if errs:
            problems.append(f"late: page errors {errs[:3]}")
    finally:
        ctx.close()


def qs_nostore_case(browser, base, shots, entries, problems, r):
    """No storage: a quest is solved and kept in the page; a note says so."""
    name = "minified-linux"
    ctx, page, errs = qs_open(browser, base, name, storage=False)
    try:
        if not qs_wait(page, "rom", "open", 120):
            problems.append("no storage: the rom quest never opened")
            return
        rom = page.request.get(f"{base}/v86/vgabios.bin").body()
        qs_answer(page, "rom", f"{rom[2] * 512 / 1024} KiB")
        s = qs_state(page)
        r["no storage"] = {"rom": s["cards"]["rom"]["state"], "note": s["note"], "stored": s["stored"]}
        if s["cards"]["rom"]["state"] != "solved" or "does not let the page store" not in s["note"] or s["stored"] != "no storage":
            problems.append(f"no storage: {r['no storage']}")
        if errs:
            problems.append(f"no storage: page errors {errs[:3]}")
    finally:
        ctx.close()


def qs_debian_case(browser, base, shots, name, problems, r):
    ctx, page, errs = qs_open(browser, base, name, "dark", q="quests=1&memmap=1")
    try:
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 900, 0.5):
            problems.append(f"{name}: no end")
            return
        qs_wait(page, "ioapic", "open", 30)
        s = qs_state(page)
        r[name + " start"] = {k: v["state"] for k, v in s["cards"].items()}
        if any(v["state"] not in ("open", "solved") for v in s["cards"].values()):
            problems.append(f"{name}: not every quest open: {r[name + ' start']}")
        story = qs_story(page)
        # the class of the device the task names, from the story's PCI line
        task = s["cards"]["pciclass"]["task"]
        m = re.search(r"device ([0-9a-f]{2}:[0-9a-f]{2}\.\d) say", task)
        line = m and next((t for t in story if f"the guest learned PCI {m.group(1)} " in t and "(PCI class" in t), None)
        cls = line and re.search(r"[0-9a-f]{4}:[0-9a-f]{4}: ([a-z ]+?),", line)
        r["pciclass"] = {"task": task[-60:], "line": line and line[:160]}
        if not cls:
            problems.append(f"{name}: no device or class for the pciclass quest: {task!r} {line!r}")
        else:
            want = cls.group(1)
            other = page.locator(f"#qs_list li[data-q=pciclass] .qs-choices button:not(:text-is('{want}'))").first
            other.click()
            wrong = qs_state(page)["cards"]["pciclass"]
            page.locator(f"#qs_list li[data-q=pciclass] .qs-choices button:text-is('{want}')").click()
            c = qs_state(page)["cards"]["pciclass"]
            r["pciclass"].update(want=want, wrong=wrong["fb"], won=c["won"])
            if "No:" not in wrong["fb"] or c["state"] != "solved" or want not in c["won"]:
                problems.append(f"{name}: pciclass with {want!r}: wrong {wrong['fb']!r}, {c}")
        # the first BAR sized after the E820 answers (the OS's)
        learned = [t for t in story if "the guest learned" in t]
        k = next((i for i, t in enumerate(learned) if "E820:" in t), None)
        sized = k is not None and next((re.search(r"(PCI \S+ \S+) sized: (\d+) bytes", t) for t in learned[k:] if " sized: " in t), None)
        r["barsize"] = {"line": sized and sized.group(0)}
        if not sized:
            problems.append(f"{name}: no BAR sized after the E820 answers in the story")
        else:
            qs_answer(page, "barsize", str(int(sized.group(2)) * 2))
            wrong = qs_state(page)["cards"]["barsize"]
            qs_answer(page, "barsize", sized.group(2) + " bytes")
            c = qs_state(page)["cards"]["barsize"]
            r["barsize"].update(wrong=wrong["fb"], won=c["won"])
            if "Too large" not in wrong["fb"] or c["state"] != "solved" or sized.group(1)[4:] + " first" not in c["won"]:
                problems.append(f"{name}: barsize: {wrong['fb']!r} {c}")
        # the I/O APIC: the hint blinks the MP table's pointer; the story's
        # MultiProcessor line has the address; its cell
        qs_card(page, "ioapic", ".qs-hintbtn").click()
        qs_hint_wait(page)
        h = qs_state(page)["hl"]
        r["ioapic hint"] = h
        if not h or not h["cells"] or not all(0xf0000 <= a < b <= 0x100000 for a, b in h["ranges"]) or "_MP_" not in h["label"]:
            problems.append(f"{name}: the I/O APIC's hint does not blink the MP pointer: {h}")
        page.screenshot(path=str(shots / "quests-debian-hint-1280-dark.png"))
        mp = next((re.search(r"I/O APIC 0 (0x[0-9a-f]+)", t) for t in story if "MultiProcessor Specification table:" in t), None)
        if not mp:
            problems.append(f"{name}: no MultiProcessor line in the story")
        else:
            qs_click_addr(page, int(mp.group(1), 16))
            qs_wait(page, "ioapic", "solved", 5)
            c = qs_state(page)["cards"]["ioapic"]
            r["ioapic"] = c["won"]
            if c["state"] != "solved" or mp.group(1) not in c["won"]:
                problems.append(f"{name}: ioapic not solved by the cell of {mp.group(1)}: {c}")
        # the other hints: the device's regions, the PCI BAR regions; a hint
        # blinks for 8 s, then the map is as before
        hl = {}
        for q in ("pciclass", "barsize"):
            qs_card(page, q, ".qs-hintbtn").click()
            # a new hint: its cells drawn
            page.evaluate("MKB.MMAP.hl.cells = 0")
            qs_hint_wait(page)
            hl[q] = qs_state(page)["hl"]
        r["pci hints"] = hl
        if not hl["pciclass"] or not hl["pciclass"]["ranges"] or not hl["pciclass"]["cells"] or not hl["barsize"] or len(hl["barsize"]["ranges"]) < 2:
            problems.append(f"{name}: PCI hints: {hl}")
        time.sleep(8.5)
        mm_frames(page, 3)
        if qs_state(page)["hl"]:
            problems.append(f"{name}: a hint still blinks after 8 s: {qs_state(page)['hl']}")
        s = qs_state(page)
        r[name + " end"] = s["progress"]
        if s["progress"] != f"3 of 7 solved on {name}." and not s["progress"].startswith(f"3 of 7 solved on {name} ("):
            problems.append(f"{name}: progress {s['progress']!r}")
        page.evaluate("document.getElementById('qs').scrollIntoView()")
        page.screenshot(path=str(shots / "quests-debian-1280-dark.png"))
        if errs:
            problems.append(f"{name}: page errors {errs[:3]}")
    finally:
        ctx.close()


def qs_phone_case(browser, base, shots, entries, problems, r):
    for scheme in ("light", "dark"):
        ctx, page, errs = qs_open(browser, base, "minified-linux", scheme, phone=True)
        try:
            if not qs_wait(page, "rom", "open", 120):
                problems.append(f"390 {scheme}: the rom quest never opened")
                continue
            rom = page.request.get(f"{base}/v86/vgabios.bin").body()
            qs_answer(page, "rom", str(rom[2] * 512))
            qs_answer(page, "loader", "12")
            wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5)
            qs_wait(page, "ramend", "open", 30)
            qs_card(page, "ramend", ".qs-hintbtn").click()
            qs_card(page, "ramend", ".qs-learn summary").click()
            if scheme == "light":
                # typed this time: the end of the highest usable E820 entry
                mm_frames(page, 3)
                ends = [int(m.group(2), 16) for t in qs_story(page) for m in [re.search(r"E820: (0x[0-9a-f]+)-(0x[0-9a-f]+) usable RAM", t)] if m]
                if ends:
                    qs_answer(page, "ramend", hex(max(ends)))
                if qs_state(page)["cards"]["ramend"]["state"] != "solved":
                    problems.append(f"390 light: ramend typed {[hex(e) for e in ends]}: {qs_state(page)['cards']['ramend']}")
            time.sleep(0.3)
            page.evaluate("document.getElementById('qs').scrollIntoView()")
            page.screenshot(path=str(shots / f"quests-390-{scheme}.png"), full_page=False)
            s = qs_state(page)
            r[f"390 {scheme}"] = {"hscroll": s["hscroll"], "fits": {k: v["fits"] for k, v in s["cards"].items()}, "rom": s["cards"]["rom"]["state"]}
            if s["hscroll"] or not all(v["fits"] for v in s["cards"].values()) or s["cards"]["rom"]["state"] != "solved":
                problems.append(f"390 {scheme}: {r[f'390 {scheme}']}")
            if errs:
                problems.append(f"390 {scheme}: page errors {errs[:3]}")
        finally:
            ctx.close()


def suite_quests(ctx):
    problems, r = [], {}
    if "minified-linux" in ctx.entries:
        qs_minified_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, problems, r)
        qs_late_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, problems, r)
        qs_nostore_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, problems, r)
        qs_phone_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, problems, r)
    for name in debian_names(ctx)[:1]:
        qs_debian_case(ctx.browser, ctx.base, ctx.shots, name, problems, r)
    return report("quests", problems, r)
