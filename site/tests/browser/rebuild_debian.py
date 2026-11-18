
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
    elf32_symbols, rb_debug, rb_debug_check, rb_download, rb_kit, RB_LINES_JS, rb_press, rb_shipped_image, rb_shots,
    rb_state, RB_WATCH_JS,
)
from tests.browser.rebuild_linux import rbl_compiled, rbl_open, rbl_users
from tests.browser.types import watch


# ---- rebuild of the editable Debian (E6) -------------------------------------
# The page's kernel debian-6.1-686-clang (Debian 6.1 686 rebuilt from its
# source with clang, gen-debian-clang.sh) booted to Debian's initramfs
# prompt, then in the Source tab:
#   1. init/main.c edited ("Run %s as init process" -> "Run %s as init
#      (edited in the page)", and a function added at its end, e6_moved,
#      which moves the code after init/main.o), Rebuild and boot: exactly init/main.o is
#      compiled (the kit's users of the file; recordmcount after it), the
#      kit's link steps run, the kernel boots to the edited line and the
#      (initramfs) prompt; the edit-to-screen time is measured; the data
#      files of the rebuilt kernel are made in this browser and run_init_process's
#      DWARF lines are the edited file's; do_sys_open (fs/open.c, not
#      compiled, moved by the edit) has the shipped kernel's lines, locals
#      and CFI at the same offsets (CI's DWARF mapped to the new addresses),
#      and the Source tab lists every file it listed before;
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
UNTOUCHED = "do_sys_open"
# A function added at the end of init/main.c: the code after init/main.o
# (do_sys_open's too) moves. And globals clang shrinks to a byte (e6_flag:
# its value is the byte, e6_on: 1 - the byte) and splits into pieces
# (e6_pair), whose DWARF the page reads from the compiled object.
ADD = "\n/* E6: a function added, so the code after this unit moves. */\n__attribute__((used)) int e6_moved(int x);\n" \
    "__attribute__((used)) int e6_moved(int x)\n{\n\treturn x * 3 + 1;\n}\n" \
    "\n/* E6: globals clang shrinks to a byte or splits into pieces. */\n" \
    "static int e6_flag;\nstatic int e6_on = 1;\nstatic struct { int a; int b; } e6_pair;\n" \
    "__attribute__((used)) void e6_globals_set(int x);\n__attribute__((used)) int e6_globals_get(void);\n" \
    "__attribute__((used)) void e6_globals_set(int x)\n{\n\te6_flag = 1;\n\te6_on = 0;\n\te6_pair.a = x;\n\te6_pair.b = x + 1;\n}\n" \
    "__attribute__((used)) int e6_globals_get(void)\n{\n\treturn e6_flag + e6_on + e6_pair.a + e6_pair.b;\n}\n" \
    "\n/* E6a: new globals the Variables panel reads after the rebuild. */\n" \
    "typedef unsigned int e6a_u32;\nenum e6a_mode { E6A_OFF, E6A_ON };\n" \
    "int e6a_read(void);\n" \
    "int e6a_count = 42;\n" \
    "struct e6a_rec { int id; char tag[8]; e6a_u32 n; enum e6a_mode mode; const char *name; void *p; int (*fn)(void); };\n" \
    "struct e6a_rec e6a_rec = { 7, \"e6a\", 3, E6A_ON, \"rec\", 0, e6a_read };\n" \
    "__attribute__((used)) int e6a_read(void)\n{\n\treturn e6a_count + e6a_rec.id + e6a_rec.tag[0] + e6a_rec.n + e6a_rec.mode;\n}\n"
# Watches of the new globals (plan E6a) and what their rows must show.
E6A_WATCHES = {"e6a_count": "42", "e6a_rec.id": "7", "e6a_rec.tag": '"e6a"', "e6a_rec.n": "3", "e6a_rec.mode": "E6A_ON"}

# The edited line on the screen, counted once the screen was without it.
WATCH_JS = """(msg) => { clearInterval(window.rbScrTimer); const s = window.rbScr = { t0: Date.now(), cleared: false, at: null };
  window.rbScrTimer = setInterval(() => { const has = document.getElementById('screen_container').innerText.includes(msg);
    if (!has) s.cleared = true; else if (s.cleared && s.at === null) s.at = (Date.now() - s.t0) / 1000; }, 50); }"""


