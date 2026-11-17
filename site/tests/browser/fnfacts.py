"""Browser test suite fnfacts (test_browser.py --suites)."""

import json
import os
import re
import subprocess
import time

from tests.browser.common import debug_elf, kernel_entries, report, show_tab, SITE, symbolize_frames, wait_for
from tests.browser.types import same_path


# Function facts (fnfacts.js): per kernel the function a Run to stops at.
FNFACTS_POINTS = {"minified-linux": "start_kernel", "debian-6.1-686": "start_kernel"}

# The Debugger's function rows: per frame of the inline chain its facts
# (the .fnfacts element's data), its entry line and frame.
FNROWS_JS = """() => { const e = document.getElementById('cur_fn'); if (!e) return null;
    return { state: e.dataset.state, rows: [...e.querySelectorAll('.fnframe-row')].map((r) => {
        const f = r.querySelector('.fnfacts'); if (!f) return { func: r.dataset.func, facts: null, text: r.innerText };
        const fr = f.querySelector('.fnframe'), en = f.querySelector('[data-entry]');
        return { func: r.dataset.func, facts: { ...f.dataset }, frame: fr && fr.dataset.frame ? JSON.parse(fr.dataset.frame) : null,
            frameText: fr ? fr.textContent : '', entry: en ? en.dataset.entry : '' }; }) }; }"""


def gdb_info_line(elf, name):
    """gdb's `info line NAME`: (line, file) of the code at the function's entry."""
    out = subprocess.run(["gdb", "-nx", "-batch", "-ex", "info line " + name, str(elf)], capture_output=True, text=True).stdout
    m = re.search(r'Line (\d+) of "([^"]+)" starts at address', out)
    return (int(m.group(1)), m.group(2)) if m else (None, out.strip()[:200])


def fnfacts_vs_refs(elf, name, va, facts, frame, entry, problems, r, tag):
    """The facts of the out-of-line function name at virtual va against gdb (ptype, the symbol's file:line,
    external = gdb's global symbol, `info line` for the entry line) and llvm-dwarfdump --debug-frame."""
    from tests.dwarf.funcs import gdb_funcs, frame_of_rows
    from tests.dwarf.cfi import dwarfdump_cfi
    g = gdb_funcs(str(elf), [[name, va, True]])[0]
    r[f"{tag} gdb"] = g
    if g[0] == "error" or re.sub(r"\s+", "", g[0]) != re.sub(r"\s+", "", facts["type"]) or not same_path(g[1], facts["file"]) or \
            g[2] != int(facts["line"]) or g[3] != (facts["external"] == "1"):
        problems.append(f"{tag}: page {facts['type']!r} at {facts['file']}:{facts['line']} external={facts['external']}, gdb {g}")
    il = gdb_info_line(elf, name)
    r[f"{tag} gdb info line"] = il
    if not entry or il[0] is None or not same_path(il[1], entry.rsplit(":", 1)[0]) or int(entry.rsplit(":", 1)[1]) != il[0]:
        problems.append(f"{tag}: entry line {entry!r}, gdb info line {il}")
    fde = next((f for f in dwarfdump_cfi(str(elf)) if f[0] == va), None)
    want = frame_of_rows(fde[2]) if fde else None
    names = ["EAX", "ECX", "EDX", "EBX", "ESP", "EBP", "ESI", "EDI"]
    have = (frame["max"], frame["fp"], frame["fpOff"], {names[int(k)]: v for k, v in frame["saved"].items()}) if frame else None
    r[f"{tag} frame"] = have
    if have != want:
        problems.append(f"{tag}: frame {have}, llvm-dwarfdump --debug-frame {want}")


