"""Browser test suite branches (test_browser.py --suites)."""

import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import debug_elf, kernel_entries, open_page, report, show_tab, SITE, wait_for


# Branch targets in the listings: per kernel, where the case pauses (Run to)
# and the extra pages it reads (symbols whose page is opened; the audit's
# Debian sample page 0x1d8d000 is setup_arch's).
BRANCH_POINTS = {
    "minified-linux": ("start_kernel", []),
    "debian-6.1-686": ("start_kernel", ["setup_arch"]),
}

# The listing lines of a container: address, text, and the branch-target
# names (.brt) in them.
BRANCHES_JS = """(sel) => [...document.querySelectorAll(sel + ' .ins')].map((l) => ({
    pa: +l.dataset.addr, text: l.textContent,
    brt: [...l.querySelectorAll('.brt')].map((b) => ({ name: b.dataset.name, va: +b.dataset.va, pa: +b.dataset.pa,
        text: b.textContent })) }))"""

# A direct branch as llvm-objdump and iced-x86 print it (prefixes, the
# mnemonic, an absolute target), and the `<name+0xoff>` after it.
BRANCH_RE = re.compile(r"^(?:(?:lock|rep\w*|notrack|bnd|[cdefgs]s|data16|data32|addr16|addr32)\s+)*"
                       r"(?:j[a-z]+|call[lwq]?|loop[a-z]*|xbegin[lw]?)\s+(0x[0-9a-fA-F]+)(?:\s+<([^<>]+)>)?(?:\s|$)")

# Runs inside gdb on the -g ELF: `info symbol` of ADDRS, `ptype` of NAMES.
GDB_BRANCHES = r"""
import gdb, json
def run(cmd):
    try:
        return gdb.execute(cmd, to_string=True).strip()
    except Exception as e:
        return "error: %s" % e
json.dump([[run("info symbol %d" % a) for a in ADDRS], [run("ptype " + n) for n in NAMES]], open(DST, "w"))
"""


def gdb_branches(elf, addrs, names):
    with tempfile.TemporaryDirectory() as tmp:
        py, dst = os.path.join(tmp, "br.py"), os.path.join(tmp, "br.json")
        pathlib.Path(py).write_text(f"ADDRS, NAMES, DST = {addrs!r}, {names!r}, {dst!r}\n" + GDB_BRANCHES)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", "set print demangle off", "-ex", f"source {py}", str(elf)],
                       capture_output=True, text=True, timeout=600)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else None


def branch_lines(lines):
    """The direct branches of listing lines: [(pa, target va, `<name>` in the text or None, .brt spans)]."""
    out = []
    for l in lines:
        body = re.sub(r"^▶?\s*[0-9a-f]{8} +(?:[0-9a-f]{2})+…? *", "", l["text"].strip())
        m = BRANCH_RE.match(body)
        if m:
            out.append((l["pa"], int(m.group(1), 16), m.group(2), l["brt"]))
    return out


def code_symbol_at(syms, va):
    """The smallest sized symbol holding va, else a sizeless one exactly at
    va (the last listed, as llvm-objdump would also name it), or None."""
    best = None
    for s in syms:
        if s["size"] > 0 and s["vaddr"] <= va < s["vaddr"] + s["size"] and (not best or s["size"] < best["size"]):
            best = s
    return best or next((s for s in reversed(syms) if not s["size"] and s["vaddr"] == va), None)


