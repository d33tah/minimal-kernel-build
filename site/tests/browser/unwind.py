"""Browser test suite unwind (test_browser.py --suites)."""

import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import time

from tests.dwarf.operands import write_core
from tests.browser.common import (
    debug_elf, kernel_entries, LOCALS_DUMP_JS, locals_open_all, report, show_tab, SITE, UNWIND_DUMP_JS, wait_for,
)


# Where the unwind suite pauses: (kernel, function to Run to, Steps after it).
# Deep boot code (CFI only for the minified Linux, CFI + EBP chain for
# Debian's GCC build) and timer interrupts (entry code's pt_regs).
UNWIND_POINTS = [
    ("minified-linux", "memblock_add_range", (0, 9)),
    ("minified-linux", "tty_write", (0, 5)),
    ("debian-6.1-686", "timer_interrupt", (0, 7)),
]


# Runs inside gdb on a core: for each frame (inlined calls are frames of
# their own) its function and the value of every parameter and local of the
# blocks around its pc: the unsigned integer of a scalar, "out" (optimized
# out), "agg" (no scalar) or "err:..." (memory not in the core).
GDB_LOCALS = r"""
import gdb, json
def scalar(v):
    if v.is_optimized_out:
        return "out"
    t = v.type.strip_typedefs()
    if t.code not in (gdb.TYPE_CODE_INT, gdb.TYPE_CODE_PTR, gdb.TYPE_CODE_ENUM, gdb.TYPE_CODE_BOOL, gdb.TYPE_CODE_CHAR):
        return "agg"
    return str(int(v) & ((1 << (8 * t.sizeof)) - 1))
def enumerator(s):
    t = s.type.strip_typedefs() if s.type is not None else None
    return t is not None and t.code == gdb.TYPE_CODE_ENUM and any(x.name == s.name for x in t.fields())
out, f = [], gdb.newest_frame()
while f is not None and len(out) < NFRAMES:
    fn = f.function()
    fr = {"func": fn.name if fn else None, "inlined": f.type() == gdb.INLINE_FRAME, "pc": int(f.pc()), "vars": []}
    try:
        b = f.block()
    except RuntimeError:
        b = None
    while b is not None:
        for s in b:
            if not (s.is_argument or s.is_variable or (s.is_constant and not enumerator(s))) or s.addr_class == gdb.SYMBOL_LOC_LABEL:
                continue
            if s.type is not None and str(s.type) == "__CORE_ADDR":
                continue
            try:
                val = scalar(s.value(f))
            except gdb.error as e:
                val = "out" if "optimized out" in str(e) else "err:" + str(e)
            fr["vars"].append([s.name, val])
        if b.function is not None:
            break
        b = b.superblock
    out.append(fr)
    try:
        f = f.older()
    except gdb.error:
        break
json.dump(out, open(DST, "w"))
"""


