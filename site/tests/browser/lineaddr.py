"""Browser test suite lineaddr (test_browser.py --suites)."""

import gzip
import json
import os
import time

from tests.browser.common import debug_elf, kernel_entries, report, run_to_wait, SITE, symbolize_frames, wait_for


# Line -> address: per kernel, start_kernel statements of init/main.c (found
# by their text) and how the case reaches each: "panel" (click the line in
# the Source panel, then its Run to button) or "typed" (main.c:LINE in the
# Debugger's Run to field). local_irq_enable() is all inlined code (the
# breakpoint is where the inlined call starts).
LINEADDR_POINTS = {
    "minified-linux": [("setup_arch(", "panel"), ("sched_init();", "typed"), ("local_irq_enable();", "panel")],
    "debian-6.1-686": [("setup_arch(", "panel"), ("sched_init();", "typed")],
}


def main_c_lines(name, texts):
    """The line of init/main.c's start_kernel that starts with each text (the shipped source)."""
    src = gzip.decompress((SITE / "kernels" / name / "src" / "init" / "main.c.gz").read_bytes()).decode().splitlines()
    first = next(k for k, l in enumerate(src) if "start_kernel(void)" in l)
    return [next(k + 1 for k in range(first, len(src)) if src[k].strip().startswith(t)) for t in texts]


def line_code(page):
    """The Source panel's address list once loaded: (line, bps, rows, calls)."""
    wait_for(lambda: page.get_attribute("#src_addrs", "data-state") in ("ok", "none"), 60)
    g = lambda k: json.loads(page.get_attribute("#src_addrs", "data-" + k) or "[]")
    return int(page.get_attribute("#src_addrs", "data-line") or 0), g("bps"), g("rows"), g("calls")


def at_line(frames, path, line, inner):
    """llvm-symbolizer frames (innermost first) are path:line: the innermost one, or (not inner) any."""
    hit = lambda f: f[2] == line and (f[1] == path or f[1].endswith("/" + path))
    return bool(frames) and (hit(frames[0]) if inner else any(hit(f) for f in frames))


