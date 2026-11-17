"""Part of a browser suite (test_browser.py)."""

import json
import pathlib
import re
import time
import urllib.parse

from tests.browser.common import (
    ALIVE, counters, gate_on_screen, HELLO, kernel_entries, machine_do, rev_click, run_to_wait, show_tab, SITE,
    wait_for,
)


def panic_text_case(browser, base, shots):
    """'---[ end Kernel panic - not syncing: ... ]---' on the screen stops the machine."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return ["no kernel entry"], r
    r["build_info"] = page.inner_text("#build_info")
    if not r["build_info"]:
        problems.append("no build line in the header")
    # Write the line through the VGA MMIO handlers, as the guest would, on a
    # row past the first screen (vgacon scrolls through the 32 KiB window).
    page.evaluate("""() => { const cpu = MKB.emulator.v86.cpu;
        const t = "---[ end Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0) ]---";
        for (let i = 0; i < t.length; i++) { cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i, t.charCodeAt(i)); cpu.mmap_write8(0xb8000 + 160 * 40 + 2 * i + 1, 7); } }""")
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text"
                  and page.get_attribute("#dbg_state", "data-state") == "paused", 3, 0.1)
    r["banner"] = page.inner_text("#end_banner")
    if not ok or "VFS: Unable to mount root fs on unknown-block(0,0)" not in r["banner"]:
        problems.append("panic text did not stop the machine: " + r["banner"] + " / " + page.get_attribute("#dbg_state", "data-state"))
    page.screenshot(path=str(shots / "panic-text.png"))
    page.close()
    panic_text_real_time(browser, base, problems, r, errors)
    for spin in (False, True):
        panic_text_no_end(browser, base, spin, problems, r, errors)
    count_give_up(browser, base, problems, r, errors)
    panic_budget_stepped(browser, base, problems, r, errors)
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def panic_budget_stepped(browser, base, problems, r, errors):
    """The minified Linux stepped from the reset vector with a panic budget
    of 2: it stops 2 instructions after panic() is entered (its end line
    comes later) and says so. Stepped back 3,000 instructions and forward
    again, the re-run stops at the two milestones and at that end as the
    first run did."""
    tag = "panic budget, stepped"
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1&mips=10&fastforward=0&revk=20000&panicbudget=2#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    for _ in range(10):  # each Step stops at a milestone
        if page.evaluate("!!MKB.stepper.end"):
            break
        page.evaluate("MKB.doStep(3000000)")
    page.evaluate("MKB.afterStop()")
    time.sleep(0.5)
    rows = counters(page)
    pe, end = rows.get("panic_entry") or {}, rows.get("end") or {}
    banner = page.inner_text("#end_banner")
    want = f"panic() entered, no end-of-panic text in the next 2 instructions. Stopped after {(pe.get('entry') or 0) + 2:,} instructions from the kernel entry."
    r[tag] = [page.get_attribute("#end_banner", "data-kind"), pe, end]
    if page.get_attribute("#end_banner", "data-kind") != "panic-budget" or banner != want or end.get("steps") != (pe.get("steps") or 0) + 2:
        problems.append(f"{tag}: banner {banner!r}, want {want!r}; panic() entered at {pe}, the end at {end}")
        page.close()
        return
    page.fill("#dbg_n", "3000")
    _, _, text, st = rev_click(page, "#dbg_backn", False)
    stops = []
    for _ in range(5):
        if page.evaluate("!!MKB.stepper.end"):
            break
        page.fill("#dbg_n", "5000")
        page.click("#dbg_stepn")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        time.sleep(0.3)
        stops.append(page.evaluate("MKB.stepper.steps"))
    want_stops = [rows[HELLO]["steps"], rows[ALIVE]["steps"], end["steps"]]
    again = page.inner_text("#end_banner")
    r[tag + ", again"] = [st, stops]
    if st != "done" or stops != want_stops or again != banner:
        problems.append(f"{tag}: stepped back 3,000 ({st}: {text[:100]!r}) and forward, the re-run stopped at {stops} "
                        f"(want {want_stops}), banner {again!r}")
    page.close()


PANIC_TEXT_JS = """([row, t]) => { const cpu = MKB.emulator.v86.cpu;
    for (let i = 0; i < t.length; i++) { cpu.mmap_write8(0xb8000 + 160 * row + 2 * i, t.charCodeAt(i)); cpu.mmap_write8(0xb8000 + 160 * row + 2 * i + 1, 7); } }"""


def count_give_up(browser, base, problems, r, errors):
    """Counting at full speed gives up after 200 minutes of stepping
    (COUNT_LIMIT_MS; the time spent is set past it as the minified Linux's
    count starts), or when count is switched off while it counts: the
    count line says so and the JIT runs the boot on to its end."""
    for how, want in (("limit", "Gave up after 12000 s of stepping."), ("count off", "Counting switched off.")):
        page = browser.new_page(viewport={"width": 1100, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full&count=1")
        # Set (or clicked) in the same check that sees the count running:
        # the minified Linux's count takes about a second.
        page.wait_for_function("typeof window.MKB?.counting !== 'undefined' && MKB.counting && MKB.runState === 'stepped'" +
                               (" && (MKB.countMs = 12000001)" if how == "limit" else " && (document.getElementById('count').click(), true)"),
                               timeout=60000)
        ok = wait_for(lambda: page.evaluate("MKB.runState === 'jit' || !!(MKB.stepper && MKB.stepper.end)"), 30)
        note = page.evaluate("MKB.countNote")
        wait_for(lambda: gate_on_screen(page) and page.evaluate("!!MKB.stepper.end"), 120)
        r["count " + how] = [ok, note, gate_on_screen(page), page.evaluate("MKB.counting")]
        if r["count " + how] != [True, want, True, False]:
            problems.append(f"count ({how}): [the JIT took over, note, both lines, counting] {r['count ' + how]}")
        page.close()


def panic_text_no_end(browser, base, spin, problems, r, errors):
    """A "Kernel panic - not syncing:" line without its end line (another
    kernel's panic) under the page's JIT (virtual clock, full speed, count
    off), from start_kernel: the stepper takes over and runs on to the
    minified Linux's own panic (init exits) and halt (the hlt before EIP);
    spinning there (jmp $) it stops after the panic budget's instructions
    instead, and the banner says so in one sentence each."""
    tag = "panic line without its end" + (", spinning" if spin else "")
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1&speed=full&count=0&panicbudget=3000000#debugger")
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(x for x in syms if x["name"] == "start_kernel")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", "start_kernel")
    page.click("#dbg_runto")
    if not run_to_wait(page, {sk["paddr"]}, 120)[0]:
        problems.append(f"{tag}: Run to start_kernel: {page.inner_text('#dbg_status')!r}")
        page.close()
        return
    if spin:
        page.evaluate("(pa) => MKB.emulator.write_memory(new Uint8Array([0xeb, 0xfe]), pa)", sk["paddr"])
    page.evaluate(PANIC_TEXT_JS, [41, "Kernel panic - not syncing: no end line follows"])
    machine_do(page, "click", "#dbg_continue")
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and page.evaluate("!!MKB.stepper.end"), 120)
    got = page.evaluate("""() => ({ kind: MKB.stepper.end.kind, banner: document.getElementById('end_banner').textContent,
        budget: MKB.stepper.panicBudget, eip: MKB.stepper.nextPhys(), before: MKB.emulator.read_memory(MKB.stepper.nextPhys() - 1, 1)[0] })""")
    r[tag] = {k: v for k, v in got.items() if k != "banner"} | {"banner": got["banner"][:100]}
    if spin:
        want = f"panic() entered, no end-of-panic text in the next {3000000:,} instructions. Stopped after "
        if not ok or got["kind"] != "panic-budget" or not got["banner"].startswith(want) or got["eip"] != sk["paddr"]:
            problems.append(f"{tag}: {got}, want {want!r} at start_kernel")
    elif not ok or not got["banner"].startswith("Kernel panic (init exited with code 0)") or got["before"] != 0xF4:
        problems.append(f"{tag}: {got}, want the kernel's own panic, halted (hlt, 0xf4, before EIP)")
    page.close()


def panic_text_real_time(browser, base, problems, r, errors):
    """The real-time clock at full speed (v86's own loop, which the page
    cannot stop inside): the minified Linux paused at start_kernel is made
    to spin there (jmp $, as panic() ends); Step 10^15 gives up after 5 s;
    it runs on the page's JIT (virtual clock) and the clock is switched to
    real time while it runs; then Debian's "(initramfs) " prompt at a
    row's start pauses the machine once, Continue runs on, and an end of
    panic text stops it (both checked 4 times a second, where the run has
    got to: not exact). The command line comes from ?cmdline=."""
    cmdline = "nokaslr cryptomgr.notests mkb.clock=real"
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1&speed=full&count=0&cmdline=" + urllib.parse.quote(cmdline))
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        problems.append("real time: the minified Linux did not start paused")
        page.close()
        return
    r["real_time_cmdline"] = page.input_value("#cmdline")
    if r["real_time_cmdline"] != cmdline:
        problems.append(f"?cmdline={cmdline}: the command line field says {r['real_time_cmdline']!r}")
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(x for x in syms if x["name"] == "start_kernel")
    show_tab(page, "debugger")
    page.fill("#dbg_addr", "start_kernel")
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        problems.append(f"real time: Run to start_kernel: {page.inner_text('#dbg_status')!r}")
        page.close()
        return
    page.evaluate("(pa) => MKB.emulator.write_memory(new Uint8Array([0xeb, 0xfe]), pa)", sk["paddr"])
    # Step 10^15 on the spin (no machine steps that many in 5 s, CI's ran
    # 100,000,000 in less): Step gives up after 5 s, still there.
    page.fill("#dbg_n", "1000000000000000")
    page.click("#dbg_stepn")
    wait_for(lambda: page.inner_text("#dbg_status").startswith("Stepped "), 30)
    r["step_limit"] = [page.inner_text("#dbg_status"), page.get_attribute("#regs", "data-phys")]
    if not re.fullmatch(r"Stepped [\d,]+ instructions \(5 s limit\)\.", r["step_limit"][0]) or r["step_limit"][1] != str(sk["paddr"]):
        problems.append(f"Step 10^15 on a jmp $: {r['step_limit']}, want the 5 s limit at {sk['paddr']}")
    machine_do(page, "click", "#dbg_continue")
    jit = wait_for(lambda: page.evaluate("MKB.runState === 'jit' && !!MKB.jitLoopId && MKB.CLOCK.virtual"), 10)
    machine_do(page, "select_option", "#clock_mips", "real")
    real = wait_for(lambda: page.evaluate("MKB.runState === 'jit' && !MKB.jitLoopId && !MKB.CLOCK.virtual && MKB.emulator.is_running()"), 10)
    r["clock switched while running"] = [jit, real]
    if not jit or not real:
        problems.append(f"the clock switched to real time while the page's JIT ran: [JIT on the virtual clock, then v86's own loop] {[jit, real]}")
    page.evaluate(PANIC_TEXT_JS, [40, "(initramfs) "])
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and "(initramfs) prompt" in page.inner_text("#dbg_status"), 10, 0.1)
    r["real_time_prompt"] = [page.inner_text("#dbg_status"), page.evaluate("MKB.stepper.prompt && MKB.stepper.prompt.exact")]
    if not ok or r["real_time_prompt"][1] is not False:
        problems.append(f"real time, the (initramfs) prompt on the screen: {r['real_time_prompt']}, "
                        f"state {page.get_attribute('#dbg_state', 'data-state')}, want paused at the prompt (not exact)")
    page.evaluate(PANIC_TEXT_JS, [41, "---[ end Kernel panic - not syncing: spinning on the real-time clock ]---"])
    machine_do(page, "click", "#dbg_continue")
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text"
                  and page.get_attribute("#dbg_state", "data-state") == "paused", 10, 0.1)
    r["real_time_banner"] = [page.inner_text("#end_banner"), page.evaluate("MKB.stepper.end && MKB.stepper.end.exact"),
                             page.get_attribute("#regs", "data-phys")]
    if not ok or "Kernel panic: spinning on the real-time clock" not in r["real_time_banner"][0] or r["real_time_banner"][1] is not False:
        problems.append(f"real time, an end of panic text: banner {r['real_time_banner']}, "
                        f"state {page.get_attribute('#dbg_state', 'data-state')}")
    if r["real_time_banner"][2] != str(sk["paddr"]):
        problems.append(f"real time: stopped at {r['real_time_banner'][2]}, not in the jmp $ at start_kernel ({sk['paddr']})")
    page.close()


def distro_panic_case(browser, base, cache):
    """Optional ($MKB_DISTRO_CACHE from test_distro_symbols.py): Debian's 686
    kernel without an initrd panics; the page stops itself."""
    kernel = next(iter(sorted(pathlib.Path(cache).glob("boot/vmlinuz-*"))), None)
    if not kernel:
        return [f"no boot/vmlinuz-* in {cache}"], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0")
    wait_for(lambda: gate_on_screen(page), 60)
    show_tab(page, "machine")
    page.fill("#cmdline", "console=tty0")
    t0 = time.monotonic()
    page.set_input_files("#file", str(kernel))
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text", 300, 1)
    r = {"seconds": round(time.monotonic() - t0), "banner": page.inner_text("#end_banner"),
         "state": page.get_attribute("#dbg_state", "data-state")}
    page.close()
    bad = [] if ok and r["state"] == "paused" and "Unable to mount root fs" in r["banner"] else [f"distro panic not caught: {r}"]
    return bad, r
