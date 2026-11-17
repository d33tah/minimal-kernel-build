"""Browser test suite types (test_browser.py --suites)."""

import base64
import json
import os
import re
import shutil
import subprocess
import time

from tests.browser.common import debug_elf, hover_byte, kernel_entries, open_page, report, show_tab, SITE, wait_for


def watch(page, expr):
    """Add a watch; its row's text once the types have loaded ("" if not shown)."""
    wait_for(lambda: page.get_attribute("#watches", "data-state") == "ok", 120)
    page.fill("#watch_expr", expr)
    page.click("#watch_add")
    sel = f'#watches .watch[data-expr="{expr}"]'
    wait_for(lambda: page.locator(sel).count() > 0, 10)
    return page.inner_text(sel).replace("×", "").strip() if page.locator(sel).count() else ""


def gdb_values(elf, exprs):
    """gdb's print of each expression from the ELF's own data (the values at
    boot, before the kernel changes them), or None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    args = ["gdb", "-nx", "-batch"]
    for e in exprs:
        args += ["-ex", f"print {e}"]
    out = subprocess.run(args + [str(elf)], capture_output=True, text=True).stdout
    return [re.sub(r"^\$\d+ = ", "", l) for l in out.splitlines() if re.match(r"^\$\d+ = ", l)]


# Watches whose values at start_kernel are the ELF's own data (gdb prints them from the file).
TYPES_WATCHES = {
    "minified-linux": ["init_task.comm", "jiffies_64", "init_task.pid", "current_task", "current_task->comm",
                       "init_task.comm[1]"],
    "debian-6.1-686": ["init_task.comm", "jiffies_64", "init_uts_ns.name.release", "linux_banner", "system_state"],
}


def boxed_line_ink(page, sel):
    """Whether the underscores of the boxed line sel (the EIP line: .cur, an
    outline drawn 1 px inside its box) are drawn: for every `_` of its
    text, some pixel row between the box's outline rows has ink (pixels
    far from both the background and the outline colour) across at
    least a third of the character cell. The screenshot is decoded in
    the page (canvas): no imaging module needed. [(char index, ok)] or
    None when there is no such line."""
    geo = page.evaluate("""(sel) => { const el = document.querySelector(sel); if (!el) return null;
        el.scrollIntoView({ block: 'center' });
        const cs = getComputedStyle(el), box = el.getBoundingClientRect(), cells = [];
        const w = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
        let k = 0;
        for (let n; (n = w.nextNode());)
            for (let i = 0; i < n.data.length; i++, k++)
                if (n.data[i] === '_') {
                    const rg = document.createRange(); rg.setStart(n, i); rg.setEnd(n, i + 1);
                    const r = rg.getBoundingClientRect(); cells.push([k, r.left, r.right]);
                }
        const rgb = (c) => (c.match(/[\\d.]+/g) || []).slice(0, 3).map(Number);
        let bg = cs.backgroundColor, e = el;
        while (e && /^rgba\\(.*, 0\\)$/.test(bg)) { e = e.parentElement; bg = getComputedStyle(e || document.body).backgroundColor; }
        return { top: box.top, bottom: box.bottom, cells, bg: rgb(bg), ol: rgb(cs.outlineColor) }; }""", sel)
    if not geo:
        return None
    out = []
    for k, left, right in geo["cells"]:
        clip = {"x": left, "y": geo["top"] - 2, "width": max(1, right - left), "height": geo["bottom"] - geo["top"] + 4}
        png = base64.b64encode(page.screenshot(clip=clip)).decode()
        ok = page.evaluate("""async ([b64, bg, ol]) => {
            const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
            const c = document.createElement('canvas'); c.width = img.width; c.height = img.height;
            const x = c.getContext('2d'); x.drawImage(img, 0, 0);
            const d = x.getImageData(0, 0, c.width, c.height).data;
            const dist = (i, q) => Math.abs(d[i] - q[0]) + Math.abs(d[i + 1] - q[1]) + Math.abs(d[i + 2] - q[2]);
            const rows = [];
            for (let y = 0; y < c.height; y++) {
                let ink = 0, line = 0;
                for (let x0 = 0; x0 < c.width; x0++) {
                    const i = (y * c.width + x0) * 4;
                    if (dist(i, ol) < 60) line++;
                    else if (dist(i, bg) > 90) ink++;
                }
                rows.push([ink / c.width, line / c.width]);
            }
            // The outline's rows: the first and last mostly in its colour.
            const lines = rows.map((r, y) => r[1] > 0.6 ? y : -1).filter((y) => y >= 0);
            if (lines.length < 2) return false;
            const a = lines.find((y, j) => j + 1 < lines.length && lines[j + 1] > y + 1), b = lines[lines.length - 1];
            if (a === undefined) return false;
            for (let y = a + 1; y < b; y++) if (rows[y][0] >= 0.34) return true;
            return false; }""", [png, geo["bg"], geo["ol"]])
        out.append((k, ok))
    return out


def eip_box_check(page, name, problems, r, shots):
    """The Debugger's boxed EIP line draws the underscores of its names
    (`start_kernel+0x0`, `<__fentry__>`), light and dark."""
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        got = boxed_line_ink(page, "#context .ins.cur")
        text = page.inner_text("#context .ins.cur") if page.locator("#context .ins.cur").count() else ""
        r[f"eip_box_{scheme}"] = f"{text!r}: {got}"
        if not got:
            problems.append(f"{name}: no underscore in the boxed EIP line to check ({text!r})")
        elif not all(ok for _, ok in got):
            problems.append(f"{name}: the boxed EIP line hides underscores ({scheme}): {text!r} {got}")
        page.locator("#context").screenshot(path=str(shots / f"eip-box-{name}-{scheme}.png"))
    page.emulate_media(color_scheme="light")


# vars_edges: watches whose row the case checks, per kernel. (expression,
# what to check): "error: text" (the row's error), "gdb" (its value is gdb's
# print), "addr=NAME" (its address is variable NAME's), "note: text" (a
# note under it starts so), "kids N: note" (opened: N children and that
# note), "summary: text", "type=gdb" (its type is gdb's whatis), "decl: text".
VARS_EDGES = {
    "minified-linux": [
        ("123", "error: not a variable name: 123"),
        ("no_such_var_xyz", "error: no variable no_such_var_xyz in this kernel's DWARF"),
        ("*high_memory", "error: the pointer (0x0) is not a kernel address in RAM"),
        ("vc0.vc_need_wrap.x", "error: a bit field has no members"),
        ("vc0.vc_need_wrap", "gdb"),
        ("kmalloc_caches[0][3]", "gdb"),
        ("kmalloc_caches[0][20]", "note: index 20 is past the end (14 elements)"),
        ("kmalloc_caches[0]", "kids 14: "),
        ("*delay_fn", "type=gdb"),
        ("*current_task", "addr=init_task"),
        ("current_task[0]", "addr=init_task"),
        ("kmem_cache_init::boot_kmem_cache", "note: static local of kmem_cache_init()"),
        ("initcall_levels", "note: in .init.data: freed once the kernel has booted (free_initmem), then the memory is reused"),
        ("vector_irq", "kids 64: first 64 of 256 elements: watch name[i] for others"),
        ("vector_irq[1000000]", "value: (not in RAM)"),
        ("slash_name.hash", "gdb"),
    ],
    "debian-6.1-686": [
        ("cpu_tss_rw", "note: per-CPU variable"),
        ("do_initcalls::__func__", "decl: defined at an unknown place (its DIE has no DW_AT_decl_file)"),
        ("posix_clocks", "summary: [?]"),
        ("posix_clocks", "kids 0: flexible array: its length is not in the type"),
    ],
}


def vars_edges(page, name, T, off, elf, shots, problems, r):
    """Watches past the common ones (VARS_EDGES), each against gdb's print
    of the ELF or the types.json numbers; the task row's Watch and a +
    suggestion add theirs; Watch with an empty field adds
    nothing, Enter in the field adds; an opened row closed again stays
    closed after Refresh; the name suggestions stop at 40. In the Page
    listing: a byte pointed at in the ASCII column names what its hex
    column names, a byte in no variable says so, one in a union names the
    union's other members, a per-CPU variable's byte is its template, a
    click on a hex row's address unpins the description."""
    T_names = {v[0] for v in T["vars"]}
    show_tab(page, "debugger")
    sel = lambda e: f'#watches .watch[data-expr="{e}"]'
    # The task row's Watch: *current_task, the task_struct of init_task at start_kernel.
    page.locator("#cur_task button").click()
    wait_for(lambda: page.locator(sel("*current_task")).count() > 0, 10)
    it = next(v for v in T["vars"] if v[0] == "init_task")
    r["task_row_watch"] = page.get_attribute(sel("*current_task"), "data-addr") if page.locator(sel("*current_task")).count() else None
    if r["task_row_watch"] != str(it[1]):
        problems.append(f"{name}: the task row's Watch: *current_task at {r['task_row_watch']}, init_task is at {it[1]}")
    # A "+ name" suggestion adds that watch.
    quick = page.locator("#watch_quick button").first
    qname = quick.inner_text()[2:] if quick.count() else None
    if qname:
        quick.click()
        wait_for(lambda: page.locator(sel(qname)).count() > 0, 10)
    r["quick_watch"] = [qname, page.locator(sel(qname)).count() if qname else 0]
    if not qname or qname not in T_names or r["quick_watch"][1] != 1 or page.locator(f'#watch_quick button:text-is("+ {qname}")').count():
        problems.append(f"{name}: a + suggestion: {r['quick_watch']}")
    n0 = page.locator("#watches .watch").count()
    page.fill("#watch_expr", "  ")
    page.click("#watch_add")
    if page.locator("#watches .watch").count() != n0:
        problems.append(f"{name}: Watch with an empty field added a watch")
    gdb = lambda exprs: gdb_values(elf, exprs) or [None] * len(exprs)
    r["vars_edges"] = {}
    for e, want in VARS_EDGES[name]:
        if not page.locator(sel(e)).count():
            page.fill("#watch_expr", e)
            page.press("#watch_expr", "Enter")
            wait_for(lambda: page.locator(sel(e)).count() > 0, 10)
        w = page.locator(sel(e))
        if not w.count():
            problems.append(f"{name}: Enter did not add the watch {e}")
            continue
        state = w.get_attribute("data-state")
        text = w.inner_text().replace("×", "").strip()
        val = w.locator(".wval").first.inner_text() if w.locator(".wval").count() else None
        notes = w.locator(":scope > div.note:not(.wdecl):not(.wothers)").all_inner_texts()
        got = text if want.startswith("error") else val
        kind, _, arg = want.partition(": ") if ": " in want else (want, "", "")
        if want.startswith("error: "):
            ok = state == "error" and text == f"{e}: {arg}"
        elif want == "gdb":
            g = gdb([e])[0]
            got = [val, g]
            ok = state == "ok" and g is not None and val == ("NULL" if re.sub(r"^\([^)]*\) ", "", g) == "0x0" else g)
        elif want == "type=gdb":
            g = subprocess.run(["gdb", "-nx", "-batch", "-ex", f"whatis {e}", str(elf)], capture_output=True, text=True).stdout.strip() if elf else ""
            got = [val, w.locator(".wtype").first.inner_text(), g]
            ok = state == "ok" and val == "" and g == "type = " + got[1]
        elif want.startswith("addr="):
            v = next(x for x in T["vars"] if x[0] == want[5:])
            got = w.get_attribute("data-addr")
            ok = state == "ok" and got == str(v[1])
        elif kind == "note":
            got = notes
            if e == "cpu_tss_rw":
                # The template until setup_per_cpu_areas() set __per_cpu_offset[0] (gdb: its value in the ELF).
                pco = gdb(["__per_cpu_offset[0]"])[0]
                arg = "the template in .data..percpu" if pco == "0" else "CPU 0's copy (template + __per_cpu_offset[0]"
                ok = state == "ok" and notes[:1] == ["per-CPU variable"] and any(n.startswith(arg) for n in notes)
            else:
                ok = state == "ok" and any(n.startswith(arg) for n in notes)
            if e == "kmem_cache_init::boot_kmem_cache":
                g = gdb(["&kmem_cache_init::boot_kmem_cache"])[0]
                got = [notes, w.get_attribute("data-addr"), g]
                ok = ok and g is not None and f"{int(w.get_attribute('data-addr')) + off:#x}" in g
        elif kind == "decl":
            got = w.locator(".wdecl").inner_text() if w.locator(".wdecl").count() else None
            ok = got == arg
        elif kind in ("summary", "value"):
            ok = state == "ok" and val == arg
            if e == "vector_irq[1000000]":
                ok = ok and notes == ["index 1000000 is past the end (256 elements)"]
                got = [val, notes]
        elif kind.startswith("kids "):
            n = int(kind[5:])
            w.locator("summary").first.click()
            wait_for(lambda: w.locator(".wkids").count() > 0, 5)
            kids = w.locator(".wkids").first
            names = kids.locator(":scope > * .wname").all_inner_texts()
            knote = kids.locator(":scope > .note").all_inner_texts()
            got = [len(names), knote, val]
            ok = len(names) == n and knote == ([arg] if arg else [])
            if e == "kmalloc_caches[0]":
                # gdb: {0x0 <repeats 14 times>}
                vals = kids.locator(":scope > * .wval").all_inner_texts()
                g = gdb([e])[0]
                got.append(g)
                ok = ok and val == "[14]" and names == [f"[{i}]" for i in range(14)] and \
                    g == "{0x0 <repeats 14 times>}" and vals == ["NULL"] * 14
            if e == "vector_irq":
                # Closed again: it stays closed after Refresh.
                w.locator("summary").first.click()
                page.click("#watch_refresh")
                closed = page.evaluate("(s) => !document.querySelector(s + ' details').open", sel(e))
                got.append(["closed after Refresh", closed])
                ok = ok and closed
        else:
            ok = False
        r["vars_edges"][e + " " + want[:12]] = str(got)[:200]
        if not ok:
            problems.append(f"{name}: watch {e} ({want}): {got}")
    # The name suggestions: at most 40 of those starting with what is typed.
    page.fill("#watch_expr", "in")
    page.dispatch_event("#watch_expr", "input")
    opts = page.eval_on_selector_all("#watch_names option", "os => os.map((o) => o.value)")
    want_n = min(40, sum(1 for x in T_names if x.startswith("in")))
    r["suggestions_in"] = [len(opts), want_n]
    if len(opts) != want_n or not all(o.startswith("in") and o in T_names for o in opts) or len(set(opts)) != len(opts):
        problems.append(f"{name}: suggestions for 'in': {len(opts)} {opts[:5]}, want {want_n} names starting with it")
    page.fill("#watch_expr", "")
    page.screenshot(path=str(shots / f"vars-edges-{name}-1280.png"), full_page=True)
    # The Page listing.
    show_tab(page, "memory")
    picks = {"minified-linux": {"union": "slash_name", "gap": 0x125000},
             "debian-6.1-686": {"gap": 0x1c43e40, "percpu": "cpu_tss_rw"}}[name]
    def at(pa):
        if str(pa // 4096) != page.get_attribute("#listing", "data-page"):
            open_page(page, pa // 4096)
        return hover_byte(page, pa)
    it = next(v for v in T["vars"] if v[0] == "init_task")
    pa = it[1] + 16 + it[3] // 3
    hexpath = at(pa)
    xy = page.evaluate("""(pa) => { for (const l of document.querySelectorAll('#listing .hex')) {
        if (pa < +l.dataset.addr || pa >= +l.dataset.end) continue;
        l.scrollIntoView({ block: 'center' });
        const r = l.getBoundingClientRect(), cw = r.width / l.textContent.length, i = pa - +l.dataset.row, per = +l.dataset.per;
        return [r.left + (9 + per * 3 - 1 + (per === 16 ? 1 : 0) + 2 + i + 0.5) * cw, r.top + r.height / 2]; }
        return null; }""", pa)
    page.mouse.move(*xy)
    wait_for(lambda: page.get_attribute("#var_info", "data-addr") == str(pa), 3)
    r["ascii_column"] = [hexpath, page.get_attribute("#var_info", "data-path")]
    if not hexpath or page.get_attribute("#var_info", "data-path") != hexpath:
        problems.append(f"{name}: byte {pa:#x} in the ASCII column names {r['ascii_column'][1]!r}, its hex column {hexpath!r}")
    at(picks["gap"])
    r["no_variable"] = page.inner_text("#var_info")
    if any(v[1] <= picks["gap"] < v[1] + max(v[3], 1) for v in T["vars"]) or page.get_attribute("#var_info", "data-state") != "none" or \
            r["no_variable"] != f"{picks['gap']:#x} ({picks['gap'] + off:#x}): no variable of the DWARF here (heap, stack, page tables, code, or data without debug info)":
        problems.append(f"{name}: byte {picks['gap']:#x} (in no variable): {r['no_variable']!r}")
    if "union" in picks:
        # slash_name: a struct qstr, whose first member is a union of an anonymous struct (hash, len) and hash_len.
        u = next(v for v in T["vars"] if v[0] == picks["union"])
        at(u[1])
        r["union"] = page.inner_text("#var_info")
        if f"{u[1] + off:#x} slash_name.hash: u32 = " not in r["union"] or "; the same bytes are also hash_len (union)" not in r["union"]:
            problems.append(f"{name}: a byte of {picks['union']}'s union does not name the union's other member: {r['union']!r}")
    if "percpu" in picks:
        v = next(x for x in T["vars"] if x[0] == picks["percpu"])
        at(v[1] + 8)
        r["percpu_hover"] = page.inner_text("#var_info")
        if "(this is the template; the Variables panel shows CPU 0's copy)" not in r["percpu_hover"]:
            problems.append(f"{name}: a byte of {picks['percpu']} does not say it is the template: {r['percpu_hover']!r}")
    # Pinned, then a click on the row's address: unpinned.
    at(pa)
    hover_byte(page, pa, click=True)
    pinned = page.locator("#var_watch").count()
    xy = page.evaluate("""(pa) => { for (const l of document.querySelectorAll('#listing .hex')) {
        if (pa < +l.dataset.addr || pa >= +l.dataset.end) continue;
        const r = l.getBoundingClientRect(), cw = r.width / l.textContent.length;
        return [r.left + 2.5 * cw, r.top + r.height / 2]; } return [0, 0]; }""", pa)
    page.mouse.click(*xy)
    r["unpin"] = [pinned, page.inner_text("#var_info"), page.get_attribute("#var_info", "data-state")]
    if r["unpin"] != [1, "", ""]:
        problems.append(f"{name}: a click on a hex row's address after a pin: [Watch button before, text, state] {r['unpin']}")
    show_tab(page, "debugger")


def var_decl(T, v):
    """Where types.json says variable v is defined: (file, line) or (None, None)."""
    return (T["files"][v[7]], v[8]) if "files" in T and len(v) > 7 and v[7] >= 0 else (None, None)


def vardecl_checks(page, name, T, off, elf, shots, problems, r):
    """Variables say where they are defined (DW_AT_decl_file/decl_line):
    the watch row's "defined at" (gdb's symbol's file:line) opens the
    Source panel there; for a name several variables share, the note lists
    the other definitions, and `file.c::name` (path or unique suffix)
    picks the one gdb's &'file.c'::name is; an unknown or ambiguous file
    is refused with the definitions; the hex-hover text and the memory
    operands' tooltips say it too. Screenshots light/dark, 1280/390 px."""
    from tests.dwarf.vars import gdb_vars
    use_gdb = bool(elf and shutil.which("gdb"))
    if not use_gdb and os.environ.get("CI"):
        problems.append(f"{name}: no gdb or -g ELF to check where variables are defined")
    if "files" not in T:
        problems.append(f"{name}: types.json has no files: variables do not say where they are defined")
        return
    show_tab(page, "debugger")
    it = next(v for v in T["vars"] if v[0] == "init_task")
    sel = '#watches .watch[data-expr="init_task"]'
    if not page.locator(sel).count():
        watch(page, "init_task")
    loc = page.locator(sel + " .wdecl .varloc")
    want = var_decl(T, it)
    have = (loc.get_attribute("data-file"), int(loc.get_attribute("data-line") or 0)) if loc.count() else None
    r["init_task defined at"] = have
    if have != want or want[0] is None:
        problems.append(f"{name}: init_task's watch says defined at {have}, types.json {want}")
    if use_gdb:
        g = gdb_vars(str(elf), [["init_task", "", it[1] + off, "", True]])[0]
        r["init_task gdb"] = g
        if g["why"] or not same_path(g["file"], want[0]) or g["line"] != want[1]:
            problems.append(f"{name}: init_task defined at {want}, gdb {g}")
    if loc.count():
        loc.click()
        if not wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-file") == want[0]
                        and page.locator(f'#src_body [data-line="{want[1]}"].pick').count() > 0, 60):
            problems.append(f"{name}: init_task's 'defined at' did not open {want[0]}:{want[1]} in the Source panel "
                            f"({page.inner_text('#src_body')[:200]!r})")
        else:
            r["init_task decl line"] = page.inner_text(f'#src_body [data-line="{want[1]}"]').strip()[:90]
            if "init_task" not in r["init_task decl line"]:
                problems.append(f"{name}: the Source panel's line {want[1]} does not define init_task: {r['init_task decl line']!r}")
        page.click("#src_close")
    # A qualifier no definition matches is refused.
    bad_q = watch(page, "nosuch/file.c::init_task")
    r["unknown file"] = bad_q
    if "not defined in" not in bad_q:
        problems.append(f"{name}: nosuch/file.c::init_task is not refused: {bad_q!r}")
    page.locator('#watches .watch[data-expr="nosuch/file.c::init_task"] .wdel').click()
    # A name several file statics share (Debian): the note lists the others; file::name picks by file.
    count, files = {}, {}
    for v in T["vars"]:
        count[v[0]] = count.get(v[0], 0) + 1
        f = var_decl(T, v)[0]
        if f and not v[5] & 4:
            files.setdefault(v[0], {}).setdefault(f, []).append(v)
    shared = sorted(n for n, fs in files.items() if len(fs) >= 3 and all(len(x) == 1 for x in fs.values()))
    if name == "debian-6.1-686" and not shared:
        problems.append(f"{name}: no name of several file statics to pick from")
    if shared:
        nm = "_acpi_module_name" if "_acpi_module_name" in shared else shared[0]
        text = watch(page, nm)
        wsel = f'#watches .watch[data-expr="{nm}"]'
        others = page.locator(wsel + " .wothers .varloc")
        r["shared"] = f"{nm}: {count[nm]} variables, note: " + (page.inner_text(wsel + " .wothers")[:160] if others.count() else "none")
        if f"{count[nm]} variables have this name" not in text or others.count() != min(count[nm] - 1, 20):
            problems.append(f"{name}: watch {nm} does not list the other {count[nm] - 1} definitions: {text[:300]!r}")
        chosen = int(page.get_attribute(wsel, "data-addr") or -1)
        # Each listed definition: its file:line is types.json's for the variable at that address.
        for k in range(others.count()):
            o = others.nth(k)
            v = next((x for x in T["vars"] if x[0] == nm and x[1] == int(o.get_attribute("data-var"))), None)
            if v is None or v[1] == chosen or var_decl(T, v) != (o.get_attribute("data-file"), int(o.get_attribute("data-line"))):
                problems.append(f"{name}: {nm}'s note lists {o.inner_text()!r} at {o.get_attribute('data-var')}, types.json {v}")
                break
        rnd = __import__("random").Random(5)
        fs = files[nm]
        picks = rnd.sample(sorted(f for f, vs in fs.items() if vs[0][1] != chosen), 2)
        probes, exprs = [], []
        for f in picks:
            v = fs[f][0]
            base = f.rsplit("/", 1)[-1]
            short = base if sum(1 for g in fs if g == base or g.endswith("/" + base)) == 1 else f
            for q in sorted({f, short}):
                exprs.append((q + "::" + nm, v))
            probes.append([nm, "", v[1] + off, f])
        for e, v in exprs:
            watch(page, e)
            got = page.get_attribute(f'#watches .watch[data-expr="{e}"]', "data-addr")
            r[e] = got
            if got != str(v[1]):
                problems.append(f"{name}: {e} picks {got}, the variable defined in that file is at {v[1]}")
        if use_gdb:
            for p, g in zip(probes, gdb_vars(str(elf), probes)):
                r[f"gdb &'{p[3]}'::{nm}"] = g["qual"]
                if g["qual"] != p[2]:
                    problems.append(f"{name}: gdb &'{p[3]}'::{nm} = {g['qual']}, the page's is {p[2]:#x}")
    # Several files end with the qualifier: refused, listing them.
    amb = None
    for n, fs in sorted(files.items()):
        base = {}
        for f in fs:
            base.setdefault(f.rsplit("/", 1)[-1], set()).add(f)
        b = next((b for b, x in sorted(base.items()) if len(x) > 1), None)
        if b:
            amb = (b, n)
            break
    if amb:
        e = amb[0] + "::" + amb[1]
        t = watch(page, e)
        r["ambiguous"] = t[:200]
        if "ambiguous" not in t:
            problems.append(f"{name}: {e} is not refused as ambiguous: {t[:200]!r}")
        page.locator(f'#watches .watch[data-expr="{e}"] .wdel').click()
    page.locator("#watches").scroll_into_view_if_needed()
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#watches").screenshot(path=str(shots / f"vardecl-watches-{name}-1280-{scheme}.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Variables with 'defined at' scroll horizontally at 390 px ({scheme})")
        page.locator("#watches .wothers, #watches .wdecl").first.scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"vardecl-watches-{name}-390-{scheme}.png"))
    page.set_viewport_size({"width": 1280, "height": 1000})
    # The Page listing: the hovered byte's text, and the memory operands' tooltips.
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    pa = it[1] + 16 + it[3] // 3
    hover_byte(page, pa, click=True)
    vl = page.locator("#var_info .varloc")
    r["hover"] = page.inner_text("#var_info")[-160:]
    if not vl.count() or (vl.first.get_attribute("data-file"), int(vl.first.get_attribute("data-line") or 0)) != want or \
            f"defined at {want[0]}:{want[1]}" not in page.inner_text("#var_info"):
        problems.append(f"{name}: the hovered byte of init_task does not say defined at {want}: {r['hover']!r}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        # Just below the sticky header and tabs (an element screenshot would show the header over it).
        page.evaluate("""() => { const tabs = document.querySelector('[role=tablist]').getBoundingClientRect().bottom;
            window.scrollBy(0, document.getElementById('var_info').getBoundingClientRect().top - tabs - 40); }""")
        page.screenshot(path=str(shots / f"vardecl-hover-{name}-1280-{scheme}.png"))
    page.emulate_media(color_scheme="light")
    sk = next(s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"] if s["name"] == "start_kernel")
    open_page(page, sk["paddr"] // 4096)
    wait_for(lambda: page.locator('#listing .opn[data-kind="var"]').count() > 0, 60)
    ops = page.evaluate("""() => [...document.querySelectorAll('#listing .opn[data-kind="var"], #listing .opn[data-kind="percpu"]')]
        .map((e) => [e.dataset.name, +e.dataset.va, e.title])""")
    checked, probes = 0, []
    for nm, va, title in ops:
        # "&name" for a lea (-O0 takes addresses of globals with lea).
        root = re.match(r"&?(\w+)", nm)[1]
        v = next((x for x in T["vars"] if x[0] == root and x[1] <= va - off < x[1] + max(x[3], 1)), None)
        if v is None:
            continue
        f, ln = var_decl(T, v)
        checked += 1
        if f and f"defined at {f}:{ln}" not in title or not f and "defined at" in title:
            problems.append(f"{name}: operand <{nm}> tooltip {title!r} does not say defined at {f}:{ln}")
        if f and len(probes) < 8 and root not in [p[0] for p in probes]:
            probes.append([root, v[6] if v[5] & 4 and len(v) > 6 else "", v[1] + off, "", True, f, ln])
    r["operands with defined at"] = f"{checked} of {len(ops)}"
    if not checked:
        problems.append(f"{name}: no global operand on start_kernel's page to check")
    if use_gdb and probes:
        g = gdb_vars(str(elf), [p[:5] for p in probes])
        diff = [(p[0], p[5], p[6], x) for p, x in zip(probes, g) if not x["why"] and (not same_path(x["file"], p[5]) or x["line"] != p[6])]
        r["operand vars vs gdb"] = f"{sum(1 for x in g if not x['why']) - len(diff)}/{len(g)} = gdb"
        if diff:
            problems.append(f"{name}: operand variables defined elsewhere for gdb: {diff[:3]}")
        if sum(1 for x in g if x["why"]) > len(g) // 2:
            problems.append(f"{name}: gdb confirms only {sum(1 for x in g if not x['why'])} of {len(g)} operand variables: {g[:3]}")
    show_tab(page, "debugger")


def types_case(browser, base, shots, name):
    """Variables of the shipped DWARF (types.json): at start_kernel the
    Variables panel prints watches equal to gdb's print of the ELF's data,
    opens structs, follows a pointer to the Page tab; in the Page listing
    a pointed-at byte names variable + member path, which gdb evaluates to
    an lvalue holding that byte; data symbols' label lines carry the type."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    T = json.loads((SITE / entry["dwarf"] / "types.json").read_text())
    off = sk["vaddr"] - sk["paddr"]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    # A watch list in this browser that is not JSON: the panel starts with no watches.
    page.add_init_script(f"localStorage.setItem('mkb-watch:{name}', '[\"init_task\", oops')")
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 1):
        page.close()
        return [f"{name}: Run to start_kernel did not stop there"], r
    eip_box_check(page, name, problems, r, shots)
    r["status"] = page.inner_text("#watch_status") if wait_for(
        lambda: page.get_attribute("#watches", "data-state") == "ok", 120) else page.inner_text("#watch_status")
    if page.locator("#watches .watch").count() or not page.inner_text("#watches").startswith("No watches"):
        problems.append(f"{name}: a stored watch list that is not JSON: {page.inner_text('#watches')[:100]!r}")
    if f"{len(T['vars'])} variables" not in r["status"]:
        problems.append(f"{name}: Variables status {r['status']!r}")
    exprs = TYPES_WATCHES[name]
    rows = {e: watch(page, e) for e in exprs}
    r["watches"] = rows
    elf = debug_elf(name)
    want = gdb_values(elf, exprs)
    if want is None and os.environ.get("CI"):
        problems.append(f"{name}: no gdb or -g ELF to check the watches against")
    for e, w in zip(exprs, want or []):
        got = rows[e]
        # gdb: "swapper\000..." / 4294892296 / SYSTEM_BOOTING / (struct task_struct *) 0xc... <init_task>
        g = re.sub(r"(\\000)+(\"?)$", r"\2", re.sub(r", '\\000' <repeats \d+ times>$", "", w))
        g = re.sub(r"^\([^)]*\) ", "", g)
        # A string: its first characters; else the first word (number, enumerator, pointer).
        key = g[g.index('"'):][:30] if '"' in g else g.split(" ")[0]
        if key not in got:
            problems.append(f"{name}: watch {e} shows {got!r}, gdb prints {w!r}")
    # What a watch cannot read says why, in its row.
    bad = {"init_task.nosuch": "struct task_struct has no member nosuch",
           "jiffies_64->x": "not a pointer",
           "init_task.comm!": "cannot read “!” (use .member, ->member, [index])",
           "init_task.pid.x": "pid_t has no member x"}
    r["watch errors"] = {}
    for e, want in bad.items():
        got = watch(page, e)
        state = page.get_attribute(f'#watches .watch[data-expr="{e}"]', "data-state")
        r["watch errors"][e] = got[:100]
        if state != "error" or got != f"{e}: {want}":
            problems.append(f"{name}: watch {e}: {state} {got!r}, want the error {want!r}")
        page.locator(f'#watches .watch[data-expr="{e}"] .wdel').click()
    # A struct opens with its members; a pointer opens the Page tab at its target.
    page.locator('#watches .watch[data-expr="init_task.comm"] .wdel').click()
    watch(page, "init_task")
    page.locator('#watches .watch[data-expr="init_task"] summary').first.click()
    wait_for(lambda: page.locator('#watches .watch[data-expr="init_task"] .wkids').count() > 0, 10)
    r["init_task_members"] = page.locator('#watches .watch[data-expr="init_task"] .wkids > * .wname').count()
    kids = page.inner_text('#watches .watch[data-expr="init_task"] .wkids')
    if ".comm" not in kids or '"swapper"' not in kids:
        problems.append(f"{name}: init_task opened without .comm = \"swapper\": {kids[:300]!r}")
    page.screenshot(path=str(shots / f"vars-debugger-{name}-1280-light.png"), full_page=True)
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"vars-debugger-{name}-1280-dark.png"), full_page=True)
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger with variables scrolls horizontally at 390 px ({scheme})")
        page.locator("#watches").scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"vars-debugger-{name}-390-{scheme}.png"))
    page.set_viewport_size({"width": 1280, "height": 1000})
    it = next(v for v in T["vars"] if v[0] == "init_task")
    ptr = page.locator('#watches .watch[data-expr="init_task"] .wptr').first
    r["pointer"] = ptr.inner_text() if ptr.count() else None
    if ptr.count():
        target = int(ptr.inner_text().split()[0], 16) - off
        ptr.click()
        wait_for(lambda: page.get_attribute("#listing", "data-page") == str(target // 4096), 15)
        if page.get_attribute("#listing", "data-page") != str(target // 4096):
            problems.append(f"{name}: tapping pointer {r['pointer']!r} did not open page {target // 4096:#x}")
    else:
        problems.append(f"{name}: init_task has no pointer member to follow")
    # The Page listing: the label line of init_task and bytes inside variables.
    show_tab(page, "memory")
    open_page(page, it[1] // 4096)
    wait_for(lambda: page.locator('#listing .lbl[data-sym="init_task"][data-type]').count() > 0, 30)
    r["label_type"] = page.get_attribute('#listing .lbl[data-sym="init_task"]', "data-type")
    if r["label_type"] != "struct task_struct":
        problems.append(f"{name}: init_task's label line has type {r['label_type']!r}")
    rnd = __import__("random").Random(7)
    near = [v for v in T["vars"] if v[3] > 0 and abs(v[1] - it[1]) < 3 * 4096 and not v[5] & 4]
    picks = [it[1] + 16 + it[3] // 3] + [v[1] + rnd.randrange(v[3]) for v in rnd.sample(near, min(24, len(near)))]
    got = []
    for pa in picks:
        if pa // 4096 != int(page.get_attribute("#listing", "data-page")):
            open_page(page, pa // 4096)
        # Repeated rows are collapsed to "*": their bytes have no line to point at.
        if page.evaluate("(pa) => [...document.querySelectorAll('#listing .hex')].some((l) => pa >= +l.dataset.addr && pa < +l.dataset.end)", pa):
            got.append((pa, hover_byte(page, pa), page.inner_text("#var_info")))
    r["hover_first"] = got[0][2] if got else None
    r["hovered"] = f"{len(got)} of {len(picks)} (the others in collapsed rows)"
    if len(got) < 10:
        problems.append(f"{name}: only {len(got)} bytes to point at in the listing")
    named = [(pa, p) for pa, p, _ in got if p]
    if len(named) < len(got):
        problems.append(f"{name}: bytes inside variables named no variable: {[(hex(a), t) for a, p, t in got if not p][:3]}")
    if elf and shutil.which("gdb"):
        from tests.dwarf.types import gdb_eval
        def start(pa, var):
            return next(v[1] for v in T["vars"] if v[0] == var and v[1] <= pa < v[1] + max(v[3], 1))
        res = gdb_eval(str(elf), [[re.match(r"\w+", p)[0], "", p, start(pa, re.match(r"\w+", p)[0]) + off] for pa, p in named])
        bad = [(hex(pa), p, g) for (pa, p), g in zip(named, res) if g[0] in ("error", None) or not g[0] <= pa + off < g[0] + max(g[1], 1)]
        r["hover_vs_gdb"] = f"{len(named) - len(bad)}/{len(named)} paths hold the byte in gdb"
        if bad:
            problems.append(f"{name}: hovered paths gdb places elsewhere: {bad[:3]}")
    # A tap pins the description and offers to watch the path.
    if named and named[0][0] // 4096 != int(page.get_attribute("#listing", "data-page")):
        open_page(page, named[0][0] // 4096)
    path = hover_byte(page, named[0][0], click=True) if named else None
    # Just below the sticky header and tabs.
    page.evaluate("""() => { const tabs = document.querySelector('[role=tablist]').getBoundingClientRect().bottom;
        window.scrollBy(0, document.getElementById('var_info').getBoundingClientRect().top - tabs - 40); }""")
    page.screenshot(path=str(shots / f"vars-page-{name}-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"vars-page-{name}-1280-dark.png"))
    page.emulate_media(color_scheme="light")
    if page.locator("#var_watch").count():
        page.click("#var_watch")
        wait_for(lambda: page.locator(f'#watches .watch[data-expr="{path}"]').count() > 0, 10)
    r["tap_watch"] = page.inner_text(f'#watches .watch[data-expr="{path}"]') if page.locator(f'#watches .watch[data-expr="{path}"]').count() else None
    if not r["tap_watch"] or page.get_attribute(f'#watches .watch[data-expr="{path}"]', "data-state") != "ok":
        problems.append(f"{name}: tapping a byte did not offer a working watch of {path!r}")
    vardecl_checks(page, name, T, off, elf, shots, problems, r)
    vars_edges(page, name, T, off, elf, shots, problems, r)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def types_import_case(browser, base, shots):
    """Variables of an imported unstripped vmlinux, read by a second Worker in
    one pass over every compile unit: the same data as the shipped types.json
    (clang forms: the minified Linux; GCC forms: Debian's -dbg vmlinux), kept
    in IndexedDB across a reload."""
    problems, r = [], {}
    same_js = """async (url) => { const t = await (await fetch(url)).text();
        const T = MKB.DBG.types; return t === JSON.stringify({ format: T.format, types: T.types.slice(0, T.types.length - (T.sub ? T.sub.size : 0)),
            vars: T.vars, sections: T.sections, files: T.files }); }"""
    for name, size in (("minified-linux", None), ("debian-6.1-686", None)):
        elf = debug_elf(name)
        if not elf:
            if os.environ.get("CI"):
                problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
            continue
        entry = kernel_entries()[name]
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)))
        url = f"{base}/index.html?kernel={name}&paused=1#debugger"
        page.goto(url)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.evaluate("MKB.startPaused = true")
        page.set_input_files("#symfile", str(elf))
        wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 120)
        t0 = time.monotonic()
        show_tab(page, "debugger")
        wait_for(lambda: page.get_attribute("#watches", "data-state") in ("ok", "none"), 300, 0.5)
        r[f"{name}_seconds"] = round(time.monotonic() - t0, 1)
        r[f"{name}_status"] = page.inner_text("#watch_status")
        if page.get_attribute("#watches", "data-state") != "ok":
            problems.append(f"{name} import: variables not read: {r[f'{name}_status']!r}")
            page.close()
            continue
        if not page.evaluate(same_js, entry["dwarf"] + "types.json"):
            problems.append(f"{name} import: the Worker's variables differ from the shipped types.json")
        # Paused at the reset vector: the values are not the kernel's yet, and the panel says so.
        if page.get_attribute("#watches", "data-started") != "no" or "has not run yet" not in r[f"{name}_status"]:
            problems.append(f"{name} import: before the kernel runs the Variables do not say so: {r[f'{name}_status']!r}")
        if name == "minified-linux":
            sk = next(s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"] if s["name"] == "start_kernel")
            page.fill("#dbg_addr", hex(sk["vaddr"]))
            page.click("#dbg_runto")
            wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120)
            r[f"{name}_comm"] = watch(page, "init_task.comm")
            if '"swapper"' not in r[f"{name}_comm"] or page.get_attribute("#watches", "data-started") == "no":
                problems.append(f"{name} import: init_task.comm at start_kernel shows {r[f'{name}_comm']!r}")
            page.screenshot(path=str(shots / "vars-import-1280.png"), full_page=True)
            page.goto("about:blank")
            page.goto(url)
            wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
            show_tab(page, "debugger")
            wait_for(lambda: page.get_attribute("#watches", "data-state") == "ok", 30)
            r["reload_status"] = page.inner_text("#watch_status")
            if page.get_attribute("#watches", "data-state") != "ok" or not page.evaluate(same_js, entry["dwarf"] + "types.json"):
                problems.append(f"after a reload the imported variables are gone: {r['reload_status']!r}")
        show_tab(page, "machine")
        page.click("#sym_forget")
        page.close()
        if errors:
            problems.append(f"{name} import page errors: " + "; ".join(errors))
    return problems, r


def same_path(a, b):
    """A path from gdb or llvm (maybe with the build directory in front) and the page's tree-relative one."""
    return bool(a and b) and (a == b or a.endswith("/" + b))


def suite_types(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "debian-6.1-686") if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        failed |= report("types", *types_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed | report("types_import", *types_import_case(ctx.browser, ctx.base, ctx.shots))
