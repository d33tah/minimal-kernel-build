"""Browser test suite tabs (test_browser.py --suites)."""

import json
import os
import pathlib
import tempfile
import time

from tests.browser.common import (
    ALIVE, HELLO, kernel_entries, machine_do, nav_press, PHONE, report, show_tab, SITE, wait_for,
)


# ---- tabs: names, emoji, compact mode, the Machine tab (U3) ---------------
# The operator: "coverage zamień na Source, Graph na Calls. emoji przed
# etykietami. w ustawieniach możliwość zwinięcia do samego emoji.
# zarządzanie symulacją niech będzie kolejną kartą". The tab bar names the
# tabs with an emoji (aria-hidden) and the word (the accessible name); the
# old hashes and ?tab= values still open the renamed tabs; "Compact tabs"
# (Settings tab since U4) shows the emoji only, remembered across a reload;
# the Machine tab holds the kernel, RAM, file, symbol, initrd, cmdline and
# offset controls and Boot, and the header keeps the screen, the run bar
# and the counters, so it is shorter at 390 px than before (TABS_HEADER_390_BEFORE).
# U4 (operator: "settings jako oddzielna zakładka", "wymyśl mniej
# metaforyczne emoji"): a Settings tab before About holds Compact tabs and
# every preference the page remembers (each localStorage key it wrote and
# the IndexedDB of imported DWARF), each with an explanation, a way to
# change or forget it, and "Reset all settings"; the Machine tab keeps
# only machine controls. The emoji show what each tab holds (TABS_EMOJI)
# and render as colour emoji, not as text glyphs, in two font stacks.
# U5 (operator: "a i wszystko nad tabami niech będzie w pierwszym tabie"):
# above the tabs only the title and the tab bar remain (H1 moved the
# one-line run status U5 put next to the tab bar into the menu toggle,
# #runbadge: state, speed, instruction count, Pause/Continue); the
# screen, the run bar, the counters and the notes are in the Machine tab,
# which opens on the first load. The VM keeps running and its screen keeps
# changing while another tab is shown (tabs_u5).

# In the order of the tab bar.
TABS_WORDS = {"machine": "Machine", "memory": "Memory", "page": "Page", "debugger": "Debugger", "calls": "Calls",
              "source": "Source", "snapshots": "Snapshots", "events": "Events", "quests": "Quests", "settings": "Settings",
              "about": "About"}
TABS_EMOJI = {"machine": "\U0001F5A5\uFE0F", "memory": "\U0001F5FA\uFE0F", "events": "\U0001F4CB", "quests": "\U0001F3AF",
              "page": "\U0001F522",
              "debugger": "\U0001F41E", "calls": "\U0001F4CA", "source": "\U0001F4DD", "snapshots": "\U0001F4BE",
              "settings": "\u2699\uFE0F", "about": "\u2139\uFE0F"}
TABS_ALIASES = {"graph": "calls", "coverage": "source"}
# The header's height at 390 px (hello booted, light) on 8dc5c89c7, before
# the Machine tab took its controls: measured in this suite's setting.
TABS_HEADER_390_BEFORE = 1003
TABS_MACHINE_IDS = ["kernel", "memory", "file", "symfile", "initrd_file", "cmdline", "sym_offset", "boot",
                    "kinfo", "build_info", "sym_status", "initrd_status"]
TABS_SETTINGS_IDS = ["compact_tabs", "set_speed", "set_mips", "set_count", "settings_stored", "settings_reset"]
# The preferences with a control of their own in the Settings tab: key, control, default at 1280 px.
TABS_PREFS = [("mkb-compact-tabs", "compact_tabs", False), ("mkb-speed", "set_speed", "1000"),
              ("mkb-mips", "set_mips", "10"), ("mkb-count", "set_count", True)]
# What was above the tab bar before U5: now in the Machine tab.
TABS_RUN_IDS = ["status", "problems", "pwa_notes", "screen_container", "screen_text", "screen_canvas", "vm_overlay",
                "dbg_pause", "dbg_continue", "dbg_step", "dbg_speed", "clock_mips", "count", "dbg_state", "counters",
                "vclock", "entry_info", "end_banner", "count_info"]
# Leaf elements drawn in the header outside the title, the tab bar and the run status.
TABS_HEADER_EXTRA_JS = """() => [...document.querySelectorAll('header.top *')].filter((e) => e.getClientRects().length
  && !e.children.length && !e.closest('h1, .tabbar')).map((e) => e.id || e.className || e.tagName)"""
# The run status on every tab (H1: the menu toggle, bottom right): its face
# and the menu's state in words, the VM state it shows, the count.
TABS_MINI_JS = """() => { const m = document.getElementById('runbadge'), st = document.getElementById('dbg_state');
  if (!m) return null; const r = document.getElementById('runbadge_toggle').getBoundingClientRect();
  return { text: m.textContent.replace(/\\s+/g, ' ').trim(), state: m.dataset.vm || '', steps: Number(m.dataset.steps || -1),
    vmState: st ? st.dataset.state || '' : '', shown: r.width > 0 && r.height > 0, top: Math.round(r.top),
    bottom: Math.round(r.bottom), right: Math.round(r.right) }; }"""

