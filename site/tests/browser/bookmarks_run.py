"""Part of a browser suite (test_browser.py)."""

import json
import pathlib
import time

from tests.browser.common import (
    bm_close_menu, BM_DIALOG_JS, bm_dialog_save, bm_open_list, bm_state, bm_tap, graph_box_xy, kernel_entries, nav_at,
    page_center, run_to_wait, show_tab, shown_tab, SITE, wait_for,
)
from tests.browser.snapdiff_run import BOOKMARKS_POINTS


def tip_long_press(page, cdp, x, y, ms=800):
    cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": x, "y": y}]})
    time.sleep(ms / 1000)
    cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})

BM_WHERE_JS = """() => { const sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
  const L = document.getElementById('listing'), lb = L.getBoundingClientRect();
  const vis = (a) => { const e = L.querySelector('.ins[data-addr="' + a + '"]'); if (!e) return false;
    const r = e.getBoundingClientRect(); return r.top >= lb.top - 1 && r.bottom <= lb.bottom + 1 && r.bottom > 0 && r.top < innerHeight; };
  return { tab: MKB.tab, page: MKB.selectedPage, listingPage: Number(L.dataset.page ?? -1), srcOpen: !sp.hidden, file: sb.dataset.file || '',
    title: document.getElementById('src_title').textContent, pick: Number((sb.querySelector('.pick') || {dataset: {}}).dataset.line || 0),
    hit: MKB.jumpHit, hash: location.hash,
    jhit: [...L.querySelectorAll('.ins.jhit[data-addr]')].map((e) => Number(e.dataset.addr)),
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, insVisible: (() => {
      // On screen: inside the listing, under the top bar, above the Source panel's sheet (a phone).
      const top = Math.max(lb.top, document.querySelector('.top').getBoundingClientRect().bottom, 0);
      const pr = sp.hidden ? null : sp.getBoundingClientRect();
      const bottom = Math.min(lb.bottom, innerHeight, pr && pr.left <= 0 && pr.top > 0 ? pr.top : Infinity);
      return [...L.querySelectorAll('.ins[data-addr]')].filter((e) => { const r = e.getBoundingClientRect();
        return r.top >= top - 1 && r.bottom <= bottom + 1; }).map((e) => Number(e.dataset.addr)); })() }; }"""


def bm_where(page):
    return page.evaluate(BM_WHERE_JS)


def bm_press(page, sel, phone, cdp, nth=0):
    """A context menu (right click) at 1280, a long press at 390."""
    loc = page.locator(sel).nth(nth)
    loc.scroll_into_view_if_needed()
    b = loc.bounding_box()
    x, y = b["x"] + min(b["width"] / 2, 60), b["y"] + b["height"] / 2
    if phone:
        tip_long_press(page, cdp, x, y)
    else:
        page.mouse.click(x, y, button="right")


def bm_press_xy(page, x, y, phone, cdp):
    if phone:
        tip_long_press(page, cdp, x, y)
    else:
        page.mouse.click(x, y, button="right")


def bm_shots(page, shots, stem, scheme):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))
    page.emulate_media(color_scheme=scheme)


def bm_star_tip(page, sel, name, phone, problems, tag):
    """The ★ at sel exists and its popover names the bookmark."""
    star = page.locator(sel)
    if not star.count():
        problems.append(f"{tag}: no ★ mark at {sel}")
        return
    star.first.scroll_into_view_if_needed()
    if phone:
        star.first.tap()
    else:
        star.first.hover()
    if not wait_for(lambda: page.is_visible("#tip_pop"), 3):
        problems.append(f"{tag}: the ★ at {sel} shows no popover")
        return
    t = page.inner_text("#tip_pop")
    if name not in t or "ookmark" not in t:
        problems.append(f"{tag}: the ★ popover at {sel} does not name the bookmark {name!r}: {t[:160]!r}")
    # Closed as a tap elsewhere would (Escape in the Source panel also closes the panel).
    page.evaluate("MKB.hideTip()")
    page.mouse.move(2, 300)


def bm_jump_check(page, b, F, syms, problems, tag, phone):
    """Jump to bookmark b from the list; check the place independently, then Back."""
    if not bm_open_list(page, phone):
        problems.append(f"{tag}: the bookmarks list does not open")
        return
    before = nav_at(page)
    where0 = bm_where(page)
    sel = f'#bm_list .bmrow[data-id="{b["id"]}"] .bmgo'
    if not page.locator(sel).count():
        problems.append(f"{tag}: bookmark {b['name']!r} is not listed")
        return
    bm_tap(page, sel, phone)
    time.sleep(1.4)
    k, t = b["kind"], b["t"]
    ok = True
    if k in ("code", "pos"):
        pa = t["pa"]
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == pa // 4096 and pa in w["insVisible"])(bm_where(page)), 15)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the {k} bookmark {b['name']!r} did not show {hex(pa)} in the Page listing: tab {w['tab']}, "
                            f"page {w['listingPage']}, lines on screen {[hex(x) for x in w['insVisible'][:3]]}..")
    elif k == "page":
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == t["page"])(bm_where(page)), 15)
        if not ok:
            problems.append(f"{tag}: the page bookmark {b['name']!r} did not open page {hex(t['page'])}: {bm_where(page)['listingPage']}")
    elif k == "fn":
        s = syms.get(t["name"])
        want = s["paddr"] if s else t["pa"]
        if want != t["pa"]:
            problems.append(f"{tag}: the function bookmark {t['name']} holds {hex(t['pa'])}, symbols.json says {hex(want)}")
        ok = wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == want // 4096 and want in w["insVisible"])(bm_where(page)), 15)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the function bookmark {b['name']!r} did not show {t['name']} at {hex(want)}: tab {w['tab']} page {w['listingPage']}")
    elif k == "line":
        ok = wait_for(lambda: (lambda w: w["srcOpen"] and w["file"] == t["path"] and w["pick"] == t["line"])(bm_where(page)), 20)
        if not ok:
            w = bm_where(page)
            problems.append(f"{tag}: the line bookmark {b['name']!r} did not open {t['path']}:{t['line']}: {w['file']}:{w['pick']} open {w['srcOpen']}")
        else:
            vis = page.evaluate("""(l) => { const sb = document.getElementById('src_body'), e = sb.querySelector('[data-line="' + l + '"]');
                if (!e) return false; const a = e.getBoundingClientRect(), b = sb.getBoundingClientRect(); return a.top >= b.top - 1 && a.bottom <= b.bottom + 1; }""", t["line"])
            if not vis:
                problems.append(f"{tag}: the line bookmark's line {t['line']} is not in view in the Source panel")
    elif k == "file":
        ok = wait_for(lambda: (lambda w: w["srcOpen"] and w["file"] == t["path"] and "(whole file)" in w["title"])(bm_where(page)), 20)
        if not ok:
            problems.append(f"{tag}: the file bookmark {b['name']!r} did not open {t['path']} (whole file): {bm_where(page)['title']!r}")
    after = nav_at(page)
    if after[0] != before[0] + 1:
        problems.append(f"{tag}: the jump to {b['name']!r} is not one history entry: {before} -> {after}")
    if bm_where(page)["hscroll"]:
        problems.append(f"{tag}: horizontal scroll after the jump to {b['name']!r}")
    # Back returns to where the jump started.
    bm_close_menu(page)
    page.keyboard.press("Alt+ArrowLeft") if not phone else page.go_back()
    if not wait_for(lambda: nav_at(page)[0] == before[0] and bm_where(page)["tab"] == where0["tab"], 8):
        problems.append(f"{tag}: Back after the jump to {b['name']!r} did not return: {nav_at(page)} tab {bm_where(page)['tab']}, want {before} {where0['tab']}")
    time.sleep(0.5)
    return ok


