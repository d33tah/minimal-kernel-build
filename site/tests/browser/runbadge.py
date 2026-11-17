"""Browser test suite runbadge (test_browser.py --suites)."""

import time

from tests.browser.common import kernel_entries, machine_do, PHONE, report, show_tab, shown_tab, wait_for
from tests.browser.navhist import nav_cover


# ---- runbadge: the menu toggle with the run state (H1) --------------------
# The operator: "back next niech będzie za hamburger toggle, który pokazuje
# też wizualnie czy vm pracuje i z jaką prędkością". One small fixed toggle
# (#runbadge_toggle, bottom right) replaces the Back and Forward box and the
# run status next to the tab bar (U5). Its face says at a glance what the VM
# does: a dot that pulses while the CPU runs (static under
# prefers-reduced-motion), static when paused, its own colour at the end
# (halted) and at a panic, and a short word: the speed while running (1/s,
# 1k/s, full), else paused / halted / panic. A click or tap opens a menu
# with Back and Forward (#nav_back, #nav_fwd, as before), the run state in
# words with the instruction count (a click opens Machine), Pause/Continue
# and the "new version" notice of the PWA. Esc and a tap outside close it.

# The toggle's face for each state (data-state of #runbadge).
RUNBADGE_FACE = {"paused": "paused", "halted": "halted", "panic": "panic", "off": "off"}
RUNBADGE_SPEED = {"1": "1/s", "10": "10/s", "1000": "1k/s", "full": "full"}
RUNBADGE_JS = """() => { const w = document.getElementById('runbadge'), t = document.getElementById('runbadge_toggle'),
  m = document.getElementById('runbadge_menu'), st = document.getElementById('dbg_state'), e = document.getElementById('end_banner');
  if (!w || !t || !m) return null;
  const r = t.getBoundingClientRect(), dot = t.querySelector('.rbdot'), cs = dot ? getComputedStyle(dot) : null;
  const words = document.getElementById('runbadge_state');
  return { state: w.dataset.state || '', vm: w.dataset.vm || '', steps: Number(w.dataset.steps || -1),
    face: t.innerText.replace(/\\s+/g, ' ').trim(), label: t.getAttribute('aria-label') || '', tip: t.title || '',
    expanded: t.getAttribute('aria-expanded'), controls: t.getAttribute('aria-controls'),
    open: !m.hidden && m.getClientRects().length > 0, words: words ? words.textContent.trim() : '',
    pos: getComputedStyle(w).position, rect: [r.left, r.top, r.right, r.bottom].map(Math.round), vw: innerWidth, vh: innerHeight,
    anim: cs ? cs.animationName : '', dot: cs ? cs.backgroundColor + ' ' + cs.borderColor : '',
    vmState: st ? st.dataset.state || '' : '', end: e ? e.dataset.kind || '' : '',
    vmSteps: typeof window.MKB?.stepper !== 'undefined' && MKB.stepper ? MKB.stepper.steps : -1,
    focused: document.activeElement === t, hscroll: document.documentElement.scrollWidth > innerWidth + 1,
    newer: w.dataset.newer || '', runMini: !!document.getElementById('run_mini') }; }"""


def runbadge(page):
    return page.evaluate(RUNBADGE_JS)


def runbadge_want(b):
    """The badge state the VM's state asks for."""
    if b["vmState"] == "ff":
        return "ff"
    if b["vmState"] in ("stepped", "jit"):
        return "run"
    if b["vmState"] == "paused":
        return "panic" if b["end"].startswith("panic") else "halted" if b["end"] == "halt" else "paused"
    return "off"


