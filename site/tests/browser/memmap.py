"""Browser test suite memmap (test_browser.py --suites)."""

import time

from tests.browser.common import (
    debian_names, JITDET_CAP, JITDET_WATCH_JS, machine_do, mev_page, report, show_tab, wait_for,
)
from tests.browser.memevents import MEV_DIGEST_JS


# ---- memmap: the live memory map (M2) ----------------------------------------
# memmap.js draws the 32-bit physical address space on a Hilbert curve, cut
# at the regions memevents.js names (each label names its standard), and
# lights the cells of each memory event in its initiator's colour; the CPU's
# RAM writes are sampled per frame from v86.wasm's write bitmap. Checked: the
# layout covers the address space with every named region at least 16 cells;
# the loader's drop (Replay from the first event); the colour text memory
# rebuilt from the log equals the stepper's mirror of the screen; the story
# of first touches (the loader's parts, the kernel's code writing the
# screen with its function and line, Debian's I/O APIC from the
# MultiProcessor table, the E820 answers it learned); the slider and Live;
# a cell's events from a click and from the keyboard, "go there" stepping
# back to the event's instruction (the map follows the cut log); only
# first touch; closing stops recording and drawing; the events are the same
# as with the debug list (the map changes nothing); screenshots at 1280 and
# 390 px, light and dark, and a filmstrip of a replay; the cost at full JIT
# speed on Debian with the map shown against closed.
MM_STATE_JS = """() => { const M = MKB.MMAP, segs = M.segs; const st = MKB.stepper;
  let gaps = 0; for (let k = 1; k < segs.length; k++) if (segs[k].lo !== segs[k - 1].hi || segs[k].c0 !== segs[k - 1].c1) gaps++;
  const dec = (b, from, n) => { let s = ''; for (let i = from; i < from + n; i += 2) s += String.fromCharCode(b[i] || 32); return s; };
  let diff = 0; for (let i = 0; i < M.text.length; i += 2) if (M.text[i] !== st.text[i] || M.text[i + 1] !== st.text[i + 1]) diff++;
  return { segs: segs.length, first: segs.length ? segs[0].lo : -1, last: segs.length ? segs[segs.length - 1].hi : -1, cells: segs.length ? segs[segs.length - 1].c1 : 0, gaps,
    small: segs.filter((g) => g.kind !== 'none' && g.cells < 16).map((g) => M.segName(g)),
    nostd: segs.filter((g) => g.kind !== 'none' && !/\\(.+\\)/.test(M.segName(g))).map((g) => M.segName(g)),
    idx: M.idx, n: MKB.MEV.n, live: M.live, on: MKB.MEV.on, frames: M.frames, raf: !!M.raf, track: M.trackFrom, samples: M.samples.length,
    textDiff: diff, textFrom: M.textFrom, screen: dec(M.text, M.textTop * 160, 4000),
    story: [...document.querySelectorAll('#mm_story li')].map((l) => l.textContent),
    storyLinks: document.querySelectorAll('#mm_story .srclink').length,
    status: document.getElementById('mm_status').textContent, regions: document.querySelectorAll('#mm_regions li').length,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, drawMs: M.drawMs, steps: st.steps }; }"""


def mm_open(browser, base, name, scheme="light", phone=False, q="memmap=1"):
    ctx, page, errs = mev_page(browser, scheme, phone)
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10&{q}", timeout=180000)
    show_tab(page, "memory")
    return ctx, page, errs


def mm_cell_xy(page, d):
    """Viewport point of map cell d."""
    page.locator("#mm_map").scroll_into_view_if_needed()
    box = page.locator("#mm_map").bounding_box()
    x, y = page.evaluate("(d) => [MKB.MMAP.cellX[d], MKB.MMAP.cellY[d]]", d)
    return box["x"] + (x + 0.5) / 128 * box["width"], box["y"] + (y + 0.5) / 128 * box["height"]


