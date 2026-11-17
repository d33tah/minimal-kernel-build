"""Browser test suite operands (test_browser.py --suites)."""

import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import time

from tests.dwarf.operands import write_core
from tests.browser.common import debug_elf, kernel_entries, report, SITE, UNWIND_DUMP_JS, wait_for


# Pauses where the instruction at EIP reads or writes a parameter or local
# on the stack: (kernel, function, offset, the variable the operand is).
OPERAND_POINTS = [
    ("minified-linux", "memblock_add_range", 0xa, "type"),  # -O0: movl %ecx, 0x18(%esp): type into its slot
    ("minified-linux-os", "memblock_add_range", 0x2b, "type"),  # -Os: movl (%esp), %eax: clang spills type at ESP+0
    ("debian-6.1-686", "start_kernel", 0x20, "command_line"),  # mov %ebx, -0x1c(%ebp): GCC, DW_OP_fbreg -36
]

# The memory operand names of the listing lines in a container: per
# instruction its address, text and names.
OPNAMES_JS = """(sel) => [...document.querySelectorAll(sel + ' .ins')].map((l) => ({
    pa: +l.dataset.addr, cur: l.classList.contains('cur'), text: l.textContent,
    ops: [...l.querySelectorAll('.opn')].map((o) => ({ op: o.dataset.op, name: o.dataset.name, kind: o.dataset.kind,
        va: o.dataset.va === undefined ? null : +o.dataset.va, title: o.title })) }))"""

# Runs inside gdb on a core of the pause: `info symbol` of ADDRS, and for
# each path of PATHS its [address, size] in the first frame (innermost,
# inlined calls included) where it is an lvalue, or None.
GDB_OPNAMES = r"""
import gdb, json
syms = []
for a in ADDRS:
    try:
        syms.append(gdb.execute("info symbol %d" % a, to_string=True).strip())
    except Exception as e:
        syms.append("error: %s" % e)
frames, f = [], gdb.newest_frame()
while f is not None and len(frames) < 8:
    frames.append(f)
    if f.type() != gdb.INLINE_FRAME:
        break
    f = f.older()
at = []
for path in PATHS:
    got = None
    for f in frames:
        try:
            f.select()
            v = gdb.parse_and_eval(path)
            if v.address is not None:
                got = [int(v.address), v.type.sizeof]
                break
        except gdb.error:
            pass
    at.append(got)
json.dump([syms, at], open(DST, "w"))
"""


def gdb_opnames(elf, dump, addrs, paths):
    with tempfile.TemporaryDirectory() as tmp:
        core, py, dst = (os.path.join(tmp, n) for n in ("core", "ops.py", "ops.json"))
        regs = {**dump["regs"], "cs": 0x60, "eflags": 0x202, "ss": 0x68, "ds": 0x7b, "es": 0x7b}
        write_core(core, regs, dump["pages"])
        pathlib.Path(py).write_text(f"ADDRS, PATHS, DST = {addrs!r}, {paths!r}, {dst!r}\n" + GDB_OPNAMES)
        subprocess.run(["gdb", "-nx", "-batch", "-ex", "set pagination off", "-ex", f"source {py}", str(elf), core],
                       capture_output=True, text=True, timeout=300)
        return json.loads(pathlib.Path(dst).read_text()) if os.path.exists(dst) else None


def opnames_vs_gdb(elf, dump, lines, problems, r, tag):
    """The names in listing lines against gdb on a core of the pause: a
    global (DWARF variable, per-CPU template or ELF symbol) is what `info
    symbol` says at its address; a parameter or local with an address
    (the operand at EIP, stack slots of EIP's frame) is where gdb puts
    that path in EIP's frame. Returns the number of names compared."""
    glob = [o for l in lines for o in l["ops"] if o["kind"] in ("var", "percpu", "sym") and o["va"] is not None]
    loc = [o for l in lines for o in l["ops"] if o["kind"] in ("param", "local") and o["va"] is not None]
    paths = sorted({re.sub(r"\+\d+$", "", o["name"].lstrip("&")) for o in loc})
    got = gdb_opnames(elf, dump, sorted({o["va"] for o in glob}), paths)
    if got is None:
        problems.append(f"{tag}: gdb wrote nothing")
        return 0
    syms = dict(zip(sorted({o["va"] for o in glob}), got[0]))
    at = dict(zip(paths, got[1]))
    n = 0
    # ELF symbols by address (llvm-nm): clang names a static local
    # function.name, which gdb prints demangled in odd ways.
    nm_at = {}
    for f in (l.split() for l in subprocess.run(["llvm-nm", "--defined-only", str(elf)], capture_output=True, text=True).stdout.splitlines()):
        if len(f) == 3:
            nm_at.setdefault(int(f[0], 16), set()).add(f[2])
    for o in glob:
        s = syms[o["va"]]
        m = re.match(r"^(\S+)(?: \+ (\d+))? in section", s)
        # "&name" for a lea; "a=b" for linker marks sharing an address.
        root = re.match(r"&?([A-Za-z_][\w.]*)", o["name"]).group(1)
        off = int(re.search(r"\+(\d+)$", o["name"]).group(1)) if o["kind"] != "sym" and re.search(r"\+(\d+)$", o["name"]) else 0
        if not m:
            problems.append(f"{tag}: {o['op']} <{o['name']}> but gdb: {s}")
            continue
        # The variable (or symbol) gdb names must start the page's path; a
        # static local is name.N (GCC) or function.name (clang) in the ELF.
        gname, var = m.group(1), root.split(".")[0]
        gaddr = o["va"] - int(m.group(2) or 0)
        if not (var == gname or re.fullmatch(re.escape(var) + r"\.\d+", gname) or
                o["kind"] == "sym" and gname in o["name"].lstrip("&").split("=") or
                o["kind"] != "sym" and any(x.endswith("." + var) and gname.startswith(x[:-len(var) - 1]) for x in nm_at.get(gaddr, ()))):
            problems.append(f"{tag}: {o['op']} <{o['name']}> but gdb: {s}")
            continue
        n += 1
    for o in loc:
        path = re.sub(r"\+\d+$", "", o["name"].lstrip("&"))
        a = at.get(path)
        if a and a[0] <= o["va"] < a[0] + max(a[1], 1):
            n += 1
            r[tag + " locals equal"] = r.get(tag + " locals equal", 0) + 1
        elif a is None:
            r[tag + " locals gdb has no address for"] = r.get(tag + " locals gdb has no address for", []) + [o["name"]]
        else:
            problems.append(f"{tag}: {o['op']} <{o['name']}> at {o['va']:#x}, gdb puts {path} at {a[0]:#x} ({a[1]} bytes)")
    return n


