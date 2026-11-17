"""Browser test suite srcfiles (test_browser.py --suites)."""

import json
import time

from tests.browser.common import (
    debug_elf, kernel_entries, nav_at, report, run_to_wait, show_tab, SITE, symbolize_frames, wait_for,
)
from tests.browser.dwarf_import import SRCFILES_LIST_JS, srcfiles_state, srcfiles_wait_file


# ---- srcfiles: another file from the Source panel (F1) --------------------
# The operator: "pop up z pojedynczym plikiem powinien pozwalać łatwo
# obejrzeć inny plik". The Source panel opened from Code at EIP has a file
# switcher (#src_files: type to filter on path and basename, arrows and
# Enter, Esc), the call stack's and recently opened files on top, each file
# with its lines that ran; the path in the title is a breadcrumb (a
# directory lists its files); a whole file has a function outline
# (#src_outline) and the headers inlined into it (#src_incl). Every switch
# is a history entry and the hash keeps the file across a reload.
SRCFILES_POINTS = {"hello": ("hello_main", 40, "hello.c", "boot/hello.c", "hello_main"),
                   "minified-linux": ("start_kernel", 3000, "main.c", "init/main.c", None)}


def srcfiles_seen_lines(page, name, path):
    """The lines of path that ran, independently of the page's coverage
    code: every instruction start the stepper recorded in the kernel's
    DWARF shards, symbolized with llvm-symbolizer --inlines on the -g ELF
    (or gen-dwarf.js --lookup without one), every frame in path."""
    pas = page.evaluate("""() => { const st = MKB.stepper, out = [], bits = MKB.DBG.index.shardBits;
        for (let a = st.kernelLo; a < st.seen.length * 8; a++)
            if ((st.seen[a >> 3] >> (a & 7) & 1) && MKB.DBG.shardSet.has(Math.floor(a / 2 ** bits))) out.push(a);
        return out; }""")
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    off = next(s["vaddr"] - s["paddr"] for s in syms if s["name"] == SRCFILES_POINTS[name][0])
    elf = debug_elf(name)
    if elf:
        frames = symbolize_frames(elf, [a + off for a in pas])
        how = "llvm-symbolizer"
    else:
        from tests.dwarf.common import page_frames
        frames = page_frames(str(SITE / entry["dwarf"]), pas)
        how = "gen-dwarf.js --lookup"
    return {f[2] for fr in frames for f in fr if f[2] and (f[1] == path or f[1].endswith("/" + path))}, len(pas), how


def srcfiles_fn_line(name, path, fn):
    """The line llvm-symbolizer gives for fn's first instruction in path."""
    entry = kernel_entries()[name]
    sym = next((s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"] if s["name"] == fn), None)
    elf = debug_elf(name)
    if not sym or not elf:
        return None
    fr = symbolize_frames(elf, [sym["vaddr"]])[0]
    outer = fr[-1] if fr else None
    return outer[2] if outer and (outer[1] == path or outer[1].endswith("/" + path)) else None


def srcfiles_open_switcher(page, how, problems, tag):
    if not page.locator("#src_files").count():
        problems.append(f"{tag}: the Source panel has no file switcher button (#src_files)")
        return False
    if how == "tap":
        page.tap("#src_files")
    elif how == "key":
        page.focus("#src_files")
        page.keyboard.press("Enter")
    else:
        page.click("#src_files")
    if not wait_for(lambda: page.is_visible("#src_switch") and page.is_visible("#src_q"), 5):
        problems.append(f"{tag}: the file switcher did not open ({how})")
        return False
    if page.get_attribute("#src_files", "aria-expanded") != "true":
        problems.append(f"{tag}: #src_files aria-expanded is not true with the switcher open")
    if how != "tap" and not page.evaluate("document.activeElement && document.activeElement.id === 'src_q'"):
        problems.append(f"{tag}: the filter field does not have the focus after opening ({how})")
    return True


def srcfiles_fit(page, tag, problems, r):
    """The switcher fits in the viewport and the panel, nothing scrolls sideways."""
    got = page.evaluate("""() => { const v = [innerWidth, innerHeight];
        const box = (id) => { const e = document.getElementById(id); if (!e || e.hidden || !e.getClientRects().length) return null;
            const b = e.getBoundingClientRect(); return [b.left, b.top, b.right, b.bottom]; };
        const sp = document.getElementById('src_panel');
        return { view: v, sw: box('src_switch'), q: box('src_q'), list: box('src_list'), panel: box('src_panel'),
                 hdoc: document.documentElement.scrollWidth > innerWidth + 1,
                 hpanel: sp.scrollWidth > sp.clientWidth + 1,
                 hlist: (() => { const l = document.getElementById('src_list'); return !!l && l.scrollWidth > l.clientWidth + 1; })() }; }""")
    r[f"fit {tag}"] = got
    for k in ("sw", "q", "list"):
        b = got[k]
        if not b:
            problems.append(f"{tag}: #{'src_' + ('switch' if k == 'sw' else k)} not shown")
            continue
        if b[0] < -1 or b[2] > got["view"][0] + 1 or b[1] < -1 or b[3] > got["view"][1] + 1:
            problems.append(f"{tag}: {k} {b} outside the viewport {got['view']}")
        p = got["panel"]
        if p and (b[0] < p[0] - 1 or b[2] > p[2] + 1):
            problems.append(f"{tag}: {k} {b} wider than the Source panel {p}")
    if got["hdoc"] or got["hpanel"] or got["hlist"]:
        problems.append(f"{tag}: horizontal scroll (page {got['hdoc']}, panel {got['hpanel']}, list {got['hlist']})")


def srcfiles_shots(page, shots, stem, scheme):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))
    page.emulate_media(color_scheme=scheme)


def srcfiles_run(browser, base, shots, name, scheme, phone):
    fn, steps, query, target, outline_fn = SRCFILES_POINTS[name]
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    shipped = json.loads((SITE / entry["src"] / "index.json").read_text())["files"]
    known = sorted(set(f[0] for f in F["files"]) | set(shipped))
    problems, r = [], {}
    errors = []
    width = 390 if phone else 1280
    tag = f"{name} {width} {scheme}"
    how = "tap" if phone else "click"
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True) if phone else \
        browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
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

    # 1. The panel from Code at EIP: the function around EIP.
    show_tab(page, "debugger")
    if not wait_for(lambda: page.locator("#context .src:not(.nol)").count() > 0, 30):
        ctx.close()
        return [f"{tag}: Code at EIP shows no C line"], r
    line = page.locator("#context .src:not(.nol)").first
    line.scroll_into_view_if_needed()
    # A tap anywhere on the line; at 1280 a click on its C text (not its file:line link).
    page.tap("#context .src:not(.nol) >> nth=0") if phone else line.locator(".srctext").click()
    if not wait_for(lambda: (lambda s: s["open"] and s["state"] == "ok")(srcfiles_state(page)), 25):
        ctx.close()
        return [f"{tag}: the Source panel did not open from Code at EIP: {srcfiles_state(page)}"], r
    time.sleep(0.5)
    start = srcfiles_state(page)
    r["start"] = {k: start[k] for k in ("title", "file", "code", "executed", "hash")}
    if "(whole file)" in start["title"]:
        problems.append(f"{tag}: Code at EIP opened a whole file, not the function: {start['title']!r}")
    if not phone:
        # The title's file name opens the whole file; Back returns to the function.
        page.click(f'#src_title button.crumb[data-path="{start["file"]}"]')
        whole = srcfiles_wait_file(page, start["file"])
        page.go_back()
        back = wait_for(lambda: srcfiles_state(page)["title"] == start["title"], 10)
        r["title file"] = [whole, back]
        if not whole or not back:
            problems.append(f"{tag}: the title's {start['file']} did not open the whole file ({whole}) or Back did not return ({back})")
    entries0 = nav_at(page)

    # 2. The switcher: call stack and recent files on top, then every file.
    if not srcfiles_open_switcher(page, how, problems, tag):
        ctx.close()
        return problems, r
    wait_for(lambda: page.evaluate("document.querySelectorAll('#src_list [role=option]').length") > 0, 10)
    items = page.evaluate(SRCFILES_LIST_JS)
    r["first items"] = [(x["group"], x["path"] or x["dir"]) for x in items[:6]]
    stack = [x["path"] for x in items if x["group"] == "stack"]
    recent = [x["path"] for x in items if x["group"] == "recent"]
    listed = sorted({x["path"] for x in items if x["group"] == "all"})
    if start["file"] not in stack:
        problems.append(f"{tag}: the call stack's files {stack} lack EIP's {start['file']}")
    if start["file"] not in recent:
        problems.append(f"{tag}: the recently opened files {recent} lack {start['file']}")
    groups = [x["group"] for x in items]
    if groups and groups.index("all") < max(i for i, g in enumerate(groups) if g in ("stack", "recent")):
        problems.append(f"{tag}: the call stack and recent files are not on top: {groups[:12]}")
    if listed != known:
        problems.append(f"{tag}: the switcher lists {len(listed)} files, the DWARF and the shipped source know {len(known)}: "
                        f"missing {sorted(set(known) - set(listed))[:5]}, extra {sorted(set(listed) - set(known))[:5]}")
    srcfiles_fit(page, f"{tag} switcher", problems, r)
    srcfiles_shots(page, shots, f"srcfiles-{name}-{width}-switcher", scheme)

    # 3. Filter by a basename, pick the file.
    page.fill("#src_q", query)
    time.sleep(0.5)
    items = page.evaluate(SRCFILES_LIST_JS)
    r[f"filter {query}"] = [(x["path"], x["executed"], x["code"]) for x in items[:6]]
    paths = [x["path"] for x in items]
    if target not in paths:
        problems.append(f"{tag}: filtering {query!r} does not list {target}: {paths[:8]}")
        ctx.close()
        return problems, r
    if paths[0] != target and name == "hello":
        problems.append(f"{tag}: {target} is not the best match for {query!r}: {paths[:4]}")
    if any(query.lower() not in p.lower() and not srcfiles_fuzzy(query, p) for p in paths):
        problems.append(f"{tag}: filter {query!r} lists non-matching files {[p for p in paths if not srcfiles_fuzzy(query, p)][:4]}")
    sel = f'#src_list [role=option][data-path="{target}"]'
    if not wait_for(lambda: page.get_attribute(sel, "data-executed") not in (None, ""), 20):
        problems.append(f"{tag}: the switcher does not say how many lines of {target} ran")
    pick = next(x for x in page.evaluate(SRCFILES_LIST_JS) if x["path"] == target)
    page.tap(sel) if phone else page.click(sel)
    if not srcfiles_wait_file(page, target):
        problems.append(f"{tag}: picking {target} did not open the whole file: {srcfiles_state(page)}")
        ctx.close()
        return problems, r
    got = srcfiles_state(page)
    r["picked"] = {k: got[k] for k in ("title", "code", "executed", "hash", "switcher")}
    if got["switcher"]:
        problems.append(f"{tag}: the switcher stays open after picking a file")
    if got["title"] == start["title"] or got["code"] == 0:
        problems.append(f"{tag}: the panel did not change: {start['title']!r} -> {got['title']!r}, {got['code']} lines with code")
    if "src=" + target not in urllib_unquote(got["hash"]):
        problems.append(f"{tag}: the hash {got['hash']!r} does not keep {target}")
    at = nav_at(page)
    if at[0] != entries0[0] + 1:
        problems.append(f"{tag}: one history entry expected for the switch: before {entries0}, after {at}")
    # Coverage: the panel's ● lines are the lines that ran (independent set);
    # the switcher's count says the same.
    want, n_seen, how_sym = srcfiles_seen_lines(page, name, target)
    r["coverage"] = [len(got["exec"]), len(want), n_seen, how_sym]
    if set(got["exec"]) != want:
        problems.append(f"{tag}: {target}'s ran lines differ from {how_sym}: only page {sorted(set(got['exec']) - want)[:8]}, "
                        f"only {how_sym} {sorted(want - set(got['exec']))[:8]}")
    if got["executed"] != len(want):
        problems.append(f"{tag}: data-executed {got['executed']} != {len(want)}")
    if str(pick["executed"]) != str(len(want)) or str(pick["code"]) != str(got["code"]):
        problems.append(f"{tag}: the switcher said {pick['executed']} of {pick['code']} lines ran, the panel {len(want)} of {got['code']}")
    srcfiles_shots(page, shots, f"srcfiles-{name}-{width}-file", scheme)
    if not phone:
        # Selecting a line's text (a drag along it) does not pick it.
        pick0 = srcfiles_state(page)["pick"]
        other = page.evaluate("""(p) => { const v = document.getElementById('src_body').getBoundingClientRect();
            return [...document.querySelectorAll('#src_body [data-line]')].find((e) => { const b = e.getBoundingClientRect();
                return e.dataset.line !== p && b.top >= v.top && b.bottom <= v.bottom && e.querySelector('.srctext').textContent.trim().length > 4; }).dataset.line; }""", pick0)
        b = page.locator(f'#src_body [data-line="{other}"] .srctext').bounding_box()
        page.mouse.move(b["x"] + 2, b["y"] + b["height"] / 2)
        page.mouse.down()
        page.mouse.move(b["x"] + min(b["width"] - 2, 120), b["y"] + b["height"] / 2, steps=5)
        page.mouse.up()
        time.sleep(0.3)
        sel = [page.evaluate("window.getSelection().toString().trim().length > 0"), srcfiles_state(page)["pick"]]
        page.evaluate("window.getSelection().removeAllRanges()")
        r["drag select"] = [pick0, other, sel]
        if not sel[0] or sel[1] != pick0:
            problems.append(f"{tag}: a drag selecting line {other}'s text: [selection, pick] {sel}, want the pick {pick0!r} kept")

    # 4. The outline: a function of the file, scrolled to and picked.
    opts = page.evaluate("""() => [...document.querySelectorAll('#src_outline option')].filter(o => o.value).map(o => ({ v: o.value, t: o.textContent, fn: o.dataset.fn || '' }))""")
    r["outline"] = len(opts)
    file_fns = {f[0] for f in F["funcs"] if f[1] >= 0 and F["files"][f[1]][0] == target}
    named = {o["fn"] for o in opts}
    if not opts or not file_fns <= named:
        problems.append(f"{tag}: the outline lacks functions of {target}: {sorted(file_fns - named)[:6]} (has {len(opts)})")
    else:
        want_fn = outline_fn or next((o["fn"] for o in opts[len(opts) // 2:] if o["fn"] in file_fns and o["v"].isdigit()), opts[-1]["fn"])
        o = next(x for x in opts if x["fn"] == want_fn)
        before = nav_at(page)
        page.select_option("#src_outline", o["v"])
        time.sleep(0.8)
        s = srcfiles_state(page)
        vis = page.evaluate("""(l) => { const sb = document.getElementById('src_body'), e = sb.querySelector('[data-line="' + l + '"]');
            if (!e) return false; const a = e.getBoundingClientRect(), b = sb.getBoundingClientRect(); return a.top >= b.top - 1 && a.bottom <= b.bottom + 1; }""", o["v"])
        wl = srcfiles_fn_line(name, target, want_fn)
        r["outline jump"] = [want_fn, o["v"], s["pick"], vis, wl]
        if s["pick"] != o["v"] or not vis:
            problems.append(f"{tag}: the outline's {want_fn} (line {o['v']}) is not picked and in view: pick {s['pick']}, visible {vis}")
        if wl is not None and int(o["v"]) != wl:
            problems.append(f"{tag}: the outline puts {want_fn} at line {o['v']}, llvm-symbolizer at {wl}")
        if nav_at(page)[0] != before[0] + 1:
            problems.append(f"{tag}: the outline jump is not a history entry: {before} -> {nav_at(page)}")
        # Its first option (the count) goes nowhere.
        at = [nav_at(page), srcfiles_state(page)["pick"]]
        page.select_option("#src_outline", "")
        time.sleep(0.4)
        if [nav_at(page), srcfiles_state(page)["pick"]] != at:
            problems.append(f"{tag}: the outline's first option moved [history, pick] {at} to {[nav_at(page), srcfiles_state(page)['pick']]}")

    # 5. The breadcrumb: the directory lists its files.
    d = target.rsplit("/", 1)[0]
    crumb = page.locator(f'#src_title button.crumb[data-dir="{d}"]')
    if not crumb.count():
        problems.append(f"{tag}: no breadcrumb button for {d}/ in the title")
    else:
        crumb.tap() if phone else crumb.click()
        wait_for(lambda: page.is_visible("#src_switch"), 5)
        time.sleep(0.4)
        items = page.evaluate(SRCFILES_LIST_JS)
        files = sorted(x["path"] for x in items if x["path"])
        here = sorted(p for p in known if p.rsplit("/", 1)[0] == d)
        subdirs = sorted({p[len(d) + 1:].split("/")[0] for p in known if p.startswith(d + "/") and "/" in p[len(d) + 1:]})
        r["crumb"] = [d, len(files), [x["dir"] for x in items if x["dir"]][:4]]
        if page.get_attribute("#src_switch", "data-dir") != d or files != here or \
                sorted(x["dir"].rsplit("/", 1)[-1] for x in items if x["dir"] and x["dir"].startswith(d + "/")) != subdirs:
            problems.append(f"{tag}: the breadcrumb {d}/ lists {files[:6]} (+dirs {[x['dir'] for x in items if x['dir']][:4]}), want {here[:6]} (+{subdirs[:4]})")
        srcfiles_fit(page, f"{tag} directory", problems, r)
        if not phone:
            # Up: the top directory's subdirectories and files; a directory row lists that one.
            page.click('#src_list [role=option][data-group="up"]')
            time.sleep(0.4)
            items = page.evaluate(SRCFILES_LIST_JS)
            top_dirs = sorted({p.split("/")[0] for p in known if "/" in p})
            top_files = sorted(p for p in known if "/" not in p)
            got = [sorted(x["dir"] for x in items if x["group"] == "dir"), sorted(x["path"] for x in items if x["path"])]
            r["crumb up"] = [len(got[0]), len(got[1])]
            if page.get_attribute("#src_switch", "data-dir") != "" or got != [top_dirs, top_files]:
                problems.append(f"{tag}: Up from {d}/ lists {got[0][:6]} {got[1][:4]}, want {top_dirs[:6]} {top_files[:4]}")
            elif top_dirs:
                page.click(f'#src_list [role=option][data-group="dir"][data-dir="{top_dirs[0]}"]')
                time.sleep(0.4)
                if page.get_attribute("#src_switch", "data-dir") != top_dirs[0]:
                    problems.append(f"{tag}: the directory row {top_dirs[0]} did not list it: {page.get_attribute('#src_switch', 'data-dir')!r}")
        page.keyboard.press("Escape")
        if not wait_for(lambda: not page.is_visible("#src_switch"), 3):
            problems.append(f"{tag}: Escape does not close the switcher")
        if srcfiles_state(page)["file"] != target:
            problems.append(f"{tag}: Escape in the switcher changed the file")

    # 6. Headers inlined into the file: one pick away.
    incl = page.evaluate("""() => [...document.querySelectorAll('#src_incl option')].filter(o => o.value).map(o => o.value)""")
    r["headers"] = incl[:6]
    if name != "hello":
        if not incl:
            problems.append(f"{tag}: no inlined headers offered for {target}")
        else:
            h = incl[0]
            page.select_option("#src_incl", h)
            if not srcfiles_wait_file(page, h):
                problems.append(f"{tag}: the header {h} did not open: {srcfiles_state(page)}")
            else:
                page.go_back()
                if not srcfiles_wait_file(page, target):
                    problems.append(f"{tag}: Back from {h} did not return to {target}: {srcfiles_state(page)}")

    # 7. Back returns to the previous file and its scroll.
    while nav_at(page)[0] > entries0[0]:  # the outline jump, the switch
        page.go_back()
        time.sleep(0.6)
    if not wait_for(lambda: srcfiles_state(page)["title"] == start["title"], 15):
        problems.append(f"{tag}: Back did not return to {start['title']!r}: {srcfiles_state(page)['title']!r}")
    else:
        time.sleep(0.6)
        b = srcfiles_state(page)
        if abs(b["top"] - start["top"]) > 2:
            problems.append(f"{tag}: Back restored the scroll {b['top']}, left at {start['top']}")
    page.go_forward()
    if not srcfiles_wait_file(page, target):
        problems.append(f"{tag}: Forward did not reopen {target}: {srcfiles_state(page)}")

    # 8. Keyboard only: open, type, arrows, Enter.
    if not phone:
        page.focus("#src_body")
        if srcfiles_open_switcher(page, "key", problems, f"{tag} keyboard"):
            # Nothing matches: said so; the arrows and Enter do nothing.
            page.keyboard.type("zqxzqx")
            time.sleep(0.4)
            note = page.evaluate("""() => [document.querySelectorAll('#src_list [role=option]').length,
                (document.querySelector('#src_list .note') || {}).textContent || '', document.getElementById('src_q').getAttribute('aria-activedescendant')]""")
            page.keyboard.press("ArrowDown")
            page.keyboard.press("Enter")
            time.sleep(0.3)
            after = [page.is_visible("#src_switch"), srcfiles_state(page)["file"]]
            r["no match"] = [note, after]
            if note != [0, "No file matches.", None] or after != [True, target]:
                problems.append(f"{tag}: a filter matching nothing: [options, note, active] {note}, after Down and Enter [open, file] {after}")
            page.fill("#src_q", "")
            page.focus("#src_q")
            page.keyboard.type("main" if name != "hello" else "hello")
            time.sleep(0.4)
            items = page.evaluate(SRCFILES_LIST_JS)
            want_i = 0
            for key in ("ArrowDown", "ArrowDown", "ArrowUp"):
                page.keyboard.press(key)
                want_i = min(want_i + 1, len(items) - 1) if key == "ArrowDown" else max(want_i - 1, 0)
            time.sleep(0.2)
            items = page.evaluate(SRCFILES_LIST_JS)
            act = [x for x in items if x["active"]]
            r["keyboard"] = [x["path"] for x in items[:4]], [x["path"] for x in act]
            if not act or act[0]["path"] != items[want_i]["path"]:
                problems.append(f"{tag}: Down, Down, Up does not make item {want_i} active: {r['keyboard']}")
            elif page.get_attribute("#src_q", "aria-activedescendant") is None:
                problems.append(f"{tag}: the filter has no aria-activedescendant")
            else:
                page.keyboard.press("Enter")
                kp = act[0]["path"]
                if not srcfiles_wait_file(page, kp):
                    problems.append(f"{tag}: Enter did not open {kp}: {srcfiles_state(page)}")
                page.go_back()
                srcfiles_wait_file(page, target)

    # 9. A reload reopens the file in the hash.
    page.reload()
    if not srcfiles_wait_file(page, target, timeout=60):
        problems.append(f"{tag}: the reload did not reopen {target}: {srcfiles_state(page)}")
    if errors:
        problems.append(f"{tag}: page errors {errors}")
    ctx.close()
    return problems, r


def srcfiles_fuzzy(q, p):
    """q's characters in order in p (case-insensitive)."""
    it = iter(p.lower())
    return all(c in it for c in q.lower())


def urllib_unquote(s):
    from urllib.parse import unquote
    return unquote(s)


def srcfiles_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if linux:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = srcfiles_run(browser, base, shots, name, scheme, phone)
        out.append(report("srcfiles", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    return any(out)


def suite_srcfiles(ctx):
    return srcfiles_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)