def mm_frames(page, n=3):
    """Let the map draw n frames."""
    page.evaluate("(n) => new Promise((ok) => { const f = () => (--n > 0 ? requestAnimationFrame(f) : ok()); requestAnimationFrame(f); })", n)


def mm_story_case(browser, base, shots, name, problems, r):
    """To the end with the map shown: layout, text, story; light 1280."""
    ctx, page, errs = mm_open(browser, base, name)
    try:
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
            problems.append(f"{name}: no end with the map open")
            return
        time.sleep(0.5)
        mm_frames(page, 5)
        s = page.evaluate(MM_STATE_JS)
        r[name] = {k: s[k] for k in ("segs", "cells", "idx", "n", "samples", "textDiff", "storyLinks", "drawMs", "frames")}
        r[name + " story"] = s["story"][:40]
        if s["first"] != 0 or s["last"] != 2 ** 32 or s["cells"] != 128 * 128 or s["gaps"]:
            problems.append(f"{name}: the layout does not cover the address space once: {s['first']}..{s['last']} {s['cells']} cells, {s['gaps']} gaps")
        if s["small"] or s["nostd"]:
            problems.append(f"{name}: regions under 16 cells {s['small']} or without a standard {s['nostd']}")
        if not s["live"] or s["idx"] != s["n"] or not s["on"]:
            problems.append(f"{name}: the map is not live at the log's end: {s['idx']} of {s['n']}, on {s['on']}")
        if s["textDiff"]:
            problems.append(f"{name}: the text rebuilt from the log differs from the screen in {s['textDiff']} cells")
        if not s["samples"]:
            problems.append(f"{name}: no CPU RAM write sampled")
        story = "\n".join(s["story"])
        for want in ("the emulator first touched", "kernel protected-mode part", "BIOS image"):
            if want not in story:
                problems.append(f"{name}: the story has no {want!r}: {s['story'][:8]}")
        kern = [x for x in s["story"] if "the CPU running code in usable RAM" in x and "colour text (CGA) video memory (IBM PC memory map)" in x]
        # Debian's first writes from RAM are its decompressor's (no DWARF)
        if not kern or (".c:" not in kern[-1] and not name.startswith("debian")):
            problems.append(f"{name}: no story line of the kernel's code writing the screen with its file:line: {kern}")
        if not s["storyLinks"]:
            problems.append(f"{name}: the story has no Source link")
        if name.startswith("debian"):
            if "I/O APIC 0 (MultiProcessor Specification table)" not in story:
                problems.append(f"{name}: the I/O APIC's first touch is not in the story")
            if "the guest learned E820:" not in story or "the guest learned PCI" not in story:
                problems.append(f"{name}: the story lacks what the guest learned (E820, PCI)")
            if "Kernel panic" not in s["screen"] and "panic" not in s["screen"]:
                problems.append(f"{name}: the text window does not show the end: {s['screen'][-400:]!r}")
        else:
            if "Hello, World!" not in s["screen"]:
                problems.append(f"{name}: the text window lacks Hello, World!: {s['screen'][:300]!r}")
        page.evaluate("document.getElementById('mm').scrollIntoView()")
        page.screenshot(path=str(shots / f"memmap-{name}-end-1280-light.png"))
        if errs:
            problems.append(f"{name}: page errors {errs[:3]}")
    finally:
        ctx.close()


