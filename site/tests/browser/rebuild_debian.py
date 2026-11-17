
"""Browser test suite rebuild_debian (test_browser.py --suites)."""

import gzip
import hashlib
import json
import pathlib
import tempfile
import time

from tests.browser.common import (
    kernel_entries, report, show_tab, SITE, srcedit_overlay, srcedit_ready, srcedit_state, wait_for,
)
from tests.browser.rebuild import (
    elf32_symbols, rb_download, rb_kit, RB_LINES_JS, rb_press, rb_shipped_image, rb_shots, rb_state, RB_WATCH_JS,
)
from tests.browser.rebuild_linux import rbl_compiled, rbl_open, rbl_users


# ---- rebuild of the editable Debian (E6) -------------------------------------
# The page's kernel debian-6.1-686-clang (Debian 6.1 686 rebuilt from its
# source with clang, gen-debian-clang.sh) booted to Debian's initramfs
# prompt, then in the Source tab:
#   1. init/main.c edited ("Run %s as init process" -> "Run %s as init
#      (edited in the page)"), Rebuild and boot: exactly init/main.o is
#      compiled (the kit's users of the file; recordmcount after it), the
#      kit's link steps run, the kernel boots to the edited line and the
#      (initramfs) prompt; the edit-to-screen time is measured; the data
#      files of the rebuilt kernel are made in this browser and run_init_process's
#      DWARF lines are the edited file's (its DWARF covers the compiled units
#      only, so the Source tab then lists their files: recorded);
#   2. Back to the shipped kernel, Pause, a header edit: the objects that include
#      it and init/main.o (still edited) are compiled, no other;
#   3. Back again, Pause, everything reverted, Rebuild and boot: no object
#      compiled, the bzImage and vmlinux are CI's bytes and the bzImage is
#      the page's.
# Screenshots (light and dark) after the boot of the edit and of the source.
NAME = "debian-6.1-686-clang"
FILE = "init/main.c"
FROM, TO = "Run %s as init process", "Run %s as init (edited in the page)"
SHOWN = TO.replace("%s", "/init")
PROMPT = "(initramfs)"
HEADER_ADD = "/* E6: a header edit recompiles the objects that include it. */\n"

# The edited line on the screen, counted once the screen was without it.
WATCH_JS = """(msg) => { clearInterval(window.rbScrTimer); const s = window.rbScr = { t0: Date.now(), cleared: false, at: null };
  window.rbScrTimer = setInterval(() => { const has = document.getElementById('screen_container').innerText.includes(msg);
    if (!has) s.cleared = true; else if (s.cleared && s.at === null) s.at = (Date.now() - s.t0) / 1000; }, 50); }"""


def screen(page):
    return page.inner_text("#screen_container")


def build(page, sel, msg, timeout=900):
    """Press sel, wait for the build's end: (state, seconds to the end,
    seconds to msg on the screen or None)."""
    page.evaluate(RB_WATCH_JS)
    page.evaluate(WATCH_JS, msg)
    t0 = time.monotonic()
    rb_press(page, sel, False)
    wait_for(lambda: rb_state(page)["state"] in ("done", "failed"), timeout, 0.2)
    st = rb_state(page)
    s_end = round(time.monotonic() - t0, 1)
    if st["state"] == "done" and msg:
        wait_for(lambda: page.evaluate("rbScr.at") is not None, 1200, 0.2)
    at = page.evaluate("clearInterval(rbScrTimer), rbScr.at")
    return st, s_end, at and round(at, 1)


def run(browser, base, shots, tmp, problems, r):
    errors = []
    entry = kernel_entries()[NAME]
    kit = rb_kit(base, NAME)
    users = rbl_users(kit)
    img_path = kit["shipped"]["path"]
    elf_path = next(p for p, _, _ in kit["products"] if p != img_path)
    orig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + FILE + ".gz"})).decode()
    # A header the Source tab lists (src/: a file with code in the DWARF
    # lines) that few objects include.
    listed = set(json.loads(rb_shipped_image(base, {"path": entry["src"] + "index.json"}))["files"])
    header = min((p for p, u in users.items() if p in listed and p.startswith("include/linux/") and p.endswith(".h") and 2 <= len(u) <= 8),
                 key=lambda p: (len(users[p]), p))
    r["header file"] = header
    horig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + header + ".gz"})).decode()
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme="light")
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={NAME}&speed=full")
    srcedit_ready(page, NAME)
    if not wait_for(lambda: PROMPT in screen(page), 1500, 1):
        problems.append(f"the shipped {NAME} did not reach {PROMPT}: {screen(page)[-400:]!r}")
    r["shipped boot s"] = round(time.monotonic() - t0, 1)
    if not wait_for(lambda: rb_state(page)["go"], 120):
        problems.append(f"no Rebuild and boot to press: {rb_state(page)['status']!r}")
        ctx.close()
        return

    # 1. init/main.c edited, Rebuild and boot in the editor bar.
    if not rbl_open(page, FILE, False):
        problems.append(f"Edit did not open {FILE}: {srcedit_state(page)['state']!r}")
        ctx.close()
        return
    want = orig.replace(FROM, TO, 1)
    page.locator("#ed_text").fill(want)
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    st, s_end, s_screen = build(page, "#rb_go_panel", SHOWN)
    compiled = rbl_compiled(st["seen"])
    r["edit"] = {"status": st["status"], "compiled": compiled, "s to the line on the screen": s_screen, "s to the build's end": s_end}
    if st["state"] != "done":
        problems.append(f"the rebuild did not finish: {st['state']} {st['status']!r} log {st['log'][:600]!r}")
        ctx.close()
        return
    for w in (f"Booted {NAME} (rebuilt here)", "built from your 1 changed file", "1 object compiled", f"{len(kit['steps'])} link steps"):
        if w not in st["status"]:
            problems.append(f"the status after the rebuild lacks {w!r}: {st['status']!r}")
    if compiled != sorted(users[FILE]):
        problems.append(f"compiled {compiled}, the kit's users of {FILE} are {users[FILE]}")
    if s_screen is None or not wait_for(lambda: (lambda t: SHOWN in t and PROMPT in t[t.find(SHOWN):])(screen(page)), 1200, 1):
        problems.append(f"the rebuilt kernel's screen: {screen(page)[-600:]!r}")
    rb_shots(page, shots, "rebuild-debian-1280-machine", problems, "Machine tab")
    page.emulate_media(color_scheme="light")

    # The data files made in this browser, and the downloads.
    if not wait_for(lambda: page.evaluate("MKB.GEN.state === 'done'"), 900, 1):
        problems.append(f"the data files: {page.evaluate('[MKB.GEN.state, MKB.GEN.note, MKB.DBG.mode, MKB.SRC.mode]')}")
    r["data"] = page.evaluate("""() => ({ gen: MKB.GEN.state, note: MKB.GEN.note, dbg: MKB.DBG.mode, nsyms: MKB.symbols.symbols.length,
      insns: MKB.disasm ? MKB.disasm.insns.length : 0 })""")
    name_elf, elf = rb_download(page, "#rb_dl_elf", False, tmp)
    _, img = rb_download(page, "#rb_dl_image", False, tmp)
    r["downloads"] = [name_elf, len(elf), len(img)]
    nm = elf32_symbols(elf)
    wl = want.split("\n")
    line = f"{FILE}:{next(i for i, x in enumerate(wl, 1) if TO in x)}"
    lines = {}
    for fn in ("run_init_process", "kernel_init", "do_sys_open"):
        got = page.evaluate(RB_LINES_JS, fn)
        if got:
            lines[fn] = got["lines"][:12]
            if got["vaddr"] != nm.get(fn):
                problems.append(f"{fn}: the page's symbol {hex(got['vaddr'])}, the ELF's {nm.get(fn) and hex(nm[fn])}")
    r["lines"] = lines
    # The edited pr_info is in run_init_process (inlined into kernel_init
    # or not): every line of both.
    every = set()
    for fn in ("run_init_process", "kernel_init"):
        got = page.evaluate(RB_LINES_JS, fn)
        every |= set(got["lines"]) if got else set()
    if line not in every:
        problems.append(f"the DWARF lines of run_init_process and kernel_init lack {line} (the edited line): {sorted(every)[:20]}")

    # The rebuilt kernel's DWARF has the compiled units only (the kit's
    # objects have no debug sections): the Source tab lists their files.
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 120)
    page.fill("#cov_filter", header.split("/")[-1])
    r["after the rebuild the file list has"] = {header: wait_for(lambda: page.locator(
        f'#cov_tree .covrow[data-kind="file"][data-path="{header}"]').count() > 0, 10)}
    page.fill("#cov_filter", "")

    # 2. Back to the shipped kernel (its file list), paused as a user would
    # before a build (the VM at full speed in the same tab made the builds
    # 10x slower on CI's 2 cores: run 44094339542), a header edit: its
    # users and init/main.c's (still edited) are compiled, no other.
    back_paused(page, r, "back 1")
    if not rbl_open(page, header, False):
        problems.append(f"Edit did not open {header}")
    else:
        page.locator("#ed_text").fill(horig + HEADER_ADD)
        wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
        st, s_end, _ = build(page, "#rb_go_panel", "")
        compiled = rbl_compiled(st["seen"])
        want_objs = sorted(set(users[header]) | set(users[FILE]))
        r["header"] = {"status": st["status"], "compiled": compiled, "kit users": want_objs, "s to the build's end": s_end}
        if st["state"] != "done" or sorted(compiled) != want_objs or f"{len(want_objs)} objects compiled" not in st["status"]:
            problems.append(f"the header edit: {st['state']} {st['status']!r}, compiled {compiled}, the kit's users {want_objs}")

    # 3. Back to the shipped kernel, paused, Revert all, Rebuild and boot:
    # CI's bytes (the shipped kernel's boot to the prompt is step 0's).
    back_paused(page, r, "back 2")
    show_tab(page, "source")
    rb_press(page, "#ed_revert_all", False)
    rb_press(page, "#ed_revert_all", False)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    st, s_end, _ = build(page, "#rb_go", "")
    r["unchanged"] = {"status": st["status"], "s": s_end}
    if st["state"] != "done" or "identical to CI's build" not in st["status"] or "0 objects compiled" not in st["status"]:
        problems.append(f"the unchanged rebuild: {st['state']} {st['status']!r}")
    _, elf = rb_download(page, "#rb_dl_elf", False, tmp)
    _, img = rb_download(page, "#rb_dl_image", False, tmp)
    want_sha = {p: h for p, h, _ in kit["products"]}
    r["unchanged bytes"] = {"vmlinux": hashlib.sha256(elf).hexdigest() == want_sha[elf_path],
                            "bzImage": hashlib.sha256(img).hexdigest() == want_sha[img_path],
                            "the page's bzImage": img == rb_shipped_image(base, entry)}
    if not all(r["unchanged bytes"].values()):
        problems.append(f"the unchanged rebuild is not CI's: {r['unchanged bytes']}")
    rb_shots(page, shots, "rebuild-debian-1280-source", problems, "Source tab")
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    ctx.close()
    return