# Per tab button: its id, accessible-name inputs, the emoji and the word as drawn.
TABS_LOOK_JS = """() => [...document.querySelectorAll('.tabbar [role=tab]')].map((b) => {
  const emo = b.querySelector('[aria-hidden=true]'); const word = b.querySelector('.tword');
  const r = (word || b).getBoundingClientRect();
  return { id: b.id, label: b.getAttribute('aria-label'), text: b.textContent.trim(), tip: b.dataset.tip || '',
    emoji: emo ? emo.textContent.trim() : '', wordShown: !!word && r.width > 0 && getComputedStyle(word).display !== 'none',
    top: Math.round(b.getBoundingClientRect().top) }; })"""


def tabs_look(page):
    return page.evaluate(TABS_LOOK_JS)


def tabs_where(page, ids):
    """The tab panel (or "header") each element is in."""
    return page.evaluate("""(ids) => Object.fromEntries(ids.map((id) => { const e = document.getElementById(id);
      if (!e) return [id, null]; const p = e.closest('[role=tabpanel]');
      return [id, p ? p.id : e.closest('header.top') ? 'header' : 'elsewhere']; }))""", ids)


# Each tab's emoji drawn on a canvas in magenta, in the tab button's font and
# in a serif stack: a colour emoji ignores the fill colour, a text-style glyph
# (or a missing-glyph box) comes out magenta.
TABS_EMOJI_DRAW_JS = """(fonts) => [...document.querySelectorAll('.tabbar [role=tab] [aria-hidden=true]')].map((e) => {
  const out = { id: e.closest('[role=tab]').id };
  for (const [name, fam] of Object.entries(fonts)) {
    const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d');
    g.font = '40px ' + (fam || getComputedStyle(e).fontFamily); g.fillStyle = 'rgb(255,0,255)'; g.textBaseline = 'top';
    g.fillText(e.textContent.trim(), 4, 8);
    const d = g.getImageData(0, 0, 64, 64).data; let ink = 0, magenta = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i + 3] > 128) { ink++;
      if (d[i] > 200 && d[i + 1] < 80 && d[i + 2] > 200) magenta++; }
    out[name] = { ink, magenta };
  }
  return out; })"""

TABS_STORED_JS = """() => Object.keys(localStorage).filter((k) => k.startsWith('mkb-')).sort()"""
# What the Settings tab shows for a key: its row (or control) and the explanation next to it.
TABS_ROW_JS = """(k) => { const e = [...document.querySelectorAll('#panel_settings [data-key]')].find((x) => x.dataset.key === k);
  if (!e) return null; const row = e.closest('.setrow') || e;
  return { text: row.textContent.trim(), visible: !!row.getClientRects().length }; }"""
TABS_IDB_JS = """(op) => new Promise((res) => { const rq = indexedDB.open('mkb-dwarf', 1);
  rq.onupgradeneeded = () => rq.result.createObjectStore('kv');
  rq.onerror = () => res(-1);
  rq.onsuccess = () => { const db = rq.result; const tx = db.transaction('kv', op ? 'readwrite' : 'readonly');
    const s = tx.objectStore('kv'); if (op) s.put(new Uint8Array(8), op);
    const n = s.count(); tx.oncomplete = () => { db.close(); res(n.result); }; }; })"""


