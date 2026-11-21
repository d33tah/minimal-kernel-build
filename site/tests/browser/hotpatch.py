"""Browser test suite hotpatch (test_browser.py --suites)."""

import os
import time

from tests.browser.common import (
    report, run_to_wait, show_tab, SITE, snap_click, snap_rows, snap_save, src_tab_once, srcedit_ready, srcedit_state,
    wait_for,
)
from tests.browser.rebuild import rb_press, rb_shots
from tests.browser.rebuild_linux import rbl_open


# ---- Patch running kernel (plan E7, hotpatch.jsx) ----------------------------
# What a user does: pause the machine, open a C file in the Source panel,
# Edit, change a function the kernel calls later; the page checks the edit
# in the background (the unit compiled twice) and enables "Patch running
# kernel" only when a patch can apply it, saying why not otherwise; press
# it, Continue: the new code runs, no reboot.
#   hello: paused at hello_main (Run to, stepped: the run stays
#     replayable), a variable added is refused, a running machine is
#     refused, put_line swapping the letters' case is patched, Continue
#     shows "hELLO" and "sTILL ALIVE"; snapshots before and after the patch restore
#     without and with it; Replay of the run makes the patch at the same
#     instruction and matches; 390 px dark: the editor bar's button.
#   minified Linux (linked with LTO): paused at start_kernel, a new call in
#     console_on_rootfs is refused (LTO's fastcc); do_exit (reached when
#     /init exits after its two lines) writes a line into the VGA text
#     memory, Continue shows it.
#   Debian (rebuilt from source): at the (initramfs) prompt, paused;
#     run_init_process (pr_debug, static keys) is refused; /proc/version's
#     function patched, Continue, `cat /proc/version` shows the new line.
# Each: the reserved memory (0x60000-0x7ffff) is zero before the first
# patch and holds the patch's bytes after the new code ran; the old entry
# starts with the jump; the copies are in the symbols; edit-to-effect
# times (edit saved -> ready, press -> patched, Continue -> on the screen).
HP_PA, HP_SIZE = 0x60000, 0x20000
# The minified Linux's do_exit, edited: a line into the VGA text memory
# (mapped at PAGE_OFFSET + 0xb8000) before it panics (printk prints
# nothing in this kernel).
HP_DO_EXIT = ("\t{\n\t\tvolatile unsigned short *vga = (volatile unsigned short *)0xc00b8000UL;\n"
              "\t\tconst char *m = \"Hot patched do_exit\";\n\t\tint i;\n\n"
              "\t\tfor (i = 0; m[i]; i++)\n\t\t\tvga[i] = 0x0700 | m[i];\n\t}\n")

HP_STATE_JS = """() => { const $ = (i) => document.getElementById(i);
  return { state: $('hp_status').dataset.state || '', status: $('hp_status').textContent, go: !$('hp_go').disabled,
    goPanel: $('hp_go_panel') ? !$('hp_go_panel').disabled : null,
    why: $('hp_go_why') ? ($('hp_go_why').getClientRects().length ? $('hp_go_why').textContent : '(not shown)') : null, list: [...document.querySelectorAll('#hp_list li')].map((e) => e.textContent),
    patches: MKB.hpPatches().map((p) => ({ n: p.n, at: p.at, path: p.path, used: p.used, funcs: p.funcs.map((f) => [f.name, f.va, f.old]) })),
    syms: MKB.symbols ? MKB.symbols.symbols.filter((s) => s.patch).map((s) => [s.name, s.vaddr, s.paddr]) : [],
    run: MKB.runState, steps: MKB.stepper ? MKB.stepper.steps : null,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1 }; }"""

MEM_JS = "([pa, n]) => Array.from(MKB.emulator.read_memory(pa, n))"
# The innermost frame [func, file, line] at each address of [pa, pa + n).
FRAMES_JS = """([pa, n]) => { const out = [];
  for (let a = pa; a < pa + n; a++) { const f = MKB.dbgFrames(a); if (f.state === 'ok') out.push([f.frames[0].func, f.frames[0].file, f.frames[0].line]); }
  return out; }"""
SYM_JS = "(n) => { const s = MKB.symbols.symbols.find((x) => x.name === n && !x.patch); return s ? [s.vaddr, s.paddr, s.size] : null; }"


def hp_state(page):
    src_tab_once(page)
    return page.evaluate(HP_STATE_JS)