def bookmarks_run(browser, base, shots, name, scheme, phone, other=None, problems=None):
    fn, steps, src_path = BOOKMARKS_POINTS[name]
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    problems, r = [] if problems is None else problems, {}
    errors = []
    width = 390 if phone else 1280
    tag = f"{name} {width} {scheme}"
    vp = {"width": 390, "height": 844} if phone else {"width": 1280, "height": 1000}
    ctx = browser.new_context(viewport=vp, has_touch=True, is_mobile=True, accept_downloads=True) if phone else \
        browser.new_context(viewport=vp, accept_downloads=True)
    page = ctx.new_page()
    cdp = ctx.new_cdp_session(page)
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(syms[fn]["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {syms[fn]["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{tag}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)
    eip = int(page.get_attribute("#regs", "data-phys"))
    icount = page.evaluate("MKB.emulator.v86.cpu.instruction_counter[0] >>> 0")  # v86's count: Restore puts it back
    steps = page.evaluate("MKB.stepper.steps")  # the count the Machine tab shows, and the bookmark's name (X2)
    r["eip"] = [hex(eip), steps, icount]
    show_tab(page, "debugger")
    added = {}

    # 1. Code at EIP: its ☆ button; the default names EIP's symbol.
    if not page.locator("#bm_add_eip").count():
        ctx.close()
        return [f"{tag}: no ☆ button by Code at EIP (#bm_add_eip)"], r
    bm_tap(page, "#bm_add_eip", phone)
    if wait_for(lambda: page.evaluate(BM_DIALOG_JS) is not None, 5):
        bm_shots(page, shots, f"bookmarks-{name}-{width}-dialog", scheme)
    b = bm_dialog_save(page, problems, tag, "code", want_default=None)
    if b:
        added["eip"] = b
        if b["t"]["pa"] != eip:
            problems.append(f"{tag}: the Code at EIP bookmark holds {hex(b['t']['pa'])}, EIP is at {hex(eip)}")
    # ... and the line shows the ★ (in Code at EIP).
    wait_for(lambda: page.locator(f'#context .ins[data-addr="{eip}"] .bmstar').count() > 0, 5)
    if b:
        bm_star_tip(page, f'#context .ins[data-addr="{eip}"] .bmstar', b["name"], phone, problems, f"{tag} Code at EIP")

    # 2. A frame of the call stack: its ☆; a function bookmark of that frame's function.
    frames = json.loads(page.get_attribute("#call_stack", "data-frames") or "[]")
    r["frames"] = [f["name"] for f in frames]
    k = next((i for i, f in enumerate(frames) if f["name"] in syms and not f["user"]), None)
    if k is None or not page.locator("#call_stack .cframe .bmadd").count():
        problems.append(f"{tag}: no call stack frame with a ☆ button (frames {r['frames']})")
    else:
        bm_tap(page, f"#call_stack .cframe >> nth={k} >> .bmadd", phone)
        b = bm_dialog_save(page, problems, tag, "fn", name="frame " + frames[k]["name"], want_default=frames[k]["name"])
        if b:
            added["frame"] = b
            if b["t"]["name"] != frames[k]["name"] or b["t"]["pa"] != syms[frames[k]["name"]]["paddr"]:
                problems.append(f"{tag}: the call stack bookmark holds {b['t']}, want {frames[k]['name']} at {hex(syms[frames[k]['name']]['paddr'])}")
            wait_for(lambda: page.locator("#call_stack .cframe .bmstar").count() > 0, 5)
            if not page.locator("#call_stack .cframe .bmstar").count():
                problems.append(f"{tag}: the bookmarked call stack frame has no ★")

    # 3. A listing line in the Page tab: context menu / long press, a typed name.
    target = syms[fn]["paddr"]
    page.evaluate(f"MKB.jumpTo({target})")  # the Page tab at the function, as a symbol link does
    time.sleep(1.4)
    addrs = page.evaluate("[...document.querySelectorAll('#listing .ins[data-addr]')].map((e) => Number(e.dataset.addr))")
    line_pa = next((a for a in addrs if a > target + 4), None)
    if line_pa is None:
        problems.append(f"{tag}: the Page listing has no instruction after {fn}")
    else:
        bm_press(page, f'#listing .ins[data-addr="{line_pa}"]', phone, cdp)
        b = bm_dialog_save(page, problems, tag, "code", name="my line", key="Enter", want_default=fn + "+0x")
        if b:
            added["line"] = b
            if b["t"]["pa"] != line_pa:
                problems.append(f"{tag}: the listing line bookmark holds {hex(b['t']['pa'])}, the line is {hex(line_pa)}")
            bm_star_tip(page, f'#listing .ins[data-addr="{line_pa}"] .bmstar', "my line", phone, problems, f"{tag} listing")
        # The B key with the pointer over a line (a mouse).
        if not phone:
            other_pa = next((a for a in addrs if a > line_pa), None)
            page.hover(f'#listing .ins[data-addr="{other_pa}"]')
            page.keyboard.press("b")
            b = bm_dialog_save(page, problems, tag + " B key", "code")
            if b and b["t"]["pa"] != other_pa:
                problems.append(f"{tag}: B over {hex(other_pa)} bookmarked {hex(b['t']['pa'])}")
            if b:
                added["bkey"] = b
            # Escape cancels the dialog.
            page.hover(f'#listing .ins[data-addr="{other_pa}"]')
            page.keyboard.press("b")
            if wait_for(lambda: page.evaluate(BM_DIALOG_JS) is not None, 3):
                n0 = len(bm_state(page)["list"])
                page.keyboard.press("Escape")
                if not wait_for(lambda: page.evaluate(BM_DIALOG_JS) is None, 3) or len(bm_state(page)["list"]) != n0:
                    problems.append(f"{tag}: Escape did not cancel the dialog")
            else:
                problems.append(f"{tag}: the B key opened no dialog the second time")
        # The function's label line above its first instruction: a bookmark
        # of the function, by a right click or a long press. At 390 px a long
        # press on the minified Linux's start_kernel label opened no dialog
        # (CI run 43509415990): Chromium gave its touch events to the whole
        # listing (#listing), not to the label under the finger.
        lbl = f'#listing .lbl[data-addr="{target}"]'
        if not page.locator(lbl).count():
            problems.append(f"{tag}: the Page listing has no label line of {fn}")
        else:
            bm_press(page, lbl, phone, cdp)
            b = bm_dialog_save(page, problems, tag + " label line", "fn", name="label " + fn, want_default=fn)
            if b:
                added["label"] = b
                if b["t"].get("pa") != target or b["t"].get("name") != fn:
                    problems.append(f"{tag}: the label line's bookmark holds {b['t']}, want {fn} at {hex(target)}")
    bm_shots(page, shots, f"bookmarks-{name}-{width}-listing", scheme)

    # 4. A page of the Memory grid: context menu / long press on its square.
    mem_p = eip // 4096 + 1
    x, y = page_center(page, mem_p)
    bm_press_xy(page, x, y, phone, cdp)
    b = bm_dialog_save(page, problems, tag, "page", want_default=hex(mem_p))
    if b:
        added["page"] = b
        if b["t"]["page"] != mem_p:
            problems.append(f"{tag}: the Memory page bookmark holds page {hex(b['t']['page'])}, the square is {hex(mem_p)}")
        if shown_tab(page) != "memory":
            problems.append(f"{tag}: bookmarking a page left the Memory tab ({shown_tab(page)})")
        # B3: only the click that ends the long press is dropped. The next
        # click, here a tab's right after the dialog closed, still works (it
        # was dropped for 1 s after the press, so CI's next step, the
        # Debugger tab, sometimes stayed hidden).
        if phone:
            r["click_after_long_press_ms"] = round(page.evaluate("MKB.realNow() - MKB.BM.longAt"))
            page.click("#tab_debugger")
            if not wait_for(lambda: shown_tab(page) == "debugger", 3):
                problems.append(f"{tag}: a click on the Debugger tab {r['click_after_long_press_ms']} ms after the long press "
                                f"that opened the dialog did nothing (tab {shown_tab(page)})")
            show_tab(page, "memory")
        wait_for(lambda: page.locator(f'#pages_box .bmpage[data-page="{mem_p}"]').count() > 0, 5)
        bm_star_tip(page, f'#pages_box .bmpage[data-page="{mem_p}"]', b["name"], phone, problems, f"{tag} Memory")
    # The Memory tab's ☆ (the selected page, else EIP's).
    if not page.locator("#bm_add_mem").count():
        problems.append(f"{tag}: no ☆ button in the Memory tab (#bm_add_mem)")
    bm_shots(page, shots, f"bookmarks-{name}-{width}-memory", scheme)

    # 5. A line of the Source panel: pick it, its ☆.
    show_tab(page, "debugger")
    if not wait_for(lambda: shown_tab(page) == "debugger", 3):
        problems.append(f"{tag}: the Debugger tab did not open (tab {shown_tab(page)})")
    if wait_for(lambda: page.locator("#context .src:not(.nol)").first.is_visible(), 30):
        page.locator("#context .src:not(.nol)").first.scroll_into_view_if_needed()
        bm_tap(page, "#context .src:not(.nol) >> nth=0", phone)
        if not wait_for(lambda: page.evaluate("!document.getElementById('src_panel').hidden && document.getElementById('src_body').dataset.state === 'ok'"), 25):
            problems.append(f"{tag}: a C line of Code at EIP did not open the Source panel")
        time.sleep(0.5)
        rows = page.evaluate("[...document.querySelectorAll('#src_body .exec:not(.cont)')].map((e) => Number(e.dataset.line))")
        file = page.get_attribute("#src_body", "data-file")
        if rows:
            ln = rows[len(rows) // 2]
            page.locator(f'#src_body [data-line="{ln}"]').scroll_into_view_if_needed()
            bm_tap(page, f'#src_body [data-line="{ln}"]', phone)
            time.sleep(0.3)
            if not page.locator("#bm_add_src").count():
                problems.append(f"{tag}: no ☆ button in the Source panel (#bm_add_src)")
            else:
                bm_tap(page, "#bm_add_src", phone)
                b = bm_dialog_save(page, problems, tag, "line", want_default=file.rsplit("/", 1)[-1] + ":" + str(ln))
                if b:
                    added["src"] = b
                    if b["t"] != {"path": file, "line": ln}:
                        problems.append(f"{tag}: the Source panel bookmark holds {b['t']}, want {file}:{ln}")
                    bm_star_tip(page, f'#src_body [data-line="{ln}"] .bmstar', b["name"], phone, problems, f"{tag} Source panel")
            bm_shots(page, shots, f"bookmarks-{name}-{width}-srcpanel", scheme)
        else:
            problems.append(f"{tag}: the Source panel shows no executed line")
        if page.is_visible("#src_close"):
            bm_tap(page, "#src_close", phone)
        else:
            problems.append(f"{tag}: the Source panel is not open after bookmarking a line of it")
    else:
        problems.append(f"{tag}: Code at EIP shows no C line")

    # 6. A file of the Source tab: the row's ☆.
    show_tab(page, "source")
    page.fill("#cov_filter", src_path.rsplit("/", 1)[-1])
    sel = f'#cov_tree .covrow[data-kind="file"][data-path="{src_path}"] .bmadd'
    if not wait_for(lambda: page.locator(sel).count() > 0, 20):
        problems.append(f"{tag}: the Source tab's row of {src_path} has no ☆ button")
    else:
        bm_tap(page, sel, phone)
        b = bm_dialog_save(page, problems, tag, "file", want_default=src_path)
        if b:
            added["file"] = b
            if b["t"] != {"path": src_path}:
                problems.append(f"{tag}: the Source tab bookmark holds {b['t']}, want {src_path}")
            wait_for(lambda: page.locator(f'#cov_tree .covrow[data-path="{src_path}"] .bmstar').count() > 0, 5)
            if not page.locator(f'#cov_tree .covrow[data-path="{src_path}"] .bmstar').count():
                problems.append(f"{tag}: the bookmarked file row of the Source tab has no ★")
        bm_fit(page, tag + " Source tab", problems)
        bm_shots(page, shots, f"bookmarks-{name}-{width}-source", scheme)
    page.fill("#cov_filter", "")

    # 7. A box of the Calls tab: selected, then the Calls ☆ (1280) or a long press (390).
    show_tab(page, "calls")
    if not wait_for(lambda: page.evaluate("MKB.G.rects.length > 0"), 20):
        problems.append(f"{tag}: the Calls timeline drew no box")
    else:
        xy = graph_box_xy(page, fn)
        if not xy:
            problems.append(f"{tag}: no {fn} box in the Calls tab")
        else:
            if phone:
                tip_long_press(page, cdp, xy["x"], xy["y"])
            else:
                page.mouse.click(xy["x"], xy["y"])
                time.sleep(0.7)
                if not page.locator("#bm_add_calls").count():
                    problems.append(f"{tag}: no ☆ button in the Calls tab (#bm_add_calls)")
                else:
                    page.click("#bm_add_calls")
            b = bm_dialog_save(page, problems, tag, "fn", want_default=fn)
            if b:
                added["calls"] = b
                if b["t"] != {"pa": syms[fn]["paddr"], "name": fn}:
                    problems.append(f"{tag}: the Calls bookmark holds {b['t']}, want {fn} at {hex(syms[fn]['paddr'])}")
                time.sleep(1.2)
                marked = page.evaluate("Number(document.getElementById('graph_canvas').dataset.bmarks || 0)")
                if not marked:
                    problems.append(f"{tag}: no box of the Calls tab shows the bookmark's ★ (data-bmarks {marked})")
            if shown_tab(page) != "calls":
                problems.append(f"{tag}: bookmarking a box left the Calls tab ({shown_tab(page)})")
        bm_shots(page, shots, f"bookmarks-{name}-{width}-calls", scheme)

    # 8. The machine's position: the menu's ☆.
    bm_open_list(page, phone)
    if not page.locator("#bm_here").count():
        problems.append(f"{tag}: the menu has no ☆ for the machine's position (#bm_here)")
    else:
        bm_tap(page, "#bm_here", phone)
        # At 1280 with a snapshot saved along (S1), so the bookmark can restore the machine.
        def tick():
            if wait_for(lambda: page.is_visible("#bm_snap"), 3):
                page.check("#bm_snap")
                page.focus("#bm_name")
            else:
                problems.append(f"{tag}: the position dialog offers no snapshot (#bm_snap)")
        b = bm_dialog_save(page, problems, tag, "pos", want_default="@" + f"{steps:,}", then=None if phone else tick)
        if b:
            added["pos"] = b
            if b["t"]["pa"] != eip or b["t"].get("steps") != steps or b["t"]["icount"] != icount:
                problems.append(f"{tag}: the position bookmark holds {b['t']}, want EIP {hex(eip)} at {steps} (v86 {icount})")
            if not phone:
                if not wait_for(lambda: (bm_state(page)["list"][-1]["t"].get("snap") or "") != "", 30):
                    problems.append(f"{tag}: the position bookmark has no snapshot tied to it: {bm_state(page)['list'][-1]['t']}")
                else:
                    added["pos"] = bm_state(page)["list"][-1]

    # 9. Rename one in the list (inline), check the list and the storage.
    st = bm_state(page)
    r["added"] = {k: [v["kind"], v["name"]] for k, v in added.items()}
    if len(st["rows"]) != len(added):
        problems.append(f"{tag}: the list shows {len(st['rows'])} bookmarks, {len(added)} were added: {[x['name'] for x in st['rows']]}")
    ren = added.get("eip")
    if ren:
        bm_open_list(page, phone)
        bm_tap(page, f'#bm_list .bmrow[data-id="{ren["id"]}"] .bmren', phone)
        inp = f'#bm_list .bmrow[data-id="{ren["id"]}"] input.bmedit'
        if not wait_for(lambda: page.locator(inp).count() > 0, 3):
            problems.append(f"{tag}: ✎ does not make the name editable")
        else:
            page.fill(inp, "EIP here")
            page.press(inp, "Enter")
            wait_for(lambda: any(x["name"] == "EIP here" for x in bm_state(page)["rows"]), 3)
            ren["name"] = "EIP here"
    st = bm_state(page)
    if not st["store"] or len(st["store"].get("bookmarks", [])) != len(added):
        problems.append(f"{tag}: localStorage mkb-bookmarks holds {st['store'] and len(st['store'].get('bookmarks', []))}, want {len(added)}")
    if any(x["group"] != "this" for x in st["rows"]):
        problems.append(f"{tag}: bookmarks of this kernel listed as another's: {[(x['name'], x['group']) for x in st['rows']]}")
    bm_fit(page, tag, problems)
    bm_shots(page, shots, f"bookmarks-{name}-{width}-list", scheme)
    bm_close_menu(page)

    # 10. Reload: every bookmark is listed with its name; each jump lands; Back returns.
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: len(bm_state(page)["rows"]) == len(added) and all(x["group"] == "this" for x in bm_state(page)["rows"]), 20)
    st = bm_state(page)
    names = sorted(x["name"] for x in st["rows"])
    want = sorted(v["name"] for v in added.values())
    if names != want:
        problems.append(f"{tag}: after the reload the list has {names}, want {want}")
    if any(x["group"] != "this" for x in st["rows"]):
        problems.append(f"{tag}: after the reload bookmarks are not this kernel's: {[(x['name'], x['group']) for x in st['rows']]}")
    show_tab(page, "debugger")
    for key in ("eip", "line", "page", "src", "file", "calls", "frame", "pos"):
        if key in added:
            bm_jump_check(page, added[key], F, syms, problems, f"{tag} jump {key}", phone)
    # The position's snapshot restores the machine there (after the reload: from IndexedDB).
    if not phone and added.get("pos", {}).get("t", {}).get("snap"):
        bm_open_list(page, phone)
        rs = f'#bm_list .bmrow[data-id="{added["pos"]["id"]}"] .bmrestore'
        if not wait_for(lambda: page.locator(rs).count() > 0, 15):
            problems.append(f"{tag}: the position bookmark with a snapshot has no Restore")
        else:
            page.click(rs)
            if not wait_for(lambda: page.evaluate("(MKB.emulator.v86.cpu.instruction_counter[0] >>> 0)") == icount and
                            int(page.get_attribute("#regs", "data-phys") or -1) == eip, 30):
                problems.append(f"{tag}: Restore did not bring the machine to EIP {hex(eip)} after {icount}: "
                                f"{page.evaluate('MKB.emulator.v86.cpu.instruction_counter[0] >>> 0')} at {page.get_attribute('#regs', 'data-phys')}")
        bm_close_menu(page)
    # Keyboard: Tab to the first bookmark in the list, Enter jumps.
    if not phone and "page" in added:
        bm_open_list(page, phone)
        page.focus("#bm_filter")
        page.fill("#bm_filter", added["page"]["name"][:6])
        time.sleep(0.3)
        shown = [x for x in bm_state(page)["rows"] if x["shown"]]
        if not shown or any(added["page"]["name"][:6].lower() not in x["name"].lower() for x in shown):
            problems.append(f"{tag}: the filter {added['page']['name'][:6]!r} shows {[x['name'] for x in shown]}")
        page.keyboard.press("Tab")
        foc = page.evaluate("document.activeElement && document.activeElement.className")
        before = nav_at(page)
        page.keyboard.press("Enter")
        if not wait_for(lambda: nav_at(page)[0] == before[0] + 1 and bm_where(page)["listingPage"] == added["page"]["t"]["page"], 8):
            problems.append(f"{tag}: Tab and Enter from the filter did not jump to the filtered bookmark (focus on {foc!r}): {nav_at(page)}")
        bm_open_list(page, phone)
        page.fill("#bm_filter", "")
        bm_close_menu(page)

    # 11. Delete one; it stays deleted after a reload.
    gone = added.get("bkey") or added.get("pos")
    if gone:
        bm_open_list(page, phone)
        bm_tap(page, f'#bm_list .bmrow[data-id="{gone["id"]}"] .bmdel', phone)
        if not wait_for(lambda: not any(x["id"] == gone["id"] for x in bm_state(page)["rows"]), 3):
            problems.append(f"{tag}: ✕ did not delete {gone['name']!r}")
        added = {k: v for k, v in added.items() if v is not gone}

    # 12. Export, then import into a fresh context.
    bm_open_list(page, phone)
    exported = None
    if not page.locator("#bm_export").count():
        problems.append(f"{tag}: no Export button")
    else:
        with page.expect_download() as dl:
            bm_tap(page, "#bm_export", phone)
        path = dl.value.path()
        try:
            exported = json.loads(pathlib.Path(path).read_text())
        except Exception as e:
            problems.append(f"{tag}: the export is no JSON: {e}")
        if exported:
            got = sorted(x["name"] for x in exported.get("bookmarks", []))
            if got != sorted(v["name"] for v in added.values()):
                problems.append(f"{tag}: the export has {got}, the list {sorted(v['name'] for v in added.values())}")
            r["export"] = {"format": exported.get("format"), "n": len(exported.get("bookmarks", []))}
            ex = pathlib.Path(shots) / f"bookmarks-{name}-{width}.json"
            ex.write_text(json.dumps(exported))
    link = next((x["link"] for x in bm_state(page)["rows"] if added.get("calls") and x["id"] == added["calls"]["id"]), "")
    bm_close_menu(page)
    if errors:
        problems.append(f"{tag}: page errors {errors}")
    ctx.close()

    if exported:
        ctx2 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p2 = ctx2.new_page()
        p2.emulate_media(color_scheme=scheme)
        p2.goto(f"{base}/index.html?kernel={name}&paused=1#machine")
        wait_for(lambda: p2.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p2, phone)
        if not len(bm_state(p2)["rows"]) == 0:
            problems.append(f"{tag}: a fresh context lists bookmarks before the import")
        if not p2.locator("#bm_import_file").count():
            problems.append(f"{tag}: no import file input (#bm_import_file)")
        else:
            p2.set_input_files("#bm_import_file", str(ex))
            wait_for(lambda: len(bm_state(p2)["rows"]) == len(exported["bookmarks"]), 5)
            got = sorted(x["name"] for x in bm_state(p2)["rows"])
            if got != sorted(x["name"] for x in exported["bookmarks"]):
                problems.append(f"{tag}: the import restored {got}, the file has {sorted(x['name'] for x in exported['bookmarks'])}")
            if any(x["group"] != "this" for x in bm_state(p2)["rows"]):
                problems.append(f"{tag}: imported bookmarks are not this kernel's")
            bm_fit(p2, tag + " import", problems)
        ctx2.close()

    # 13. A link with a bookmark opens its place (a fresh context: nothing stored).
    if not link or "#bm=" not in link:
        problems.append(f"{tag}: the Calls bookmark has no link with #bm= ({link!r})")
    else:
        r["link"] = link[-80:]
        ctx3 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p3 = ctx3.new_page()
        p3.emulate_media(color_scheme=scheme)
        p3.goto(link)
        want_pa = syms[fn]["paddr"]
        # The machine runs (no paused=1): the listing may follow EIP afterwards, so
        # the check is the jump itself: the Page tab at the function, its line outlined.
        if not wait_for(lambda: (lambda w: w["tab"] == "page" and w["listingPage"] == want_pa // 4096 and want_pa in w["jhit"])(bm_where(p3)), 90):
            w = bm_where(p3)
            problems.append(f"{tag}: the link did not open {fn} in the Page tab: tab {w['tab']}, page {w['listingPage']}")
        else:
            time.sleep(1)
            bm_shots(p3, shots, f"bookmarks-{name}-{width}-link", scheme)
        ctx3.close()

    # 14. Another kernel: listed apart, with a switch to the kernel they are for.
    if other:
        ctx4 = browser.new_context(viewport=vp, has_touch=phone, is_mobile=phone)
        p4 = ctx4.new_page()
        p4.emulate_media(color_scheme=scheme)
        p4.goto(f"{base}/index.html?kernel={name}&paused=1#machine")
        wait_for(lambda: p4.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p4, phone)
        p4.set_input_files("#bm_import_file", str(ex))
        wait_for(lambda: len(bm_state(p4)["rows"]) > 0, 5)
        n = len(bm_state(p4)["rows"])
        p4.goto(f"{base}/index.html?kernel={other}&paused=1#machine")
        wait_for(lambda: p4.get_attribute("#dbg_state", "data-state") == "paused", 60)
        bm_open_list(p4, phone)
        if not wait_for(lambda: len(bm_state(p4)["rows"]) == n and all(x["group"] == "other" for x in bm_state(p4)["rows"]), 15):
            problems.append(f"{tag}: on {other} the {name} bookmarks are not listed as another kernel's: "
                            f"{[(x['name'], x['group']) for x in bm_state(p4)['rows']]}")
        sw = p4.locator("#bm_list .bmrow .bmswitch")
        if not sw.count():
            problems.append(f"{tag}: no switch action for another kernel's bookmarks")
        else:
            bm_shots(p4, shots, f"bookmarks-{name}-{width}-other", scheme)
            sw.first.tap() if phone else sw.first.click()
            if not wait_for(lambda: p4.input_value("#kernel") == name and all(x["group"] == "this" for x in bm_state(p4)["rows"]), 60):
                problems.append(f"{tag}: the switch did not load {name} with its bookmarks as this kernel's: "
                                f"{p4.input_value('#kernel')} {[(x['name'], x['group']) for x in bm_state(p4)['rows']]}")
        ctx4.close()
    return problems, r


def bm_fit(page, tag, problems):
    got = page.evaluate("""() => { const box = (id) => { const e = document.getElementById(id); if (!e || e.hidden || !e.getClientRects().length) return null;
        const b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
        const l = document.getElementById('bm_list');
        return { view: [innerWidth, innerHeight], menu: box('runbadge_menu'), list: box('bm_list'),
                 hdoc: document.documentElement.scrollWidth > innerWidth + 1, hlist: !!l && l.scrollWidth > l.clientWidth + 1 }; }""")
    for k in ("menu", "list"):
        b = got[k]
        if b and (b[0] < -1 or b[1] < -1 or b[2] > got["view"][0] + 1 or b[3] > got["view"][1] + 1):
            problems.append(f"{tag}: the bookmarks {k} {b} is outside the viewport {got['view']}")
    if got["hdoc"] or got["hlist"]:
        problems.append(f"{tag}: horizontal scroll with the bookmarks list open (page {got['hdoc']}, list {got['hlist']})")