def gdb_locals(elf, regs, pages, nframes):
    """gdb's frames (inlined ones included) with their variables' values,
    from a core of the registers and stack pages; None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "locals.py", "locals.json"))
        write_core(core, regs, pages)
        pathlib.Path(py).write_text(f"DST, NFRAMES = {dst!r}, {nframes}\n" + GDB_LOCALS)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=300)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else []


def locals_compare(elf, locs, udump):
    """The page's parameters and locals (locs: LOCALS_DUMP_JS) against
    gdb's on a core of the same registers and stack pages (udump:
    UNWIND_DUMP_JS), over the frames from the CPU's registers down to the
    first one not found by CFI (gdb unwinds those the same way, see
    unwind_compare). Each inlined call and function must have the same
    variables; a scalar both read must be equal, and one gdb finds
    optimized out the page must not show. Returns (values compared, values
    only gdb has, problems) or None without gdb."""
    fr = udump["frames"]
    n = 1
    while n < len(fr) and fr[n]["method"] == "cfi":
        n += 1
    # A frame without a DWARF function (assembly) is one gdb frame without variables.
    page = [sc for d in locs[:n] for sc in (d["scopes"] or [{"func": None, "vars": []}])]
    regs = {**udump["regs"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b}
    want = gdb_locals(elf, regs, udump["pages"], len(page))
    if want is None:
        return None
    same, more, bad = 0, 0, []
    for i, sc in enumerate(page):
        if i >= len(want):
            bad.append(f"frame {i} ({sc['func']}): gdb has no such frame")
            break
        g = want[i]
        if g["func"] != sc["func"]:
            bad.append(f"frame {i}: page {sc['func']}, gdb {g['func']}")
            continue
        if sorted(v["name"] for v in sc["vars"]) != sorted(x[0] for x in g["vars"]):
            # gdb also lists, as optimized out, the variables of a block whose
            # concrete copy has no DW_AT_abstract_origin, out of scope or
            # twice (test_dwarf.py gdb_extra_explained, with llvm-dwarfdump).
            from tests.dwarf.locals import gdb_extra_explained
            at = g["pc"] - (1 if g["pc"] != want[0]["pc"] else 0)  # a caller's block: the call, before the return address
            mine = [(v["name"], "out" if v["state"] == "out" else "x") for v in sc["vars"]]
            theirs = [(n, "out" if val == "out" else "x") for n, val in g["vars"]]
            if not gdb_extra_explained(str(elf), at, theirs, mine, sc["func"]):
                bad.append(f"frame {i} ({sc['func']}) at {at:#x}: variables page {sorted(mine)} gdb {sorted(theirs)}")
                continue
            g = {**g, "vars": [x for x in g["vars"] if x[0] in {v["name"] for v in sc["vars"]}]}
        gv = {}
        for name, val in g["vars"]:
            gv.setdefault(name, []).append(val)
        pv = {}
        for v in sc["vars"]:
            pv.setdefault(v["name"], []).append(v)
        for name, mine in pv.items():
            # Same-named variables of nested blocks: compare as sets of values.
            theirs = gv[name]
            for v in mine:
                nums = [x for x in theirs if x not in ("out", "agg") and not x.startswith("err:")]
                if v["raw"] is not None:
                    if v["raw"] in nums:
                        same += 1
                    elif any(x.startswith("err:") or x == "agg" for x in theirs) and not nums:
                        pass  # gdb could not read it from the core (memory outside the stack pages)
                    elif all(x == "out" for x in theirs):
                        bad.append(f"frame {i} ({sc['func']}): {name} = {v['raw']} on the page, optimized out for gdb")
                    else:
                        bad.append(f"frame {i} ({sc['func']}): {name} = {v['raw']} ({v['loc']}) on the page, gdb {theirs}")
                elif v["state"] == "out" and nums and len(mine) == 1:
                    more += 1
    return same, more, bad


def locals_check(page, elf, dump, r, tag):
    """The Parameters and locals section at a pause: one frame per unwound
    frame, the innermost with variables, every variable either with a
    value and its location or saying why not, and with the -g ELF the
    values equal gdb's on a core of the same registers and stack."""
    problems = []
    locs = locals_open_all(page)
    unwound = [f for f in dump["frames"] if not f.get("user") and f["method"] != "recorded"]
    if len(locs) != len(unwound):
        problems.append(f"{len(locs)} frames in Parameters and locals for {len(unwound)} unwound frames")
    nv = sum(len(sc["vars"]) for d in locs for sc in d["scopes"])
    shown = sum(1 for d in locs for sc in d["scopes"] for v in sc["vars"] if v["state"] not in ("out", "unmapped"))
    r[tag + " locals"] = f"{len(locs)} frames, {nv} variables, {shown} with a value; frame 0: " + "; ".join(
        f"{sc['func']}: " + ", ".join(f"{v['name']}={v['value']}" for v in sc["vars"]) for sc in (locs[0]["scopes"] if locs else []))
    if not locs or locs[0]["state"] != "ok" or not any(d["scopes"] for d in locs):
        problems.append(f"no parameters and locals in any frame: {locs[:2]}")
    for d in locs:
        for sc in d["scopes"]:
            for v in sc["vars"]:
                if not v["loc"].strip() or (v["state"] in ("out", "unmapped") and v["value"]):
                    problems.append(f"{sc['func']}.{v['name']}: no location or reason shown ({v})")
    if elf:
        res = locals_compare(elf, locs, dump)
        if res is not None:
            same, more, bad = res
            r[tag + " locals vs gdb"] = f"{same} values equal, {more} only gdb has (entry values it resolves), {len(bad)} differ"
            r["locals_equal"] = r.get("locals_equal", 0) + same
            problems += bad
    return problems