def screen(page):
    return page.inner_text("#screen_container")


def swapped(s):
    """hello's lines from the patched put_line (each byte ^ 0x20)."""
    return "hELLO" in s and "wORLD" in s and "sTILL ALIVE" in s and "Hello" not in s


def hp_edit(page, text):
    """The editor's text set (as typed), saved, and the patch box's check of
    the previous text gone."""
    was = hp_state(page)["status"]
    page.locator("#ed_text").fill(text)
    t = time.monotonic()
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    wait_for(lambda: hp_state(page)["status"] != was, 30, 0.05)
    return t


def hp_why_shown(st, tag, problems):
    """A disabled Patch running kernel in the Source panel says why as text
    under it (a title is never seen on a phone); an enabled one says nothing."""
    if st["goPanel"] is False and (not st["why"] or st["why"] == "(not shown)"):
        problems.append(f"{tag}: the panel's Patch running kernel is disabled ({st['state']}) without its reason as text: {st['why']!r}")
    elif st["goPanel"] and st["why"] is not None:
        problems.append(f"{tag}: the panel's Patch running kernel is enabled with a reason under it: {st['why']!r}")


def hp_wait(page, states, timeout=300):
    wait_for(lambda: hp_state(page)["state"] in states, timeout, 0.1)
    return hp_state(page)


def hp_mem(page, pa, n):
    return page.evaluate(MEM_JS, [pa, n])


def hp_continue(page):
    show_tab(page, "machine")
    page.click("#dbg_continue")


def hp_apply(page, r, key, problems, tag, phone=False, sel="#hp_go_panel"):
    """Press the button once the check says ready; the patch's state."""
    t0 = time.monotonic()
    hp_why_shown(hp_state(page), tag + " checking", problems)
    st = hp_wait(page, ("ready", "refused", ""), 600)
    r[key + " check s"] = round(time.monotonic() - t0, 1)
    hp_why_shown(st, tag + " " + (st["state"] or "no state"), problems)
    if st["state"] != "ready" or not st["go"]:
        problems.append(f"{tag}: the edit is not ready to patch: {st['state']} {st['status']!r}")
        return None
    t1 = time.monotonic()
    rb_press(page, sel, phone)
    st = hp_wait(page, ("done", "failed"), 120)
    r[key + " press to patched s"] = round(time.monotonic() - t1, 2)
    r[key] = {k: st[k] for k in ("state", "status", "patches", "syms")}
    if st["state"] != "done" or not st["patches"]:
        problems.append(f"{tag}: the patch: {st['state']} {st['status']!r}")
        return None
    return st


def hp_check_jumps(page, st, problems, tag):
    """Every replaced function starts with e9 to its copy; the copies are
    symbols."""
    p = st["patches"][-1]
    koff = None
    for name, va, old in p["funcs"]:
        if old is None:
            continue
        s = page.evaluate(SYM_JS, name)
        koff = s[0] - s[1]
        b = hp_mem(page, s[1], 5)
        rel = int.from_bytes(bytes(b[1:5]), "little", signed=True)
        if s[0] != old or b[0] != 0xE9 or (old + 5 + rel) & 0xFFFFFFFF != va:
            problems.append(f"{tag}: {name} at {s[0]:#x} starts {bytes(b).hex()}, want e9 to {va:#x}")
        if not any(n == f"{name} (patch {p['n']})" and v == va for n, v, _ in st["syms"]):
            problems.append(f"{tag}: no symbol {name} (patch {p['n']}) at {va:#x}: {st['syms']}")
    return koff


def hp_reserved_zero(page, problems, tag, r):
    b = hp_mem(page, HP_PA, HP_SIZE)
    nz = [i for i, x in enumerate(b) if x]
    r["reserved memory nonzero bytes before the patch"] = len(nz)
    if nz:
        problems.append(f"{tag}: the reserved memory is not zero after the boot: {len(nz)} bytes, first at {HP_PA + nz[0]:#x}")


def hp_hello(browser, base, shots, problems, r):
    tag = "hello"
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme="light")
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&fastforward=0&speed=full#debugger")
        srcedit_ready(page, "hello")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        hm = page.evaluate(SYM_JS, "hello_main")
        page.fill("#dbg_addr", "hello_main")
        page.click("#dbg_runto")
        ok, at, why = run_to_wait(page, {hm[1]}, 120)
        if not ok:
            problems.append(f"{tag}: Run to hello_main stopped at {at:#x}: {why!r}")
            return
        hp_reserved_zero(page, problems, tag, r)
        snap_save(page, 1, False)
        # A variable added: refused, the button disabled, the reason shown.
        if not rbl_open(page, "boot/hello.c", False):
            problems.append(f"{tag}: Edit did not open boot/hello.c")
            return
        shipped = page.input_value("#ed_text")
        hp_edit(page, shipped.replace("static void put_line", "static int hp_count;\n\nstatic void put_line").replace("while (*s)", "hp_count++;\n\twhile (*s)"))
        st = hp_wait(page, ("refused", "ready"), 300)
        r["refused: a variable"] = st["status"]
        if st["state"] != "refused" or st["go"] or st["goPanel"] or "adds hp_count" not in st["status"]:
            problems.append(f"{tag}: a variable added: {st['state']} go={st['go']} {st['status']!r}")
        hp_why_shown(st, tag + " a variable added", problems)
        if st["why"] and "adds hp_count" not in st["why"]:
            problems.append(f"{tag}: a variable added: the reason under the panel's button {st['why']!r}")
        # An edit that does not compile: clang's message.
        hp_edit(page, shipped.replace("while (*s)", "int broken = ;\n\twhile (*s)"))
        st = hp_wait(page, ("refused", "ready"), 300)
        r["refused: does not compile"] = st["status"][:200]
        if st["state"] != "refused" or st["go"] or "does not compile" not in st["status"] or "expected expression" not in st["status"]:
            problems.append(f"{tag}: a compile error: {st['state']} {st['status'][:300]!r}")
        # The edit: put_line swaps the letters' case.
        t_edit = hp_edit(page, shipped.replace("(unsigned char)*s++;", "(unsigned char)(*s++ ^ 0x20);"))
        st = hp_apply(page, r, "patch", problems, tag)
        if not st:
            return
        r["edit saved to patched s"] = round(time.monotonic() - t_edit, 2)
        hp_check_jumps(page, st, problems, tag)
        if [p["funcs"][0][0] for p in st["patches"]] != ["put_line"] or not st["list"] or "put_line" not in st["list"][0]:
            problems.append(f"{tag}: the patch list: {st['patches']} {st['list']}")
        code = hp_mem(page, HP_PA, 16)
        # Step back past the patch's count: the memory without it; Step
        # again: the patch made again at its count (replay.js inputRedo).
        pl = page.evaluate(SYM_JS, "put_line")
        show_tab(page, "machine")
        at = page.evaluate("MKB.stepper.steps")
        page.click("#dbg_back")
        wait_for(lambda: page.evaluate("MKB.stepper.steps") == at - 1 and page.evaluate("MKB.runState") == "paused", 60)
        back = [hp_mem(page, pl[1], 1)[0], len(hp_state(page)["patches"])]
        # Two Steps: the patch goes in before the instruction at its count runs.
        for k in (at, at + 1):
            page.click("#dbg_step")
            wait_for(lambda: page.evaluate("MKB.stepper.steps") == k and page.evaluate("MKB.runState") == "paused", 60)
        fwd = [hp_mem(page, pl[1], 1)[0], len(hp_state(page)["patches"])]
        r["step back / step over the patch"] = [back, fwd]
        if back != [0x55, 0] or fwd != [0xE9, 1]:
            problems.append(f"{tag}: put_line's first byte and patches after Step back {back}, after Step {fwd}: want [0x55, 0], [0xe9, 1]")
        # The copy's debug info: frames at its addresses name put_line and
        # the edited file's line of the changed statement.
        frames = page.evaluate(FRAMES_JS, [HP_PA, 83])
        r["frames in the copy"] = sorted(set(map(tuple, frames)))[:8]
        if ["put_line", "boot/hello.c", 17] not in frames:
            problems.append(f"{tag}: no frame put_line boot/hello.c:17 in the copy: {r['frames in the copy']}")
        snap_save(page, 2, False)
        t_go = time.monotonic()
        hp_continue(page)
        if not wait_for(lambda: swapped(screen(page)), 120, 0.05):
            problems.append(f"{tag}: after Continue the screen is {screen(page)[-300:]!r}")
        r["continue to the new text s"] = round(time.monotonic() - t_go, 2)
        r["edit saved to the new text s"] = round(time.monotonic() - t_edit, 2)
        if hp_mem(page, HP_PA, 16) != code:
            problems.append(f"{tag}: the reserved memory changed after the run")
        rb_shots(page, shots, "hotpatch-hello-1280-machine", problems, tag)
        page.emulate_media(color_scheme="light")
        show_tab(page, "source")
        rb_shots(page, shots, "hotpatch-hello-1280-source", problems, tag)
        page.emulate_media(color_scheme="light")
        # Snapshots: before the patch (no jump, no copies), after (both).
        show_tab(page, "snapshots")
        rows = snap_rows(page)["rows"]
        for i, want in ((0, False), (1, True)):
            snap_click(page, f"#snapshots tr[data-id='{rows[i]['id']}'] button.restore", False)
            wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rows[i]["id"], 30)
            wait_for(lambda: bool(hp_state(page)["patches"]) == want, 10)
            st = hp_state(page)
            b = hp_mem(page, page.evaluate(SYM_JS, "put_line")[1], 1)[0]
            r[f"restored snapshot {i + 1}"] = [st["patches"] and st["patches"][0]["n"], hex(b), st["list"], len(st["syms"])]
            if bool(st["patches"]) != want or (b == 0xE9) != want or bool(st["syms"]) != want or bool(st["list"]) != want:
                problems.append(f"{tag}: restoring snapshot {i + 1} ({'after' if want else 'before'} the patch): patches {st['patches']}, "
                                f"put_line starts {b:#x}, symbols {st['syms']}, list {st['list']}")
        # Replay: the patch at the same instruction, the same end.
        hp_continue(page)
        if not wait_for(lambda: swapped(screen(page)) and page.evaluate("MKB.runState") == "paused", 120):
            problems.append(f"{tag}: after restoring the snapshot with the patch, Continue: {screen(page)[-200:]!r}")
        want_at = hp_state(page)["patches"][0]["at"]
        show_tab(page, "snapshots")
        page.click("#run_replay")
        t0 = time.monotonic()
        while time.monotonic() - t0 < 600:
            v = page.get_attribute("#run_status", "data-replay")
            if v in ("match", "differ", "stopped", "error"):
                break
            if page.evaluate("MKB.runState") == "paused" and page.evaluate("MKB.replayActive()"):
                show_tab(page, "machine")
                if page.is_enabled("#dbg_continue"):
                    page.click("#dbg_continue")
            time.sleep(0.5)
        st = hp_state(page)
        r["replay"] = [page.get_attribute("#run_status", "data-replay"), page.inner_text("#run_status"), st["patches"]]
        if page.get_attribute("#run_status", "data-replay") != "match" or not st["patches"] or st["patches"][0]["at"] != want_at or \
                not swapped(screen(page)):
            problems.append(f"{tag}: the replay: {r['replay']}, screen {screen(page)[-200:]!r}, want the patch at {want_at}")
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()


def hp_hello_phone(browser, base, shots, problems, r):
    """390 px dark, touch: paused at the reset vector (before hello_main
    ran), hello_main's string edited, the editor bar's button."""
    tag = "hello 390"
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, is_mobile=True, has_touch=True)
    page = ctx.new_page()
    page.emulate_media(color_scheme="dark")
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&speed=full")
        srcedit_ready(page, "hello")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 60)
        if not rbl_open(page, "boot/hello.c", True):
            problems.append(f"{tag}: Edit did not open boot/hello.c")
            return
        text = page.input_value("#ed_text")
        hp_edit(page, text.replace('"Hello, World!"', '"Hello, patched!"'))
        st = hp_apply(page, r, "phone patch", problems, tag, phone=True)
        if not st:
            return
        rb_shots(page, shots, "hotpatch-hello-390-editor", problems, tag)
        page.emulate_media(color_scheme="dark")
        hp_continue(page)
        if not wait_for(lambda: "Hello, patched!" in screen(page), 120, 0.1):
            problems.append(f"{tag}: after Continue the screen is {screen(page)[-300:]!r}")
        if hp_state(page)["hscroll"]:
            problems.append(f"{tag}: the page scrolls sideways")
    finally:
        ctx.close()


def hp_minified(browser, base, shots, problems, r):
    tag = "minified-linux"
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full&paused=1#debugger")
        srcedit_ready(page, tag)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        sk = page.evaluate(SYM_JS, "start_kernel")
        page.fill("#dbg_addr", "start_kernel")
        page.click("#dbg_runto")
        ok, at, why = run_to_wait(page, {sk[1]}, 300)
        # The first stop can be on the way (the Run to goes on to the hit):
        # paused at start_kernel with the count still for 2 s.
        last = [None]

        def settled():
            now = page.evaluate("[MKB.runState, MKB.stepper.steps, MKB.stepper.nextPhys()]")
            same, last[0] = now == last[0], now
            return same and now[0] == "paused" and now[2] == sk[1]
        if not ok or not wait_for(settled, 120, 2):
            problems.append(f"{tag}: Run to start_kernel stopped at {at:#x}: {why!r} {last[0]}")
            return
        hp_reserved_zero(page, problems, tag, r)
        # Refused: the kernel is linked with LTO, which changed how
        # console_on_rootfs calls IS_ERR (fastcc: ECX).
        if not rbl_open(page, "init/main.c", False):
            problems.append(f"{tag}: Edit did not open init/main.c")
            return
        text = page.input_value("#ed_text")
        after = "\tinit_dup(file);\n\tinit_dup(file);\n\tinit_dup(file);\n"
        i = text.index(after) + len(after)
        hp_edit(page, text[:i] + '\tkernel_write(file, "Hot patched!\\n", 13, NULL);\n' + text[i:])
        st = hp_wait(page, ("refused", "ready"), 300)
        r["refused: LTO"] = st["status"]
        if st["state"] != "refused" or st["go"] or "LTO" not in st["status"]:
            problems.append(f"{tag}: console_on_rootfs edit: {st['state']} {st['status']!r}")
        page.evaluate("MKB.srcOverlay.revert('init/main.c')")  # the refused edit dropped (srcedit.js API)
        wait_for(lambda: "init/main.c" not in page.evaluate("MKB.srcOverlay.paths()"), 10)
        # A header: every unit that includes it would change.
        if not rbl_open(page, "include/linux/tty_driver.h", False):
            problems.append(f"{tag}: Edit did not open include/linux/tty_driver.h")
            return
        hp_edit(page, page.input_value("#ed_text") + "/* E7: a header edit */\n")
        st = hp_wait(page, ("", "refused", "ready"), 120)
        r["refused: a header"] = st["status"]
        if st["go"] or "header edit" not in st["status"]:
            problems.append(f"{tag}: a header edit: {st['state']} {st['status']!r}")
        page.evaluate("MKB.srcOverlay.revert('include/linux/tty_driver.h')")
        wait_for(lambda: not page.evaluate("MKB.srcOverlay.paths()"), 10)
        # do_exit (init exits after its two lines): it writes a line into
        # the VGA text memory before it panics.
        if not rbl_open(page, "kernel/exit.c", False):
            problems.append(f"{tag}: Edit did not open kernel/exit.c")
            return
        text = page.input_value("#ed_text")
        t_edit = hp_edit(page, text.replace("\tpanic(\"Attempted to kill init!", HP_DO_EXIT + "\tpanic(\"Attempted to kill init!", 1))
        st = hp_apply(page, r, "patch", problems, tag)
        if not st:
            return
        hp_check_jumps(page, st, problems, tag)
        if [f[0] for f in st["patches"][0]["funcs"]] != ["do_exit"]:
            problems.append(f"{tag}: the patch replaces {st['patches'][0]['funcs']}, want do_exit")
        code = hp_mem(page, HP_PA, 64)
        t_go = time.monotonic()
        hp_continue(page)
        # Running: the button says to pause first.
        st = hp_wait(page, ("",), 10)
        r["while it runs"] = st["status"]
        if st["go"] or "Pause the machine first" not in st["status"]:
            problems.append(f"{tag}: while it runs the button says {st['status']!r} (go={st['go']})")
        if not wait_for(lambda: "Hot patched do_exit" in screen(page) and "Still alive" in screen(page), 600, 0.2):
            problems.append(f"{tag}: after Continue the screen is {screen(page)[-400:]!r}")
        r["continue to the new line s"] = round(time.monotonic() - t_go, 2)
        r["edit saved to the new line s"] = round(time.monotonic() - t_edit, 2)
        if hp_mem(page, HP_PA, 64) != code:
            problems.append(f"{tag}: the reserved memory changed after the run")
        rb_shots(page, shots, "hotpatch-minified-1280-machine", problems, tag)
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()


def hp_debian(browser, base, shots, problems, r):
    tag = "debian-6.1-686-clang"
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    try:
        t0 = time.monotonic()
        page.goto(f"{base}/index.html?kernel={tag}&speed=full")
        srcedit_ready(page, tag)
        if not wait_for(lambda: "(initramfs)" in screen(page), 1500, 1):
            problems.append(f"{tag}: no (initramfs) prompt: {screen(page)[-300:]!r}")
            return
        r["boot to the prompt s"] = round(time.monotonic() - t0)
        show_tab(page, "machine")
        # The page may have stopped at the prompt itself (waiting for keys).
        if page.evaluate("MKB.runState") != "paused" and page.is_enabled("#dbg_pause"):
            page.click("#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 60)
        r["paused at"] = page.evaluate("MKB.stepper.steps")
        hp_reserved_zero(page, problems, tag, r)
        # Refused: pr_debug's static keys in run_init_process.
        if not rbl_open(page, "init/main.c", False):
            problems.append(f"{tag}: Edit did not open init/main.c")
            return
        text = page.input_value("#ed_text")
        hp_edit(page, text.replace('"Run %s as init process\\n"', '"Run %s as init (hot patched)\\n"'))
        st = hp_wait(page, ("refused", "ready"), 900)
        r["refused: static keys"] = st["status"]
        if st["state"] != "refused" or st["go"] or "static keys" not in st["status"]:
            problems.append(f"{tag}: run_init_process edit: {st['state']} {st['status']!r}")
        page.evaluate("MKB.srcOverlay.revert('init/main.c')")  # the refused edit dropped (srcedit.js API)
        wait_for(lambda: "init/main.c" not in page.evaluate("MKB.srcOverlay.paths()"), 10)
        # /proc/version's function.
        if not rbl_open(page, "fs/proc/version.c", False):
            problems.append(f"{tag}: Edit did not open fs/proc/version.c")
            return
        text = page.input_value("#ed_text")
        t_edit = hp_edit(page, text.replace("\tseq_printf(m, linux_proc_banner,", '\tseq_puts(m, "Hot patched /proc/version\\n");\n\tseq_printf(m, linux_proc_banner,'))
        st = hp_apply(page, r, "patch", problems, tag)
        if not st:
            return
        hp_check_jumps(page, st, problems, tag)
        code = hp_mem(page, HP_PA, 64)
        t_go = time.monotonic()
        hp_continue(page)
        page.click("#screen_container")
        page.keyboard.type("cat /proc/version\n", delay=60)
        if not wait_for(lambda: "Hot patched /proc/version" in screen(page), 300, 0.2):
            problems.append(f"{tag}: after cat /proc/version the screen is {screen(page)[-400:]!r}")
        r["continue to the new line s"] = round(time.monotonic() - t_go, 2)
        r["edit saved to the new line s"] = round(time.monotonic() - t_edit, 2)
        if hp_mem(page, HP_PA, 64) != code:
            problems.append(f"{tag}: the reserved memory changed after the run")
        rb_shots(page, shots, "hotpatch-debian-1280-machine", problems, tag)
    finally:
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
        ctx.close()


def suite_hotpatch(ctx):
    if not ctx.base.startswith("https://") and not ((SITE / "kits" / "index.json").exists() and (SITE / "clang" / "clang.wasm").exists()):
        return report("hotpatch", ["no site/kits or site/clang/clang.wasm: nothing to compile"], {})
    failed = False
    # MKB_HOTPATCH_ONLY=hp_hello,hp_debian: those cases only.
    only = [x for x in os.environ.get("MKB_HOTPATCH_ONLY", "").split(",") if x]
    for name, fn, width in (("hello", hp_hello, 1280), ("hello", hp_hello_phone, 390), ("minified-linux", hp_minified, 1280),
                            ("debian-6.1-686-clang", hp_debian, 1280)):
        if only and fn.__name__ not in only:
            continue
        if name not in ctx.entries:
            failed |= report("hotpatch", [f"no kernel {name}"], {})
            continue
        problems, r = [], {}
        t0 = time.monotonic()
        try:
            fn(ctx.browser, ctx.base, ctx.shots, problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
        failed |= report("hotpatch", problems, r, kernel=name, width=width, seconds=round(time.monotonic() - t0))
    return failed