def branches_vs_refs(syms, elf, lines, problems, r, tag):
    """Every direct branch whose target is inside a sized symbol (or at a
    sizeless one) carries `<name+0xoff>` (as llvm-objdump spells it) of
    that symbol, and gdb's `info symbol` there agrees (an alias at the
    same address counts); a target outside every symbol carries none;
    each name is one .brt span with the target's address."""
    br = branch_lines(lines)
    r[tag + " branches"] = len(br)
    want = {}
    for pa, va, name, spans in br:
        s = code_symbol_at(syms, va)
        exp = None if not s else s["name"] + (f"+{va - s['vaddr']:#x}" if va > s["vaddr"] else "")
        want[va] = (s, exp)
        if exp and name != exp:
            problems.append(f"{tag}: {pa:#x} branch to {va:#x} names <{name}>, want <{exp}>")
        elif not exp and name is not None:
            problems.append(f"{tag}: {pa:#x} branch to {va:#x} outside every symbol names <{name}>")
        if name is not None and [(x["name"], x["va"]) for x in spans] != [(name, va)]:
            problems.append(f"{tag}: {pa:#x} <{name}> is not one .brt span for {va:#x}: {spans}")
    named = sum(1 for _, _, n, _ in br if n)
    r[tag + " named"] = named
    if elf and shutil.which("gdb"):
        addrs = sorted(want)
        got = gdb_branches(elf, addrs, [])
        if got is None:
            problems.append(f"{tag}: gdb wrote nothing")
            return
        alias = same = outside = 0
        for va, g in zip(addrs, got[0]):
            s, exp = want[va]
            m = re.match(r"^(\S+)(?: \+ (\d+))? in section", g)
            if not s:
                if m is None:
                    outside += 1
                else:  # inside a sizeless symbol, past its start: not named (llvm-objdump would)
                    r.setdefault(tag + " targets in no sized symbol, gdb", []).append(f"{va:#x} {g}")
                continue
            off = va - s["vaddr"]
            if not m or int(m.group(2) or 0) != off and m.group(1) == s["name"]:
                problems.append(f"{tag}: target {va:#x} <{exp}>, gdb: {g}")
            elif m.group(1) == s["name"] and int(m.group(2) or 0) == off:
                same += 1
            elif any(x["name"] == m.group(1) and x["vaddr"] == s["vaddr"] for x in syms) and int(m.group(2) or 0) == off:
                alias += 1
            else:
                problems.append(f"{tag}: target {va:#x} <{exp}>, gdb: {g}")
        r[tag + " targets equal to gdb"] = same
        r[tag + " targets gdb names by an alias"] = alias
        r[tag + " targets outside every symbol (gdb too)"] = outside
    elif os.environ.get("CI"):
        problems.append(f"{tag}: no -g ELF or no gdb")


def callee_tooltips(page, sel, elf, problems, r, tag, n=4):
    """Hover the names of n distinct called functions (C functions with a
    prototype for gdb): the tooltip (dbgTitle, the function facts) shows
    the callee's prototype, which matches gdb's `ptype` (return type,
    then the parameter types in order), and where it is defined."""
    loc = page.locator(f"{sel} .brt")
    names = page.evaluate("""(sel) => [...document.querySelectorAll(sel + ' .brt')].map((b) =>
        /\\bcall/.test(b.parentElement.textContent) && !b.dataset.name.includes('+') ? b.dataset.name : '')""", sel)
    cands = list(dict.fromkeys(x for x in names if x))[:16]
    gdb = elf and shutil.which("gdb")
    ptypes = gdb_branches(elf, [], cands)[1] if gdb and cands else [None] * len(cands)
    picks = [(c, pt) for c, pt in zip(cands, ptypes)
             if pt is None or re.match(r"^type = [^(]*\(.*\)$", pt, re.S) and "(*" not in pt][:n]
    if not picks:
        problems.append(f"{tag}: no call to a C function to hover ({cands})")
        return
    checked = []
    for name, pt in picks:
        el = loc.nth(names.index(name))
        # The listing is redrawn when debug info it uses arrives (a locals or
        # facts shard): the locator finds the new line on the next try.
        for _ in range(5):
            try:
                el.scroll_into_view_if_needed(timeout=5000)
                break
            except PlaywrightError:
                time.sleep(0.5)
        title = ""
        for _ in range(60):
            try:
                el.hover(timeout=5000)
                title = el.get_attribute("title") or ""
            except PlaywrightError:
                title = ""
            if "reading" not in title and "defined at" in title:
                break
            page.mouse.move(0, 0)
            time.sleep(0.25)
        if f"{name}(" not in title or "defined at" not in title:
            problems.append(f"{tag}: the tooltip of <{name}> has no prototype and facts: {title!r}")
            continue
        m = re.match(r"^type = (.*?)\s*\((.*)\)$", pt or "", re.S)
        if m:
            ret, args = m.group(1), [a.strip() for a in m.group(2).split(",")] if m.group(2) else []
            # gdb says (void) also for an unprototyped function (assembly), which the page writes ().
            rx = re.escape(ret) + r"\s*" + re.escape(name) + r"\(" + ("(?:void)?" if args == ["void"] else r",\s*".join(
                re.escape(a) + ("" if a == "..." else r"\s*\w*") for a in args)) + r"\)"
            if not re.search(rx, title):
                problems.append(f"{tag}: the tooltip of <{name}> does not show gdb's {pt!r}: {title.splitlines()[:3]}")
                continue
        checked.append(name)
    r[tag + " callee tooltips with gdb's prototype"] = checked