def runbadge_check(page, tag, problems, r, want, speed=None, moving=True, reduced=False):
    """The toggle shows want (and the speed's short text while running);
    it matches the VM; it is fixed in the bottom right corner, at least
    40 px square, clear of the page width; the dot pulses only while
    running and never under reduced motion."""
    b = runbadge(page)
    r[tag] = b and {k: b[k] for k in ("state", "vm", "steps", "face", "anim", "rect", "dot")}
    if not b:
        problems.append(f"{tag}: no menu toggle (#runbadge, #runbadge_toggle, #runbadge_menu)")
        return None
    if b["state"] != want or runbadge_want(b) != want:
        problems.append(f"{tag}: the toggle says {b['state']!r} ({b['face']!r}), the VM {b['vmState']!r} end {b['end']!r}; want {want!r}")
    face = RUNBADGE_SPEED[speed] if want == "run" and speed else RUNBADGE_FACE.get(want)
    if face and face not in b["face"]:
        problems.append(f"{tag}: the toggle's face {b['face']!r} does not say {face!r}")
    if want not in b["label"] and (face or "") not in b["label"]:
        problems.append(f"{tag}: the toggle's accessible name {b['label']!r} does not say {want!r}")
    x0, y0, x1, y1 = b["rect"]
    if b["pos"] != "fixed" or x1 > b["vw"] or y1 > b["vh"] or x0 < b["vw"] / 2 or y0 < b["vh"] / 2:
        problems.append(f"{tag}: the toggle is not fixed in the bottom right corner: {b['pos']} {b['rect']} in {b['vw']}x{b['vh']}")
    if x1 - x0 < 40 or y1 - y0 < 40 or x1 - x0 > 64:
        problems.append(f"{tag}: the toggle is {x1 - x0}x{y1 - y0} px (want 40 to 64 wide, 40 high or more)")
    if b["hscroll"]:
        problems.append(f"{tag}: horizontal scroll")
    if b["runMini"]:
        problems.append(f"{tag}: the run status next to the tab bar (#run_mini) is still there")
    pulse = want in ("run", "ff") and not reduced
    if pulse != (b["anim"] not in ("", "none")):
        problems.append(f"{tag}: the dot's animation is {b['anim']!r} in state {want!r}{' with reduced motion' if reduced else ''}")
    if moving and want in ("run", "ff"):
        s0 = b["steps"]
        time.sleep(2)
        b2 = runbadge(page)
        if b2["steps"] <= s0 or abs(b2["steps"] - b2["vmSteps"]) > max(5000, b2["vmSteps"] // 50):
            problems.append(f"{tag}: the toggle's count does not follow the VM: {s0} -> {b2['steps']}, VM {b2['vmSteps']}")
    return b


def runbadge_open(page, phone):
    t = page.locator("#runbadge_toggle")
    if t.get_attribute("aria-expanded") != "true":
        t.tap() if phone else t.click()
        wait_for(lambda: t.get_attribute("aria-expanded") == "true", 3)


def runbadge_menu(page, tag, phone, problems, r, shots, pause_word):
    """The menu: opens on a click or tap, holds Back and Forward (disabled
    with no jumps, with tooltips), the state in words with the count, and
    Pause/Continue; Esc closes it and gives the focus back to the toggle; a
    tap outside closes it."""
    runbadge_open(page, phone)
    b = runbadge(page)
    if not b or not b["open"] or b["expanded"] != "true" or b["controls"] != "runbadge_menu":
        problems.append(f"{tag}: the toggle does not open its menu: {b and (b['open'], b['expanded'], b['controls'])}")
        return
    inside = page.evaluate("""() => ['nav_back', 'nav_fwd', 'runbadge_pause', 'runbadge_state'].map((id) => {
        const e = document.getElementById(id); return [id, !!e && !!e.closest('#runbadge_menu') && e.getClientRects().length > 0,
        e ? e.title || '' : '', e ? e.disabled : null, e ? e.textContent.trim() : '']; })""")
    r[f"{tag} menu"] = inside
    for id_, shown, title, disabled, text in inside:
        if not shown:
            problems.append(f"{tag}: #{id_} is not shown in the open menu")
    nb, nf = inside[0], inside[1]
    if not nb[3] or not nf[3] or "no earlier view" not in nb[2]:
        problems.append(f"{tag}: Back and Forward before any jump: {nb}, {nf} (want disabled, the tooltip saying so)")
    if inside[2][4] != pause_word:
        problems.append(f"{tag}: the menu's run button says {inside[2][4]!r}, want {pause_word!r}")
    if b["state"] not in b["words"] and RUNBADGE_FACE.get(b["state"], "running") not in b["words"] or "instructions" not in b["words"]:
        problems.append(f"{tag}: the menu's state in words {b['words']!r} lacks the state or the count")
    m = page.evaluate("""() => { const r = document.getElementById('runbadge_menu').getBoundingClientRect();
        return [r.left, r.top, r.right, r.bottom].map(Math.round); }""")
    if m[0] < 0 or m[1] < 0 or m[2] > b["vw"] or m[3] > b["vh"]:
        problems.append(f"{tag}: the open menu leaves the screen: {m}")
    page.screenshot(path=str(shots / f"runbadge-{tag}-menu.png"))
    if phone:
        page.tap("header.top h1")
    else:
        page.keyboard.press("Escape")
    if not wait_for(lambda: page.get_attribute("#runbadge_toggle", "aria-expanded") == "false", 3) or runbadge(page)["open"]:
        problems.append(f"{tag}: {'a tap outside' if phone else 'Esc'} does not close the menu")
    elif not phone and not runbadge(page)["focused"]:
        problems.append(f"{tag}: after Esc the focus is not back on the toggle")
    if not phone:
        # The keyboard: the toggle takes the focus, Enter opens, Esc closes.
        page.focus("#runbadge_toggle")
        page.keyboard.press("Enter")
        if page.get_attribute("#runbadge_toggle", "aria-expanded") != "true":
            problems.append(f"{tag}: Enter on the focused toggle does not open the menu")
        page.keyboard.press("Escape")


def runbadge_case(browser, base, shots, linux):
    problems, r, errors = [], {}, []

    def fresh(scheme, phone, reduced=False):
        c = browser.new_context(color_scheme=scheme, reduced_motion="reduce" if reduced else "no-preference",
                                **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
        pg = c.new_page()
        pg.on("pageerror", lambda e: errors.append(str(e)))
        return c, pg

    def run_at(page, v):
        machine_do(page, "select_option", "#dbg_speed", v)
        if page.is_enabled("#dbg_continue"):
            machine_do(page, "click", "#dbg_continue")
        wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 10)

    def tap(page, phone, sel):
        page.tap(sel) if phone else page.click(sel)

    for scheme, phone in (("light", False), ("dark", False), ("light", True), ("dark", True)):
        tag = f"{390 if phone else 1280}-{scheme}"
        c, page = fresh(scheme, phone)
        page.goto(f"{base}/index.html?kernel=hello&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        time.sleep(0.5)
        b = runbadge_check(page, f"{tag} paused", problems, r, "paused")
        if not b:
            page.screenshot(path=str(shots / f"runbadge-{tag}-before.png"))
            c.close()
            continue
        dot_paused = b["dot"]
        runbadge_menu(page, tag, phone, problems, r, shots, "Continue")
        # Continue from the menu on the Debugger tab, at 1/s.
        machine_do(page, "select_option", "#dbg_speed", "1")
        runbadge_open(page, phone)
        tap(page, phone, "#runbadge_pause")
        wait_for(lambda: runbadge(page)["state"] == "run", 10)
        if phone:
            page.tap("header.top h1")
        else:
            page.keyboard.press("Escape")
        b = runbadge_check(page, f"{tag} 1/s", problems, r, "run", "1", moving=False)
        time.sleep(3)
        b2 = runbadge(page)
        if b and b2["steps"] <= b["steps"]:
            problems.append(f"{tag} 1/s: the count does not rise: {b['steps']} -> {b2['steps']}")
        if b and b["dot"] == dot_paused:
            problems.append(f"{tag}: the dot looks the same running and paused ({dot_paused})")
        page.screenshot(path=str(shots / f"runbadge-{tag}-running.png"))
        # The toggle covers no listing text on the Page tab.
        show_tab(page, "page")
        page.evaluate("window.scrollBy(0, document.getElementById('listing').getBoundingClientRect().top - 200)")
        time.sleep(0.6)
        nav_cover(page, f"{tag} Page tab", problems, r, need_lines=True, scheme_after=scheme)
        page.screenshot(path=str(shots / f"runbadge-{tag}-page.png"))
        show_tab(page, "debugger")
        runbadge_open(page, phone)
        tap(page, phone, "#runbadge_pause")
        wait_for(lambda: runbadge(page)["state"] == "paused", 10)
        runbadge_check(page, f"{tag} paused from the menu", problems, r, "paused")
        # The state in words opens the Machine tab.
        tap(page, phone, "#runbadge_state")
        if not wait_for(lambda: shown_tab(page) == "machine", 5):
            problems.append(f"{tag}: the menu's state in words does not open Machine ({shown_tab(page)})")
        # The PWA's "new version" notice shows on the toggle on every tab.
        show_tab(page, "about")
        page.evaluate("typeof window.MKB?.pwaNewer === 'function' && MKB.pwaNewer('0123456789ab')")
        time.sleep(0.3)
        b = runbadge(page)
        if not b["newer"] or not page.locator("#runbadge_toggle .rbnews").is_visible():
            problems.append(f"{tag}: a new deploy shows no mark on the toggle (newer {b['newer']!r})")
        runbadge_open(page, phone)
        rl = page.locator("#runbadge_menu button", has_text="Reload")
        if not rl.count() or not rl.first.is_visible() or "new version" not in page.inner_text("#runbadge_menu"):
            problems.append(f"{tag}: the open menu has no new version notice with Reload")
        page.screenshot(path=str(shots / f"runbadge-{tag}-newer.png"))
        if phone:
            page.tap("header.top h1")
        else:
            page.keyboard.press("Escape")
        # The end of the run: hello halts.
        run_at(page, "full")
        if not wait_for(lambda: runbadge(page)["state"] == "halted", 60):
            problems.append(f"{tag}: hello at full speed: the toggle never says halted: {runbadge(page)}")
        runbadge_check(page, f"{tag} halted", problems, r, "halted")
        page.screenshot(path=str(shots / f"runbadge-{tag}-halted.png"))
        c.close()

    if any("no menu toggle" in p for p in problems):
        return problems, r
    # Reduced motion: no animation, the state still told apart.
    c, page = fresh("light", False, reduced=True)
    page.goto(f"{base}/index.html?kernel=hello&speed=10#debugger")
    wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 60)
    b = runbadge_check(page, "1280 reduced motion 10/s", problems, r, "run", "10", reduced=True)
    machine_do(page, "click", "#dbg_pause")
    wait_for(lambda: runbadge(page)["state"] == "paused", 10)
    b2 = runbadge_check(page, "1280 reduced motion paused", problems, r, "paused", reduced=True)
    if b and b2 and b["dot"] == b2["dot"]:
        problems.append(f"reduced motion: the dot looks the same running and paused ({b['dot']})")
    page.screenshot(path=str(shots / "runbadge-1280-reduced.png"))
    c.close()

    # The minified Linux at 1k/s, then at full speed into its own panic (VFS:
    # no root); Debian at full speed (the JIT), then the panic text on its screen.
    if linux:
        for scheme, phone in (("light", False), ("dark", True)):
            tag = f"linux-{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000#page")
            wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 90)
            wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run", 10)
            runbadge_check(page, f"{tag} 1k/s", problems, r, "run", "1000")
            page.screenshot(path=str(shots / f"runbadge-{tag}-1k.png"))
            machine_do(page, "select_option", "#dbg_speed", "full")
            if not wait_for(lambda: runbadge(page)["state"] == "panic", 90):
                problems.append(f"{tag}: at full speed the minified Linux's panic does not show on the toggle: {runbadge(page)}")
            runbadge_check(page, f"{tag} panic", problems, r, "panic")
            page.screenshot(path=str(shots / f"runbadge-{tag}-panic.png"))
            c.close()
    if "debian-6.1-686" in kernel_entries():
        for scheme, phone in (("light", False), ("dark", True)):
            tag = f"debian-{390 if phone else 1280}-{scheme}"
            c, page = fresh(scheme, phone)
            page.goto(f"{base}/index.html?kernel=debian-6.1-686&speed=full#debugger")
            if not wait_for(lambda: runbadge(page) and runbadge(page)["state"] == "run" and runbadge(page)["vmSteps"] > 0, 90):
                problems.append(f"{tag}: Debian at full speed: the toggle says {runbadge(page)}")
            runbadge_check(page, f"{tag} full", problems, r, "run", "full")
            page.screenshot(path=str(shots / f"runbadge-{tag}-full.png"))
            page.evaluate("""() => { const cpu = MKB.emulator.v86.cpu;
                const t = "---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---";
                for (let i = 0; i < t.length; i++) { cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i, t.charCodeAt(i)); cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i + 1, 7); } }""")
            if not wait_for(lambda: runbadge(page)["state"] == "panic", 10):
                problems.append(f"{tag}: after the panic text the toggle says {runbadge(page)}")
            runbadge_check(page, f"{tag} panic text", problems, r, "panic")
            c.close()

    # The About tab's legend has a sample of every state of the toggle.
    c, page = fresh("light", False)
    page.goto(f"{base}/index.html?kernel=hello&paused=1#about")
    got = page.evaluate("[...document.querySelectorAll('#about_legend .rbtog[data-state]')].map((e) => e.dataset.state)")
    r["legend"] = got
    for s in ("run", "ff", "paused", "halted", "panic", "off"):
        if s not in got:
            problems.append(f"the legend has no sample of the toggle in state {s!r}")
    c.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_runbadge(ctx):
    return report("runbadge", *runbadge_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries))