def lineaddr_case(browser, base, shots, name):
    """Line -> address: clicking a C line in the Source panel lists the
    addresses of its code (every inlined copy, every call inlined on it),
    each a jump to the Page tab; its Run to (and main.c:LINE typed in the
    Debugger's Run to field) stops at one of the line's breakpoint
    addresses, where llvm-symbolizer on the -g ELF names that line
    (innermost, or as the call site of inlined code). Every listed range
    is that line for llvm-symbolizer. A C line of the Page listing opens
    the panel with its addresses; a line with more than 40 ranges lists 40
    and counts the rest. Screenshots light/dark at 1280 and 390 px, no
    horizontal scroll."""
    problems, r = [], {}
    points = LINEADDR_POINTS[name]
    lines = main_c_lines(name, [t for t, _ in points])
    r["lines"] = lines
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    off = syms[0]["vaddr"] - syms[0]["paddr"]
    elf = debug_elf(name)
    if not elf and os.environ.get("CI"):
        problems.append(f"no {name}.elf in $MKB_DEBUG_ELF_DIR")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("MKB.openSourceFile('init/main.c')")
    first_bps = None
    for (text, how), line in zip(points, lines):
        t0 = time.monotonic()
        if how == "panel":
            if page.is_hidden("#src_panel"):
                page.click("#cur_src .frame button")  # the Debugger's source row opens the function
                wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
            row = page.locator(f'#src_body [data-line="{line}"]')
            if not wait_for(lambda: row.count() > 0, 30):
                problems.append(f"{name}: no clickable line {line} in the Source panel")
                break
            row.click()
            got_line, bps, rows, calls = line_code(page)
            if got_line != line or not bps:
                problems.append(f"{name}: main.c:{line} in the panel lists line {got_line}, breakpoints {bps}")
                break
            # Every listed range (first and last byte) and inlined call is that line for llvm-symbolizer.
            if elf:
                probes = [a for a, b in rows for a in (a, b - 1)]
                fr = symbolize_frames(elf, [a + off for a in probes + [c[0] for c in calls]])
                wrong = [hex(a) for a, f in zip(probes, fr) if not at_line(f, "init/main.c", line, True)]
                wrong += [hex(c[0]) for c, f in zip(calls, fr[len(probes):]) if not at_line(f[1:], "init/main.c", line, False)]
                if wrong:
                    problems.append(f"{name}: main.c:{line}: listed code that llvm-symbolizer puts elsewhere: {wrong[:5]}")
            r[f"main.c:{line} listed"] = f"{len(rows)} ranges, {len(calls)} inlined calls, {len(bps)} breakpoints"
            if first_bps is None:
                first_bps = bps
                for scheme in ("light", "dark"):
                    page.emulate_media(color_scheme=scheme)
                    time.sleep(0.2)
                    page.screenshot(path=str(shots / f"lineaddr-{name}-1280-{scheme}.png"))
                    page.set_viewport_size({"width": 390, "height": 900})
                    time.sleep(0.3)
                    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                        problems.append(f"{name}: the Source panel's address list scrolls the page horizontally at 390 px ({scheme})")
                    page.screenshot(path=str(shots / f"lineaddr-{name}-390-{scheme}.png"))
                    page.set_viewport_size({"width": 1280, "height": 1000})
                page.emulate_media(color_scheme="light")
            page.click("#src_runto")
        else:
            # The Debian tree has several main.c: the short name is refused as ambiguous there.
            short = name == "minified-linux"
            if not short:
                page.fill("#dbg_addr", f"main.c:{line}")
                page.click("#dbg_runto")
                wait_for(lambda: "ambiguous" in page.inner_text("#dbg_status"), 30)
                r["main.c typed"] = page.inner_text("#dbg_status")
                if "init/main.c" not in r["main.c typed"]:
                    problems.append(f"{name}: main.c:{line} typed: {r['main.c typed']!r}")
            q = "main.c" if short else "init/main.c"
            got = page.evaluate("async ([q, l]) => { const r = await MKB.dbgLineCodeWait(q, l); return [r.bps || [], r.rows || []]; }", [q, line])
            bps, rows = got[0], [x[:2] for x in got[1]]
            if not bps:
                problems.append(f"{name}: {q}:{line} has no breakpoint addresses")
                break
            page.fill("#dbg_addr", f"{q}:{line}")
            page.click("#dbg_runto")
        ok, pa, status = run_to_wait(page, bps)
        # A stop in the line's own code is that line innermost; at the start
        # of a call inlined on it, the line is the call site.
        inner = any(a <= pa < b for a, b in rows)
        r[f"run to main.c:{line} ({how})"] = f"{status} {time.monotonic() - t0:.0f} s"
        if not ok:
            problems.append(f"{name}: Run to main.c:{line} ({how}) did not stop at one of {[hex(x) for x in bps[:8]]}: {status!r}")
            break
        if f"main.c:{line}" not in status:
            problems.append(f"{name}: the status does not name main.c:{line}: {status!r}")
        if elf:
            fr = symbolize_frames(elf, [pa + off])[0]
            r[f"llvm-symbolizer at {pa:#x}"] = fr
            if not at_line(fr, "init/main.c", line, inner):
                problems.append(f"{name}: Run to main.c:{line} stopped at {pa:#x}, which llvm-symbolizer calls {fr}")
    if not problems and not inner:
        # Stopped in inlined code (irqflags.h): the Debugger's source row opens its line, whose
        # code is in many functions; every copy is listed and is that line for llvm-symbolizer.
        page.click("#cur_src .frame button")
        got_line, bps, rows, calls = line_code(page)
        f0 = json.loads(page.get_attribute("#cur_src", "data-frames"))[0]
        head = page.inner_text("#src_addrs").splitlines()[0]
        r["inlined line"] = f"{f0['file']}:{got_line}: {head}"
        if got_line != f0["line"] or len(rows) < 2 or "inlined copies" not in head:
            problems.append(f"{name}: the inlined line {f0['file']}:{f0['line']} lists {got_line}, {len(rows)} ranges: {head!r}")
        elif elf:
            fr = symbolize_frames(elf, [a + off for a, _ in rows])
            wrong = [hex(a) for (a, _), f in zip(rows, fr) if not at_line(f, f0["file"], f0["line"], True)]
            if wrong:
                problems.append(f"{name}: copies of {f0['file']}:{f0['line']} that llvm-symbolizer puts elsewhere: {wrong[:5]}")
        page.locator(f'#src_body [data-line="{got_line}"]').scroll_into_view_if_needed()
        page.screenshot(path=str(shots / f"lineaddr-{name}-inlined-1280-light.png"))
        page.click("#src_close")
        page.evaluate("MKB.openSourceFile('init/main.c')")
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    if not problems:
        # An address of the list jumps to the Page tab; the listing's C line there lists the same addresses.
        page.locator(f'#src_body [data-line="{lines[0]}"]').click()
        line_code(page)
        btn = page.locator("#src_addrs li button").first
        a = int(btn.get_attribute("data-addr"))
        btn.click()
        if not wait_for(lambda: page.get_attribute("#tab_page", "aria-selected") == "true" and
                        page.get_attribute("#listing", "data-page") == str(a // 4096), 30):
            problems.append(f"{name}: the address {a:#x} of the list did not show its page")
        page.click("#src_close")
        src = page.locator("#listing .src", has_text=f"main.c:{lines[0]}").first
        if not wait_for(lambda: src.count() > 0, 30):
            problems.append(f"{name}: no C line main.c:{lines[0]} in the Page listing")
        else:
            src.click()
            got_line, bps, _, _ = line_code(page)
            r["listing C line"] = f"main.c:{got_line}, {len(bps)} breakpoints"
            if got_line != lines[0] or sorted(bps) != sorted(first_bps):
                problems.append(f"{name}: the listing's C line main.c:{lines[0]} lists line {got_line} {bps}, the panel {first_bps}")
    if not problems and name == "minified-linux":
        # get_current() is inlined everywhere: its line has more ranges
        # than the list shows (40); the rest are counted, all of them are
        # that line for llvm-symbolizer.
        path = "arch/x86/include/asm/current.h"
        text = gzip.decompress((SITE / "kernels" / name / "src" / (path + ".gz")).read_bytes()).decode().split("\n")
        line = next(k + 1 for k, l in enumerate(text) if "this_cpu_read_stable(current_task)" in l)
        page.evaluate(f"MKB.openSourceFile({json.dumps(path)})")
        row = page.locator(f'#src_body [data-line="{line}"]')
        wait_for(lambda: row.count() > 0, 30)
        row.click()
        got_line, bps, rows, calls = line_code(page)
        items = page.evaluate("[...document.querySelectorAll('#src_addrs li')].map((e) => e.textContent)")
        n = len(rows) + len(calls)
        r[f"{path}:{line}"] = [len(rows), len(calls), len(bps), items[-1] if items else None]
        if got_line != line or n <= 40 or len(items) != 41 or \
                items[-1] != f"… and {n - 40} more (Run to sets all {len(bps)} breakpoints)":
            problems.append(f"{name}: {path}:{line} ({len(rows)} ranges, {len(calls)} calls) lists {len(items)} items, the last {items[-1:]}")
        elif elf:
            fr = symbolize_frames(elf, [a + off for a, _ in rows])
            wrong = [hex(a) for (a, _), f in zip(rows, fr) if not at_line(f, path, line, True)]
            if wrong:
                problems.append(f"{name}: copies of {path}:{line} that llvm-symbolizer puts elsewhere: {wrong[:5]}")
        page.screenshot(path=str(shots / f"lineaddr-{name}-many-1280-light.png"))
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def lineaddr_import_case(browser, base, shots):
    """The same for an imported -g vmlinux (the minified Linux's): the
    Worker finds the line's code in every compile unit that names the
    file; the breakpoint addresses equal the shipped shards', and a typed
    Run to init/main.c:LINE stops at one of them."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    line = main_c_lines("minified-linux", ["setup_arch("])[0]
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    shipped = page.evaluate("async (l) => (await MKB.dbgLineCodeWait('init/main.c', l)).bps", line)
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "import" and
             "compile units" in page.inner_text("#sym_status"), 30)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    got = page.evaluate("async (l) => { const r = await MKB.dbgLineCodeWait('init/main.c', l); return [r.state, r.bps, r.how || r.why]; }", line)
    r["import"] = got
    if got[0] != "ok" or sorted(got[1]) != sorted(shipped or []):
        problems.append(f"import: init/main.c:{line} breakpoints {got}, shipped {shipped}")
    page.fill("#dbg_addr", f"init/main.c:{line}")
    page.click("#dbg_runto")
    ok, pa, status = run_to_wait(page, got[1] or [], 120)
    r["run to"] = status
    if not ok:
        problems.append(f"import: Run to init/main.c:{line} did not stop at a breakpoint address: {status!r}")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_lineaddr(ctx):
    failed = False
    for name in [n for n in LINEADDR_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = lineaddr_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("lineaddr", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    t0 = time.monotonic()
    p, r = lineaddr_import_case(ctx.browser, ctx.base, ctx.shots)
    return failed | report("lineaddr_import", p, r, seconds=round(time.monotonic() - t0))