SHRUNK_JS = """async () => {
  let t = MKB.dbgTypes();
  for (let i = 0; i < 1200 && t.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); t = MKB.dbgTypes(); }
  if (t.state !== 'ok') return { why: t.why };
  const T = t.T, out = {};
  const sh = T.vars.find((v) => v[0] === 'tracing_disabled' && v[5] & 8);
  if (sh) {
    const e = MKB.varsEval(T, 'tracing_disabled'), b = MKB.varsRead(e.pa, sh[3]);
    const [, k, c] = (T.shrunk || []).find((x) => x[0] === sh[1]) || [];
    out.shrunk = { pa: sh[1], k, c, byte: b ? b[0] : null, text: MKB.varsScalar(T, e.type, e.pa, e.bits).text, notes: e.notes };
  }
  out.pieces = T.vars.filter((v) => v[0] === '__futex_data').map((v) => [v[5] & 16, v[3], (MKB.dwWhere(T, v[1]) || {}).path || null]);
  out.pieceEval = MKB.varsEval(T, '__futex_data').error || 'no error';
  // The edited unit's own globals, from the DWARF made in the page (its type table).
  const O = MKB.DBG.merge && MKB.DBG.merge.ownT;
  out.own = O ? O.vars.filter((v) => v[0].startsWith('e6_')).map((v) => [v[0], v[5] & 24, v[3], (MKB.dwWhere(O, v[1]) || {}).path || null,
    ((O.shrunk || []).find((x) => x[0] === v[1]) || []).slice(1)]) : null;
  // The edit's globals in the merged list (E6a).
  out.merged = T.vars.filter((v) => v[0].startsWith('e6')).map((v) => v[0]);
  return out; }"""


def screen(page):
    return page.inner_text("#screen_container")


