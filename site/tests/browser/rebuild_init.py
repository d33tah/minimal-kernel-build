"""Browser test suite rebuild_init (test_browser.py --suites)."""

import pathlib
import tempfile
import time

from tests.browser.common import (
    ALIVE, gate_on_screen, HELLO, kernel_entries, report, show_tab, shown_tab, SITE, srcedit_overlay, srcedit_ready, srcedit_state,
    wait_for,
)
from tests.browser.rebuild import rb_buildlog, rb_buildlog_open, rb_download, rb_kit, rb_press, rb_shots, rb_state, RB_WATCH_JS
from tests.browser.rebuild_linux import rbl_compiled


# ---- the minified Linux's /init rebuilt in the page (E9) --------------------
# /init is minified/elo/init.nasm, assembled by `nasm -f bin` and packed with
# elo/'s other files into usr/initramfs_data.cpio, which initramfs_data.S
# links into vmlinux. As a user: the Source panel's Files lists
# elo/init.nasm (no DWARF names it), pick it, Edit, change the string /init
# prints, Rebuild and boot. The page assembles /init with nasm in wasm, makes
# the cpio (kitcpio.js), compiles usr/initramfs_data.o and runs the link
# steps; the screen shows the new string and then "Still alive", the
# bzImage holds it. An assembly error stops the build with nasm's file:line
# message and the machine keeps the kernel it ran. The unchanged rebuild of
# every object (nasm and the cpio included) is rebuild_linux's last step.
RBI_FILE = "elo/init.nasm"
RBI_FROM = "'Hello, World!'"
RBI_MSG = "Hello from an edited /init!"
RBI_BAD = "    mov eax, [nosuchlabel]\n"


def rbi_open(page, phone):
    """elo/init.nasm from the Source panel's Files switcher (the panel
    opened from the file list's init/main.c), then Edit: (None or what went
    wrong, the panel's text before Edit)."""
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 120)
    page.fill("#cov_filter", "main.c")
    rb_press(page, '#cov_tree .covrow[data-kind="file"][data-path="init/main.c"] .covname', phone)
    wait_for(lambda: page.get_attribute("#src_body", "data-file") == "init/main.c", 60)
    page.tap("#src_files") if phone else page.click("#src_files")
    if not wait_for(lambda: page.is_visible("#src_q"), 5):
        return "the file switcher did not open", ""
    page.fill("#src_q", "init.nasm")
    sel = f'#src_list [role=option][data-path="{RBI_FILE}"]'
    if not wait_for(lambda: page.locator(sel).count() > 0, 10):
        return "Files does not list " + RBI_FILE + ": " + page.inner_text("#src_list")[:300], ""
    page.tap(sel) if phone else page.click(sel)
    if not wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == RBI_FILE, 30):
        return "picking it did not open it: " + str(page.get_attribute("#src_body", "data-state")), ""
    shown = page.inner_text("#src_panel")
    if not srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", phone)
    if not wait_for(lambda: (lambda s: s["editing"] and s["taShown"])(srcedit_state(page)), 30, 0.05):
        return "Edit did not open the editor: " + srcedit_state(page)["state"], shown
    return None, shown


def rbi_build(page, phone, timeout=600):
    """Rebuild and boot in the editor bar, the build log opened while it
    runs: (state, seconds to the end, seconds to the new string on the
    screen or None)."""
    page.evaluate(RB_WATCH_JS)
    page.evaluate("""(msg) => { clearInterval(window.rbiTimer); const s = window.rbiScr = { t0: Date.now(), at: null };
      window.rbiTimer = setInterval(() => { if (s.at === null && document.getElementById('screen_container').innerText.includes(msg))
        s.at = (Date.now() - s.t0) / 1000; }, 50); }""", RBI_MSG)
    t0 = time.monotonic()
    rb_press(page, "#rb_go_panel", phone)
    if wait_for(lambda: page.evaluate("(() => { const b = document.getElementById('rb_buildlog_box'); return !!b && !b.hidden; })()"), 30, 0.05):
        show_tab(page, "source")
        rb_buildlog_open(page, phone)
    wait_for(lambda: rb_state(page)["state"] in ("done", "failed"), timeout, 0.1)
    st = rb_state(page)
    s_end = round(time.monotonic() - t0, 1)
    if st["state"] == "done":
        wait_for(lambda: page.evaluate("rbiScr.at") is not None, 180, 0.1)
    at = page.evaluate("clearInterval(rbiTimer), rbiScr.at")
    return st, s_end, at and round(at, 1)