def fnfacts_case(browser, base, shots, name):
    """Function facts from DW_TAG_subprogram: at a Run to stop in the
    function the Debugger's function row gives its prototype (gdb's
    ptype), where it is declared (gdb's symbol), external or static (gdb's
    global symbol), the line its code starts at (gdb's info line), its
    out-of-line code with the frame the call frame information gives
    (llvm-dwarfdump --debug-frame) and its inlined copies. At an inlined
    copy in it every function of the inline chain has a row (names as
    llvm-symbolizer --inlines), the inlined one with its prototype and
    declaration and copy count as llvm-dwarfdump has them and the copy
    there listed. The same facts in the call stack's tooltip, on the Page
    listing's label line, in the Source tab's function detail and (the
    minified Linux) in the Calls tab's detail of a selected frame.
    Screenshots light/dark at 1280 and 390 px, no horizontal scroll."""
    from tests.dwarf.funcs import dwarfdump_named, ctype
    problems, r = [], {}
    fn = FNFACTS_POINTS[name]
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    off = sk["vaddr"] - sk["paddr"]
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                    page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 240):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    rows_ready = lambda: (page.evaluate(FNROWS_JS) or {}).get("state") == "ok" and all(
        x["facts"] and x["entry"] and "reading" not in x["frameText"] for x in page.evaluate(FNROWS_JS)["rows"])
    if not wait_for(rows_ready, 90):
        rows = page.evaluate(FNROWS_JS)
        page.close()
        return [f"{name}: no function facts at {fn}: {rows}"], r
    got = page.evaluate(FNROWS_JS)["rows"]
    r["at entry"] = [(x["func"], x["facts"]["proto"], x["facts"]["file"] + ":" + x["facts"]["line"], x["entry"], x["frameText"][:80]) for x in got]
    if len(got) != 1 or got[0]["func"] != fn:
        problems.append(f"{name}: function rows at {fn}'s entry: {got}")
    top = got[0]
    if elf:
        fnfacts_vs_refs(elf, fn, sk["vaddr"], top["facts"], top["frame"], top["entry"], problems, r, f"{name} {fn}")
    # The call stack's tooltip and the Page listing's label line say the same.
    tip = page.get_attribute("#call_stack button", "title") or ""
    if top["facts"]["proto"] not in tip or "defined at " + top["facts"]["file"] + ":" + top["facts"]["line"] not in tip:
        problems.append(f"{name}: the call stack's tooltip lacks the facts: {tip!r}")
    # "defined at" opens the Source panel at the declaration.
    page.click("#cur_fn .fnloc")
    decl = page.locator(f'#src_body [data-line="{top["facts"]["line"]}"].pick')
    if not wait_for(lambda: page.is_visible("#src_panel") and decl.count() > 0 and
                    page.get_attribute("#src_body", "data-file") == top["facts"]["file"], 60):
        problems.append(f"{name}: 'defined at' did not open {top['facts']['file']}:{top['facts']['line']} in the Source panel")
    else:
        r["defined at opens"] = decl.first.inner_text().strip()[:80]
    page.click("#src_close")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#sym_info").screenshot(path=str(shots / f"fnfacts-{name}-debugger-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    # An inlined copy in it: the first one after EIP that its facts list.
    target = page.evaluate("""async (pa) => {
        for (let t = 0; t < 200; t++) {
            const fn = MKB.dbgFunction(pa);
            if (fn.state === 'ok') {
                for (const [a, , frames] of fn.rows) {
                    if (a <= pa || frames.length < 2) continue;
                    const x = MKB.dbgFnFacts(frames[0].func);
                    if (x.state === 'loading') break;
                    const hit = x.state === 'ok' && x.fns.some((f) => f.e[8].includes(a));
                    if (hit) return [a, frames.map((f) => f.func)];
                }
                if (fn.rows.every(([a, , frames]) => a <= pa || frames.length < 2 || MKB.dbgFnFacts(frames[0].func).state !== 'loading')) return null;
            }
            await new Promise((res) => setTimeout(res, 100));
        }
        return null; }""", sk["paddr"])
    r["inlined copy"] = target
    if not target:
        problems.append(f"{name}: no inlined copy starts in {fn} after its entry")
    else:
        pa = target[0]
        page.fill("#dbg_addr", hex(pa + off))
        page.click("#dbg_runto")
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                        page.get_attribute("#regs", "data-phys") == str(pa), 240):
            problems.append(f"{name}: Run to the inlined copy at {pa:#x} did not stop there: {page.inner_text('#dbg_status')}")
        elif not wait_for(lambda: (page.evaluate(FNROWS_JS) or {}).get("state") == "ok" and
                          all(x["facts"] for x in page.evaluate(FNROWS_JS)["rows"]), 60):
            problems.append(f"{name}: no facts for the inline chain at {pa:#x}: {page.evaluate(FNROWS_JS)}")
        else:
            rows = page.evaluate(FNROWS_JS)["rows"]
            r["inline chain"] = [(x["func"], x["facts"]["proto"], x["facts"]["file"] + ":" + x["facts"]["line"], x["facts"]["copies"]) for x in rows]
            if [x["func"] for x in rows] != target[1] or rows[-1]["func"] != fn:
                problems.append(f"{name}: function rows {[x['func'] for x in rows]} at {pa:#x}, line table {target[1]}")
            if elf:
                want = [f[0] for f in symbolize_frames(elf, [pa + off])[0]]
                if [x["func"] for x in rows] != want:
                    problems.append(f"{name}: function rows {[x['func'] for x in rows]}, llvm-symbolizer --inlines {want}")
                inner = rows[0]
                f = inner["facts"]
                if not page.evaluate("([n, pa]) => MKB.dbgFnFacts(n).fns.some((x) => x.e[0] === n && x.e[8].includes(pa))", [inner["func"], pa]):
                    problems.append(f"{name}: {inner['func']}'s copies do not list {pa:#x}")
                dd = dwarfdump_named(str(elf), [inner["func"]], [])
                decl = [d for o, d in dd.items() if d["tag"] == "DW_TAG_subprogram" and "origin" not in d and d.get("name") == inner["func"]
                        and d.get("line") == int(f["line"]) and same_path(d.get("file"), f["file"])]
                if not decl:
                    problems.append(f"{name}: no DIE declares {inner['func']} at {f['file']}:{f['line']} in llvm-dwarfdump")
                else:
                    d = max(decl, key=lambda x: len(x["params"]))
                    want_t = ctype(d["type"]) + "(" + ", ".join([ctype(x["type"]) for x in d["params"]] + (["..."] if d["varargs"] else [])) + ")"
                    have_t = ctype(f["type"]).replace("(void)", "()").replace(" (", "(")
                    r["inlined vs llvm-dwarfdump"] = [have_t, want_t]
                    if have_t != want_t.replace(" (", "("):
                        problems.append(f"{name}: {inner['func']} {f['type']!r}, llvm-dwarfdump {want_t!r}")
                    origins = {o for o, x in dd.items() if x is d or (x["tag"] == "DW_TAG_subprogram" and "origin" not in x and
                               x.get("name") == inner["func"] and x.get("line") == d.get("line") and x.get("file") == d.get("file"))}

                    def root(o):
                        for _ in range(8):
                            x = dd.get(o)
                            if x is None or "origin" not in x:
                                return o
                            o = x["origin"]
                        return o
                    n = sum(1 for o, x in dd.items() if x["tag"] == "DW_TAG_inlined_subroutine" and x["code"] and root(o) in origins)
                    r["inlined copies"] = [f["copies"], n]
                    if int(f["copies"]) != n:
                        problems.append(f"{name}: {inner['func']} has {f['copies']} inlined copies on the page, {n} in llvm-dwarfdump")
            # Its copy list opens and lists this one.
            page.locator("#cur_fn .fnframe-row").first.locator("details.fncopies summary").click()
            if not wait_for(lambda: page.locator(f'#cur_fn .fnframe-row >> nth=0 >> details.fncopies li button[data-addr="{pa}"]').count() > 0, 5) \
                    and int(rows[0]["facts"]["copies"]) <= 40:
                problems.append(f"{name}: the opened copy list does not list {pa:#x}")
            for scheme in ("light", "dark"):
                page.emulate_media(color_scheme=scheme)
                time.sleep(0.2)
                page.locator("#sym_info").screenshot(path=str(shots / f"fnfacts-{name}-inlined-1280-{scheme}.png"))
                page.set_viewport_size({"width": 390, "height": 900})
                time.sleep(0.3)
                if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                    problems.append(f"{name}: the function rows scroll the page horizontally at 390 px ({scheme})")
                page.screenshot(path=str(shots / f"fnfacts-{name}-390-{scheme}.png"), full_page=True)
                page.set_viewport_size({"width": 1280, "height": 1000})
            page.emulate_media(color_scheme="light")
    # The Page listing: the function's label line has its prototype and the facts in its title.
    page.evaluate("(a) => MKB.jumpTo(a)", sk["paddr"])
    lbl = page.locator(f'#listing .lbl[data-sym="{fn}"]')
    if not wait_for(lambda: lbl.count() > 0 and lbl.first.get_attribute("data-proto"), 60):
        problems.append(f"{name}: the Page listing's {fn} label line has no prototype")
    else:
        r["label line"] = lbl.first.inner_text()
        if lbl.first.get_attribute("data-proto") != top["facts"]["proto"] or "defined at" not in (lbl.first.get_attribute("title") or ""):
            problems.append(f"{name}: label line {lbl.first.get_attribute('data-proto')!r} / {lbl.first.get_attribute('title')!r}")
    # The Source tab: the function row's arrow shows the same facts.
    show_tab(page, "source")
    page.fill("#cov_filter", fn)
    path = top["facts"]["file"] + ":" + fn
    tog = page.locator(f'#cov_tree .covrow[data-kind="func"][data-path="{path}"] .ttog')
    if not wait_for(lambda: tog.count() > 0, 60):
        problems.append(f"{name}: no Coverage row {path}")
    else:
        tog.first.click()
        det = page.locator(f'#cov_tree .covfacts[data-path="{path}"] .fnfacts')
        if not wait_for(lambda: det.count() > 0, 30):
            problems.append(f"{name}: the Coverage row {path} shows no facts")
        else:
            cf = det.first.evaluate("(e) => ({ ...e.dataset })")
            if cf != top["facts"]:
                problems.append(f"{name}: Coverage facts {cf} differ from the Debugger's {top['facts']}")
            page.emulate_media(color_scheme="dark")
            time.sleep(0.2)
            page.locator("#panel_source").screenshot(path=str(shots / f"fnfacts-{name}-coverage-1280-dark.png"))
            page.emulate_media(color_scheme="light")
    if name == "minified-linux":
        # The Calls tab: step to record calls, select the widest frame of a DWARF
        # function (tapping a box is graph_check's; the detail is the same render).
        show_tab(page, "debugger")
        page.fill("#dbg_n", "3000")
        page.click("#dbg_stepn")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        show_tab(page, "calls")
        time.sleep(0.5)
        xy = page.evaluate("""() => { const st = MKB.G.stepper;
            let best = -1, w = -1;
            for (let k = 0; k < st.fr.n; k++) if (!MKB.hiddenFrame(st, k) && (() => { const x = MKB.dbgFnFacts(st.nodeName(st.fr.node(k))); return x.state === 'ok' && x.fns.length; })()) {
                const e = (st.fr.end(k) < 0 ? st.steps : st.fr.end(k)) - Math.max(st.fr.start(k), MKB.runStart()); if (e > w) { w = e; best = k; } }
            if (best < 0) return null;
            MKB.G.selected = best; MKB.renderGraphInfo();
            return [0, 0, st.nodeName(st.fr.node(best))]; }""")
        if not xy:
            problems.append(f"{name}: no Graph frame of a DWARF function")
        else:
            gf = page.locator("#graph_fn .fnfacts")
            if not wait_for(lambda: gf.count() > 0, 30):
                problems.append(f"{name}: the Graph detail of {xy[2]} shows no facts: {page.inner_text('#graph_info')[:300]!r}")
            else:
                r["graph"] = [xy[2], gf.first.get_attribute("data-proto")]
                if gf.first.get_attribute("data-name") != xy[2].split(".")[0]:
                    problems.append(f"{name}: Graph detail of {xy[2]} names {gf.first.get_attribute('data-name')}")
                page.locator("#graph_info").screenshot(path=str(shots / f"fnfacts-{name}-graph-1280-light.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def fnfacts_import_case(browser, base, shots):
    """An imported -g vmlinux (the minified Linux's): the Worker collects
    the same function facts as the shipped shards (every function's
    prototype, declaration, flags, out-of-line code and inlined copies)
    and the Debugger's function row at start_kernel is the same."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    render = """async () => {
        for (let t = 0; t < 600 && MKB.dbgTypes().state === 'loading'; t++) await new Promise((res) => setTimeout(res, 100));
        const ty = MKB.dbgTypes(); if (ty.state !== 'ok') return ty.why;
        let F;
        if (MKB.DBG.mode === 'shards') {
            if (!MKB.DBG.index.funcs) return 'index.json has no function facts';
            F = { files: [], fns: [] };
            for (let k = 0; k < MKB.DBG.index.funcs.shards; k++) {
                const b = await MKB.dbgGunzip(await (await fetch(MKB.DBG.base + 'funcs-' + k.toString(16) + '.json.gz')).arrayBuffer());
                const s = JSON.parse(new TextDecoder().decode(b));
                F.fns.push(...s.fns.map((e) => [e[0], e[1] >= 0 ? s.files[e[1]] : null, ...e.slice(2)]));
            }
        } else if (!ty.T.funcs) return 'the import has no function facts';
        else F = { fns: ty.T.funcs.fns.map((e) => [e[0], e[1] >= 0 ? ty.T.funcs.files[e[1]] : null, ...e.slice(2)]) };
        return F.fns.map((e) => JSON.stringify([e[0], e[1], e[2], e[3], e[4], MKB.dwFuncProto(ty.T, e), e[7], e[8]])).sort(); }"""
    shipped = page.evaluate(render)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) and
             ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("entry"), 120)
    want_row = page.evaluate(FNROWS_JS)
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    imported = page.evaluate(render)
    r["functions"] = [len(shipped) if isinstance(shipped, list) else shipped, len(imported) if isinstance(imported, list) else imported]
    if not isinstance(imported, list) or imported != shipped:
        diff = sorted(set(imported) ^ set(shipped))[:4] if isinstance(imported, list) and isinstance(shipped, list) else imported
        problems.append(f"import: the function facts differ from the shipped shards: {diff}")
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) and
             ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("entry") and
             "reading" not in ((page.evaluate(FNROWS_JS) or {}).get("rows") or [{}])[0].get("frameText", "reading"), 120)
    got_row = page.evaluate(FNROWS_JS)
    r["row"] = (got_row or {}).get("rows")
    if got_row != want_row:
        problems.append(f"import: the function row at start_kernel {got_row}, shipped {want_row}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_fnfacts(ctx):
    failed = False
    for name in [n for n in FNFACTS_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = fnfacts_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("fnfacts", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = fnfacts_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("fnfacts_import", p, r, seconds=round(time.monotonic() - t0))