# Runs inside gdb: the physical frames (inlined ones folded into the
# function they are in) from the core's registers: [pc, CFA or None].
GDB_BT = r"""
import gdb, json
out, f = [], gdb.newest_frame()
while f is not None and len(out) < 64:
    if f.type() != gdb.INLINE_FRAME:
        out.append(int(f.pc()))
    try:
        f = f.older()
    except gdb.error:
        break
json.dump(out, open(DST, "w"))
"""


def gdb_backtrace(elf, regs, pages):
    """gdb's backtrace (return addresses, innermost first) of a core made of
    the registers and stack pages, or None without gdb."""
    if not elf or not shutil.which("gdb"):
        return None
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "bt.py", "bt.json"))
        write_core(core, regs, pages)
        pathlib.Path(py).write_text(f"DST = {dst!r}\n" + GDB_BT)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=120)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else []


def unwind_compare(elf, dump, offset):
    """The page's unwound frames against gdb, segment by segment: from the
    CPU's registers, and from the registers of each frame found another
    way (pt_regs: gdb does not cross entry code; entry ESP, EBP chain).
    Returns (frames compared, problems)."""
    fr, n, bad = dump["frames"], 0, []
    # gdb starts again at each frame not found by CFI (its registers as the page has them).
    seg = [i for i, f in enumerate(fr) if f["method"] in ("regs", "pt_regs", "entry", "fp") and not f.get("user")]
    for k, i in enumerate(seg):
        end = seg[k + 1] if k + 1 < len(seg) else len(fr)
        # Only the frames CFI found: gdb's own fallbacks (prologue analysis) are not the page's.
        part = [fr[i]]
        for f in fr[i + 1:end]:
            if f["method"] != "cfi":
                break
            part.append(f)
        names = {0: "eax", 1: "ecx", 2: "edx", 3: "ebx", 4: "esp", 5: "ebp", 6: "esi", 7: "edi"}
        regs = {names[int(q)]: v for q, v in (fr[i]["regs"] or {}).items() if v is not None}
        regs.update({"eip": fr[i]["va"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b})
        want = gdb_backtrace(elf, regs, dump["pages"])
        if want is None:
            return 0, []
        # va: EIP, or a caller's return address (gdb's pc of that frame).
        got = [f["va"] for f in part]
        if want[:len(got)] != got:
            bad.append(f"from frame {i} ({fr[i]['name']}): page {[hex(x) for x in got]} gdb {[hex(x) for x in want[:len(got) + 2]]}")
        n += len(got)
    return n, bad


def unwind_case(browser, base, shots, name, fn, steps):
    """The Debugger's call stack is unwound with the DWARF call frame
    information at each pause: every frame names its method, the frames
    CFI and pt_regs found equal gdb's backtrace of a core made of the same
    registers and stack, the unwind reaches the stack's start (or says why
    not), and the CFI shards fetched are only the ones the frames are in."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    offset = sk["vaddr"] - sk["paddr"]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    cfi = set()
    page.on("response", lambda resp: ("/dwarf/cfi-" in resp.url or "/dwarf/locals-" in resp.url) and
            cfi.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    t0 = time.monotonic()
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]) or "Halted" in page.inner_text("#dbg_status"), 240) \
            or page.get_attribute("#regs", "data-phys") != str(sk["paddr"]):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    r["runto_s"] = round(time.monotonic() - t0)
    elf = debug_elf(name)
    for n in steps:
        if n:
            before = page.evaluate("MKB.stepper.steps")
            page.fill("#dbg_n", str(n))
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("MKB.stepper.steps") - before >= n and
                     page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
        dump = page.evaluate(UNWIND_DUMP_JS)
        fr = dump["frames"]
        tag = f"{fn}+{n}"
        r[tag] = " <- ".join(f"{f['name']}[{f['method']}]" for f in fr) + f" ({dump['unwind']}: {dump['why']})"
        if dump["unwind"] not in ("ok", "stopped"):
            problems.append(f"{name} {tag}: unwind state {dump['unwind']}: {dump['why']}")
        methods = [f["method"] for f in fr]
        if not fr or methods[0] != "regs" or any(m not in ("regs", "cfi", "pt_regs", "entry", "fp", "recorded") for m in methods):
            problems.append(f"{name} {tag}: frames without a known method: {methods}")
        if sum(1 for m in methods if m == "cfi") < 2:
            problems.append(f"{name} {tag}: fewer than 2 frames from CFI: {r[tag]}")
        if fn in ("timer_interrupt", "tty_write"):
            # Kernel code an interrupt interrupted, or user space that made the system call.
            if "pt_regs" not in methods or (fn == "tty_write") != bool(fr[-1].get("user")):
                problems.append(f"{name} {tag}: no frame from the entry code's pt_regs (or the wrong kind): {r[tag]}")
            if dump["unwind"] != "ok":
                problems.append(f"{name} {tag}: the unwind did not reach the start of the stack: {r[tag]}")
        if dump["unwind"] == "stopped" and not dump["why"]:
            problems.append(f"{name} {tag}: the unwind stopped without saying why")
        if fr and fr[-1].get("user"):
            # The "user space" frame opens nothing (a disabled button): it must not be faded.
            op = page.evaluate("getComputedStyle([...document.querySelectorAll('#call_stack button')].pop()).opacity")
            if op != "1":
                problems.append(f"{name} {tag}: the user space frame is faded (opacity {op})")
        labels = page.eval_on_selector_all("#call_stack .fmeth", "els => els.map(e => e.textContent)")
        if len(labels) != len(fr):
            problems.append(f"{name} {tag}: {len(labels)} method labels for {len(fr)} frames")
        if elf:
            k, bad = unwind_compare(elf, dump, offset)
            r[tag + " vs gdb"] = f"{k} frames, {len(bad)} segments differ"
            problems += [f"{name} {tag}: {b}" for b in bad]
        problems += [f"{name} {tag}: {x}" for x in locals_check(page, elf, dump, r, tag)]
        r["locals"] = page.evaluate(LOCALS_DUMP_JS)
    if elf and shutil.which("gdb") and not r.get("locals_equal"):
        problems.append(f"{name}: no parameter or local value compared with gdb")
    r["frames"] = fr
    r["cfi_shards"] = sorted(x for x in cfi if x.startswith("cfi-"))
    r["locals_shards"] = sorted(x for x in cfi if x.startswith("locals-"))
    if not r["cfi_shards"]:
        problems.append(f"{name}: no CFI shard was fetched")
    if not r["locals_shards"]:
        problems.append(f"{name}: no locals shard was fetched")
    page.screenshot(path=str(shots / f"unwind-{name}-{fn}-1280-light.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"unwind-{name}-{fn}-390-{scheme}.png"), full_page=True)
    page.emulate_media(color_scheme="dark")
    page.set_viewport_size({"width": 1280, "height": 1000})
    page.screenshot(path=str(shots / f"unwind-{name}-{fn}-1280-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.locator("#locals").screenshot(path=str(shots / f"locals-{name}-{fn}-1280-{scheme}.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def unwind_import_case(browser, base, shots, want):
    """An imported -g vmlinux: the Worker parses its .debug_frame and the
    call stack at the same pause equals the one from the shipped shards
    (want: frames and Parameters and locals of unwind_case at
    memblock_add_range+9), and so do the parameters and locals the
    Worker reads per compile unit; after a reload the call frame
    information and the units' locals come back from IndexedDB."""
    problems, r = [], {}
    want, want_locals = want if want else (None, None)
    elf = debug_elf("minified-linux")
    if not elf or not want:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf or no shipped frames"]), {"skipped": "no -g ELF"}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "memblock_add_range")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel=minified-linux&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return ["imported -g vmlinux: Run to memblock_add_range did not stop there"], r
    before = page.evaluate("MKB.stepper.steps")
    page.fill("#dbg_n", "9")
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("MKB.stepper.steps") - before >= 9 and page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    got = json.loads(page.get_attribute("#call_stack", "data-frames"))
    # pc: the call instruction with the build's disassembly, return address - 1 without (an import has none).
    key = lambda fr: [(f["name"], f["va"], f["method"], f.get("cfa")) for f in fr]
    r["import"] = " <- ".join(f"{f['name']}[{f['method']}]" for f in got)
    if key(got) != key(want):
        problems.append(f"imported vmlinux: call stack {key(got)} differs from the shipped CFI's {key(want)}")
    r["title"] = page.get_attribute("#call_stack", "title")
    if "imported vmlinux" not in (r["title"] or ""):
        problems.append(f"imported vmlinux: the call stack does not say where its CFI is from: {r['title']!r}")
    page.screenshot(path=str(shots / "unwind-import-1280.png"))
    # The variables pass of the Worker first (dbgTypes), then each frame's unit.
    wait_for(lambda: page.evaluate("MKB.dbgTypes().state") != "loading", 120)
    locs = locals_open_all(page, 60)
    r["locals"] = sum(len(sc["vars"]) for d in locs for sc in d["scopes"])
    if locs != want_locals:
        problems.append(f"imported vmlinux: Parameters and locals differ from the shipped shards': {locs} vs {want_locals}")
    if not page.evaluate("document.getElementById('locals').textContent.length > 0") or not r["locals"]:
        problems.append("imported vmlinux: no parameters and locals")
    names_js = """(pas) => pas.map((pa) => { const x = MKB.dbgLocals(pa);
        return x.state === 'ok' ? x.frames.map((f) => [f.func, f.params.map((v) => v[0]), f.locals.map((v) => v[0])]) : x.state; })"""
    rows_js = """async (pas) => { const out = [];
        for (const pa of pas) { let x = MKB.dbgCfiRow(pa);
            for (let i = 0; i < 50 && x === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); x = MKB.dbgCfiRow(pa); }
            out.push(x && x.cfa ? MKB.cfiRowText(x) : x); }
        return out; }"""
    pas = [f["pc"] for f in got]
    before = page.evaluate(rows_js, pas)
    lbefore = page.evaluate(names_js, pas)
    # Reload: rows from IndexedDB, without the file.
    page.goto("about:blank")
    page.goto(url)
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
    rows = page.evaluate(rows_js, pas)
    r["after_reload"] = rows
    wait_for(lambda: page.evaluate("MKB.dbgTypes().state") == "ok", 30)
    lafter = page.evaluate(names_js, pas)
    if lafter != lbefore:
        problems.append(f"after a reload: parameters and locals {lafter}, before {lbefore}")
    if rows != before or sum(1 for x in rows if isinstance(x, str) and x.startswith("CFA=")) < len(rows) - 1:
        problems.append(f"after a reload: CFI rows {rows}, before {before}")
    show_tab(page, "machine")
    page.click("#sym_forget")
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") != "imported", 30)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_unwind(ctx):
    failed, want = False, None
    for name, fn, steps in UNWIND_POINTS:
        if name in ctx.entries and "dwarf" in ctx.entries[name]:
            t0 = time.monotonic()
            p, r = unwind_case(ctx.browser, ctx.base, ctx.shots, name, fn, steps)
            frames = r.pop("frames", None)
            locs = r.pop("locals", None)
            if fn == "memblock_add_range":
                want = (frames, locs)
            failed |= report("unwind", p, r, kernel=name, at=fn, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = unwind_import_case(ctx.browser, ctx.base, ctx.shots, want)
    return failed | report("unwind_import", p, r, seconds=round(time.monotonic() - t0))