def mm_ui_case(browser, base, shots, problems, r):
    """Replay (the drop), slider, Live, a cell from a click and the keyboard,
    go there, only first touch, closing; 1280 dark then 390 light and dark."""
    name = "minified-linux"
    ctx, page, errs = mm_open(browser, base, name, "dark")
    try:
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
            problems.append("ui: no end")
            return
        mm_frames(page)
        n = page.evaluate("MKB.MEV.n")
        # Replay: the loader's drop first. On a slow CI runner (runs
        # 43649993796, 43658380830: no drop, 7,972 events replayed) a frame
        # comes 100 ms after the last and applies ~160 events; the 40
        # newest animations once pushed the loader's drops out before they
        # were drawn. That frame, made here: the drop must be in it.
        slow = page.evaluate("""() => { MKB.MMAP.play(true); MKB.MMAP.frame(MKB.MMAP.t + 100);
          return { idx: MKB.MMAP.idx, anims: MKB.MMAP.anims.length,
                   drops: MKB.MMAP.anims.filter((a) => a.type === 'drop').map((a) => MKB.MEV.reasons[MKB.MEV.cols.src[a.i & (MKB.MEV.cap - 1)]]) }; }""")
        r["replay slow frame"] = slow
        if "kernel protected-mode part" not in slow["drops"]:
            problems.append(f"ui: a replay frame 100 ms after the start lost the kernel's drop: {slow}")
        page.evaluate("MKB.MMAP.play(false)")
        # Sampled in the page, at the frame the drop is on the map.
        page.click("#mm_play")
        drop = page.evaluate("""() => new Promise((done) => {
          const t0 = MKB.realNow();
          const look = () => {
            const s = { drops: MKB.MMAP.anims.filter((a) => a.type === 'drop').map((a) => MKB.MEV.reasons[MKB.MEV.cols.src[a.i & (MKB.MEV.cap - 1)]]),
              heat: MKB.MMAP.heat[2 * 16384 + MKB.MMAP.cellOf(0x100000)], playing: MKB.MMAP.playing, status: document.getElementById('mm_status').textContent };
            if (s.drops.includes('kernel protected-mode part') && s.heat >= 0.3 || MKB.realNow() - t0 > 5000)
              done(s);
            else
              requestAnimationFrame(look);
          };
          look();
        })""")
        r["replay"] = drop
        if "kernel protected-mode part" not in drop["drops"] or drop["heat"] < 0.3 or not drop["playing"]:
            problems.append(f"ui: Replay shows no drop of the kernel at 1 MiB: {drop}")
        film = []
        page.select_option("#mm_speed", "4")
        for k in range(6):
            time.sleep(0.7)
            page.evaluate("document.getElementById('mm').scrollIntoView()")
            p = shots / f"memmap-replay-{k}-1280-dark.png"
            page.screenshot(path=str(p))
            film.append(page.evaluate("[MKB.MMAP.idx, MKB.MMAP.anims.length]"))
        r["filmstrip"] = film
        if len({f[0] for f in film}) < 3:
            problems.append(f"ui: the replay does not move: {film}")
        # a replay that reaches the log's end goes live again
        if not wait_for(lambda: page.evaluate("MKB.MMAP.live && !MKB.MMAP.playing && MKB.MMAP.idx === MKB.MEV.n"), 30, 0.2):
            problems.append(f"ui: the replay did not end live: {page.evaluate('[MKB.MMAP.live, MKB.MMAP.playing, MKB.MMAP.idx, MKB.MEV.n]')}")
        # the slider: the first event, then Live
        page.evaluate("() => { const s = document.getElementById('mm_time'); s.value = '0'; s.dispatchEvent(new Event('input')); }")
        mm_frames(page)
        at0 = page.evaluate(MM_STATE_JS)
        if at0["live"] or at0["idx"] > 1 or "Hello" in at0["screen"]:
            problems.append(f"ui: the slider at 0 shows {at0['idx']} events, screen {at0['screen'][:60]!r}")
        page.click("#mm_live")
        mm_frames(page)
        live = page.evaluate(MM_STATE_JS)
        if not live["live"] or live["idx"] != n or "Hello, World!" not in live["screen"]:
            problems.append(f"ui: Live: {live['idx']} of {n}, {live['screen'][:60]!r}")
        r["1280 labels"] = mm_labels_check(page, "ui 1280", problems)
        # a RAM page the CPU wrote (sampled): its cell lists the stretch
        a = page.evaluate("MKB.MMAP.samples.length ? MKB.MMAP.samples[MKB.MMAP.samples.length - 1].pages[0] * 4096 : -1")
        if a < 0:
            problems.append("ui: no CPU RAM write sampled")
        else:
            x, y = mm_cell_xy(page, page.evaluate("(a) => MKB.MMAP.cellOf(a)", a))
            page.mouse.click(x, y)
            wrote = page.evaluate("[...document.querySelectorAll('#mm_info li')].map((l) => l.textContent).filter((t) => t.startsWith('CPU wrote here between'))")
            r["sampled cell"] = {"page": hex(a), "rows": wrote[:2]}
            if not wrote:
                problems.append(f"ui: the cell of sampled page {hex(a)} lists no CPU write")
        # only first touch, before "go there" below: the map samples the
        # CPU's RAM writes once per map frame (at most ~20 a second), so on a
        # runner whose frames starve while the JIT runs the whole boot can be
        # one sample that ends past the step back's target, which drops it
        # (run 43893874205: 0 samples left, "0 of 0 cells"). Here at the
        # log's end the samples checked above are all there.
        # only first touch at the log's end: whole regions lit once
        page.check("#mm_first")
        page.evaluate("() => { const s = document.getElementById('mm_time'); s.value = s.max; s.dispatchEvent(new Event('input')); }")
        mm_frames(page)
        ft = page.evaluate("""() => { const g = MKB.MMAP.segs[MKB.MMAP.segOf(0xb8000)]; let lit = 0; for (let d = g.c0; d < g.c1; d++) if (MKB.MMAP.touched[d]) lit++;
          // the CPU's sampled RAM writes (layer 3) light whole regions too: the
          // segment of the last sampled page, every cell
          const s = MKB.MMAP.samples[MKB.MMAP.samples.length - 1], h = s ? MKB.MMAP.segs[MKB.MMAP.segOf(s.pages[0] * 4096)] : null; let ram = 0;
          if (h) for (let d = h.c0; d < h.c1; d++) if (MKB.MMAP.touched[d] & 8) ram++;
          return { cells: g.cells, lit, story: MKB.MMAP.story.length, ramCells: h ? h.cells : 0, ramLit: ram, samples: MKB.MMAP.samples.length,
                   samplePos: MKB.MMAP.samples.map((x) => x.pos) }; }""")
        r["first touch"] = ft
        if ft["lit"] != ft["cells"]:
            problems.append(f"ui: only first touch lights {ft['lit']} of the text region's {ft['cells']} cells")
        if not ft["ramCells"] or ft["ramLit"] != ft["ramCells"]:
            problems.append(f"ui: only first touch at the log's end lights {ft['ramLit']} of {ft['ramCells']} cells of the last sampled RAM page's region")
        page.evaluate("document.getElementById('mm').scrollIntoView()")
        page.screenshot(path=str(shots / "memmap-firsttouch-1280-dark.png"))
        page.uncheck("#mm_first")
        # a click on the colour text memory's cell: its events
        d = page.evaluate("MKB.MMAP.cellOf(0xb8000)")
        x, y = mm_cell_xy(page, d)
        page.mouse.click(x, y)
        info = page.evaluate("""() => ({ head: document.querySelector('#mm_info p').textContent, rows: document.querySelectorAll('#mm_info li').length,
          go: document.querySelectorAll('#mm_info .mm-go').length, links: document.querySelectorAll('#mm_info .srclink').length })""")
        r["click"] = info
        if "colour text (CGA) video memory (IBM PC memory map)" not in info["head"] or info["rows"] < 5 or not info["go"] or not info["links"]:
            problems.append(f"ui: a click on 0xb8000's cell: {info}")
        # go there: the newest event of the cell; the machine steps back to it
        target = page.evaluate("""() => { const b = document.querySelector('#mm_info .mm-go'); const pos = Number(b.title.match(/instruction ([\\d,]+)/)[1].replace(/,/g, ''));
          b.click(); return pos; }""")
        ok = wait_for(lambda: page.evaluate("!MKB.REV.busy && MKB.runState === 'paused' && MKB.stepper.steps <= %d" % target), 300, 0.2)
        mm_frames(page)
        back = page.evaluate("({ steps: MKB.stepper.steps, n: MKB.MEV.n, idx: MKB.MMAP.idx, live: MKB.MMAP.live, samples: MKB.MMAP.samples.length, rev: document.getElementById('rev_info').textContent.slice(0, 160) })")
        r["go there"] = {"target": target, **back}
        if not ok or back["steps"] != target or back["idx"] != back["n"] or back["n"] >= n:
            problems.append(f"ui: go there to {target:,}: {back}")
        # keyboard: PageDown twice, Enter
        page.focus("#mm_map")
        page.keyboard.press("PageDown")
        page.keyboard.press("PageDown")
        page.keyboard.press("ArrowRight")
        page.keyboard.press("Enter")
        for k in ("End", "ArrowDown", "ArrowLeft", "Home", "ArrowUp", "PageUp", "x"):
            page.keyboard.press(k)
        page.keyboard.press("PageDown")
        page.keyboard.press("PageDown")
        page.keyboard.press("Enter")
        kb = page.evaluate("({ cursor: document.getElementById('mm_cursor').textContent, head: (document.querySelector('#mm_info p') || {}).textContent || '', pick: MKB.MMAP.pick })")
        r["keyboard"] = kb
        if not kb["cursor"] or not kb["head"].startswith("0x") or kb["pick"] < 0:
            problems.append(f"ui: keyboard: {kb}")
        # the region list: a region's button shows its first cell's events;
        # pointing at the map names the cell, leaving it clears that
        page.click("#mm_regions li:nth-child(1) button")
        reg = page.evaluate("({ pick: MKB.MMAP.pick, head: document.querySelector('#mm_info p').textContent })")
        x, y = mm_cell_xy(page, page.evaluate("MKB.MMAP.cellOf(0xb8000)"))
        page.mouse.move(x, y)
        hov = page.get_attribute("#mm_map", "title")
        page.mouse.move(x, y - 2000 if y > 2000 else 1)
        page.evaluate("document.dispatchEvent(new Event('visibilitychange'))")
        r["region list"] = {**reg, "hover": hov}
        if reg["pick"] != 0 or not reg["head"].startswith("0x0-0x") or "colour text (CGA)" not in (hov or ""):
            problems.append(f"ui: region list or pointing: {reg} {hov!r}")
        # closing: nothing recorded or drawn
        page.click("#mm summary")
        mm_frames(page)
        time.sleep(0.3)
        closed = page.evaluate("({ on: MKB.MEV.on, raf: !!MKB.MMAP.raf, track: MKB.MMAP.trackFrom, open: MKB.MMAP.open })")
        r["closed"] = closed
        if closed["on"] or closed["raf"] or closed["track"] != -1 or closed["open"]:
            problems.append(f"ui: closed, the map still runs: {closed}")
        if errs:
            problems.append(f"ui: page errors {errs[:3]}")
    finally:
        ctx.close()
    for scheme in ("light", "dark"):
        ctx, page, errs = mm_open(browser, base, name, scheme, phone=True)
        try:
            if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
                problems.append(f"ui 390 {scheme}: no end")
                continue
            page.click("#mm_play")
            time.sleep(1.5)
            page.click("#mm_play")  # pause mid-replay
            mm_frames(page)
            page.evaluate("document.getElementById('mm').scrollIntoView()")
            page.screenshot(path=str(shots / f"memmap-390-{scheme}.png"))
            s = page.evaluate(MM_STATE_JS)
            r[f"390 {scheme}"] = {"idx": s["idx"], "status": s["status"], "hscroll": s["hscroll"]}
            if s["hscroll"]:
                problems.append(f"ui 390 {scheme}: horizontal scroll")
            if s["live"] or not s["status"].startswith("At "):
                problems.append(f"ui 390 {scheme}: paused replay: {s['status']!r}")
            # the region labels wrap and are never cut short (the runner saw
            # "PCI 00:12.0 BAR0: display c..." at 390 px); each stays inside
            page.evaluate("() => { const s = document.getElementById('mm_time'); s.value = s.max; s.dispatchEvent(new Event('input')); }")
            mm_frames(page)
            labels = mm_labels_check(page, f"ui 390 {scheme}", problems)
            r[f"390 {scheme} labels"] = labels
            if not any(lb.startswith("PCI 00:12.0") for lb in labels):
                problems.append(f"ui 390 {scheme}: the display controller's region has no label: {labels}")
            if scheme == "dark":
                mm_glow_check(page, shots, problems, r)
            if errs:
                problems.append(f"ui 390 {scheme}: page errors {errs[:3]}")
        finally:
            ctx.close()