def build(page, sel, msg, timeout=900):
    """Press sel, wait for the build's end: (state, seconds to the end,
    seconds to msg on the screen or None)."""
    page.evaluate(RB_WATCH_JS)
    page.evaluate(WATCH_JS, msg)
    # The machine's run state while the build runs (the page pauses it).
    page.evaluate("""() => { clearInterval(window.rbRunTimer); window.rbRun = { before: MKB.runState, during: [] };
      window.rbRunTimer = setInterval(() => { if (document.getElementById('rb_status').dataset.state === 'running'
        && !rbRun.during.includes(MKB.runState)) rbRun.during.push(MKB.runState); }, 100); }""")
    t0 = time.monotonic()
    rb_press(page, sel, False)
    wait_for(lambda: rb_state(page)["state"] in ("done", "failed"), timeout, 0.2)
    st = rb_state(page)
    st["run during the build"] = page.evaluate("clearInterval(rbRunTimer), rbRun")
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
    # The Debugger's view of a function of an untouched unit (fs/open.c), to
    # compare after the rebuild.
    dbg_before = rb_debug(page, [UNTOUCHED])

    # 1. init/main.c edited, Rebuild and boot in the editor bar.
    if not rbl_open(page, FILE, False):
        problems.append(f"Edit did not open {FILE}: {srcedit_state(page)['state']!r}")
        ctx.close()
        return
    want = orig.replace(FROM, TO, 1) + ADD
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

    # The rebuilt ELF has DWARF of the compiled unit only (the kit's objects
    # have no debug sections): the page maps CI's DWARF of every other unit
    # to the new addresses. do_sys_open (fs/open.c, moved by the edit) keeps
    # its lines, locals and CFI; the Source tab lists every file.
    rb_debug_check(dbg_before, rb_debug(page, [UNTOUCHED, "kernel_init", "e6_moved"]), [UNTOUCHED], ["kernel_init", "e6_moved"], problems, r, NAME,
                   moved_by_edit=True)
    # Globals clang shrank or split (types.json flags 8 and 16), moved too:
    # tracing_disabled's value is 1 - its byte; __futex_data's pieces are
    # named with their member paths and a watch of it says why it is refused.
    g = page.evaluate(SHRUNK_JS)
    r["clang globals"] = g
    sh = g.get("shrunk") or {}
    if not sh or sh.get("byte") is None or sh["text"] != str(sh["byte"] * sh["k"] + sh["c"]) or not any("clang keeps it" in n for n in sh["notes"]):
        problems.append(f"tracing_disabled (a global clang shrank) after the rebuild: {g}")
    paths = [p for _, _, p in g.get("pieces", [])]
    if len(paths) < 2 or not all(f for f, _, _ in g["pieces"]) or len(set(paths)) != len(paths) or "split by clang" not in g.get("pieceEval", ""):
        problems.append(f"__futex_data (a global clang split) after the rebuild: {g}")
    own = sorted(map(tuple, (tuple(x[:4]) + (tuple(x[4]),) for x in g.get("own") or [])))
    want = [("e6_flag", 8, 1, "e6_flag", (1, 0)), ("e6_on", 8, 1, "e6_on", (-1, 1)),
            ("e6_pair", 16, 4, "e6_pair.a", ()), ("e6_pair", 16, 4, "e6_pair.b", ())]
    if own != want:
        problems.append(f"the edited unit's globals clang shrank or split, in the DWARF made here: {own}, want {want}")
    # Globals the edit added are in the merged Variables list (CI's table
    # plus the compiled unit's, plan E6a), with their types: watched in
    # the Debugger tab, read from the rebuilt kernel's memory.
    merged = sorted(set(g.get("merged") or []))
    if merged != ["e6_flag", "e6_on", "e6_pair", "e6a_count", "e6a_rec"]:
        problems.append(f"the edit's new globals in the merged Variables list: {merged}")
    show_tab(page, "debugger")
    rows = {e: watch(page, e) for e in E6A_WATCHES}
    r["new globals watched"] = rows
    for e, w in E6A_WATCHES.items():
        if w not in rows[e] or "not" in rows[e]:
            problems.append(f"the watch of {e} (a global the edit added) shows {rows[e]!r}, want {w!r} in it")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 120)
    page.fill("#cov_filter", header.split("/")[-1])
    listed_edit = wait_for(lambda: page.locator(f'#cov_tree .covrow[data-kind="file"][data-path="{header}"]').count() > 0, 30)
    r["after the rebuild the file list has"] = {header: listed_edit}
    if not listed_edit:
        problems.append(f"after the rebuild the Source tab's file list has no {header}")
    page.fill("#cov_filter", "")
    rb_shots(page, shots, "rebuild-debian-1280-edited-source", problems, "Source tab of the edited kernel")
    page.emulate_media(color_scheme="light")

    # 2. Back to the shipped kernel (its file list), running: the page
    # pauses it for the build itself (a VM at full speed in the same tab
    # made the builds 10x slower on CI's 2 cores: run 44094339542). A
    # header edit: its users and init/main.c's (still edited) are compiled.
    back_running(page, r, "back 1")
    if not rbl_open(page, header, False):
        problems.append(f"Edit did not open {header}")
    else:
        page.locator("#ed_text").fill(horig + HEADER_ADD)
        wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
        st, s_end, _ = build(page, "#rb_go_panel", "")
        compiled = rbl_compiled(st["seen"])
        want_objs = sorted(set(users[header]) | set(users[FILE]))
        r["header"] = {"status": st["status"], "compiled": compiled, "kit users": want_objs, "s to the build's end": s_end,
                       "machine": st["run during the build"]}
        paused_during(st, problems, "the header edit's build")
        if st["state"] != "done" or sorted(compiled) != want_objs or f"{len(want_objs)} objects compiled" not in st["status"]:
            problems.append(f"the header edit: {st['state']} {st['status']!r}, compiled {compiled}, the kit's users {want_objs}")

    # 3. Back to the shipped kernel (running), Revert all, Rebuild and boot:
    # CI's bytes (the shipped kernel's boot to the prompt is step 0's).
    back_running(page, r, "back 2")
    show_tab(page, "source")
    rb_press(page, "#ed_revert_all", False)
    rb_press(page, "#ed_revert_all", False)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    st, s_end, _ = build(page, "#rb_go", "")
    r["unchanged"] = {"status": st["status"], "s": s_end, "machine": st["run during the build"]}
    paused_during(st, problems, "the unchanged rebuild's build")
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
    # Its data files are the shipped kernel's (the same bytes): the whole
    # file list, and DWARF lines of a function of any unit.
    if "Its data files are the shipped kernel's" not in st["status"]:
        problems.append(f"the unchanged rebuild's status does not say it uses the shipped data files: {st['status']!r}")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 120)
    page.fill("#cov_filter", header.split("/")[-1])
    listed_after = wait_for(lambda: page.locator(f'#cov_tree .covrow[data-kind="file"][data-path="{header}"]').count() > 0, 30)
    page.fill("#cov_filter", "")
    lines = page.evaluate(RB_LINES_JS, "do_sys_open")
    r["unchanged: data files"] = {"file list has " + header: listed_after, "do_sys_open lines": lines and lines["lines"][:4]}
    if not listed_after or not lines or not any(x.startswith("fs/open.c:") for x in lines["lines"]):
        problems.append(f"the unchanged rebuild's data files: {r['unchanged: data files']}")
    rb_shots(page, shots, "rebuild-debian-1280-source", problems, "Source tab")
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    ctx.close()
    return


def back_running(page, r, key):
    """Back to the shipped kernel, which boots and runs (not paused here:
    the page pauses it for the build)."""
    show_tab(page, "source")
    rb_press(page, "#rb_back", False)
    wait_for(lambda: rb_state(page)["kernel"] == NAME, 60)
    r[key] = wait_for(lambda: page.evaluate("MKB.runState") in ("jit", "stepped"), 120, 0.2)


def paused_during(st, problems, what):
    """The machine ran before the press and was paused while the page built."""
    run = st.get("run during the build") or {}
    if run.get("before") not in ("jit", "stepped") or run.get("during") != ["paused"]:
        problems.append(f"{what}: the machine before the build {run.get('before')!r}, during it {run.get('during')} (want running, then paused only)")


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