def rebuild_init_run(browser, base, shots, tmp, name, phone, scheme):
    problems, r, errors = [], {}, []
    tag = f"{name} {390 if phone else 1280} px {scheme}"
    stem = f"rebuild-init-{name}-{390 if phone else 1280}"
    kit = rb_kit(base, name)
    gens = kit.get("gen") or []
    if [g["tool"] for g in gens] != ["nasm", "cpio"]:
        return [f"{tag}: the kit's generators are {[g['tool'] for g in gens]}, not nasm then cpio"], r
    cpio = gens[1]["outputs"][0][0]
    users = sorted(o["out"] for o in kit["objects"] if any(kit["files"][d][0] == cpio for d in o["deps"]))
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True, accept_downloads=True) if phone else \
        browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
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

    # 1. elo/init.nasm from Files, its note, Edit; the string typed over.
    why, info = rbi_open(page, phone)
    if why:
        ctx.close()
        return problems + [f"{tag}: {RBI_FILE}: {why}"], r
    orig = srcedit_state(page)["value"]
    if "the source of /init" not in info:
        problems.append(f"{tag}: the Source panel does not say what {RBI_FILE} is: {info[-400:]!r}")
    want = orig.replace(RBI_FROM, f"'{RBI_MSG}'")
    if want == orig:
        ctx.close()
        return problems + [f"{tag}: {RBI_FILE} has no {RBI_FROM}"], r
    if phone:
        page.locator("#ed_text").fill(want)
    else:
        page.locator("#ed_text").click()
        page.evaluate("(s) => { const ta = document.getElementById('ed_text'), i = ta.value.indexOf(s); ta.setSelectionRange(i + 1, i + s.length - 1); }", RBI_FROM)
        page.keyboard.type(RBI_MSG)
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    if srcedit_state(page)["value"] != want:
        problems.append(f"{tag}: the editor holds another text than the one typed")
    rb_shots(page, shots, f"{stem}-editor", problems, f"{tag} editor")
    page.emulate_media(color_scheme=scheme)

    # 2. Rebuild and boot: nasm, the cpio, its one user compiled, the link.
    st, s_end, s_screen = rbi_build(page, phone)
    compiled = rbl_compiled(st["seen"])
    r["edit"] = {"status": st["status"], "compiled": compiled, "s to the string on the screen (edit-to-boot)": s_screen, "s to the end": s_end}
    if st["state"] != "done":
        ctx.close()
        return problems + [f"{tag}: the rebuild did not finish: {st['state']} {st['status']!r} {st['log'][:600]!r}"], r
    for w in (f"Booted {name} (rebuilt here)", "built from your 1 changed file", f"{len(users)} object compiled", f"{len(kit['steps'])} link steps"):
        if w not in st["status"]:
            problems.append(f"{tag}: the status lacks {w!r}: {st['status']!r}")
    if compiled != users:
        problems.append(f"{tag}: compiled {compiled}, the kit's users of {cpio} are {users}")
    texts = [t for t, _ in st["seen"]]
    for w in ("Making elo/init (nasm, 1 of 2)", f"Making {cpio} (gen_init_cpio, 2 of 2)", f"Compiling {users[0]} (1 of 1)"):
        if not any(w in t for t in texts):
            problems.append(f"{tag}: the progress never said {w!r}")
    lg = rb_buildlog(page, tag, problems, r, ["$ nasm -f bin -o elo/init elo/init.nasm", "$ usr/gen_init_cpio <<EOF",
                                               "file /init elo/init 755 0 0", "file /init.nasm elo/init.nasm 644 0 0",
                                               "nod /dev/console 600 0 0 c 5 1", f"stdout to {cpio} (", "$ clang ", "# Done in "])
    if lg and r["log"]["commands"] != len(gens) + len(users) + len(kit["steps"]):
        problems.append(f"{tag}: the build log has {r['log']['commands']} commands, the build ran {len(gens) + len(users) + len(kit['steps'])}")
    if shown_tab(page) != "machine":
        problems.append(f"{tag}: after the rebuild the {shown_tab(page)} tab is shown, not the Machine tab")
    screen = lambda: page.inner_text("#screen_container")
    if s_screen is None or not wait_for(lambda: screen().find(ALIVE, max(0, screen().find(RBI_MSG))) > screen().find(RBI_MSG) >= 0, 120):
        problems.append(f"{tag}: the rebuilt kernel's screen: {screen()[:400]!r}")
    elif HELLO in screen():
        problems.append(f"{tag}: the rebuilt kernel still printed {HELLO!r}: {screen()[:400]!r}")
    rb_shots(page, shots, f"{stem}-machine", problems, f"{tag} Machine tab")
    page.emulate_media(color_scheme=scheme)
    _, img = rb_download(page, "#rb_dl_image", phone, tmp)
    if (RBI_MSG + "\n").encode() not in img or b"Hello, World!\n" in img:
        problems.append(f"{tag}: the downloaded bzImage lacks {RBI_MSG!r} or still has Hello, World!")
    # The rebuilt kernel's src/ (made here) lists elo/init.nasm too.
    if not wait_for(lambda: page.evaluate("MKB.GEN.state === 'done' && MKB.SRC.mode === 'shipped' && /indexed in this browser/.test(MKB.SRC.info || '')"), 300):
        problems.append(f"{tag}: the data files: {page.evaluate('[MKB.GEN.state, MKB.GEN.note, MKB.SRC.mode, MKB.SRC.info]')}")
    elif not page.evaluate(f"MKB.SRC.have.has('{RBI_FILE}')"):
        problems.append(f"{tag}: the rebuilt kernel's src/ does not list {RBI_FILE}")
    wait_for(lambda: page.evaluate(f"MKB.srcFile('{RBI_FILE}').state") == "ok", 30)
    if page.evaluate(f"MKB.srcFile('{RBI_FILE}').lines") != want.split("\n"):
        problems.append(f"{tag}: the rebuilt kernel's {RBI_FILE} is not the edited text")

    # 3. An assembly error: nasm's file:line message, the kernel stays.
    show_tab(page, "source")
    if not srcedit_state(page)["editing"] or page.get_attribute("#src_body", "data-file") != RBI_FILE:
        page.evaluate("(p) => MKB.openSourceFile(p)", RBI_FILE)
        if not srcedit_state(page)["editing"]:
            rb_press(page, "#src_edit", phone)
        wait_for(lambda: srcedit_state(page)["taShown"], 30)
    lines = want.split("\n")
    at = next(i for i, l in enumerate(lines) if "int 0x80" in l) + 1
    bad = "\n".join(lines[:at] + [RBI_BAD.rstrip("\n")] + lines[at:])
    page.locator("#ed_text").fill(bad)
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    before = page.evaluate("MKB.image.name")
    page.evaluate(RB_WATCH_JS)
    rb_press(page, "#rb_go_panel", phone)
    wait_for(lambda: rb_state(page)["state"] in ("done", "failed"), 300, 0.1)
    st = rb_state(page)
    msg = f"{RBI_FILE}:{at + 1}: error:"
    r["asm error"] = {"status": st["status"], "log": st["log"][:300]}
    if st["state"] != "failed" or msg not in st["log"] or not st["logShown"] or "Not built: elo/init: nasm failed" not in st["status"]:
        problems.append(f"{tag}: the assembly error: {st['state']} {st['status']!r}, messages {st['log'][:300]!r} (want {msg!r})")
    if rb_buildlog(page, tag, problems, r, ["$ nasm -f bin -o elo/init elo/init.nasm", msg, "exit 1"], key="asm log") and \
            "$ clang " in page.inner_text("#rb_buildlog"):
        problems.append(f"{tag}: the build went on after nasm failed")
    if page.evaluate("MKB.image.name") != before or not wait_for(lambda: RBI_MSG in screen(), 30):
        problems.append(f"{tag}: after the error the machine runs {page.evaluate('MKB.image.name')}, screen {screen()[:200]!r}")
    rb_shots(page, shots, f"{stem}-error", problems, f"{tag} assembly error")
    page.emulate_media(color_scheme=scheme)

    # 4. Revert, back to the shipped kernel.
    rb_press(page, "#ed_revert_all", phone)
    rb_press(page, "#ed_revert_all", phone)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    rb_press(page, "#rb_back", phone)
    wait_for(lambda: rb_state(page)["kernel"] == name, 30)
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def suite_rebuild_init(ctx):
    if not ctx.base.startswith("https://") and not ((SITE / "kits" / "index.json").exists() and (SITE / "clang" / "clang.wasm").exists()):
        return report("rebuild_init", ["no site/kits (the kits artifact) or site/clang/clang.wasm: nothing to rebuild"], {})
    failed = False
    with tempfile.TemporaryDirectory() as td:
        for name, phone, scheme in (("minified-linux", False, "light"), ("minified-linux-os", True, "dark")):
            if name not in ctx.entries or name not in kernel_entries():
                continue
            t0 = time.monotonic()
            problems, r = rebuild_init_run(ctx.browser, ctx.base, ctx.shots, pathlib.Path(td), name, phone, scheme)
            failed |= report("rebuild_init", problems, r, kernel=name, width=390 if phone else 1280, scheme=scheme,
                             seconds=round(time.monotonic() - t0))
    return failed