def mm_labels_check(page, tag, problems):
    """The labels drawn last: whole (no ellipsis, the lines give the text back) and inside the map."""
    drawn = page.evaluate("MKB.MMAP.labelsDrawn || []")
    for lb in drawn:
        if any(ln.endswith("…") for ln in lb["lines"]) or " ".join(lb["lines"]) != lb["text"]:
            problems.append(f"{tag}: a label cut short: {lb}")
        if lb["left"] < -0.5 or lb["right"] > lb["width"] + 0.5:
            problems.append(f"{tag}: a label outside the map: {lb}")
    if not drawn:
        problems.append(f"{tag}: no label drawn")
    return [lb["text"] for lb in drawn]


def mm_glow_check(page, shots, problems, r):
    """The colour text inset right after the BIOS's last write to the text
    memory (it cleared the screen and printed its banner): only painted
    characters glow, the cleared cells stay black. An earlier build washed
    every written cell in the glow's amber (olive on the dark page)."""
    at = page.evaluate("""() => { let last = -1; const c = MKB.MEV.cols, m = MKB.MEV.cap - 1;
      for (let i = MKB.MEV.first(); i < MKB.MEV.n; i++) { const k = i & m;
        if ((c.meta[k] & 3) === 0 && (c.meta[k] & 4) && c.addr[k] >= 0xb8000 && c.addr[k] < 0xc0000 && MKB.MEV.nameOf(MKB.MEV.regionOf(c.eipPhys[k] >>> 0)).kind !== 'ram') last = i; }
      return last - MKB.MEV.first(); }""")
    page.evaluate("(i) => { const s = document.getElementById('mm_time'); s.value = String(i + 1); s.dispatchEvent(new Event('input')); }", at)
    mm_frames(page, 2)
    px = page.evaluate("""() => { const cv = document.getElementById('mm_text'), d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data;
      let wash = 0; for (let i = 0; i < d.length; i += 4) if (d[i] > 40 && d[i + 1] > 35 && d[i + 2] < 0.6 * d[i + 1]) wash++;
      return { wash: wash / (d.length / 4), text: MKB.MMAP.text.slice(0, 160).filter((b, i) => !(i & 1) && b > 32).length }; }""")
    page.evaluate("document.getElementById('mm_text').scrollIntoView()")
    page.screenshot(path=str(shots / "memmap-390-dark-bios-text.png"))
    r["390 dark glow"] = {"event": at, **px}
    if at < 0 or px["wash"] > 0.1:
        problems.append(f"ui 390 dark: the text inset after the BIOS's writes is washed: {px} at event {at}")