def tabs_settings(page, problems, r, shots):
    """The Settings tab: every remembered key shown with an explanation
    (entries whose value is not JSON too), changing a preference takes
    effect and is remembered, forget and reset clear them (the running
    kernel's imported symbols with a reload of its own; a key written while
    Reset waits) (hello booted at 1280 px)."""
    other = "mkb-map-" + "0" * 64
    # Entries a later or older version may have left unreadable.
    broken = {"mkb-map-" + "1" * 64: "Imported symbols for another bzImage (SHA-256 111111111111\u2026): ? (?, 0 symbols), "
                                      "used instead of the kernel's own after every reload.",
              "mkb-watch:hello-os": "Watch expressions for hello-os in the Debugger tab: none."}
    page.evaluate("""([other, broken]) => { localStorage.setItem('mkb-watch:hello', JSON.stringify(['msg']));
      localStorage.setItem(other, JSON.stringify({ name: 'other.map', source: 'System.map', symbols: [], sections: [] }));
      for (const k of broken) localStorage.setItem(k, '{not JSON');
      localStorage.setItem('mkb-later-step', 'x'); }""", [other, list(broken)])
    page.evaluate(TABS_IDB_JS, other + ":index")
    page.reload()
    wait_for(lambda: "Running hello " in (page.text_content("#status") or "") and ALIVE in page.inner_text("#screen_container"), 60)
    show_tab(page, "machine")
    page.fill("#cmdline", "console=tty0 u4=1")
    page.press("#cmdline", "Enter")
    wait_for(lambda: any(k.startswith("mkb-cmdline-") for k in page.evaluate(TABS_STORED_JS)), 10)
    wait_for(lambda: ALIVE in page.inner_text("#screen_container"), 60)
    if not page.locator("#tab_settings").count():
        problems.append("no Settings tab")
        return
    # IndexedDB can be slow (CI run 42704392268: the list was still empty
    # when read): the localStorage rows must not wait for it.
    page.evaluate("""() => { const orig = MKB.dbgIdbOp;
      MKB.dbgIdbOp = async (...a) => { await new Promise((r) => setTimeout(r, 1500)); return orig(...a); }; }""")
    show_tab(page, "settings")
    stored = page.evaluate(TABS_STORED_JS)
    r["stored_keys"] = stored
    for want in ("mkb-watch:hello", other, "mkb-later-step", "mkb-ref-hello-10"):
        if want not in stored:
            problems.append(f"setup: {want} not in localStorage ({stored})")
    if not any(k.startswith("mkb-cmdline-") for k in stored):
        problems.append(f"setup: no remembered cmdline after Boot with one ({stored})")
    for k in stored:
        row = page.evaluate(TABS_ROW_JS, k)
        if not row or not row["visible"]:
            problems.append(f"Settings does not show the remembered {k}")
        elif len(row["text"]) < 25:
            problems.append(f"Settings row of {k} has no explanation: {row['text']!r}")
    for k, want in broken.items():
        row = page.evaluate(TABS_ROW_JS, k)
        if not row or want not in row["text"]:
            problems.append(f"Settings row of {k} (not JSON): {row and row['text']!r}, want {want!r}")
    wait_for(lambda: page.evaluate(TABS_ROW_JS, "idb:mkb-dwarf"), 10)
    row = page.evaluate(TABS_ROW_JS, "idb:mkb-dwarf")
    if not row or "1" not in row["text"]:
        problems.append(f"Settings does not show the imported DWARF kept in IndexedDB: {row}")
    for key, ctl, default in TABS_PREFS:
        if not page.locator(f"#{ctl}").count():
            problems.append(f"no #{ctl} in Settings for {key}")
        elif page.get_attribute(f"#{ctl}", "data-key") != key:
            problems.append(f"#{ctl}: data-key {page.get_attribute(f'#{ctl}', 'data-key')!r}, want {key}")
    page.screenshot(path=str(shots / "tabs-1280-settings.png"), full_page=True)

    # Each preference takes effect and is remembered.
    if page.locator("#set_speed").count():
        page.select_option("#set_speed", "100")
        if page.input_value("#dbg_speed") != "100" or page.evaluate("localStorage.getItem('mkb-speed')") != "100":
            problems.append(f"Settings speed 100/s: Machine tab {page.input_value('#dbg_speed')}, "
                            f"stored {page.evaluate('localStorage.getItem(\'mkb-speed\')')}")
        show_tab(page, "machine")
        machine_do(page, "select_option", "#dbg_speed", "10")
        show_tab(page, "settings")
        if page.input_value("#set_speed") != "10":
            problems.append(f"after the Machine tab's speed 10/s Settings shows {page.input_value('#set_speed')}")
    if page.locator("#set_mips").count():
        page.select_option("#set_mips", "100")
        if page.input_value("#clock_mips") != "100" or page.evaluate("localStorage.getItem('mkb-mips')") != "100":
            problems.append(f"Settings clock 100/us: Machine tab {page.input_value('#clock_mips')}")
    if page.locator("#set_count").count():
        page.uncheck("#set_count")
        if page.is_checked("#count") or page.evaluate("localStorage.getItem('mkb-count')") != "0":
            problems.append("Settings count off: the Machine tab's count stays on or is not remembered")
    page.reload()
    wait_for(lambda: page.locator("#tab_settings").count() and "Running hello " in (page.text_content("#status") or ""), 60)
    after = {"speed": page.evaluate("MKB.prefSpeed"), "clock": page.input_value("#clock_mips"), "count": page.is_checked("#count")}
    r["after_reload"] = after
    if after != {"speed": "10", "clock": "100", "count": False}:
        problems.append(f"preferences after a reload: {after}, want speed 10, clock 100, count off")
    show_tab(page, "settings")
    if page.locator("#set_count").count() and page.is_checked("#set_count"):
        problems.append("Settings count shows on after a reload with count off")

    # Forget single entries: each goes, with its effect.
    def forget(k):
        b = page.locator("#panel_settings .setrow").filter(has=page.locator(f'[data-key="{k}"]')).locator("button.forget")
        if not b.count():
            problems.append(f"no Forget button for {k}")
            return False
        b.first.click()
        if not wait_for(lambda: k not in page.evaluate(TABS_STORED_JS) and not page.evaluate(TABS_ROW_JS, k), 10):
            problems.append(f"Forget {k}: still stored or shown")
            return False
        return True
    show_tab(page, "debugger")  # loads the kernel's watch list
    show_tab(page, "settings")
    if page.evaluate("MKB.watchList") != ["msg"]:
        problems.append(f"setup: the watch list of hello is {page.evaluate('MKB.watchList')}")
    if forget("mkb-watch:hello") and page.evaluate("MKB.watchList") != []:
        problems.append(f"after Forget the watch list of hello is {page.evaluate('MKB.watchList')}")
    cmd = next((k for k in page.evaluate(TABS_STORED_JS) if k.startswith("mkb-cmdline-")), None)
    if cmd and forget(cmd) and page.input_value("#cmdline") != "nokaslr cryptomgr.notests":
        problems.append(f"after Forget the command line is {page.input_value('#cmdline')!r}, want the default")
    forget(other)
    forget("mkb-later-step")
    if page.evaluate(TABS_IDB_JS, "") != 0:
        problems.append("Forget the other bzImage's symbols left its DWARF in IndexedDB")
    tabs_forget_imported(page, problems, r, forget)

    # Reset all settings: nothing of ours stays, the defaults are back. The
    # boot (10/s after the reload) ends first: its "Still alive" count is a
    # new measurement, remembered again when it arrives.
    page.select_option("#set_speed", "1000")
    if not wait_for(lambda: ALIVE in page.inner_text("#screen_container"), 60):
        problems.append("hello did not reach Still alive at 1k/s set in Settings")
    page.evaluate("(k) => localStorage.setItem(k, '{}')", other)
    page.evaluate(TABS_IDB_JS, other + ":index")
    page.check("#compact_tabs")
    if not page.locator("#settings_reset").count():
        problems.append("no Reset all settings button")
        return
    # A key written while Reset waits for IndexedDB to forget the imported
    # DWARF (a boot measuring its Still alive count then) goes too.
    page.evaluate("""() => { const orig = MKB.dbgIdbOp;
      MKB.dbgIdbOp = async (...a) => { if (a[0] === 'readwrite') { MKB.dbgIdbOp = orig; localStorage.setItem('mkb-ref-hello-real', '123'); }
        return orig(...a); }; }""")
    page.click("#settings_reset")
    time.sleep(0.5)
    left = page.evaluate(TABS_STORED_JS)
    r["after_reset"] = left
    if left:
        problems.append(f"Reset all settings left {left}")
    if page.evaluate(TABS_IDB_JS, "") != 0:
        problems.append("Reset all settings left the IndexedDB of imported DWARF")
    now = {"compact": page.is_checked("#compact_tabs"), "speed": page.evaluate("MKB.prefSpeed"),
           "clock": page.input_value("#clock_mips"), "count": page.is_checked("#count"),
           "words": all(t["wordShown"] for t in tabs_look(page))}
    if now != {"compact": False, "speed": "1000", "clock": "10", "count": True, "words": True}:
        problems.append(f"after Reset all settings: {now}, want the defaults")
    if page.locator("#panel_settings .setrow button.forget").count():
        problems.append("after Reset all settings Settings still lists remembered entries")