def operands_case(browser, base, shots, name, fn, off, want):
    """Memory operands in the listings are named from the DWARF by one
    helper (asmnames.js through varops.js opNamesHtml) in the Debugger and
    the Page tab's merged hex + asm listing: at a pause on an instruction
    that uses a stack slot, that operand names the parameter or local
    (at its address in this pause), absolute addresses name the variable
    and member (or ELF symbol), and gdb on a core of the same registers
    and stack agrees with every name that has an address. The Page tab's
    lines carry the same names as the Debugger's; tooltips say what each
    is; no horizontal scroll at 390 px."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    va, pa = sk["vaddr"] + off, sk["paddr"] + off
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(va))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(pa) or "Halted" in page.inner_text("#dbg_status"), 240) \
            or page.get_attribute("#regs", "data-phys") != str(pa):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn}+{off:#x} did not stop there: {status}"], r
    # The names come once the types, the CFI and the locals shard are in.
    wait_for(lambda: page.evaluate("[...document.querySelectorAll('#context .ins.cur .opn')].length") > 0, 60)
    dbg = page.evaluate(OPNAMES_JS, "#context")
    cur = next((l for l in dbg if l["cur"]), None)
    r["debugger"] = [f"{l['pa']:#x} " + " ".join(f"<{o['name']}>:{o['kind']}" for o in l["ops"]) for l in dbg if l["ops"]]
    if not cur or not any(o["name"] == want and o["kind"] in ("param", "local") and o["va"] is not None for o in cur["ops"]):
        problems.append(f"{name}: the operand at EIP does not name {want} with its address: {cur}")
    for l in dbg:
        for o in l["ops"]:
            if not o["title"] or o["name"] not in o["title"]:
                problems.append(f"{name}: <{o['name']}> has no tooltip saying what it is: {o['title']!r}")
    elf = debug_elf(name)
    dump = page.evaluate(UNWIND_DUMP_JS)
    if elf and shutil.which("gdb"):
        r["debugger vs gdb"] = opnames_vs_gdb(elf, dump, dbg, problems, r, f"{name} Debugger")
    elif os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR or no gdb")
    page.emulate_media(color_scheme="light")
    page.locator("#context").screenshot(path=str(shots / f"operands-{name}-debugger-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    time.sleep(0.2)
    page.locator("#context").screenshot(path=str(shots / f"operands-{name}-debugger-1280-dark.png"))
    # The Page tab: the same lines, the same names; the whole page's globals vs gdb.
    page.click(f"#context .ins.cur")
    wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true" and
             page.get_attribute("#listing", "data-page") == str(pa // 4096) and
             page.evaluate("document.querySelectorAll('#listing .ins .opn').length") > 0, 60)
    time.sleep(0.5)
    lst = page.evaluate(OPNAMES_JS, "#listing")
    by = {l["pa"]: l for l in lst}
    same = 0
    for l in dbg:
        o = by.get(l["pa"])
        if o is None:
            continue
        if [(x["name"], x["kind"]) for x in o["ops"]] != [(x["name"], x["kind"]) for x in l["ops"]]:
            problems.append(f"{name}: {l['pa']:#x} Page tab {[x['name'] for x in o['ops']]}, Debugger {[x['name'] for x in l['ops']]}")
        else:
            same += 1
    r["page lines equal to the debugger's"] = same
    kinds = {}
    for l in lst:
        for o in l["ops"]:
            kinds[o["kind"]] = kinds.get(o["kind"], 0) + 1
    r["page names"] = kinds
    info = page.inner_text("#dump_info")
    if f"{sum(kinds.values())} memory operands named" not in info:
        problems.append(f"{name}: the listing's info line does not count the named operands: {info!r}")
    if elf and shutil.which("gdb"):
        r["page vs gdb"] = opnames_vs_gdb(elf, dump, lst, problems, r, f"{name} Page")
    if not kinds.get("var"):
        problems.append(f"{name}: no global variable named in the page of {fn}: {kinds}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.2)
        line = page.locator("#listing .ins.cur")
        if line.count():
            line.scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"operands-{name}-page-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Page tab scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"operands-{name}-page-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_operands(ctx):
    failed = False
    for name, fn, off, want in OPERAND_POINTS:
        if name in ctx.entries and "dwarf" in ctx.entries[name]:
            t0 = time.monotonic()
            p, r = operands_case(ctx.browser, ctx.base, ctx.shots, name, fn, off, want)
            failed |= report("operands", p, r, kernel=name, at=f"{fn}+{off:#x}", seconds=round(time.monotonic() - t0))
    return failed
