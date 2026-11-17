"""Browser test suite unoptimized (test_browser.py --suites)."""

import json
import os
import time

from tests.browser.common import (
    debug_elf, kernel_entries, locals_open_all, report, run_to_wait, SITE, UNWIND_DUMP_JS, wait_for,
)
from tests.browser.unwind import locals_check


# Pauses for the unoptimized suite, in boot order: Run to each function,
# then single-step that many instructions (into calls), reading the
# Variables panel at every address not seen before. kmem_cache_alloc's
# walk crosses its inlined slab_alloc_node, whose list_first_entry_or_null
# block (head__, pos__, __mptr) covers only part of it.
UNOPT_PAUSES = [("start_kernel", 30), ("setup_arch", 30), ("kmem_cache_init", 30), ("kmem_cache_alloc", 260),
                ("rest_init", 20), ("kernel_init", 30), ("kernel_execve", 30), ("vfs_write", 40)]


def unoptimized_case(browser, base, shots, name):
    """minified-linux is built with -O0 (U2): for each of UNOPT_PAUSES, a
    fresh boot runs to the function and single-steps from there; at every
    address of the walk no parameter or local of any frame is optimized out
    (state "out", or the text "optimized out" anywhere in the Debugger),
    and at the first stop the values equal gdb's on a core of the same
    registers and stack (locals_check). minified-linux-os (the -Os product
    build) walks the same functions it has as symbols and must show some
    optimized-out variables (the check sees them); its count is reported.
    No page errors. Screenshots of the Variables panel light/dark, 1280 px."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    syms = {s["name"]: s for s in json.loads((SITE / entry["symbols"]).read_text())["symbols"]}
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"{name}: no -g ELF in $MKB_DEBUG_ELF_DIR to compare the values with gdb")
    errors = []
    seen, outs, nvars, shown, walks, unmapped = set(), [], 0, 0, {}, 0
    for fn, steps in UNOPT_PAUSES:
        s = syms.get(fn)
        if not s:
            if name == "minified-linux":
                problems.append(f"{name}: no symbol {fn} (inlined: not an -O0 build?)")
            continue
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e, fn=fn: errors.append(f"{fn}: {e}"))
        page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)

        def pc():
            return int(page.get_attribute("#regs", "data-phys") or -1)

        def step1():
            before = page.evaluate("MKB.stepper.steps")
            page.fill("#dbg_n", "1")
            page.click("#dbg_stepn")
            wait_for(lambda: page.evaluate("MKB.stepper.steps") - before >= 1 and
                     page.get_attribute("#dbg_state", "data-state") == "paused", 30, 0.05)

        page.fill("#dbg_addr", hex(s["vaddr"]))
        page.click("#dbg_runto")
        ok, at, status = run_to_wait(page, {s["paddr"]}, 300)
        if not ok:
            problems.append(f"{name}: Run to {fn} did not stop there: {status}")
            page.close()
            continue
        n0, here = len(seen), set()
        for k in range(steps + 1):
            a = pc()
            if a not in here:
                here.add(a)
                seen.add(a)
                wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
                for fr in locals_open_all(page):
                    for sc in fr["scopes"]:
                        for v in sc["vars"]:
                            nvars += 1
                            if v["state"] == "out" or "optimized out" in (v.get("value") or "") + v.get("loc", ""):
                                outs.append(f"{a:#x} {sc['func']}.{v['name']}: {v['loc']}")
                            elif v["state"] == "unmapped":
                                unmapped += 1
                            else:
                                shown += 1
                text = page.inner_text("#panel_debugger")
                if "optimized out" in text and not any(o.startswith(f"{a:#x} ") for o in outs):
                    outs.append(f"{a:#x}: the Debugger says 'optimized out': " +
                                text[max(0, text.find("optimized out") - 80):text.find("optimized out") + 40])
                if k == 0:
                    dump = page.evaluate(UNWIND_DUMP_JS)
                    problems += [f"{name} at {fn}: {p}" for p in locals_check(page, elf, dump, r, fn)]
                    if fn == "kmem_cache_alloc":
                        for scheme in ("light", "dark"):
                            page.emulate_media(color_scheme=scheme)
                            time.sleep(0.2)
                            page.locator("#locals").screenshot(path=str(shots / f"unoptimized-{name}-{fn}-{scheme}.png"))
                        page.emulate_media(color_scheme="light")
            if k < steps:
                step1()
        walks[fn] = len(seen) - n0
        page.close()
    r["walks (new addresses)"] = walks
    r["addresses"], r["variables read"], r["with a value"], r["not mapped"] = len(seen), nvars, shown, unmapped
    r["optimized_out"] = len(outs)
    r["optimized out, first"] = outs[:6]
    if name == "minified-linux":
        if outs:
            problems.append(f"{name}: {len(outs)} optimized out at {len(seen)} addresses: " + "; ".join(outs[:8]))
        if len(seen) < 300:
            problems.append(f"{name}: only {len(seen)} addresses walked")
    elif not outs:
        problems.append(f"{name}: no optimized-out variable seen in the -Os build: the check does not see them")
    if errors:
        problems.append(f"{name}: page errors: {errors[:3]}")
    return problems, r


def suite_unoptimized(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "minified-linux-os") if n in ctx.entries]:
        t0 = time.monotonic()
        p, r = unoptimized_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("unoptimized", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed
