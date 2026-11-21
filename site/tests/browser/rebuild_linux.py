"""Browser test suite rebuild_linux (test_browser.py --suites)."""

import gzip
import hashlib
import pathlib
import re
import tempfile
import time

from tests.browser.common import (
    ALIVE, gate_on_screen, HELLO, kernel_entries, report, show_tab, shown_tab, SITE, srcedit_overlay, srcedit_ready,
    src_tab_once, srcedit_state, wait_for,
)
from tests.browser.rebuild import (
    elf32_symbols, rb_buildlog, rb_buildlog_open, rb_debug, rb_debug_check, rb_download, rb_kit, RB_LINES_JS, rb_press,
    rb_shipped_image, rb_shots, rb_state, rb_wait, RB_WATCH_JS,
)


# ---- rebuild of the minified Linux -O0 and -Os (E5) -------------------------
# The same clicks on a real kernel: open init/main.c from the Source tab's
# file list, Edit, add a line that writes a string to the console (printk is
# a no-op in this kernel, /dev/console is not), Rebuild and boot. The page
# compiles init/main.o with clang.wasm, runs the kit's 44 link steps (the LTO
# ld -r, ld, the boot code, tools/build), boots the bzImage and makes the
# data files in this browser: the string is on the screen before /init's
# Hello, World!, the DWARF lines of kernel_init are the edited file's, the
# Source panel shows the edited text. Then a header edit: the objects
# compiled are exactly the users of that header and of init/main.c in the
# kit's dependency map (kitUsers). Revert all and "compile every object":
# 127 objects compiled here, vmlinux and bzImage are CI's bytes, and the
# bzImage is the page's shipped one but for the build-id. Times from the
# press to the string on the screen are reported.
RBL_FILE = "init/main.c"
RBL_MSG = "Hello from an edited init/main.c!"
RBL_DECL = "static void __init console_on_rootfs(void)\n"
RBL_INSERT_DECL = f'static const char e5_msg[] = "{RBL_MSG}\\n";\n\n'
RBL_AFTER = "\tinit_dup(file);\n\tinit_dup(file);\n\tinit_dup(file);\n"
RBL_INSERT = "\tkernel_write(file, e5_msg, sizeof(e5_msg) - 1, NULL);\n"
RBL_HEADER = "include/linux/tty_driver.h"
RBL_HEADER_ADD = "/* E5: a header edit recompiles the objects that include it. */\n"
# A function of a unit the edit does not touch (E6: its lines, locals and
# CFI stay after the rebuild).
RBL_UNTOUCHED = "filp_open"


def rbl_users(kit):
    """kitUsers(kit) in Python: path -> the objects whose deps list it."""
    users = {}
    for o in kit["objects"]:
        for d in o["deps"]:
            users.setdefault(kit["files"][d][0], []).append(o["out"])
    return users


def rbl_open(page, path, phone):
    """path from the Source tab's file list (the filter, then the file's
    row), then Edit."""
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 120)
    page.fill("#cov_filter", path.split("/")[-1])
    rb_press(page, f'#cov_tree .covrow[data-kind="file"][data-path="{path}"] .covname', phone)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == path, 60)
    if not srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", phone)
    return wait_for(lambda: (lambda s: s["editing"] and s["taShown"])(srcedit_state(page)), 30, 0.05)


def rbl_edited(orig):
    i = orig.index(RBL_DECL)
    t = orig[:i] + RBL_INSERT_DECL + orig[i:]
    j = t.index(RBL_AFTER) + len(RBL_AFTER)
    return t[:j] + RBL_INSERT + t[j:]


def rbl_type(page, orig):
    """As a user: the caret before console_on_rootfs, type the string; the
    caret after the three init_dup lines, type the call."""
    ta = page.locator("#ed_text")
    ta.click()
    caret = "(s) => { const ta = document.getElementById('ed_text'), i = ta.value.indexOf(s[0]) + s[1]; ta.setSelectionRange(i, i); }"
    page.evaluate(caret, [RBL_DECL, 0])
    page.keyboard.type(RBL_INSERT_DECL)
    page.evaluate(caret, [RBL_AFTER, len(RBL_AFTER)])
    page.keyboard.type(RBL_INSERT)
    return rbl_edited(orig)