def branches_case(browser, base, shots, name):
    """Direct call/jmp/jcc targets inside the kernel are named in both
    listings (the Debugger's Code at EIP and the Page tab's merged hex +
    asm), also when the code is decoded live from guest memory (iced-x86:
    the Debian sample, every import, the "live decode" toggle): `<name+off>`
    of the sized symbol holding the target, as llvm-objdump spells it,
    equal to gdb's `info symbol`; the name's tooltip is the callee's
    facts (prototype = gdb's ptype); clicking it opens the target. The
    minified Linux: the live names equal disasm.json's (llvm-objdump)
    line for line. No horizontal scroll at 390 px."""
    problems, r = [], {}
    fn, extra = BRANCH_POINTS[name]
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    elf = debug_elf(name)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 240):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    if name == "minified-linux":
        # Live decode on: the same code from guest memory, named by the page.
        show_tab(page, "page")
        page.check("#live_decode")
        show_tab(page, "debugger")
    wait_for(lambda: page.get_attribute("#context", "data-source") == "live" and
             page.evaluate("document.querySelectorAll('#context .brt').length") > 0, 60)
    dbg = page.evaluate(BRANCHES_JS, "#context")
    branches_vs_refs(syms, elf, dbg, problems, r, f"{name} Debugger")
    page.emulate_media(color_scheme="light")
    page.locator("#context").screenshot(path=str(shots / f"branches-{name}-debugger-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    time.sleep(0.2)
    page.locator("#context").screenshot(path=str(shots / f"branches-{name}-debugger-1280-dark.png"))
    # The Page tab: the page at EIP and the extra pages, decoded live.
    page.click("#context .ins.cur")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(sk["paddr"] // 4096) and
             page.get_attribute("#listing", "data-source") == "live", 60)
    time.sleep(0.5)
    lst = page.evaluate(BRANCHES_JS, "#listing")
    by = {l["pa"]: l for l in lst}
    same = 0
    for l in dbg:
        o = by.get(l["pa"])
        if o is not None and o["brt"] != l["brt"]:
            problems.append(f"{name}: {l['pa']:#x} Page tab {o['brt']}, Debugger {l['brt']}")
        elif o is not None:
            same += 1
    r["page lines equal to the debugger's"] = same
    branches_vs_refs(syms, elf, lst, problems, r, f"{name} Page {sk['paddr'] // 4096:#x}")
    callee_tooltips(page, "#listing", elf, problems, r, f"{name} Page")
    if name == "minified-linux":
        # disasm.json's text (llvm-objdump) for the same page, line for line.
        page.uncheck("#live_decode")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "build", 10)
        built = {l["pa"]: l for l in page.evaluate(BRANCHES_JS, "#listing")}
        n = 0
        for pa, va, nm, spans in branch_lines(lst):
            b = built.get(pa)
            bb = branch_lines([b]) if b else []
            if not bb or bb[0][1] != va or bb[0][2] != nm:
                problems.append(f"{name}: {pa:#x} live <{nm}>, llvm-objdump {bb[0][2] if bb else None}")
            elif [(x["name"], x["va"]) for x in bb[0][3]] != [(x["name"], x["va"]) for x in spans]:
                problems.append(f"{name}: {pa:#x} disasm.json line has not the same .brt span: {bb[0][3]}")
            else:
                n += 1
        r["live names equal to llvm-objdump's"] = n
        callee_tooltips(page, "#listing", elf, problems, r, f"{name} Page (disasm.json)", n=1)
        page.check("#live_decode")
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
    for ex in extra:
        s = next(x for x in syms if x["name"] == ex)
        if not open_page(page, s["paddr"] // 4096):
            problems.append(f"{name}: could not open the page of {ex}")
            continue
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live" and
                 page.evaluate("document.querySelectorAll('#listing .brt').length") > 0, 30)
        branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#listing"), problems, r, f"{name} Page {s['paddr'] // 4096:#x}")
    # A name opens its target.
    first = page.locator("#listing .brt").first
    if not first.count():
        problems.append(f"{name}: no branch target name to click")
    else:
        tgt, nm = int(first.get_attribute("data-pa")), first.get_attribute("data-name")
        first.click()
        if not wait_for(lambda: page.get_attribute("#listing", "data-page") == str(tgt // 4096), 10):
            problems.append(f"{name}: clicking <{nm}> did not open page {tgt // 4096:#x}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        page.screenshot(path=str(shots / f"branches-{name}-page-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Page tab scrolls horizontally at 390 px ({scheme})")
        # The listing is drawn again as DWARF shards arrive (the file:line
        # of jump sources in other code): a line found can be replaced.
        for _ in range(5):
            try:
                if page.locator("#listing .brt").count():
                    page.locator("#listing .brt").first.scroll_into_view_if_needed(timeout=5000)
                break
            except PlaywrightError:
                time.sleep(0.3)
        page.screenshot(path=str(shots / f"branches-{name}-page-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def branches_import_case(browser, base, shots):
    """An imported -g vmlinux (the minified Linux's; no disasm.json, so
    every listing is decoded live): the branch names at start_kernel equal
    gdb's, and the BIOS (outside the kernel) gets none."""
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
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return ["import: Run to start_kernel did not stop there"], r
    wait_for(lambda: page.get_attribute("#context", "data-source") == "live" and
             page.evaluate("document.querySelectorAll('#context .brt').length") > 0, 60)
    branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#context"), problems, r, "import Debugger")
    page.click("#context .ins.cur")
    wait_for(lambda: page.get_attribute("#listing", "data-page") == str(sk["paddr"] // 4096) and
             page.get_attribute("#listing", "data-source") == "live", 60)
    time.sleep(0.5)
    branches_vs_refs(syms, elf, page.evaluate(BRANCHES_JS, "#listing"), problems, r, "import Page")
    callee_tooltips(page, "#listing", elf, problems, r, "import Page", n=2)
    # The BIOS (page 0xff, 16-bit, outside the kernel): branches, no names.
    if open_page(page, 0xff):
        wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
        bios = branch_lines(page.evaluate(BRANCHES_JS, "#listing"))
        r["BIOS branches"] = len(bios)
        bad = [(hex(pa), nm) for pa, _, nm, sp in bios if nm or sp]
        if not bios or bad:
            problems.append(f"import: BIOS page 0xff: {len(bios)} branches, named: {bad[:4]}")
    else:
        problems.append("import: could not open the BIOS page 0xff")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_branches(ctx):
    failed = False
    for name in [n for n in BRANCH_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = branches_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("branches", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = branches_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("branches_import", p, r, seconds=round(time.monotonic() - t0))