def tabs_forget_imported(page, problems, r, forget):
    """Symbols imported for the running kernel (a System.map of hello's
    symbols.json with hello_main renamed): its Settings row names this
    kernel, and Forget brings back the kernel's own symbols (Run to
    hello_main works again)."""
    syms = json.loads((SITE / kernel_entries()["hello"]["symbols"]).read_text())["symbols"]
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as t:
        smap = pathlib.Path(t) / "System.map"
        smap.write_text("".join(f"{x['vaddr']:08x} {x['type']} {'renamed_main' if x['name'] == 'hello_main' else x['name']}\n"
                                for x in syms))
        before = set(page.evaluate(TABS_STORED_JS))
        show_tab(page, "machine")
        page.set_input_files("#symfile", str(smap))
        wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported"
                 and set(page.evaluate(TABS_STORED_JS)) - before, 20)
    key = next(iter(set(page.evaluate(TABS_STORED_JS)) - before), None)
    if not key or not key.startswith("mkb-map-"):
        problems.append(f"import a System.map for hello: new keys {key}, status {page.inner_text('#sym_status')!r}")
        return
    show_tab(page, "settings")
    row = page.evaluate(TABS_ROW_JS, key)
    n = page.evaluate("MKB.symbols.symbols.length")
    want = f"Imported symbols for this kernel (hello): System.map (System.map, {n:,} symbols)"
    r["imported_row"] = row and row["text"][:140]
    if not row or want not in row["text"]:
        problems.append(f"Settings row of hello's imported symbols: {row}, want {want!r}")
    if not forget(key):
        return
    ok = wait_for(lambda: page.get_attribute("#sym_status", "data-state") == ""
                  and page.evaluate("!!MKB.symbols && !MKB.symbols.imported && MKB.symbols.symbols.some((s) => s.name === 'hello_main')"), 30)
    if not ok:
        problems.append(f"Forget hello's imported symbols: status {page.inner_text('#sym_status')!r}, the kernel's own symbols not back")


def tabs_mini(page):
    return page.evaluate(TABS_MINI_JS)