def mm_same_case(browser, base, shots, problems, r):
    """The map records the same events as the debug list: it only reads."""
    out = {}
    for q in ("memevents=1", "memmap=1"):
        ctx, page, errs = mm_open(browser, base, "minified-linux", q=q)
        try:
            if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
                problems.append(f"same: no end with {q}")
                return
            time.sleep(0.5)
            steps = page.evaluate("MKB.stepper.steps")
            out[q] = page.evaluate(MEV_DIGEST_JS, steps)
        finally:
            ctx.close()
    r["same"] = out
    if out["memevents=1"] != out["memmap=1"]:
        problems.append(f"same: the map open recorded {out['memmap=1']}, the list {out['memevents=1']}")


def mm_speed_case(browser, base, shots, debian, problems, r):
    """Debian at full JIT speed to JITDET_CAP, the map closed and shown."""
    if not debian:
        return
    name = debian[0]
    for tag, q in (("closed", ""), ("shown", "&memmap=1")):
        ctx, page, errs = mev_page(browser)
        try:
            page.goto(f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips=10{q}", timeout=180000)
            if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused'"), 180, 0.5):
                problems.append(f"speed {tag}: not paused at the reset vector")
                continue
            show_tab(page, "memory")
            page.evaluate(JITDET_WATCH_JS, [JITDET_CAP, True])
            machine_do(page, "click", "#dbg_continue")
            if not wait_for(lambda: page.evaluate("window.__jd.done"), 400, 0.2):
                problems.append(f"speed {tag}: did not reach {JITDET_CAP:,}")
                continue
            s = page.evaluate("""() => { const w = window.__jd; return { mips: +(((w.c - w.c0) >>> 0) / (w.at - w.t0) / 1000).toFixed(1), events: MKB.MEV.n,
              frames: MKB.MMAP.frames, drawMs: Math.round(MKB.MMAP.drawMs), samples: MKB.MMAP.samples.length }; }""")
            r[f"speed {tag}"] = s
            if errs:
                problems.append(f"speed {tag}: page errors {errs[:3]}")
        finally:
            ctx.close()
    a, b = r.get("speed closed"), r.get("speed shown")
    if a and b:
        r["speed shown / closed"] = round(b["mips"] / a["mips"], 3)
        if a["events"] > 10 or a["frames"]:
            problems.append(f"speed: with the map closed {a['events']} events were recorded, {a['frames']} frames drawn")
        if not b["events"] or not b["frames"] or not b["samples"]:
            problems.append(f"speed: with the map shown: {b}")


def mm_first_frame_late_case(browser, base, problems, r):
    """The map open (?memmap=1) but its tab not shown until the run ended:
    no map frame drew while the machine ran. The CPU's RAM writes since the
    machine started are still sampled (one sample, the whole run), as on a
    runner whose frames starve while the JIT runs (runs 43893874205 and
    43975055292: "no CPU RAM write sampled")."""
    ctx, page, errs = mev_page(browser, "light", False)
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full&count=0&mips=10&memmap=1", timeout=180000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 600, 0.5):
            problems.append("late map: no end")
            return
        frames0 = page.evaluate("MKB.MMAP.frames")
        show_tab(page, "memory")
        mm_frames(page)
        got = page.evaluate("({ samples: MKB.MMAP.samples.length, pages: MKB.MMAP.samples.reduce((a, s) => a + s.pages.length, 0), "
                            "from: MKB.MMAP.samples.length ? MKB.MMAP.samples[0].from : -1, frames: MKB.MMAP.frames })")
        r["late map"] = {"frames before": frames0, **got}
        if frames0:
            problems.append(f"late map: {frames0} map frames while the tab was hidden (the case needs none)")
        if not got["samples"] or not got["pages"]:
            problems.append(f"late map: no CPU RAM write sampled when the map is first drawn after the run: {got}")
        if errs:
            problems.append(f"late map: page errors {errs}")
    finally:
        ctx.close()


def suite_memmap(ctx):
    problems, r = [], {}
    if "minified-linux" in ctx.entries:
        mm_first_frame_late_case(ctx.browser, ctx.base, problems, r)
    for name in ("minified-linux", *debian_names(ctx)[:1]):
        if name in ctx.entries:
            mm_story_case(ctx.browser, ctx.base, ctx.shots, name, problems, r)
    if "minified-linux" in ctx.entries:
        mm_ui_case(ctx.browser, ctx.base, ctx.shots, problems, r)
        mm_same_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    mm_speed_case(ctx.browser, ctx.base, ctx.shots, debian_names(ctx), problems, r)
    return report("memmap", problems, r)
