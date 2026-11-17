"""Browser test suite hello (test_browser.py --suites)."""

import json
import time

from tests.browser.common import (
    ALIVE, cov_open_all, cov_rows, gate_on_screen, HELLO, kernel_entries, locals_open_all, report, show_tab, SITE,
    UNWIND_DUMP_JS, wait_for,
)
from tests.browser.tasks import guest_bytes


def hello_case(browser, base, shots, name="hello"):
    """The hello kernel has the minified Linux's debug features where it
    has the code for them (D7). "hello" is built with -O0 (U1): Run to
    hello_main, then single-step to the halt; at every new instruction no
    parameter or local of any frame is optimized out (state "out" or the
    text "optimized out" in the Debugger); at the statement boundaries the
    values are the guest's: hello_main's row is the BIOS cursor row byte
    (clamped to 23), put_line's row is it (then +1), s points at
    "Hello, World!" (then "Still alive"), p at the VGA cell of row; the
    call stack is put_line <- hello_main <- startup_32, unwound to the
    stack's start. "hello-os" is the -Os build hello/Makefile makes by
    default, put_line inlined: hello_main's row and put_line's row are
    the registers the DWARF location lists give (EAX & 0xff, then EAX)
    and put_line's s is shown as optimized out. Both: Variables says there
    are no global variables, the task row that the DWARF has no
    current_task (no tasks); the Source tab's tree has boot/hello.c
    with lines run and totals equal to files.json; light/dark at 1280 and
    390 px."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sym = {s["name"]: s["paddr"] for s in syms}
    hm = next(s for s in syms if s["name"] == "hello_main")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(hm["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#regs", "data-phys") == str(hm["paddr"]), 120):
        page.close()
        return [f"{name}: Run to hello_main did not stop there: " + page.inner_text("#dbg_status")], r
    cursor = guest_bytes(page, 0x451, 1)[0]
    row0 = min(cursor, 23)

    def pc():
        return int(page.get_attribute("#regs", "data-phys"))

    def step_only(n):
        before = page.evaluate("MKB.stepper.steps")
        page.fill("#dbg_n", str(n))
        page.click("#dbg_stepn")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") - before >= n and
                 page.get_attribute("#dbg_state", "data-state") == "paused", 30, 0.05)

    def variables():
        wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
        return {(sc["func"], v["name"]): v for fr in locals_open_all(page) for sc in fr["scopes"] for v in sc["vars"]}

    def step(n, want):
        step_only(n)
        if pc() != want:
            problems.append(f"{name}: stepped to {pc():#x}, not {want:#x}")
        return variables()

    def stack():
        dump = page.evaluate(UNWIND_DUMP_JS)
        return [f["name"] for f in dump["frames"]], dump["unwind"], \
            " <- ".join(f"{f['name']}[{f['method']}]" for f in dump["frames"]) + f" ({dump['unwind']}: {dump['why']})"

    def val(v, key):
        x = v.get(key, {})
        return int(x["raw"]) if x.get("state") == "ok" and x.get("raw") is not None else None

    if name == "hello":
        # Every instruction from hello_main's first to the halt: one step at
        # a time, the Variables panel read at each address seen first.
        seen, outs, facts, unmapped = set(), [], {}, []
        alive_s = None
        # -O0 keeps put_line out of line (-Os inlines it twice).
        pl = sym.get("put_line", -1)
        if pl < 0:
            problems.append(f"{name}: no put_line symbol (inlined: not an -O0 build?)")
        # The walk ends at hello_main's hlt (cli; hlt; jmp back).
        insns = json.loads((SITE / entry["disasm"]).read_text())["insns"]
        hlt = [a for a, _, ins, *rest in insns if ins.startswith("hlt") and a > hm["paddr"]][0]
        pl_size = next((x["size"] for x in syms if x["name"] == "put_line"), 0)
        # Every instruction of hello_main up to the hlt and of put_line runs,
        # except the clamp's store when the BIOS row is at most 23.
        expect = {a for a, _, ins, *rest in insns
                  if hm["paddr"] <= a <= hlt or pl <= a < pl + pl_size and not ins.startswith("nop")}
        for _ in range(1500):
            a = pc()
            if a not in seen:
                seen.add(a)
                v = variables()
                text = page.inner_text("#panel_debugger")
                for (fn, vn), x in v.items():
                    if x.get("state") == "out" or "optimized out" in (x.get("value") or "") + x.get("loc", ""):
                        outs.append(f"{a:#x} {fn}.{vn}: {x}")
                    elif x.get("state") == "unmapped":
                        # hello_main's first two instructions: EBP is still 0,
                        # so its EBP-based slot is at 0xfffffffc.
                        unmapped.append(f"{a:#x} {fn}.{vn}")
                        if a not in (hm["paddr"], hm["paddr"] + 1) or "not mapped" not in x.get("loc", ""):
                            outs.append(f"{a:#x} {fn}.{vn} not mapped outside hello_main's prologue: {x}")
                if "optimized out" in text:
                    outs.append(f"{a:#x}: the Debugger says 'optimized out': " +
                                text[max(0, text.find("optimized out") - 80):text.find("optimized out") + 40])
                names, unwind, desc = stack()
                if a in (hm["paddr"], pl + 0xa):
                    page.locator("#locals").screenshot(path=str(shots / f"{name}-locals-{a:x}.png"))
                # Statement boundaries (is_stmt rows past the prologues):
                # values and the call stack.
                if a == hm["paddr"] + 0x13:      # line 29, row stored
                    facts["row"] = val(v, ("hello_main", "row"))
                    if facts["row"] != cursor:
                        problems.append(f"{name}: hello_main's row is {v.get(('hello_main', 'row'))} at {a:#x}, "
                                        f"the BIOS cursor row byte is {cursor}")
                    if names != ["hello_main", "startup_32"] or unwind != "ok":
                        problems.append(f"{name}: at {a:#x} the call stack is not hello_main <- startup_32: {desc}")
                    r["stack_main"] = desc
                if a == pl + 0xa:   # line 14, put_line's prologue_end
                    prow, s = val(v, ("put_line", "row")), val(v, ("put_line", "s"))
                    text_at_s = bytes(guest_bytes(page, s, len(HELLO))).decode("latin-1") if s else None
                    facts["put_line"] = prow, s, text_at_s
                    if prow != row0 or text_at_s != HELLO:
                        problems.append(f"{name}: put_line's row, s are {prow}, {s} ({text_at_s!r}) at {a:#x}, "
                                        f"want {row0}, the address of {HELLO!r}")
                    if names != ["put_line", "hello_main", "startup_32"] or unwind != "ok":
                        problems.append(f"{name}: at {a:#x} the call stack is not put_line <- hello_main <- startup_32: {desc}")
                    r["stack_put_line"] = desc
                if a == pl + 0x1a:  # line 16, p set
                    p = val(v, ("put_line", "p"))
                    facts["p"] = p
                    if p != 0xb8000 + row0 * 160:
                        problems.append(f"{name}: put_line's p is {p} at {a:#x}, want {0xb8000 + row0 * 160:#x}")
            elif a == pl + 0xa and alive_s is None:
                # Second call: "Still alive" one row down.
                v = variables()
                prow, alive_s = val(v, ("put_line", "row")), val(v, ("put_line", "s"))
                text_at_s = bytes(guest_bytes(page, alive_s, len(ALIVE))).decode("latin-1") if alive_s else None
                facts["put_line2"] = prow, alive_s, text_at_s
                if prow != row0 + 1 or text_at_s != ALIVE:
                    problems.append(f"{name}: second put_line's row, s are {prow}, {alive_s} ({text_at_s!r}), "
                                    f"want {row0 + 1}, the address of {ALIVE!r}")
            if a == hlt or len(seen) > 200:
                break
            step_only(1)
        r["addresses"], r["facts"] = len(seen), facts
        r["optimized_out"], r["unmapped"] = len(outs), unmapped
        if outs:
            problems.append(f"{name}: {len(outs)} optimized out at the {len(seen)} addresses: " + "; ".join(outs[:8]))
        r["screen"] = gate_on_screen(page)
        missed = sorted(expect - seen)
        if len(missed) != (1 if cursor <= 23 else 0) or alive_s is None or pc() != hlt or not r["screen"]:
            problems.append(f"{name}: the single-step walk saw {len(seen)} of {len(expect)} addresses (not "
                            f"{[hex(x) for x in missed]}), second put_line {alive_s}, stopped at {pc():#x} "
                            f"(hlt {hlt:#x}), both lines on the screen: {r['screen']}")
    else:
        # movb 0x451,%al; cmpb: row lives in AL (DW_OP_breg0 EAX, 0xff, and, convert, stack_value).
        v = step(2, hm["paddr"] + 7)
        names, unwind, r["stack"] = stack()
        if names != ["hello_main", "startup_32"] or unwind != "ok":
            problems.append(f"{name}: the call stack is not hello_main <- startup_32, unwound to the stack's start: {r['stack']}")
        row = v.get(("hello_main", "row"), {})
        r["row"] = row.get("raw"), row.get("value")
        if row.get("state") != "ok" or row.get("raw") is None or int(row["raw"]) != cursor:
            problems.append(f"{name}: hello_main's row is {row} at {hm['paddr'] + 7:#x}, the BIOS cursor row byte is {cursor}")
        # jb over the clamp (taken when the row is below 23), movzbl: at the shll
        # put_line's row parameter is EAX (DW_OP_reg0).
        v = step(2 if cursor < 23 else 3, hm["paddr"] + 14)
        eax = page.evaluate("MKB.stepper.regs().eax")
        prow = v.get(("put_line", "row"), {})
        r["put_line.row"] = prow.get("raw"), eax
        if prow.get("state") != "ok" or prow.get("raw") is None or int(prow["raw"]) != eax:
            problems.append(f"{name}: put_line's row parameter is {prow}, EAX is {eax}")
        if v.get(("put_line", "s"), {}).get("state") != "out":
            problems.append(f"{name}: put_line's s is not shown as optimized out: {v.get(('put_line', 's'))}")
    r["watch_status"] = page.inner_text("#watch_status")
    if not r["watch_status"].startswith("0 variables"):
        problems.append(f"{name}: Variables does not say there are no global variables: {r['watch_status']}")
    r["task"] = page.inner_text("#cur_task")
    if "no current_task" not in r["task"]:
        problems.append(f"{name}: the task row does not say the DWARF has no current_task: {r['task']}")
    page.screenshot(path=str(shots / f"{name}-debugger-1280-light.png"), full_page=True)
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 30)
    cov_open_all(page)
    rows = {x["path"]: x for x in cov_rows(page)}
    f = rows.get("boot/hello.c")
    r["coverage"] = f
    want = next((x for x in F["files"] if x[0] == "boot/hello.c"), None)
    if not f or not want or f["lines"] != want[1] or not f["linesRun"] or f["ins"] != F["insns"]["inFunctions"]:
        problems.append(f"{name}: the Coverage tree's boot/hello.c is {f}, files.json has {want}, {F['insns']}")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        for w in (1280, 390):
            page.set_viewport_size({"width": w, "height": 900})
            time.sleep(0.3)
            if w == 390 and not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                problems.append(f"{name}: the page scrolls horizontally at 390 px ({scheme})")
            page.screenshot(path=str(shots / f"{name}-coverage-{w}-{scheme}.png"), full_page=w == 390)
    show_tab(page, "debugger")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        page.screenshot(path=str(shots / f"{name}-debugger-390-{scheme}.png"), full_page=True)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_hello(ctx):
    failed = False
    for name in [n for n in ("hello", "hello-os") if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = hello_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("hello", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed
