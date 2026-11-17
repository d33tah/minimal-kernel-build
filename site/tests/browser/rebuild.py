"""Browser test suite rebuild (test_browser.py --suites)."""

import gzip
import hashlib
import json
import pathlib
import struct
import tempfile
import time

from tests.browser.common import (
    ALIVE, cov_open_all, cov_rows, gate_on_screen, HELLO, kernel_entries, report, show_tab, shown_tab, SITE,
    srcedit_overlay, srcedit_press, srcedit_ready, srcedit_state, wait_for,
)
from tests.browser.overlap import OVERLAP_PANEL_JS


# ---- rebuild: edit hello, rebuild it in the page, boot it (E4) --------------
# What a user does: open boot/hello.c from the Source tab's file list, Edit,
# type a new function and a new string, press Rebuild and boot in the editor
# bar. The page builds hello again (rebuild.js: its build kit, clang.wasm for
# the changed object, the link steps) with a progress bar and a status line,
# boots the result in the Machine tab (the new string on the screen), and
# makes its data files: the new function is in the symbols at the ELF's
# address, its lines are the edited file's in the DWARF, the Source panel and
# the Page listing show the edited text, the coverage tree counts its lines.
# Revert all, then a rebuild with "compile every object" must give CI's
# bytes (the bzImage the page ships, the ELF of the kit). A compile error is
# shown with clang's message and the running kernel stays. A site without
# kits says why there is no rebuild. Screenshots light/dark at 1280 and 390 px.
RB_FILE = "boot/hello.c"
RB_AFTER = "void hello_main(void);\n"
RB_INSERT = "int e4_marker(void);\n\nint e4_marker(void)\n{\n\treturn 4;\n}\n\n"
RB_FROM = 'put_line(row, "Hello, World!");'
RB_TO = 'put_line(row, e4_marker() == 4 ? "Hello, rebuilt kernel!" : "wrong");'
RB_HELLO = "Hello, rebuilt kernel!"

RB_STATE_JS = """() => { const shown = (e) => !!e && !e.hidden && e.getClientRects().length > 0, $ = (i) => document.getElementById(i);
  return { state: $('rb_status').dataset.state || '', status: $('rb_status').textContent, go: !$('rb_go').disabled,
    goPanel: !$('rb_go_panel').disabled, title: $('rb_go').title, label: $('rb_go_panel').textContent.trim(),
    panelShown: shown($('rb_go_panel')), progress: shown($('rb_progress')), log: $('rb_log').textContent, logShown: shown($('rb_log')),
    files: shown($('rb_files')), back: shown($('rb_back')), kernel: MKB.image ? MKB.image.name : null, from: MKB.image ? MKB.image.from || null : null,
    hscroll: document.documentElement.scrollWidth > innerWidth + 1, seen: window.rbSeen || [] }; }"""

# Every text the status line shows, with the progress bar's value.
RB_WATCH_JS = """() => { window.rbSeen = []; const st = document.getElementById('rb_status'), bar = document.getElementById('rb_progress');
  new MutationObserver(() => rbSeen.push([st.textContent, bar.hidden ? null : bar.value])).observe(st, { childList: true, characterData: true, subtree: true });
  // The build log (E4a): what it held each time it grew, and the build's state then.
  window.rbLogSeen = []; const log = document.getElementById('rb_buildlog');
  if (log) new MutationObserver(() => rbLogSeen.push([st.dataset.state || '', (log.textContent.match(/^\\$ /gm) || []).length, log.textContent.length,
      document.getElementById('rb_buildlog_box').open && log.getClientRects().length > 0, Math.round(log.scrollHeight - log.clientHeight - log.scrollTop)]))
    .observe(log, { childList: true, characterData: true, subtree: true }); }"""

# The build log after a build: its text, whether its box shows, and whether
# it fits (no sideways scroll of the page).
RB_LOG_JS = """() => { const b = document.getElementById('rb_buildlog_box'), l = document.getElementById('rb_buildlog');
  if (!b || !l) return null;
  return { text: l.textContent, boxShown: !b.hidden, open: b.open, summary: b.querySelector('summary').textContent,
    logShown: l.getClientRects().length > 0, scrollTop: l.scrollTop, scrollMax: l.scrollHeight - l.clientHeight, clientH: l.clientHeight,
    right: Math.round(b.getBoundingClientRect().right), vw: innerWidth, hscroll: document.documentElement.scrollWidth > innerWidth + 1,
    seen: window.rbLogSeen || [] }; }"""


def rb_buildlog(page, tag, problems, r, must, key="log"):
    """The live build log of the build that just ended: lines came while it
    ran, it holds `must` (each a string), it stays shown and opens."""
    lg = page.evaluate(RB_LOG_JS)
    if not lg:
        problems.append(f"{tag}: no build log (#rb_buildlog_box, #rb_buildlog)")
        return None
    running = [x for x in lg["seen"] if x[0] == "running" and x[1] > 0]
    r[key] = {"commands": lg["text"].count("\n$ ") + lg["text"].startswith("$ "), "chars": len(lg["text"]),
              "growths while running": len(running), "summary": lg["summary"]}
    if not running:
        problems.append(f"{tag}: the build log got no command line while the build ran: {lg['seen'][:10]}")
    for w in must:
        if w not in lg["text"]:
            problems.append(f"{tag}: the build log lacks {w!r}: {lg['text'][-1500:]!r}")
    if not lg["boxShown"]:
        problems.append(f"{tag}: the build log is not shown after the build")
    return lg


def rb_buildlog_open(page, phone):
    if not page.query_selector("#rb_buildlog_box"):
        return
    if not page.evaluate("document.getElementById('rb_buildlog_box').open"):
        rb_press(page, "#rb_buildlog_box > summary", phone)
    wait_for(lambda: page.evaluate("document.getElementById('rb_buildlog').getClientRects().length > 0"), 5)

RB_LINES_JS = """async (name) => {
  const s = MKB.symbols.symbols.find((x) => x.name === name);
  if (!s) return null;
  let f = MKB.dbgFunction(s.paddr);
  for (let i = 0; i < 300 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = MKB.dbgFunction(s.paddr); }
  const lines = f.state === 'ok' ? [...new Set(f.rows.flatMap(([, , fr]) => fr.map((x) => (x.file || '') + ':' + x.line)))] : [f.state, f.why];
  return { paddr: s.paddr, vaddr: s.vaddr, lines }; }"""


def rb_state(page):
    return page.evaluate(RB_STATE_JS)


def elf32_symbols(b):
    """name -> st_value of an ELF32 little-endian file's .symtab."""
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", b, 0x2e)
    secs = [struct.unpack_from("<IIIIIIIIII", b, shoff + k * shentsize) for k in range(shnum)]
    out = {}
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for off in range(s[4], s[4] + s[5], 16):
            name, value = struct.unpack_from("<II", b, off)
            end = b.index(b"\0", strtab[4] + name)
            if name:
                out[b[strtab[4] + name:end].decode()] = value
    return out


def rb_kit(base, name):
    if base.startswith("https://"):
        import urllib.request
        return json.loads(urllib.request.urlopen(f"{base}/kits/{name}.json", timeout=60).read())
    return json.loads((SITE / "kits" / f"{name}.json").read_text())


def rb_shipped_image(base, entry):
    if base.startswith("https://"):
        import urllib.request
        return urllib.request.urlopen(f"{base}/{entry['path']}", timeout=60).read()
    return (SITE / entry["path"]).read_bytes()


def rb_press(page, sel, phone):
    # The coverage tree is drawn again when the DWARF arrives (live: after
    # the press was aimed, "Element is not attached to the DOM"): tried again.
    srcedit_press(page, sel, phone)


def rb_wait(page, states, timeout):
    wait_for(lambda: rb_state(page)["state"] in states, timeout, 0.1)
    return rb_state(page)


def rb_download(page, sel, phone, tmp):
    show_tab(page, "source")
    with page.expect_download(timeout=30000) as dl:
        rb_press(page, sel, phone)
    out = tmp / dl.value.suggested_filename
    dl.value.save_as(str(out))
    return out.name, out.read_bytes()


def rb_open_file(page, phone):
    """boot/hello.c from the Source tab's file list (the filter, then the
    file's row), then Edit."""
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok", 60)
    page.fill("#cov_filter", "hello.c")
    rb_press(page, f'#cov_tree .covrow[data-kind="file"][data-path="{RB_FILE}"] .covname', phone)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == RB_FILE, 60)
    if not srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", phone)
    return wait_for(lambda: (lambda s: s["editing"] and s["taShown"])(srcedit_state(page)), 30, 0.05)


def rb_type_edit(page, orig):
    """The insert typed at the start of the line after RB_AFTER, RB_FROM
    selected and typed over: the text the editor must then hold."""
    ta = page.locator("#ed_text")
    ta.click()
    page.keyboard.press("Control+Home")
    for _ in range(orig[:orig.index(RB_AFTER) + len(RB_AFTER)].count("\n")):
        page.keyboard.press("ArrowDown")
    page.keyboard.type(RB_INSERT)
    page.evaluate("(s) => { const ta = document.getElementById('ed_text'), i = ta.value.indexOf(s); ta.setSelectionRange(i, i + s.length); }", RB_FROM)
    page.keyboard.type(RB_TO)
    i = orig.index(RB_AFTER) + len(RB_AFTER)
    return (orig[:i] + RB_INSERT + orig[i:]).replace(RB_FROM, RB_TO)


def rb_shots(page, shots, stem, problems, tag):
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{scheme}.png"))
    if rb_state(page)["hscroll"]:
        problems.append(f"{tag}: the page scrolls sideways")


# The run state toggle (#runbadge, fixed bottom right; on a phone with the
# Source sheet open, right above the sheet) must not cover the Rebuild and
# boot row: the window is scrolled so the row is level with the toggle (the
# worst case), then no word of the row may be under it (E4's 390 px
# screenshot: "compile every object" ended under "halted").
RB_COVER_JS = """() => {
  const t = document.getElementById('runbadge_toggle'), row = document.querySelector('#rb_box .scan');
  if (!t || !t.getClientRects().length || !row.getClientRects().length) return { skip: true };
  let b = t.getBoundingClientRect(), rr = row.getBoundingClientRect();
  window.scrollBy(0, (rr.top + rr.bottom) / 2 - (b.top + b.bottom) / 2);
  b = t.getBoundingClientRect(); rr = row.getBoundingClientRect();
  const hits = [], meets = (r) => r.width > 0.5 && r.height > 0.5 && Math.min(r.right, b.right) - Math.max(r.left, b.left) > 0.5
    && Math.min(r.bottom, b.bottom) - Math.max(r.top, b.top) > 0.5;
  const tw = document.createTreeWalker(row, NodeFilter.SHOW_TEXT);
  for (let n = tw.nextNode(); n; n = tw.nextNode()) {
    if (!n.parentElement.getClientRects().length) continue;
    for (const m of n.data.matchAll(/\\S+/g)) {
      const g = document.createRange(); g.setStart(n, m.index); g.setEnd(n, m.index + m[0].length);
      if ([...g.getClientRects()].some(meets)) hits.push(m[0]);
    }
  }
  for (const e of row.querySelectorAll('button, input, .tip'))
    if (e.getClientRects().length && meets(e.getBoundingClientRect())) hits.push('#' + (e.id || e.className));
  return { hits, off: Math.round((rr.top + rr.bottom) / 2 - (b.top + b.bottom) / 2), y: Math.round(scrollY),
           maxY: Math.round(document.documentElement.scrollHeight - innerHeight), badge: [b.left, b.top, b.right, b.bottom].map(Math.round),
           row: [rr.left, rr.top, rr.right, rr.bottom].map(Math.round) }; }"""


def rb_cover(page, tag, problems, r):
    got = page.evaluate(RB_COVER_JS)
    r.setdefault("cover", {})[tag] = got
    if got.get("skip"):
        problems.append(f"{tag}: no run state toggle or no Rebuild and boot row to check")
    elif got["hits"]:
        problems.append(f"{tag}: the run state toggle covers {got['hits']} of the Rebuild and boot row (toggle {got['badge']}, row {got['row']})")
    # Not level only because the page cannot scroll further that way: the
    # row never comes under the toggle.
    elif (got["off"] < -30 and got["y"] > 0) or (got["off"] > 30 and got["y"] < got["maxY"] - 1):
        problems.append(f"{tag}: the Rebuild and boot row could not be scrolled level with the toggle: {got}")


def rebuild_run(browser, base, shots, tmp):
    """hello at 1280 px: edit, rebuild, boot, every feature of the rebuilt
    kernel; Revert all and an unchanged rebuild of every object (CI's bytes);
    a compile error; back to the shipped kernel."""
    problems, r, errors = [], {}, []
    entry = kernel_entries()["hello"]
    kit = rb_kit(base, "hello")
    elf_path = next(p for p, _, _ in kit["products"] if p != kit["shipped"]["path"])
    orig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + RB_FILE + ".gz"})).decode()
    shipped_files = json.loads(rb_shipped_image(base, {"path": entry["dwarf"] + "files.json"}))
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True, permissions=["clipboard-read", "clipboard-write"])
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello")
    srcedit_ready(page, "hello")
    if not page.query_selector("#rb_go") or not wait_for(lambda: rb_state(page)["go"], 60):
        ctx.close()
        return ["no Rebuild and boot button to press for hello"], r
    st = rb_state(page)
    r["idle"] = st["status"]
    if "with no edit: CI's bytes" not in st["status"] or not st["go"]:
        problems.append(f"before an edit the box says {st['status']!r}")

    # 1. Edit boot/hello.c as a user, press Rebuild and boot in the editor bar.
    if not rb_open_file(page, False):
        ctx.close()
        return problems + [f"Edit did not open {RB_FILE}: {srcedit_state(page)['state']!r}"], r
    want = rb_type_edit(page, orig)
    if srcedit_state(page)["value"] != want:
        problems.append(f"the editor holds {srcedit_state(page)['value']!r}, typed {want!r}")
    st = rb_state(page)
    if not st["panelShown"] or not st["goPanel"]:
        problems.append(f"the editor bar has no Rebuild and boot to press: {st}")
    page.evaluate(RB_WATCH_JS)
    t0 = time.monotonic()
    rb_press(page, "#rb_go_panel", False)
    seen_running = wait_for(lambda: rb_state(page)["state"] in ("running", "booted", "done", "failed"), 10, 0.05)
    st = rb_wait(page, ("done", "failed"), 300)
    r["edited s"] = round(time.monotonic() - t0, 1)
    r["edited"] = st["status"]
    seen = st["seen"]
    r["progress texts"] = len(seen)
    if not seen_running or st["state"] != "done":
        ctx.close()
        return problems + [f"the rebuild did not finish: {st['state']} {st['status']!r} log {st['log'][:400]!r}"], r
    for w in ("Booted hello (rebuilt here)", "built from your 1 changed file", "1 object compiled", "Its data files are made"):
        if w not in st["status"]:
            problems.append(f"the status after the rebuild lacks {w!r}: {st['status']!r}")
    texts = [t for t, _ in seen]
    for w in ("Loading the compiler", "Fetching the kit", "Compiling boot/hello.o (1 of 1)", "Linking: step 6 of 6", "Making its data files"):
        if not any(w in t for t in texts):
            problems.append(f"the progress never said {w!r}: {texts}")
    fracs = [f for _, f in seen if f is not None]
    if not fracs or fracs != sorted(fracs) or not 0 < fracs[-1] <= 1:
        problems.append(f"the progress bar went {fracs}")
    if "Rebuilding" in st["label"]:
        problems.append(f"the editor bar's button still says {st['label']!r}")
    # The build log: every command with its exit and time, while it ran.
    rb_buildlog(page, "1280", problems, r, ["$ clang ", "boot/hello.c", "$ ld.lld ", "$ llvm-objcopy ", "exit 0, ", "# Done in "])

    # 2. It boots in the Machine tab: the new string on the screen.
    if shown_tab(page) != "machine":
        problems.append(f"after the rebuild the {shown_tab(page)} tab is shown, not the Machine tab with the screen")
    if not wait_for(lambda: RB_HELLO in page.inner_text("#screen_container") and ALIVE in page.inner_text("#screen_container"), 60):
        problems.append(f"the rebuilt kernel's screen: {page.inner_text('#screen_container')[:300]!r}")
    page.screenshot(path=str(shots / "rebuild-1280-machine-light.png"))

    # 3. Symbols, DWARF lines, disasm, source: the edit's.
    wait_for(lambda: page.evaluate("MKB.GEN.state === 'done' && MKB.DBG.mode === 'shards' && MKB.SRC.mode === 'shipped' && /indexed in this browser/.test(MKB.SRC.info || '')"), 120)
    k = page.evaluate("""() => ({ gen: MKB.GEN.state, dbg: MKB.DBG.mode, info: MKB.DBG.info, src: MKB.SRC.mode, srcInfo: MKB.SRC.info,
      generated: !!(MKB.symbols.imported && MKB.symbols.imported.generated), file: MKB.symbols.imported && MKB.symbols.imported.file,
      insns: MKB.disasm ? MKB.disasm.insns.length : 0, syms: MKB.symbols.symbols.map((s) => s.name) })""")
    r["data"] = {x: k[x] for x in ("gen", "dbg", "info", "src", "insns")}
    if k["gen"] != "done" or k["dbg"] != "shards" or not k["generated"] or k["file"] != elf_path.split("/")[-1] or not k["insns"]:
        problems.append(f"the rebuilt kernel's data files: {r['data']}, generated {k['generated']}, file {k['file']}")
    if "e4_marker" not in k["syms"]:
        problems.append(f"e4_marker is not in the symbols: {k['syms'][:20]}")
    name, elf = rb_download(page, "#rb_dl_elf", False, tmp)
    name_img, img = rb_download(page, "#rb_dl_image", False, tmp)
    r["downloads"] = [name, len(elf), name_img, len(img)]
    if RB_HELLO.encode() not in img or HELLO.encode() in img:
        problems.append(f"the downloaded bzImage {name_img} lacks {RB_HELLO!r} or still has {HELLO!r}")
    nm = elf32_symbols(elf)
    lines = {}
    for fn in ("e4_marker", "hello_main"):
        got = page.evaluate(RB_LINES_JS, fn)
        lines[fn] = got
        if not got or got["vaddr"] != nm.get(fn):
            problems.append(f"{fn}: the page's symbol {got and hex(got['vaddr'])}, the ELF's {nm.get(fn) and hex(nm[fn])}")
    r["lines"] = {f: v and v["lines"] for f, v in lines.items()}
    wl = want.split("\n")
    at = lambda s: f"{RB_FILE}:{next(i for i, l in enumerate(wl, 1) if s in l)}"
    for fn, s in (("e4_marker", "return 4;"), ("hello_main", RB_TO), ("hello_main", "int row = BDA_CURSOR_ROW")):
        if not lines.get(fn) or at(s) not in lines[fn]["lines"]:
            problems.append(f"{fn}'s DWARF lines {lines.get(fn) and lines[fn]['lines']} lack {at(s)} ({s!r} in the edited file)")
    wait_for(lambda: page.evaluate("MKB.srcFile('boot/hello.c').state") == "ok", 30)
    if page.evaluate("MKB.srcFile('boot/hello.c').lines") != wl:
        problems.append("the source the listings read is not the edited text")
    pa = lines["e4_marker"]["paddr"] if lines.get("e4_marker") else 0
    page.evaluate(f"MKB.jumpTo({pa})")
    wait_for(lambda: "return 4;" in page.inner_text("#listing"), 30)
    lst = page.inner_text("#listing")
    if shown_tab(page) != "page" or "e4_marker" not in lst or "return 4;" not in lst:
        problems.append(f"the Page listing at e4_marker shows no e4_marker with its C line return 4; (tab {shown_tab(page)})")
    page.screenshot(path=str(shots / "rebuild-1280-listing-light.png"))
    # The coverage tree: the rebuilt kernel's lines, run.
    wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120)
    show_tab(page, "source")
    page.fill("#cov_filter", "")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 60)
    cov_open_all(page)
    f = next((x for x in cov_rows(page, "file") if x["path"] == RB_FILE), None)
    old = next((n for p, n, *_ in shipped_files["files"] if p == RB_FILE), None)
    r["coverage"] = [f, old]
    if not f or not f["linesRun"] or not old or f["lines"] <= old:
        problems.append(f"the coverage tree's {RB_FILE}: {f}; the shipped kernel has {old} lines with code")
    st = rb_state(page)
    if not st["files"] or not st["back"]:
        problems.append(f"no downloads or no way back after the rebuild: {st}")
    rb_shots(page, shots, "rebuild-1280-source", problems, "1280 Source tab")
    page.emulate_media(color_scheme="light")
    # The build log opened, copied whole.
    if not page.query_selector("#rb_buildlog_copy"):
        problems.append("no Copy for the build log (#rb_buildlog_copy)")
    else:
        rb_buildlog_open(page, False)
        page.locator("#rb_buildlog_box").scroll_into_view_if_needed()
        rb_shots(page, shots, "rebuild-1280-buildlog", problems, "1280 build log")
        page.emulate_media(color_scheme="light")
        rb_press(page, "#rb_buildlog_copy", False)
        time.sleep(0.3)
        copied = page.evaluate("navigator.clipboard.readText()")
        if copied != page.evaluate("document.getElementById('rb_buildlog').textContent"):
            problems.append(f"Copy of the build log put {copied[:200]!r} on the clipboard")
    # The Source panel: the edited text, said to be the one built.
    page.evaluate("(p) => MKB.openSourceFile(p)", RB_FILE)
    if srcedit_state(page)["editing"]:
        rb_press(page, "#src_edit", False)
    wait_for(lambda: "rebuilt here from your text" in srcedit_state(page)["state"], 15)
    if "rebuilt here from your text" not in srcedit_state(page)["state"]:
        problems.append(f"the panel's note on the rebuilt kernel: {srcedit_state(page)['state']!r}")
    if not wait_for(lambda: "return 4;" in page.inner_text("#src_body"), 15):
        problems.append("the Source panel does not show the edited text")
    rb_shots(page, shots, "rebuild-1280-panel", problems, "1280 Source panel")
    page.emulate_media(color_scheme="light")
    # E5a: the status and the build log end at the panel's edge.
    got = page.evaluate(OVERLAP_PANEL_JS)
    r["under the panel"] = got
    if not got or got["hits"] or got["hscroll"] or not page.is_visible("#rb_buildlog"):
        problems.append(f"1280 with the Source panel open, the build log open: {got}, log shown {page.is_visible('#rb_buildlog')}")

    # 4. A compile error: clang's message, the running kernel stays.
    rb_press(page, "#src_edit", False)
    wait_for(lambda: srcedit_state(page)["taShown"], 10)
    page.locator("#ed_text").click()
    page.keyboard.press("Control+Home")
    page.keyboard.type("int broken = ;\n")
    page.evaluate(RB_WATCH_JS)
    rb_press(page, "#rb_go_panel", False)
    st = rb_wait(page, ("failed", "done"), 120)
    r["error"] = [st["status"], st["log"][:200]]
    # The log keeps clang's output and its exit code; the link never ran.
    lg = rb_buildlog(page, "compile error", problems, r, ["$ clang ", "boot/hello.c:1:14: error: expected expression", "exit 1, "], key="error log")
    if lg and "$ ld.lld" in lg["text"]:
        problems.append("compile error: the build log has link commands after the failed compile")
    if st["state"] != "failed" or "boot/hello.c:1:" not in st["log"] or "error" not in st["log"] or not st["logShown"] \
            or st["kernel"] != "hello (rebuilt here)" or "still runs hello (rebuilt here)" not in st["status"]:
        problems.append(f"a compile error: {st['state']} {st['status']!r}, log {st['log'][:300]!r}, kernel {st['kernel']}")
    if shown_tab(page) != "source" or "not built" not in srcedit_state(page)["state"]:
        problems.append(f"a compile error is not shown where it was pressed: tab {shown_tab(page)}, editor {srcedit_state(page)['state']!r}")
    page.locator("#rb_log").scroll_into_view_if_needed()
    rb_shots(page, shots, "rebuild-1280-error", problems, "1280 compile error")
    page.emulate_media(color_scheme="light")

    # 5. Revert all, compile every object: CI's bytes, CI's screen.
    rb_press(page, "#ed_revert_all", False)
    rb_press(page, "#ed_revert_all", False)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    page.check("#rb_all")
    rb_press(page, "#rb_go", False)
    st = rb_wait(page, ("done", "failed"), 300)
    r["unchanged"] = st["status"]
    if st["state"] != "done" or "identical to CI's build" not in st["status"] or "2 objects compiled" not in st["status"]:
        problems.append(f"the unchanged rebuild: {st['state']} {st['status']!r} {st['log'][:300]!r}")
    if not wait_for(lambda: gate_on_screen(page), 60):
        problems.append(f"the unchanged rebuild's screen: {page.inner_text('#screen_container')[:300]!r}")
    _, elf = rb_download(page, "#rb_dl_elf", False, tmp)
    _, img = rb_download(page, "#rb_dl_image", False, tmp)
    shipped = rb_shipped_image(base, entry)
    want_elf = next(h for p, h, _ in kit["products"] if p == elf_path)
    r["unchanged bytes"] = {"bzImage": img == shipped, "elf": hashlib.sha256(elf).hexdigest() == want_elf}
    if img != shipped or hashlib.sha256(elf).hexdigest() != want_elf:
        problems.append(f"the unchanged rebuild is not CI's: {r['unchanged bytes']}")

    # 6. Back to the shipped kernel.
    rb_press(page, "#rb_back", False)
    wait_for(lambda: rb_state(page)["kernel"] == "hello", 30)
    st = rb_state(page)
    if st["kernel"] != "hello" or st["from"] or st["back"] or st["files"]:
        problems.append(f"back to the shipped kernel: {st}")
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def rebuild_phone_run(browser, base, shots, tmp):
    """At 390 px (touch, dark): the editor, its Rebuild and boot, the
    screen; and with no kits/index.json the box says why."""
    problems, r, errors = [], {}, []
    entry = kernel_entries()["hello"]
    orig = gzip.decompress(rb_shipped_image(base, {"path": entry["src"] + RB_FILE + ".gz"})).decode()
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme="dark")
    page.goto(f"{base}/index.html?kernel=hello")
    srcedit_ready(page, "hello")
    wait_for(lambda: rb_state(page)["go"], 60)
    if not rb_open_file(page, True):
        ctx.close()
        return [f"390: Edit did not open {RB_FILE}"], r
    page.locator("#ed_text").fill(orig.replace(RB_FROM, 'put_line(row, "Hello, rebuilt kernel!");'))
    wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    st = rb_state(page)
    r["panel"] = [st["panelShown"], st["label"]]
    if not st["panelShown"] or not st["goPanel"]:
        problems.append(f"390: no Rebuild and boot in the editor bar: {st}")
    rb_shots(page, shots, "rebuild-390-editor", problems, "390 editor")
    page.emulate_media(color_scheme="dark")
    rb_cover(page, "390 editor open", problems, r)
    page.evaluate(RB_WATCH_JS)
    t0 = time.monotonic()
    rb_press(page, "#rb_go_panel", True)
    st = rb_wait(page, ("done", "failed"), 300)
    r["s"] = round(time.monotonic() - t0, 1)
    r["status"] = st["status"]
    if st["state"] != "done":
        problems.append(f"390: the rebuild: {st['state']} {st['status']!r} {st['log'][:300]!r}")
    # The screen is in the Machine tab; at 390 px the panel is closed for it.
    if not wait_for(lambda: RB_HELLO in page.inner_text("#screen_container"), 60):
        problems.append(f"390: the screen: {page.inner_text('#screen_container')[:300]!r}")
    vis = page.evaluate("(() => { const s = document.getElementById('screen_container').getBoundingClientRect(), p = document.getElementById('src_panel'); "
                        "return { panel: !p.hidden && p.getBoundingClientRect().width > 0, top: s.top, w: s.width }; })()")
    r["machine"] = vis
    if shown_tab(page) != "machine" or vis["panel"]:
        problems.append(f"390: after the rebuild the screen is not in view: tab {shown_tab(page)}, {vis}")
    rb_shots(page, shots, "rebuild-390-machine", problems, "390 Machine tab")
    show_tab(page, "source")
    page.locator("#rb_box").scroll_into_view_if_needed()
    rb_shots(page, shots, "rebuild-390-source", problems, "390 Source tab")
    rb_cover(page, "390 Source tab", problems, r)
    # The build log at 390 px: open, inside the screen's width.
    if rb_buildlog(page, "390", problems, r, ["$ clang ", "$ ld.lld ", "# Done in "]):
        rb_buildlog_open(page, True)
        page.locator("#rb_buildlog_box").scroll_into_view_if_needed()
        rb_shots(page, shots, "rebuild-390-buildlog", problems, "390 build log")
        page.emulate_media(color_scheme="dark")
        lg = page.evaluate(RB_LOG_JS)
        if not lg["logShown"] or lg["right"] > lg["vw"] or lg["hscroll"]:
            problems.append(f"390: the open build log: shown {lg['logShown']}, right {lg['right']} of {lg['vw']}, sideways scroll {lg['hscroll']}")
    rb_press(page, "#rb_back", True)
    wait_for(lambda: rb_state(page)["kernel"] == "hello", 30)
    page.evaluate("MKB.srcOverlay.revert('boot/hello.c')")
    ctx.close()
    # No build kits on the site: the reason, no button to press.
    nc = browser.new_context(viewport={"width": 1280, "height": 1000})
    nc.route("**/kits/index.json*", lambda route: route.fulfill(status=404, body="no"))
    page = nc.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello#source")
    wait_for(lambda: "No build kits" in rb_state(page)["status"], 60)
    st = rb_state(page)
    r["no kits"] = st["status"]
    if st["go"] or st["goPanel"] or "kits/index.json: HTTP 404" not in st["status"]:
        problems.append(f"without kits: {st}")
    nc.close()
    if errors:
        problems.append(f"390 page errors: {errors[:3]}")
    return problems, r


