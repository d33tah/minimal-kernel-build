"""Part of a browser suite (test_browser.py)."""

import json
import subprocess

from tests.dwarf.cfi import dwarfdump_cfi
from tests.browser.common import frames_agree, show_tab, SITE, wait_for
from tests.browser.cfi import CFI_BASE, CFI_OFFSET
from tests.browser.dwarf_forms_checks import dwf_broken, DWF_SOURCE


# The import as the page uses it (dbginfo.js): the fixture (m.c and a
# second unit, u.c) imported through "From disk / Symbols", what the
# page answers for its code, what it says after a reload with only some
# of it read before, after IndexedDB lost it, and for broken files.
DWF_SECOND = "int other(int x)\n{\n\treturn x * 3 + 1;\n}\n"

DBG_WAIT_JS = """async ([fn, args]) => {
    const plain = (r) => JSON.parse(JSON.stringify(r === undefined ? null : r, (k, v) => v instanceof Map ? [...v] : v));
    for (let i = 0; i < 300; i++) {
        const r = MKB[fn](...args);
        if (r !== 'loading' && !(r && r.state === 'loading'))
            return plain(r);
        await new Promise((ok) => setTimeout(ok, 100));
    }
    return 'timeout';
}"""


def dwf_ui_fixture(tmp):
    """m.c and u.c by clang -gdwarf-5 -O2 -mregparm=3, linked at CFI_BASE."""
    (tmp / "m.c").write_text(DWF_SOURCE)
    (tmp / "u.c").write_text(DWF_SECOND)
    elf = tmp / "ui.elf"
    flags = ["-m32", "-O2", "-ffreestanding", "-fno-pic", "-gdwarf-5", "-mregparm=3"]
    try:
        for n in ("m", "u"):
            subprocess.run(["clang", *flags, "-c", n + ".c", "-o", n + ".o"], cwd=tmp, check=True, capture_output=True, text=True)
        subprocess.run(["clang", "-m32", "-fuse-ld=lld", "-nostdlib", "-no-pie", "-Wl,-e,entry", f"-Wl,-Ttext=0x{CFI_BASE:x}",
                        "m.o", "u.o", "-o", str(elf)], cwd=tmp, check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as e:
        return None, f"clang: {getattr(e, 'stderr', '') or e}"[:300]
    return elf, None


def dwf_ui_checks(browser, base, shots, tmp, symbolize, gdb_scopes):
    problems, r = [], {}
    elf, why = dwf_ui_fixture(tmp)
    if not elf:
        return [f"the import fixture did not build: {why}"], r
    sizes = {l.split()[3]: (int(l.split()[0], 16), int(l.split()[1], 16)) for l in
             subprocess.run(["llvm-nm", "-S", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 4}
    m_vas = [va for f in ("use", "mix", "entry") for va in range(sizes[f][0], sum(sizes[f]))]
    other = sizes["other"][0]
    pa = lambda va: va - CFI_OFFSET
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    # The Machine tab: the Debugger's panels would read the variables by themselves.
    url = f"{base}/index.html?kernel=hello&paused=1#machine"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)

    def ask(fn, *args):
        return page.evaluate(DBG_WAIT_JS, [fn, list(args)])

    def imp(path, done="compile units"):
        page.evaluate("MKB.startPaused = true; document.getElementById('sym_offset').value = '0xc0000000'")
        page.set_input_files("#symfile", str(path))
        # (The status of the import before names another file, or is the same file's.)
        wait_for(lambda: page.inner_text("#sym_status").startswith(path.name + " ") and done in page.inner_text("#sym_status") and
                 page.evaluate("MKB.DBG.file && MKB.DBG.file.name") == path.name, 30)
        return page.inner_text("#sym_status")

    def reload(state):
        page.goto("about:blank")
        page.goto(url)
        wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == state, 60)
        return page.inner_text("#sym_status")

    def expect(what, got, want):
        r[what] = got
        if got != want:
            problems.append(f"import: {what}: {got!r}, want {want!r}")
    # Round A: only file:line of m.c's unit is read.
    st = imp(elf)
    if "DWARF 5 of the imported ui.elf: 2 compile units, each decoded when first needed" not in st:
        problems.append(f"import: status {st!r}")
    want = symbolize(str(elf), m_vas, [str(tmp) + "/"])
    got = [ask("dbgFrames", pa(va)) for va in m_vas]
    bad = [(hex(va), g, w) for va, g, w in zip(m_vas, got, want) if g["state"] != "ok" or not frames_agree(g["frames"], w)]
    r["frames"] = f"{len(m_vas) - len(bad)}/{len(m_vas)} agree with llvm-symbolizer"
    if bad:
        problems.append(f"import: frames differ from llvm-symbolizer: {bad[:2]}")
    expect("frames outside the units", ask("dbgFrames", 0x10), {"state": "none", "why": "no compile unit of the imported vmlinux covers this address"})
    line = DWF_SOURCE.split("\n").index("\tgll = acc;") + 1
    lc = ask("dbgLineCode", "m.c", line)
    rows = [va for a, b, _ in lc.get("rows", []) for va in (a + CFI_OFFSET, b - 1 + CFI_OFFSET)]
    lines = [w[0][2] if w else None for w in symbolize(str(elf), rows, [])]
    r["line code"] = [lc.get("state"), lc.get("how"), lines]
    if lc.get("state") != "ok" or not rows or any(x != line for x in lines) or lc.get("how") != "1 compile unit of the imported vmlinux name m.c":
        problems.append(f"import: the code of m.c:{line}: {lc}, llvm-symbolizer says {lines}")
    expect("a line without code", ask("dbgLineCode", "m.c", 1)["why"],
           "m.c:1 has no code in the line table (a comment, a declaration, or code the compiler left out or merged into another line)")
    expect("an unknown file", ask("dbgFileCode", "nosuch.c"), {"state": "none", "why": "no source file nosuch.c with code in this kernel's line table"})
    st = reload("cached")
    if "(remembered from an earlier import: units not decoded then need the file again)" not in st:
        problems.append(f"import: after a reload the status is {st!r}")
    g = ask("dbgFrames", pa(m_vas[0]))
    if g.get("state") != "ok" or not frames_agree(g["frames"], want[0]):
        problems.append(f"import: after a reload the frames at {m_vas[0]:#x} are {g}")
    expect("after a reload, a unit not read", ask("dbgFrames", pa(other)),
           {"state": "none", "why": "not decoded before the reload: import the vmlinux again to read its DWARF here"})
    expect("after a reload, the variables", ask("dbgTypes"), {"state": "none", "why": "the variables were not read before the reload: import the vmlinux again"})
    expect("after a reload, the CFI", ask("dbgCfiRow", pa(m_vas[0])), {"why": "the call frame information was not read before the reload: import the vmlinux again"})
    expect("after a reload, the code of a line", ask("dbgFileCode", "m.c"),
           {"state": "none", "why": "the imported vmlinux is not open since the reload: import it again to find the code of a line"})
    page.evaluate("new Promise((ok) => { const q = indexedDB.deleteDatabase('mkb-dwarf'); q.onsuccess = q.onerror = q.onblocked = ok; })")
    st = reload("")
    if "No file:line: import the unstripped vmlinux again to see its DWARF (file:line)" not in st:
        problems.append(f"import: after IndexedDB lost the units the status is {st!r}")
    # Round B: variables, locals, function facts and CFI read, then a reload.
    imp(elf)
    ty = ask("dbgTypes")
    if ty.get("state") != "ok":
        problems.append(f"import: the variables: {str(ty)[:300]}")
    va = next(v for v in m_vas if want[m_vas.index(v)] and len(want[m_vas.index(v)]) > 1)
    lo = ask("dbgLocals", pa(va))
    gs = gdb_scopes(str(elf), [va]).get(va) or []
    mine = sorted(v[0] for v in lo["frames"][0]["params"] + lo["frames"][0]["locals"]) if lo.get("state") == "ok" and lo["frames"] else None
    r["locals"] = [hex(va), mine, gs]
    if mine is None or mine != sorted(n for n, _ in gs):
        problems.append(f"import: the parameters and locals at {va:#x}: {str(lo)[:300]}, gdb {gs}")
    ask("dbgFnFactsAt", pa(va))
    title = page.evaluate("(pa) => MKB.dbgTitle(pa)", pa(va))
    w = want[m_vas.index(va)]
    r["title"] = title
    if not title.startswith(f"{w[0][0]} at m.c:{w[0][2]}\ninlined into {w[1][0]} at m.c:{w[1][2]}") or "(" not in title.split("\n")[2]:
        problems.append(f"import: the title at {va:#x}: {title!r}, llvm-symbolizer {w}")
    fi = ask("dbgCfiFrame", pa(m_vas[0]))
    fdes = dwarfdump_cfi(str(elf))
    expect("call frame information", page.evaluate("MKB.dbgCfiInfo()"), f".debug_frame of the imported vmlinux ({len(fdes)} FDEs)")
    if not isinstance(fi, dict) or not fi.get("fde"):
        problems.append(f"import: the frame at {m_vas[0]:#x}: {fi}")
    show_tab(page, "machine")
    for scheme, width in (("light", 1280), ("dark", 390)):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": width, "height": 900})
        page.locator("#sym_status").scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"dwarf-forms-import-{width}-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    # Broken copies, imported while the reader Workers of the good one run.
    broken = {name: (data, want) for name, data, want in dwf_broken(tmp, elf)}
    bad_elf = tmp / "abbrev.elf"
    bad_elf.write_bytes(broken["abbreviation"][0])
    imp(bad_elf)
    why = broken["abbreviation"][1][0].split(": ", 1)[1]
    expect("a broken unit's frames", ask("dbgFrames", pa(m_vas[0])), {"state": "none", "why": "debug info did not load: " + why})
    expect("a broken unit's code of a line", ask("dbgFileCode", "m.c"), {"state": "none", "why": "could not read the line tables: " + why})
    expect("a broken unit's variables", ask("dbgTypes"), {"state": "none", "why": "the variables did not load: " + why})
    v6 = tmp / "v6.elf"
    v6.write_bytes(broken["version 6"][0])
    st = imp(v6, "No file:line")
    if "No file:line: could not read the DWARF of v6.elf: DWARF version 6 at .debug_info+0." not in st:
        problems.append(f"import: DWARF version 6: the status is {st!r}")
    # Round B's reload: types, locals of m.c's unit and the CFI came back.
    imp(elf)
    ask("dbgTypes")
    ask("dbgLocals", pa(va))
    ask("dbgCfiRow", pa(m_vas[0]))
    page.wait_for_timeout(500)
    reload("cached")
    expect("after a reload, the locals of a unit not read", ask("dbgLocals", pa(other)),
           {"state": "none", "why": "the parameters and locals of this compile unit were not read before the reload: import the vmlinux again"})
    row = page.evaluate("(pa) => MKB.cfiRowText(MKB.dbgCfiRow(pa))", pa(m_vas[0]))
    f = next((f for f in fdes if f[0] <= m_vas[0] < f[1]), None)
    wrow = [t for a, t in (f[2] if f else []) if a <= m_vas[0]]
    expect("after a reload, the CFI row", row, wrow[-1] if wrow else None)
    show_tab(page, "machine")
    page.click("#sym_forget")
    page.close()
    if errors:
        problems.append(f"import: page errors: {errors[:3]}")
    return problems, r


def dwf_fail_checks(browser, base, elf):
    """What the page says when its debug info cannot be had: shipped
    files that fail to load (HTTP 404), an index.json still on its way or
    without some of its parts, a bzImage from disk (no debug info), the
    DWARF reader Worker that does not load, an import without IndexedDB
    (private mode). And the outermost function's rows (dbgFunction) of the
    shipped hello."""
    problems, r = [], {}
    hello = {s["name"]: s for s in json.loads((SITE / "kernels" / "hello" / "symbols.json").read_text())["symbols"]}
    pa = hello["hello_main"]["paddr"]
    url = f"{base}/index.html?kernel=hello&paused=1#machine"

    def opened(route=None, init=None):
        # No service worker: its cache would answer before the routes.
        ctx = browser.new_context(viewport={"width": 1280, "height": 900}, service_workers="block")
        if init:
            ctx.add_init_script(init)
        page = ctx.new_page()
        if route:
            page.route(*route)
        page.goto(url)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        return ctx, page

    def ask(page, fn, *args):
        return page.evaluate(DBG_WAIT_JS, [fn, list(args)])

    def expect(what, got, want):
        r[what] = got
        if got != want:
            problems.append(f"{what}: {got!r}, want {want!r}")
    nf = lambda route: route.fulfill(status=404, body="")
    # Every shipped file but index.json and types.json: HTTP 404.
    ctx, page = opened(("**/kernels/hello/dwarf/*", lambda rt: rt.continue_() if rt.request.url.split("?")[0].endswith(("index.json", "types.json")) else nf(rt)))
    expect("404: frames", ask(page, "dbgFrames", pa), {"state": "none", "why": "debug info did not load: HTTP 404"})
    expect("404: locals", ask(page, "dbgLocals", pa), {"state": "none", "why": "the parameters and locals did not load: HTTP 404"})
    expect("404: function facts", ask(page, "dbgFnFacts", "hello_main"), {"state": "none", "why": "the function facts did not load: HTTP 404"})
    expect("404: files", ask(page, "dbgFiles"), {"state": "none", "why": "the file list did not load: HTTP 404"})
    expect("404: CFI", ask(page, "dbgCfiRow", pa), {"why": "the call frame information did not load: HTTP 404"})
    ctx.close()
    ctx, page = opened(("**/kernels/hello/dwarf/types.json*", nf))
    expect("404: variables", ask(page, "dbgTypes"), {"state": "none", "why": "the variables did not load: HTTP 404"})
    ctx.close()
    # index.json held back, then without some parts (an index of an older gen-dwarf.js).
    held = []
    index = json.loads((SITE / "kernels" / "hello" / "dwarf" / "index.json").read_text())
    for drop in (("locals", "funcs", "files", "cfi"), ("types",)):
        held.clear()
        ctx, page = opened(("**/kernels/hello/dwarf/index.json*", lambda rt: held.append(rt)))
        if drop[0] == "locals":
            expect("index on its way: files, CFI", page.evaluate("(pa) => [MKB.dbgFiles(), MKB.dbgCfiRow(pa)]", pa),
                   [{"state": "loading", "why": "reading the debug info index…"}, "loading"])
        wait_for(lambda: held, 10)
        held[0].fulfill(status=200, content_type="application/json", body=json.dumps({k: v for k, v in index.items() if k not in drop}))
        if drop[0] == "locals":
            expect("no locals in the index", ask(page, "dbgLocals", pa),
                   {"state": "none", "why": "this kernel's debug info has no parameters and locals (locals-*.json.gz)"})
            expect("no function facts in the index", ask(page, "dbgFnFacts", "hello_main"),
                   {"state": "none", "why": "this kernel's debug info has no function facts (funcs-*.json.gz)"})
            expect("no files in the index", ask(page, "dbgFiles"), {"state": "none", "why": "this kernel's debug info has no file list (files.json)"})
            expect("no CFI in the index", ask(page, "dbgCfiRow", pa), {"why": "this kernel's debug info has no call frame information"})
        else:
            expect("no variables in the index", ask(page, "dbgTypes"), {"state": "none", "why": "this kernel's debug info has no variables (types.json)"})
        ctx.close()
    # The outermost function around an address of the shipped hello.
    ctx, page = opened()
    fn = ask(page, "dbgFunction", pa + 4)
    want_hi = pa + hello["hello_main"]["size"]
    r["function"] = [fn.get("state"), fn.get("lo"), fn.get("hi"), len(fn.get("rows", []))]
    if fn.get("state") != "ok" or fn["lo"] != pa or fn["hi"] != want_hi or fn["rows"][0][0] != pa or fn["rows"][-1][1] != want_hi:
        problems.append(f"dbgFunction(hello_main+4): {str(fn)[:300]}, want [{pa:#x}, {want_hi:#x})")
    expect("function: assembly", ask(page, "dbgFunction", hello["startup_32"]["paddr"]),
           {"state": "none", "why": "no DWARF function here (assembly or code without debug info)"})
    expect("function: no code", ask(page, "dbgFunction", 0x5000), {"state": "none", "why": "no DWARF here: not code of this kernel's vmlinux"})
    # A bzImage from disk has no debug info.
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#file", str(SITE / "kernels" / "hello" / "bzImage"))
    wait_for(lambda: page.evaluate("MKB.DBG.mode === null && MKB.DBG.why.startsWith('no debug info for a bzImage')"), 30)
    why = "no debug info for a bzImage from disk: import its unstripped vmlinux for file:line"
    expect("from disk: locals", ask(page, "dbgLocals", pa), {"state": "none", "why": why})
    expect("from disk: function facts", ask(page, "dbgFnFacts", "hello_main"), {"state": "none", "why": why})
    expect("from disk: CFI", ask(page, "dbgCfiRow", pa), {"why": why})
    expect("from disk: code of a line", ask(page, "dbgFileCode", "hello.c"), {"state": "none", "why": why})
    expect("no address: title", page.evaluate("MKB.dbgTitle(-1)"), "")
    ctx.close()
    # The Worker's script does not load: for the file:line reader, then
    # (the first one loaded) for the variables' second Worker.
    for first_ok in (False, True):
        asked = []

        def worker(rt):
            asked.append(rt.request.url)
            rt.continue_() if first_ok and len(asked) == 1 else nf(rt)
        ctx, page = opened(("**/dwarfworker.js*", worker))
        page.evaluate("MKB.startPaused = true; document.getElementById('sym_offset').value = '0xc0000000'")
        page.set_input_files("#symfile", str(elf))
        if not first_ok:
            wait_for(lambda: "the DWARF reader failed" in page.inner_text("#sym_status"), 30)
            st = page.inner_text("#sym_status")
            r["no Worker: status"] = st
            if "No file:line: the DWARF reader failed: " not in st:
                problems.append(f"no Worker: the status is {st!r}")
        else:
            wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 30)
            ty = ask(page, "dbgTypes")
            r["no second Worker: variables"] = [ty, len(asked)]
            if ty.get("state") != "none" or not ty.get("why", "").startswith("the variables did not load: ") or len(asked) != 2:
                problems.append(f"no second Worker: the variables: {ty} ({len(asked)} requests)")
        ctx.close()
    # No IndexedDB (private mode): the import works, nothing is remembered.
    ctx, page = opened(init="Object.defineProperty(window, 'indexedDB', { value: undefined });")
    page.evaluate("MKB.startPaused = true; document.getElementById('sym_offset').value = '0xc0000000'")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 30)
    g = ask(page, "dbgFrames", pa_of(elf, "use"))
    r["no IndexedDB: frames"] = g.get("state")
    if g.get("state") != "ok":
        problems.append(f"no IndexedDB: the frames of the import: {g}")
    page.goto("about:blank")
    page.goto(url)
    wait_for(lambda: "No file:line" in page.inner_text("#sym_status"), 60)
    st = page.inner_text("#sym_status")
    r["no IndexedDB: after a reload"] = st
    if "No file:line: import the unstripped vmlinux again to see its DWARF (file:line)" not in st:
        problems.append(f"no IndexedDB: after a reload the status is {st!r}")
    ctx.close()
    return problems, r


def pa_of(elf, name):
    """Physical address (va - CFI_OFFSET) of an ELF symbol."""
    for l in subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines():
        f = l.split()
        if len(f) == 3 and f[2] == name:
            return int(f[0], 16) - CFI_OFFSET
    return None