def rbl_compiled(seen):
    """The objects the progress line said it compiled, in order."""
    out = []
    for t, _ in seen:
        m = re.match(r"Compil(?:ing|ed) (\S+) \(\d+ of \d+\)", t)
        if m and m[1] not in out:
            out.append(m[1])
    return out


def rbl_build(page, sel, phone, timeout=600, open_log=False):
    """Press sel, wait for the end: (state, seconds to the end, seconds to
    the edited string on the screen or None, the progress texts). open_log:
    open the build log in the Source tab as soon as it shows (the build
    then runs on with it open)."""
    src_tab_once(page)
    page.evaluate(RB_WATCH_JS)
    # The string counts once the screen was without it (the kernel booted
    # before this build may have printed it); wall time, not the page's
    # virtual clock.
    page.evaluate("""(msg) => { clearInterval(window.rbScrTimer); const s = window.rbScr = { t0: Date.now(), cleared: false, at: null };
      window.rbScrTimer = setInterval(() => { const has = document.getElementById('screen_container').innerText.includes(msg);
        if (!has) s.cleared = true; else if (s.cleared && s.at === null) s.at = (Date.now() - s.t0) / 1000; }, 50); }""", RBL_MSG)
    t0 = time.monotonic()
    rb_press(page, sel, phone)
    if open_log and wait_for(lambda: page.evaluate("(() => { const b = document.getElementById('rb_buildlog_box'); return !!b && !b.hidden; })()"), 30, 0.05):
        show_tab(page, "source")
        rb_buildlog_open(page, phone)
    wait_for(lambda: rb_state(page)["state"] in ("done", "failed"), timeout, 0.1)
    st = rb_state(page)
    s_end = round(time.monotonic() - t0, 1)
    if st["state"] == "done":
        wait_for(lambda: page.evaluate("rbScr.at") is not None, 120, 0.1)
    at = page.evaluate("clearInterval(rbScrTimer), rbScr.at")
    st["speed"] = page.evaluate("[document.getElementById('dbg_speed').value, MKB.prefSpeed, MKB.runState]")
    return st, s_end, at and round(at, 1)


def rbl_screen_ok(page):
    t = page.inner_text("#screen_container")
    m = t.find(RBL_MSG)
    return m >= 0 and t.find(HELLO, m) >= 0 and t.find(ALIVE, t.find(HELLO, m)) >= 0