def rebuild_edges_run(browser, base):
    """A kit list without the kernel; edits the kit cannot build with (a new
    file, a deleted one) refused by name; another kernel chosen while the
    build runs: built, not booted."""
    problems, r, errors = [], {}, []
    nc = browser.new_context(viewport={"width": 1280, "height": 1000})
    nc.route("**/kits/index.json*", lambda route: route.fulfill(json={"kits": {"minified-linux": {}}}))
    page = nc.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello#source")
    wait_for(lambda: "has no build kit" in rb_state(page)["status"], 60)
    st = rb_state(page)
    r["no kit"] = st["status"]
    if st["go"] or st["status"] != "hello has no build kit: the page rebuilds minified-linux.":
        problems.append(f"a kernel without a kit: {st}")
    nc.close()
    # A server error on kits/index.json is tried again, like the kit's own
    # files (E5a: on live, right after a deploy, the button stayed disabled):
    # two 503s, then the list; always 503: the reason after the last try.
    for fails, want_calls in ((2, 3), (99, 4)):
        calls = []

        def flaky(fails, calls):
            def answer(route):
                calls.append(time.monotonic())
                if len(calls) <= fails:
                    route.fulfill(status=503, body="busy")
                else:
                    route.continue_()
            return answer
        nc = browser.new_context(viewport={"width": 1280, "height": 1000})
        nc.route("**/kits/index.json*", flaky(fails, calls))
        page = nc.new_page()
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        page.goto(f"{base}/index.html?kernel=hello#source")
        wait_for(lambda: rb_state(page)["go"] or "No build kits" in rb_state(page)["status"], 60)
        st = rb_state(page)
        r[f"index.json 503 x{fails}"] = [len(calls), st["status"]]
        if fails < want_calls and (not st["go"] or len(calls) != want_calls):
            problems.append(f"kits/index.json answered 503 {fails} times, then the list: {len(calls)} requests, {st}")
        if fails >= want_calls and (st["go"] or "kits/index.json: HTTP 503" not in st["status"] or len(calls) != want_calls):
            problems.append(f"kits/index.json always 503: {len(calls)} requests (want {want_calls}), {st}")
        nc.close()
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello#source")
    srcedit_ready(page, "hello")
    wait_for(lambda: rb_state(page)["go"], 60)
    page.evaluate("async () => { await MKB.srcOverlay.write('boot/e4_new.h', '#define E4 1\\n'); await MKB.srcOverlay.write('boot/hello.c', null); }")
    rb_press(page, "#rb_go", False)
    st = rb_wait(page, ("failed", "done"), 60)
    r["refused"] = st["status"]
    if st["state"] != "failed" or "boot/e4_new.h (not a file of the build kit)" not in st["status"] \
            or "boot/hello.c (deleted)" not in st["status"] or st["kernel"] != "hello":
        problems.append(f"edits the kit cannot build with: {st}")
    # The build log says why too; Copy without clipboard access selects it.
    lg = page.evaluate(RB_LOG_JS)
    if not lg or "# Failed: the build cannot use your edits of boot/e4_new.h" not in lg["text"] or "(0 commands)" not in lg["summary"]:
        problems.append(f"the refused build's log: {lg and (lg['summary'], lg['text'][:300])}")
    elif page.query_selector("#rb_buildlog_copy"):
        rb_buildlog_open(page, False)
        page.evaluate("() => { navigator.clipboard.writeText = () => Promise.reject(new Error('denied by the test')); }")
        rb_press(page, "#rb_buildlog_copy", False)
        wait_for(lambda: page.inner_text("#rb_buildlog_copy") != "Copy the log", 3, 0.05)
        sel = page.evaluate("[getSelection().toString(), document.getElementById('rb_buildlog').textContent, document.getElementById('rb_buildlog_copy').textContent]")
        r["copy denied"] = sel[2]
        if sel[0].strip() != sel[1].strip() or sel[2] != "Selected: copy it":
            problems.append(f"Copy without clipboard access: selected {sel[0][:100]!r}, button {sel[2]!r}")
        if not wait_for(lambda: page.inner_text("#rb_buildlog_copy") == "Copy the log", 4, 0.1):
            problems.append("the Copy button does not say Copy the log again")
    rb_press(page, "#ed_revert_all", False)
    rb_press(page, "#ed_revert_all", False)
    wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    # The kit's JSON held back until another kernel is shown.
    page.evaluate("""() => { window.rbHold = new Promise((ok) => { window.rbRelease = ok; }); const f = window.fetch;
      window.fetch = async (u, o) => { if (String(u).includes('kits/hello.json')) await rbHold; return f(u, o); }; }""")
    rb_press(page, "#rb_go", False)
    wait_for(lambda: rb_state(page)["state"] == "running", 10)
    show_tab(page, "machine")
    page.select_option("#kernel", "hello-os")
    wait_for(lambda: rb_state(page)["kernel"] == "hello-os", 60)
    page.evaluate("rbRelease()")
    st = rb_wait(page, ("failed", "done"), 120)
    r["switched"] = [st["status"], st["kernel"]]
    if st["state"] != "failed" or "another kernel was chosen" not in st["status"] or st["kernel"] != "hello-os":
        problems.append(f"another kernel chosen during the build: {st}")
    ctx.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_rebuild(ctx):
    if not ctx.base.startswith("https://") and not ((SITE / "kits" / "index.json").exists() and (SITE / "clang" / "clang.wasm").exists()):
        return report("rebuild", ["no site/kits (the kits artifact) or site/clang/clang.wasm: nothing to rebuild"], {})
    failed = False
    with tempfile.TemporaryDirectory() as td:
        failed |= report("rebuild", *rebuild_run(ctx.browser, ctx.base, ctx.shots, pathlib.Path(td)), width=1280)
        failed |= report("rebuild", *rebuild_phone_run(ctx.browser, ctx.base, ctx.shots, pathlib.Path(td)), width=390)
    failed |= report("rebuild", *rebuild_edges_run(ctx.browser, ctx.base), case="edges")
    return failed