def back_paused(page, r, key):
    """Back to the shipped kernel, then Pause (Machine tab) once it runs."""
    show_tab(page, "source")
    rb_press(page, "#rb_back", False)
    wait_for(lambda: rb_state(page)["kernel"] == NAME, 60)
    show_tab(page, "machine")
    wait_for(lambda: page.is_enabled("#dbg_pause"), 120, 0.2)
    if page.is_enabled("#dbg_pause"):
        page.click("#dbg_pause")
    r[key] = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30, 0.2)


def suite_rebuild_debian(ctx):
    if not ctx.base.startswith("https://") and not ((SITE / "kits" / f"{NAME}.json").exists() and (SITE / "clang" / "clang.wasm").exists()):
        return report("rebuild_debian", [f"no site/kits/{NAME}.json (the kit-debian artifact) or site/clang/clang.wasm: nothing to rebuild"], {})
    if NAME not in ctx.entries:
        return report("rebuild_debian", [f"no kernel {NAME} in kernels.json"], {})
    t0 = time.monotonic()
    problems, r = [], {}
    with tempfile.TemporaryDirectory() as td:
        try:
            run(ctx.browser, ctx.base, ctx.shots, pathlib.Path(td), problems, r)
        except Exception as e:  # noqa: BLE001 (the results so far are reported)
            problems.append(f"{type(e).__name__}: {str(e)[:600]}")
    return report("rebuild_debian", problems, r, kernel=NAME, width=1280, seconds=round(time.monotonic() - t0))