def tabs_u5(browser, base, shots, problems, r, fresh, linux):
    """U5: only the title, the tab bar and the run status above the tabs;
    the Machine tab opens first and holds the screen; the VM runs on and
    its screen changes while another tab is shown."""
    def selected(pg):
        return [t for t in TABS_WORDS if pg.locator(f"#tab_{t}").count()
                and pg.get_attribute(f"#tab_{t}", "aria-selected") == "true"]

    def header_check(pg, tag):
        extra = pg.evaluate(TABS_HEADER_EXTRA_JS)
        if extra:
            problems.append(f"{tag}: above the tabs besides the title and the tab bar: {extra[:8]}")
        mini = tabs_mini(pg)
        if not mini or not mini["shown"]:
            problems.append(f"{tag}: no run status (the menu toggle #runbadge)")
        h = round(pg.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))
        r[f"u5_header_{tag}"] = h
        if h > 140:
            problems.append(f"{tag}: header {h} px high (want the title, the tab bar and one status line)")
        return mini

    for scheme in ("light", "dark"):
        for phone in (False, True):
            tag = f"{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=hello")
            ok = wait_for(lambda: ALIVE in (page.text_content("#screen_text") or ""), 60)
            if not ok:
                problems.append(f"{tag}: hello did not reach {ALIVE!r} on the first load")
            if selected(page) != ["machine"] or page.evaluate("location.hash") != "#machine":
                problems.append(f"{tag}: first load shows {selected(page)} ({page.evaluate('location.hash')!r}), want Machine")
            if not page.is_visible("#screen_container") or HELLO not in page.inner_text("#screen_container"):
                problems.append(f"{tag}: the Machine tab does not show the screen with {HELLO!r}")
            time.sleep(0.5)
            mini = header_check(page, tag)
            if mini and (mini["state"] != mini["vmState"] or "paused" not in mini["text"] and "ended" not in mini["text"]
                         and "halt" not in mini["text"]):
                problems.append(f"{tag}: run status {mini} does not match the VM ({mini and mini['vmState']})")
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"{tag}: horizontal scroll")
            page.screenshot(path=str(shots / f"tabs-u5-{tag}.png"))
            show_tab(page, "debugger")
            time.sleep(0.3)
            page.screenshot(path=str(shots / f"tabs-u5-{tag}-debugger.png"))
            c.close()

    # The hash still selects a tab.
    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello#memory")
    if not wait_for(lambda: selected(page) == ["memory"], 10):
        problems.append(f"#memory on the first load shows {selected(page)}")
    c.close()

    # hello at 10/s: leave Machine after the kernel entry, before "Hello, World!".
    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello&speed=10")
    wait_for(lambda: page.text_content("#entry_info"), 60)
    left_early = HELLO not in (page.text_content("#screen_text") or "")
    r["u5_left_before_hello"] = left_early
    show_tab(page, "page")
    m1 = tabs_mini(page)
    time.sleep(2)
    m2 = tabs_mini(page)
    r["u5_page_10"] = [m1, m2]
    if not m1 or not m2 or m2["steps"] <= m1["steps"] or m2["state"] != "stepped":
        problems.append(f"hello at 10/s on the Page tab: the run status does not count on: {m1} -> {m2}")
    elif m2["state"] != m2["vmState"] or "10/s" not in m2["text"]:
        problems.append(f"hello at 10/s: the run status {m2} does not match the VM")
    if page.is_visible("#screen_container"):
        problems.append("the screen is shown on the Page tab")
    if not page.evaluate("document.getElementById('screen_container').isConnected"):
        problems.append("the screen was taken out of the page")
    # The screen element is hidden, not stopped: the text arrives meanwhile.
    if not wait_for(lambda: HELLO in (page.text_content("#screen_text") or ""), 60):
        problems.append(f"{HELLO!r} did not reach the hidden screen while the Page tab was shown")
    show_tab(page, "debugger")
    a = tabs_mini(page)
    if a:
        nav_press(page, "#runbadge_pause")
        if not wait_for(lambda: (tabs_mini(page) or {}).get("state") == "paused", 10):
            problems.append(f"Pause in the run status did not pause: {tabs_mini(page)}")
        p1 = tabs_mini(page)
        time.sleep(1)
        p2 = tabs_mini(page)
        if p1["steps"] != p2["steps"] or "paused" not in p2["text"]:
            problems.append(f"paused from the run status, it still counts: {p1} -> {p2}")
        nav_press(page, "#runbadge_pause")
        if not wait_for(lambda: (tabs_mini(page) or {}).get("state") == "stepped", 10):
            problems.append(f"Continue in the run status did not run on: {tabs_mini(page)}")
        r["u5_pause_continue"] = [p1, p2, tabs_mini(page)]
    if page.locator("#runbadge_state").count():
        nav_press(page, "#runbadge_state")
    if selected(page) != ["machine"]:
        problems.append(f"a click on the run status shows {selected(page)}, want Machine")
    if not page.is_visible("#screen_container") or HELLO not in page.inner_text("#screen_container"):
        problems.append(f"back on Machine the screen does not show {HELLO!r}")
    page.screenshot(path=str(shots / "tabs-u5-back-on-machine.png"))
    c.close()

    # The minified Linux at 1k/s on the Debugger tab: the count rises with the VM's.
    if linux:
        c, page = fresh()
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000")
        wait_for(lambda: page.text_content("#entry_info"), 60)
        show_tab(page, "debugger")
        time.sleep(0.5)
        m1 = tabs_mini(page)
        time.sleep(2)
        m2 = tabs_mini(page)
        r["u5_linux_1k"] = [m1, m2]
        if not m1 or not m2 or m2["steps"] - m1["steps"] < 500 or m2["state"] != "stepped" or "1k/s" not in m2["text"]:
            problems.append(f"minified Linux at 1k/s on the Debugger tab: run status {m1} -> {m2}")
        c.close()