def rebuild_linux_run(browser, base, shots, tmp, name, phone, scheme):
    """A minified Linux: edit init/main.c in the page, rebuild, boot, the
    data files of the rebuilt kernel; a header edit compiles its users;
    an unchanged rebuild of every object gives CI's bytes."""
    problems, r, errors = [], {}, []
    tag = f"{name} {390 if phone else 1280} px {scheme}"
    stem = f"rebuild-{name}-{390 if phone else 1280}"
    entry = kernel_entries()[name]
    kit = rb_kit(base, name)
    users = rbl_users(kit)
    img_path = kit["shipped"]["path"]
    elf_path = next(p for p, _, _ in kit["products"] if p != img_path)
    orig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + RBL_FILE + ".gz"})).decode()
    horig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + RBL_HEADER + ".gz"})).decode()
    if phone:
        ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True, accept_downloads=True)
    else:
        ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&speed=full")
    srcedit_ready(page, name)
    if not wait_for(lambda: rb_state(page)["go"], 60):
        ctx.close()
        return [f"{tag}: no Rebuild and boot to press: {rb_state(page)['status']!r}"], r
    if not wait_for(lambda: gate_on_screen(page), 120):
        problems.append(f"{tag}: the shipped kernel's screen: {page.inner_text('#screen_container')[:300]!r}")
    # The Debugger's view of a function of an untouched unit (fs/open.c), to
    # compare after the rebuild (plan E6).
    dbg_before = rb_debug(page, [RBL_UNTOUCHED])

    # 1. init/main.c edited as a user, Rebuild and boot in the editor bar.
    if not rbl_open(page, RBL_FILE, phone):
        ctx.close()
        return problems + [f"{tag}: Edit did not open {RBL_FILE}: {srcedit_state(page)['state']!r}"], r
    if phone:
        want = rbl_edited(orig)
        page.locator("#ed_text").fill(want)
    else:
        want = rbl_type(page, orig)
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    if srcedit_state(page)["value"] != want:
        problems.append(f"{tag}: the editor holds another text than the one typed")
    rb_shots(page, shots, f"{stem}-editor", problems, f"{tag} editor")
    page.emulate_media(color_scheme=scheme)
    st, s_end, s_screen = rbl_build(page, "#rb_go_panel", phone, open_log=True)
    compiled = rbl_compiled(st["seen"])
    r["edit"] = {"status": st["status"], "compiled": compiled, "s to the string on the screen": s_screen, "s to the data files": s_end, "speed": st["speed"]}
    if st["state"] != "done":
        ctx.close()
        return problems + [f"{tag}: the rebuild did not finish: {st['state']} {st['status']!r} log {st['log'][:600]!r}"], r
    ph = re.search(r"([\d.]+) s \(fetching ([\d.]+) s, compiling ([\d.]+) s, linking ([\d.]+) s\)", st["status"])
    r["edit"]["phases s (all, fetch, compile, link)"] = ph and [float(x) for x in ph.groups()]
    if not ph or not float(ph[1]) >= float(ph[2]) + float(ph[3]) + float(ph[4]) - 0.15:
        problems.append(f"{tag}: the status does not say the time of each phase: {st['status']!r}")
    for w in (f"Booted {name} (rebuilt here)", "built from your 1 changed file", "1 object compiled", "44 link steps", "Its data files are made"):
        if w not in st["status"]:
            problems.append(f"{tag}: the status after the rebuild lacks {w!r}: {st['status']!r}")
    if compiled != sorted(users[RBL_FILE]):
        problems.append(f"{tag}: compiled {compiled}, the kit's users of {RBL_FILE} are {users[RBL_FILE]}")
    texts = [t for t, _ in st["seen"]]
    for w in ("Loading the compiler", "Fetching the kit", "Compiling init/main.o (1 of 1)", "Linking: step 44 of 44", "Making its data files"):
        if not any(w in t for t in texts):
            problems.append(f"{tag}: the progress never said {w!r}")
    fracs = [f for _, f in st["seen"] if f is not None]
    if not fracs or fracs != sorted(fracs):
        problems.append(f"{tag}: the progress bar went {fracs[:40]}")
    # The build log, opened while the build ran: one command per object
    # compiled and per link step, each with its exit and time, and it
    # followed its end as it grew.
    lg = rb_buildlog(page, tag, problems, r, ["$ clang ", "init/main.c", "$ ld.lld ", "exit 0, ", "# Done in "])
    if lg:
        want_cmds = 1 + len(kit["steps"])
        if r["log"]["commands"] != want_cmds:
            problems.append(f"{tag}: the build log has {r['log']['commands']} commands, the build ran {want_cmds}")
        opened = [x for x in lg["seen"] if x[0] == "running" and x[3]]
        r["log"]["grew while open"] = len(opened)
        if not opened or opened[-1][4] > 2:
            problems.append(f"{tag}: the open build log did not follow its end while the build ran: {opened[-3:]}")
    # 2. Booted in the Machine tab: the string, then /init's lines.
    if shown_tab(page) != "machine":
        problems.append(f"{tag}: after the rebuild the {shown_tab(page)} tab is shown, not the Machine tab")
    if s_screen is None or not wait_for(lambda: rbl_screen_ok(page), 120):
        problems.append(f"{tag}: the rebuilt kernel's screen: {page.inner_text('#screen_container')[:400]!r}")
    if phone:
        vis = page.evaluate("(() => { const p = document.getElementById('src_panel'); return !p.hidden && p.getBoundingClientRect().width > 0; })()")
        if vis:
            problems.append(f"{tag}: the Source sheet still covers the screen after the rebuild")
    rb_shots(page, shots, f"{stem}-machine", problems, f"{tag} Machine tab")
    page.emulate_media(color_scheme=scheme)

    # 3. The data files made in this browser: symbols, disassembly, DWARF, source.
    if not wait_for(lambda: page.evaluate("MKB.GEN.state === 'done' && MKB.DBG.mode === 'shards' && MKB.SRC.mode === 'shipped' && /indexed in this browser/.test(MKB.SRC.info || '')"), 300):
        problems.append(f"{tag}: the data files: {page.evaluate('[MKB.GEN.state, MKB.GEN.note, MKB.DBG.mode, MKB.SRC.mode, MKB.SRC.info]')}")
    k = page.evaluate("""() => ({ gen: MKB.GEN.state, dbg: MKB.DBG.mode, generated: !!(MKB.symbols.imported && MKB.symbols.imported.generated),
      file: MKB.symbols.imported && MKB.symbols.imported.file, insns: MKB.disasm ? MKB.disasm.insns.length : 0, nsyms: MKB.symbols.symbols.length })""")
    r["data"] = k
    if not k["generated"] or k["file"] != elf_path.split("/")[-1] or k["insns"] < 1000 or k["nsyms"] < 500:
        problems.append(f"{tag}: the rebuilt kernel's data files: {k}")
    name_elf, elf = rb_download(page, "#rb_dl_elf", phone, tmp)
    _, img = rb_download(page, "#rb_dl_image", phone, tmp)
    r["downloads"] = [name_elf, len(elf), len(img)]
    if (RBL_MSG + "\n").encode() not in img:
        problems.append(f"{tag}: the downloaded bzImage lacks {RBL_MSG!r}")
    nm = elf32_symbols(elf)
    wl = want.split("\n")
    at = lambda s: f"{RBL_FILE}:{next(i for i, l in enumerate(wl, 1) if s in l)}"
    lines = {}
    for fn in ("kernel_init", "console_on_rootfs", "kernel_init_freeable"):
        got = page.evaluate(RB_LINES_JS, fn)
        if got:
            lines[fn] = got["lines"]
            if got["vaddr"] != nm.get(fn):
                problems.append(f"{tag}: {fn}: the page's symbol {hex(got['vaddr'])}, the ELF's {nm.get(fn) and hex(nm[fn])}")
    r["lines"] = lines
    every = {x for v in lines.values() for x in v}
    for s in ("kernel_write(file, e5_msg", 'panic("cannot execute /init");'):
        if at(s) not in every:
            problems.append(f"{tag}: the DWARF lines of {sorted(lines)} lack {at(s)} ({s!r} in the edited file): {sorted(every)[:30]}")
    old_panic = f"{RBL_FILE}:{next(i for i, l in enumerate(orig.split(chr(10)), 1) if 'cannot execute /init' in l)}"
    if old_panic in lines.get("kernel_init", []):
        problems.append(f"{tag}: kernel_init's DWARF lines still have the shipped file's {old_panic}")
    rb_debug_check(dbg_before, rb_debug(page, [RBL_UNTOUCHED, "kernel_init"]), [RBL_UNTOUCHED], ["kernel_init"], problems, r, tag)
    wait_for(lambda: page.evaluate(f"MKB.srcFile('{RBL_FILE}').state") == "ok", 30)
    if page.evaluate(f"MKB.srcFile('{RBL_FILE}').lines") != wl:
        problems.append(f"{tag}: the source the listings read is not the edited text")
    # The Source panel: the edited text, said to be the one built.
    page.evaluate("(p) => MKB.openSourceFile(p)", RBL_FILE)
    if srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", phone)
    if not wait_for(lambda: "rebuilt here from your text" in srcedit_state(page)["state"] and "kernel_write" in page.inner_text("#src_body"), 30):
        problems.append(f"{tag}: the Source panel: {srcedit_state(page)['state']!r}")
    rb_shots(page, shots, f"{stem}-panel", problems, f"{tag} Source panel")
    page.emulate_media(color_scheme=scheme)

    # 4. A header edit: its users and init/main.c's are compiled, no other.
    if not rbl_open(page, RBL_HEADER, phone):
        problems.append(f"{tag}: Edit did not open {RBL_HEADER}")
    else:
        page.locator("#ed_text").fill(horig + RBL_HEADER_ADD)
        wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
        st, s_end, s_screen = rbl_build(page, "#rb_go_panel", phone)
        compiled = rbl_compiled(st["seen"])
        want_objs = sorted(set(users[RBL_HEADER]) | set(users[RBL_FILE]))
        r["header"] = {"status": st["status"], "compiled": compiled, "kit users": want_objs, "s to the string on the screen": s_screen, "s to the data files": s_end, "speed": st["speed"]}
        if st["state"] != "done" or sorted(compiled) != want_objs or f"{len(want_objs)} objects compiled" not in st["status"] \
                or "built from your 2 changed files" not in st["status"]:
            problems.append(f"{tag}: the header edit: {st['state']} {st['status']!r}, compiled {compiled}, the kit's users {want_objs}, log {st['log'][:300]!r}")
        if s_screen is None:
            problems.append(f"{tag}: after the header edit the screen: {page.inner_text('#screen_container')[:300]!r}")

    # 5. Revert all, compile every object: CI's bytes.
    show_tab(page, "source")
    rb_press(page, "#ed_revert_all", phone)
    rb_press(page, "#ed_revert_all", phone)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    page.locator("#rb_all").scroll_into_view_if_needed()
    page.locator("#rb_all").tap() if phone else page.check("#rb_all")
    if not page.is_checked("#rb_all"):
        problems.append(f"{tag}: compile every object is not checked")
    t0 = time.monotonic()
    rb_press(page, "#rb_go", phone)
    st = rb_wait(page, ("done", "failed"), 900)
    r["unchanged"] = {"status": st["status"], "s": round(time.monotonic() - t0, 1), "speed": page.evaluate("[document.getElementById('dbg_speed').value, MKB.prefSpeed, MKB.runState]")}
    n = len(kit["objects"])
    if st["state"] != "done" or "identical to CI's build" not in st["status"] or f"{n} objects compiled" not in st["status"]:
        problems.append(f"{tag}: the unchanged rebuild: {st['state']} {st['status']!r} {st['log'][:300]!r}")
    if not wait_for(lambda: gate_on_screen(page), 120) or RBL_MSG in page.inner_text("#screen_container"):
        problems.append(f"{tag}: the unchanged rebuild's screen: {page.inner_text('#screen_container')[:300]!r}")
    _, elf = rb_download(page, "#rb_dl_elf", phone, tmp)
    _, img = rb_download(page, "#rb_dl_image", phone, tmp)
    shipped = rb_shipped_image(base, entry)
    want_sha = {p: h for p, h, _ in kit["products"]}
    masked = lambda b: b"".join(b[a:o] for a, o in zip([0] + [x + w for x, w in kit["shipped"]["differs"]],
                                                       [x for x, _ in kit["shipped"]["differs"]] + [len(b)]))
    r["unchanged bytes"] = {"vmlinux": hashlib.sha256(elf).hexdigest() == want_sha[elf_path],
                            "bzImage": hashlib.sha256(img).hexdigest() == want_sha[img_path],
                            "shipped but the build-id": len(img) == len(shipped) and masked(img) == masked(shipped)}
    if not all(r["unchanged bytes"].values()):
        problems.append(f"{tag}: the unchanged rebuild is not CI's: {r['unchanged bytes']}")
    page.locator("#rb_all").scroll_into_view_if_needed()
    page.locator("#rb_all").tap() if phone else page.uncheck("#rb_all")
    rb_shots(page, shots, f"{stem}-source", problems, f"{tag} Source tab")
    page.emulate_media(color_scheme=scheme)

    # 6. Back to the shipped kernel.
    rb_press(page, "#rb_back", phone)
    wait_for(lambda: rb_state(page)["kernel"] == name, 30)
    st = rb_state(page)
    if st["kernel"] != name or st["from"] or st["back"]:
        problems.append(f"{tag}: back to the shipped kernel: {st}")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def suite_rebuild_linux(ctx):
    if not ctx.base.startswith("https://") and not ((SITE / "kits" / "index.json").exists() and (SITE / "clang" / "clang.wasm").exists()):
        return report("rebuild_linux", ["no site/kits (the kits artifact) or site/clang/clang.wasm: nothing to rebuild"], {})
    failed = False
    with tempfile.TemporaryDirectory() as td:
        for name, phone, scheme in (("minified-linux", False, "light"), ("minified-linux-os", True, "dark")):
            if name not in ctx.entries:
                continue
            t0 = time.monotonic()
            problems, r = rebuild_linux_run(ctx.browser, ctx.base, ctx.shots, pathlib.Path(td), name, phone, scheme)
            failed |= report("rebuild_linux", problems, r, kernel=name, width=390 if phone else 1280, scheme=scheme,
                             seconds=round(time.monotonic() - t0))
    return failed