def tabs_case(browser, base, shots, linux=False):
    problems, r = [], {}
    errors = []

    def fresh(scheme="light", phone=False):
        c = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
        pg = c.new_page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        return c, pg

    def booted(pg, name="hello"):
        return wait_for(lambda: f"Running {name} " in (pg.text_content("#status") or "")
                        and ALIVE in pg.inner_text("#screen_container"), 60)

    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello")
    if not booted(page):
        problems.append(f"hello did not boot: {page.text_content('#status')!r}")
    look = tabs_look(page)
    r["labels"] = [(t["emoji"], t["label"] or t["text"]) for t in look]
    want_ids = ["tab_" + k for k in TABS_WORDS]
    if [t["id"] for t in look] != want_ids:
        problems.append(f"tab ids {[t['id'] for t in look]}, want {want_ids}")
    for t in look:
        want = TABS_EMOJI.get(t["id"][4:])
        if want and t["emoji"] != want:
            problems.append(f"{t['id']}: emoji {t['emoji']!r}, want {want!r}")
    drawn = page.evaluate(TABS_EMOJI_DRAW_JS, {"tab font": "", "serif": "serif"})
    r["emoji_drawn"] = drawn
    for d in drawn:
        for stack in ("tab font", "serif"):
            ink, magenta = d[stack]["ink"], d[stack]["magenta"]
            if ink < 100 or magenta > ink * 0.2:
                problems.append(f"{d['id']} emoji in the {stack} stack: {ink} px drawn, {magenta} in the text colour "
                                f"(want a colour emoji)")
    for t in look:
        word = TABS_WORDS.get(t["id"][4:])
        if not t["emoji"]:
            problems.append(f"{t['id']}: no aria-hidden emoji before the label")
        if word and not t["text"].endswith(word):
            problems.append(f"{t['id']}: label {t['text']!r}, want the emoji then {word!r}")
        if word and page.get_by_role("tab", name=word, exact=True).count() != 1:
            problems.append(f"no tab whose accessible name is {word!r}")
        if not t["wordShown"]:
            problems.append(f"{t['id']}: the word is not shown with compact tabs off")
    if len({t["emoji"] for t in look}) != len(look):
        problems.append(f"two tabs share an emoji: {r['labels']}")
    for old in ("Graph", "Coverage"):
        if page.get_by_role("tab", name=old).count():
            problems.append(f"a tab is still called {old}")
    about = page.text_content("#panel_about")
    for old in ("Graph tab", "Coverage tab", "Graph:", "Coverage:"):
        if old in about:
            problems.append(f"the About text still says {old!r}")

    # Where the controls are.
    where = tabs_where(page, TABS_MACHINE_IDS + TABS_RUN_IDS)
    r["where"] = where
    for i in TABS_MACHINE_IDS:
        if where.get(i) != "panel_machine":
            problems.append(f"#{i} is in {where.get(i)}, want the Machine tab")
    for i in TABS_RUN_IDS:
        if where.get(i) != "panel_machine":
            problems.append(f"#{i} is in {where.get(i)}, want the Machine tab")
    # The Settings tab is mounted when first shown (shell.jsx).
    show_tab(page, "settings")
    for i in TABS_SETTINGS_IDS:
        w = tabs_where(page, [i])[i]
        if w != "panel_settings":
            problems.append(f"#{i} is in {w}, want the Settings tab")
    show_tab(page, "machine")
    heads = page.evaluate("[...document.querySelectorAll('#panel_machine h2, #panel_machine h3, #panel_machine legend')]"
                          ".map((e) => e.textContent.trim())")
    if any("Settings" in h for h in heads) or page.locator("#panel_machine input[type=checkbox]:not(#count)").count():
        problems.append(f"the Machine tab still has a Settings section: {heads}")
    r["header_1280"] = round(page.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))

    # Old hashes and ?tab= open the renamed tabs.
    for old, new in TABS_ALIASES.items():
        for url in (f"{base}/index.html?kernel=hello#{old}", f"{base}/index.html?kernel=hello&tab={old}",
                    f"{base}/index.html?kernel=hello#{new}"):
            page.goto(url)
            ok = wait_for(lambda: page.locator(f"#tab_{new}").count()
                          and page.get_attribute(f"#tab_{new}", "aria-selected") == "true"
                          and page.evaluate("location.hash") == "#" + new, 10)
            if not ok:
                problems.append(f"{url[len(base):]}: the {TABS_WORDS[new]} tab is not shown with #{new} "
                                f"(hash {page.evaluate('location.hash')!r})")
    for url in (f"{base}/index.html?kernel=hello&tab=settings", f"{base}/index.html?kernel=hello#settings"):
        page.goto(url)
        if not wait_for(lambda: page.locator("#tab_settings").count()
                        and page.get_attribute("#tab_settings", "aria-selected") == "true"
                        and page.evaluate("location.hash") == "#settings", 10):
            problems.append(f"{url[len(base):]} does not open the Settings tab")
    page.goto(f"{base}/index.html?kernel=hello#machine")
    booted(page)
    if page.get_attribute("#tab_machine", "aria-selected") != "true" if page.locator("#tab_machine").count() else True:
        problems.append("#machine does not open the Machine tab")
    # A hash the browser has navigated to but whose hashchange has not run
    # yet: the page's own hash writes (writeHash, e.g. following EIP to
    # another page) must not overwrite it. CI run 43424964028 lost #coverage
    # this way and stayed on #calls.
    page.goto(f"{base}/index.html?kernel=hello#calls")
    wait_for(lambda: page.evaluate("location.hash") == "#calls", 10)
    page.evaluate("() => { location.hash = '#coverage'; MKB.writeHash(); }")
    if not wait_for(lambda: page.get_attribute("#tab_source", "aria-selected") == "true"
                    and page.evaluate("location.hash") == "#source", 10):
        problems.append(f"#coverage with a hash write before its hashchange: hash {page.evaluate('location.hash')!r}, "
                        f"Source tab selected {page.get_attribute('#tab_source', 'aria-selected')!r}")

    # Booting another kernel from the Machine tab, and Boot again.
    if page.locator("#tab_machine").count():
        show_tab(page, "machine")
        page.select_option("#kernel", "hello-os")
        if not booted(page, "hello-os"):
            problems.append(f"picking hello-os in the Machine tab did not boot it: {page.text_content('#status')!r}")
        page.select_option("#memory", str(4 << 20))
        page.click("#boot")
        if not wait_for(lambda: "4,096 KiB" in (page.text_content("#status") or "") and ALIVE in page.inner_text("#screen_container"), 60):
            problems.append(f"Boot with 4 MB from the Machine tab: {page.text_content('#status')!r}")
        page.screenshot(path=str(shots / "tabs-1280-machine.png"))

    # A bzImage dropped on the page boots from any tab (here: About).
    show_tab(page, "about")
    page.evaluate("""async () => { const b = await (await fetch('kernels/hello/bzImage')).arrayBuffer();
      const dt = new DataTransfer(); dt.items.add(new File([b], 'dropped.bzImage'));
      document.querySelector('#panel_about').dispatchEvent(new DragEvent('drop', { dataTransfer: dt, bubbles: true, cancelable: true })); }""")
    if not booted(page, "dropped.bzImage"):
        problems.append(f"a bzImage dropped on the About tab did not boot: {page.text_content('#status')!r}")

    # Compact tabs: emoji only, the word as tooltip and accessible name, kept across a reload.
    if not page.locator("#compact_tabs").count():
        problems.append("no Compact tabs setting (#compact_tabs)")
    else:
        # (before U4 the setting was in the Machine tab: the case reports, not crashes)
        home = "settings" if page.locator("#tab_settings").count() else "machine"
        show_tab(page, home)
        if page.is_checked("#compact_tabs"):
            problems.append("Compact tabs is on by default at 1280 px")
        page.check("#compact_tabs")
        for when in ("checked", "after a reload"):
            look = tabs_look(page)
            for t in look:
                word = TABS_WORDS.get(t["id"][4:])
                if t["wordShown"] or t["label"] != word or t["tip"] != word:
                    problems.append(f"compact ({when}) {t['id']}: word shown {t['wordShown']}, aria-label {t['label']!r}, tip {t['tip']!r}")
            if when == "checked":
                page.screenshot(path=str(shots / "tabs-1280-compact.png"))
                r["compact_width_1280"] = page.evaluate("Math.round(document.querySelector('.tabbar').scrollWidth)")
                page.reload()
                wait_for(lambda: page.locator("#compact_tabs").count() and page.is_checked("#compact_tabs"), 10)
        # The header grows while hello boots and moves the tab bar under a
        # resting mouse (pointerout closes the popover): hover after the boot.
        booted(page)
        time.sleep(0.5)
        page.hover("#tab_calls")
        if not wait_for(lambda: page.is_visible("#tip_pop") and page.text_content("#tip_pop") == "Calls", 3):
            problems.append(f"hovering the compact Calls tab shows {page.text_content('#tip_pop')!r}")
        page.click("#tab_calls")
        if page.get_attribute("#tab_calls", "aria-selected") != "true":
            problems.append("a click on the compact Calls tab does not open it")
        show_tab(page, home)
        page.uncheck("#compact_tabs")
        page.reload()
        wait_for(lambda: page.locator("#compact_tabs").count(), 10)
        if page.is_checked("#compact_tabs") or not all(t["wordShown"] for t in tabs_look(page)):
            problems.append("Compact tabs off does not stay off after a reload")
    c.close()

    c, page = fresh()
    page.goto(f"{base}/index.html?kernel=hello")
    if booted(page):
        tabs_settings(page, problems, r, shots)
    c.close()

    # A browser that keeps no data (site data blocked: localStorage throws,
    # no IndexedDB): Settings says nothing is remembered and its controls
    # still work for this page.
    c, page = fresh()
    c.add_init_script("""Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('blocked by the test', 'SecurityError'); } });
        Object.defineProperty(window, 'indexedDB', { get() { return undefined; } });""")
    page.goto(f"{base}/index.html?kernel=hello")
    if booted(page):
        show_tab(page, "settings")
        time.sleep(0.5)
        none = page.text_content("#settings_stored") or ""
        page.select_option("#set_speed", "100")
        page.select_option("#set_mips", "1000")
        page.uncheck("#set_count")
        page.check("#compact_tabs")
        r["no storage"] = [none[:80], page.input_value("#dbg_speed")]
        if "Nothing remembered for any kernel yet." not in none or page.locator("#settings_stored .setrow").count():
            problems.append(f"Settings without storage: {none[:200]!r}")
        now = [page.input_value("#dbg_speed"), page.input_value("#clock_mips"), page.is_checked("#count"),
               not any(t["wordShown"] for t in tabs_look(page))]
        if now != ["100", "1000", False, True]:
            problems.append(f"Settings without storage: speed 100/s, clock 1000/us, count off, compact tabs set; "
                            f"the page has [speed, clock, count, compact] {now}")
    else:
        problems.append("without storage: hello did not boot")
    c.close()

    tabs_u5(browser, base, shots, problems, r, fresh, linux)

    # 390 px, light and dark: shorter header, no sideways scroll, compact by default.
    for scheme in ("light", "dark"):
        c, page = fresh(scheme, phone=True)
        page.goto(f"{base}/index.html?kernel=hello")
        booted(page)
        time.sleep(0.5)
        h = round(page.evaluate("document.querySelector('header.top').getBoundingClientRect().height"))
        r[f"header_390_{scheme}"] = h
        if h >= TABS_HEADER_390_BEFORE:
            problems.append(f"390 px {scheme}: header {h} px, not shorter than before ({TABS_HEADER_390_BEFORE} px)")
        look = tabs_look(page)
        rows = len({t["top"] for t in look})
        r[f"rows_390_{scheme}"] = rows
        # The store's value: the Settings tab (its checkbox) is mounted when first shown.
        compact = page.evaluate("MKB.compactTabs") is True
        if not compact or rows != 1:
            problems.append(f"390 px {scheme}: compact {compact}, tab bar rows {rows} (want compact by default, one row)")
        page.screenshot(path=str(shots / f"tabs-390-{scheme}.png"))
        for t in ["machine", "memory", "calls", "source", "settings", "about"]:
            if page.locator(f"#tab_{t}").count():
                page.tap(f"#tab_{t}")
                time.sleep(0.3)
                if page.get_attribute(f"#tab_{t}", "aria-selected") != "true":
                    problems.append(f"390 px {scheme}: a tap on {t} does not open it")
                if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                    problems.append(f"390 px {scheme}: horizontal scroll on the {t} tab")
                if t in ("machine", "settings"):
                    page.screenshot(path=str(shots / f"tabs-390-{scheme}-{t}.png"), full_page=True)
        if page.locator("#compact_tabs").count():
            page.tap("#tab_settings" if page.locator("#tab_settings").count() else "#tab_machine")
            if not page.is_checked("#compact_tabs"):
                problems.append(f"390 px {scheme}: the Settings tab's Compact tabs is not ticked")
            page.uncheck("#compact_tabs")
            time.sleep(0.3)
            r[f"rows_390_words_{scheme}"] = len({t["top"] for t in tabs_look(page)})
            if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
                problems.append(f"390 px {scheme}: horizontal scroll with words")
            page.screenshot(path=str(shots / f"tabs-390-{scheme}-words.png"))
        c.close()
    for scheme in ("light", "dark"):
        c, page = fresh(scheme)
        page.goto(f"{base}/index.html?kernel=hello")
        booted(page)
        if page.evaluate("document.documentElement.scrollWidth > window.innerWidth + 1"):
            problems.append(f"1280 px {scheme}: horizontal scroll")
        page.screenshot(path=str(shots / f"tabs-1280-{scheme}.png"))
        c.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_tabs(ctx):
    return report("tabs", *tabs_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries))
