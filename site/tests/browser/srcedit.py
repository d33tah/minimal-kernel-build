"""Browser test suite srcedit (test_browser.py --suites)."""

import gzip
import pathlib
import shutil
import subprocess
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import (
    debian_names, kernel_entries, report, show_tab, SITE, srcedit_overlay, srcedit_press, srcedit_ready, srcedit_state,
    wait_for,
)


# ---- srcedit: edit the kernel's source in the page (E2) --------------------
# The Source panel's Edit turns the file shown into a text editor (a textarea
# with line numbers); the edits live in an IndexedDB overlay over the shipped
# tree (srcedit.js) and survive a reload. The Source tab lists the changed
# files with a diff and a revert each, downloads all of them as one patch
# that `git apply` takes on the shipped files, and applies an uploaded patch
# (every file or none). srcOverlay is the API the in-browser build (E3, E4)
# reads: the changed paths and a file's text with the overlay applied.
SRCEDIT_FILES = {
    # kernel: (file edited in the editor, its line to insert before, file a patch changes, a new file)
    "hello": ("boot/hello.c", 'put_line(row + 1, "Still alive");', None, None),
    "minified-linux": ("init/main.c", "panic(\"cannot execute /init\");", "include/linux/list.h", "init/e2_new.h"),
    "debian-6.1-686": ("kernel/sched/core.c", None, None, None),
}


def srcedit_orig(entry, path):
    return gzip.decompress((SITE / entry["src"] / (path + ".gz")).read_bytes()).decode()


def srcedit_open(page, path, phone):
    """The whole file in the Source panel, then Edit: the editor with its text."""
    page.evaluate("(p) => MKB.openSourceFile(p)", path)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == path, 60)
    t0 = time.monotonic()
    if not srcedit_state(page)["editing"]:
        srcedit_press(page, "#src_edit", phone)
    ok = wait_for(lambda: (lambda s: s["editing"] and s["taShown"])(srcedit_state(page)), 30, 0.05)
    return ok, time.monotonic() - t0


def srcedit_download(page, phone, tmp, stem):
    show_tab(page, "source")
    with page.expect_download(timeout=20000) as dl:
        srcedit_press(page, "#ed_download", phone)
    d = dl.value
    out = tmp / f"{stem}.patch"
    d.save_as(str(out))
    return d.suggested_filename, out


def srcedit_git_apply(tmp, files, patch):
    """Write the shipped files into a fresh tree, `git apply` the patch there:
    (ok, message, {path: text after})."""
    tree = tmp / ("tree-" + patch.stem)
    if tree.exists():
        shutil.rmtree(tree)
    tree.mkdir()
    for p, text in files.items():
        (tree / p).parent.mkdir(parents=True, exist_ok=True)
        (tree / p).write_bytes(text.encode())
    for args in (["--check"], []):
        r = subprocess.run(["git", "apply", *args, str(patch)], cwd=tree, capture_output=True, text=True)
        if r.returncode:
            return False, (r.stderr or r.stdout).strip()[:400], {}
    after = {}
    for f in tree.rglob("*"):
        if f.is_file():
            after[str(f.relative_to(tree))] = f.read_bytes().decode()
    return True, "", after


def srcedit_git_patch(tmp, files, change, stem):
    """A patch made by git: files (path -> text) committed, change (path ->
    new text, None deletes) applied, `git diff` with new files added -N."""
    tree = tmp / ("git-" + stem)
    if tree.exists():
        shutil.rmtree(tree)
    tree.mkdir()
    git = lambda *a: subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", *a], cwd=tree, check=True, capture_output=True, text=True).stdout
    git("init", "-q")
    for p, text in files.items():
        (tree / p).parent.mkdir(parents=True, exist_ok=True)
        (tree / p).write_bytes(text.encode())
    git("add", ".")
    git("commit", "-q", "-m", "base")
    for p, text in change.items():
        if text is None:
            (tree / p).unlink()
            continue
        (tree / p).parent.mkdir(parents=True, exist_ok=True)
        (tree / p).write_bytes(text.encode())
        if p not in files:
            git("add", "-N", p)
    out = tmp / f"{stem}.patch"
    out.write_text(git("diff"))
    return out


def srcedit_upload(page, phone, patch):
    show_tab(page, "source")
    page.evaluate("delete document.getElementById('ed_status').dataset.state")
    page.set_input_files("#ed_upload", str(patch))
    wait_for(lambda: page.evaluate("document.getElementById('ed_status').dataset.state") in ("applied", "refused"), 30)
    return page.evaluate("[document.getElementById('ed_status').dataset.state, document.getElementById('ed_status').textContent]")


def srcedit_counts(a, b):
    import difflib
    add = dele = 0
    for line in difflib.unified_diff(a.splitlines(True), b.splitlines(True), n=3):
        if line.startswith("+") and not line.startswith("+++"):
            add += 1
        elif line.startswith("-") and not line.startswith("---"):
            dele += 1
    return add, dele


def srcedit_fit(page, tag, problems):
    st = srcedit_state(page)
    if st["hscroll"]:
        problems.append(f"{tag}: the page scrolls sideways")
    for k in ("ta", "panel"):
        if st[k][2] > st["vw"] + 1 or st[k][0] < -1:
            problems.append(f"{tag}: {k} {st[k]} leaves the viewport {st['vw']}")


def srcedit_run(browser, base, shots, name, scheme, phone, tmp):
    path, before, header, newfile = SRCEDIT_FILES[name]
    entry = kernel_entries()[name]
    orig = srcedit_orig(entry, path)
    problems, r, errors = [], {}, []
    width = 390 if phone else 1280
    tag = f"{name} {width} {scheme}"
    stem = f"srcedit-{name}-{width}"
    ctx = browser.new_context(viewport={"width": 390, "height": 844}, has_touch=True, is_mobile=True, accept_downloads=True) if phone else \
        browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.emulate_media(color_scheme=scheme)
    page.goto(f"{base}/index.html?kernel={name}&paused=1#source")
    try:
        srcedit_ready(page, name)
    except PlaywrightError as e:
        ctx.close()
        return [f"{tag}: no srcOverlay: {str(e)[:200]}"], r
    st = srcedit_state(page)
    if st["list"] or st["download"]:
        problems.append(f"{tag}: a fresh browser lists edits {st['list']} or offers a download")

    # 1. Edit: the editor holds the shipped text, one line number per line.
    ok, sec = srcedit_open(page, path, phone)
    st = srcedit_state(page)
    r["open editor s"] = round(sec, 2)
    if not ok or st["value"] != orig:
        ctx.close()
        return problems + [f"{tag}: Edit did not show {path} as shipped: editing {st['editing']}, {len(st['value'])} vs {len(orig)} chars"], r
    if st["gutterLines"] != len(orig.split("\n")):
        problems.append(f"{tag}: {st['gutterLines']} line numbers for {len(orig.split(chr(10)))} lines")
    if st["pressed"] != "true" or st["bodyShown"]:
        problems.append(f"{tag}: while editing Edit is pressed {st['pressed']}, the listing still shown {st['bodyShown']}")
    if st["active"] != "ed_text":
        problems.append(f"{tag}: the focus is on {st['active']!r}, not in the editor")
    if name == "debian-6.1-686" and sec > 5:
        problems.append(f"{tag}: Edit of {path} ({len(orig.splitlines())} lines) took {sec:.1f} s")
    srcedit_fit(page, f"{tag} editor", problems)

    # 2. Type: Tab inserts a tab (the focus stays), Enter a line.
    at = orig.index(before) if before else orig.index("\n", len(orig) // 2) + 1
    at = orig.rfind("\n", 0, at) + 1
    page.evaluate("(at) => { const t = document.getElementById('ed_text'); t.focus(); t.setSelectionRange(at, at); }", at)
    page.keyboard.press("Tab")
    page.keyboard.type("/* E2 edit */")
    page.keyboard.press("Enter")
    want = orig[:at] + "\t/* E2 edit */\n" + orig[at:]
    if name == "hello":
        # The last newline taken away: the patch says "\ No newline at end of file".
        page.keyboard.press("Control+End")
        page.keyboard.press("Backspace")
        want = want[:-1]
    st = srcedit_state(page)
    if st["value"] != want or st["active"] != "ed_text":
        problems.append(f"{tag}: typed text wrong or the focus left (Tab): active {st['active']!r}, "
                        f"{st['value'][at - 5:at + 30]!r} vs {want[at - 5:at + 30]!r}")
    saved = wait_for(lambda: "saved" in srcedit_state(page)["state"], 10)
    st = srcedit_state(page)
    r["state"] = st["state"]
    if not saved or "line" not in st["state"]:
        problems.append(f"{tag}: the status does not say saved and where the caret is: {st['state']!r}")
    if st["gutterLines"] != len(want.split("\n")):
        problems.append(f"{tag}: {st['gutterLines']} line numbers after the edit, {len(want.split(chr(10)))} lines")
    # Scrolled to the end: the line numbers follow.
    page.evaluate("document.getElementById('ed_text').scrollTop = 1e9")
    wait_for(lambda: (lambda s: s["gutterTop"] == s["taTop"])(srcedit_state(page)), 5)
    st = srcedit_state(page)
    if st["gutterTop"] != st["taTop"]:
        problems.append(f"{tag}: the line numbers scrolled to {st['gutterTop']}, the text to {st['taTop']}")
    page.evaluate("document.getElementById('ed_text').scrollTop = 0")
    if not phone:
        srcedit_shots(page, shots, f"{stem}-editor", scheme)
    else:
        page.screenshot(path=str(shots / f"{stem}-editor-{scheme}.png"))

    # 3. Escape leaves the editor (to Edit), the panel stays open.
    page.focus("#ed_text")
    page.keyboard.press("Escape")
    st = srcedit_state(page)
    if st["active"] != "src_edit" or not st["open"] or not st["editing"]:
        problems.append(f"{tag}: Escape in the editor: focus {st['active']!r}, panel open {st['open']}, editing {st['editing']}")
    # Enter on Edit stops editing (the built text and its coverage again), Enter again edits.
    page.keyboard.press("Enter")
    wait_for(lambda: not srcedit_state(page)["editing"], 5)
    st = srcedit_state(page)
    if st["editing"] or not st["bodyShown"] or st["pressed"] != "false":
        problems.append(f"{tag}: Enter on Edit did not stop editing: editing {st['editing']}, listing {st['bodyShown']}")
    page.focus("#src_edit")
    page.keyboard.press("Enter")
    wait_for(lambda: srcedit_state(page)["taShown"], 10)
    if srcedit_state(page)["value"] != want:
        problems.append(f"{tag}: editing again does not show the edited text")

    # 4. The panel's Diff: this file's diff instead of the text, and back.
    srcedit_press(page, "#ed_diffbtn", phone)
    wait_for(lambda: srcedit_state(page)["pdiffShown"], 5)
    st = srcedit_state(page)
    if not st["pdiffShown"] or st["taShown"] or "+\t/* E2 edit */" not in st["pdiff"] or "@@ -" not in st["pdiff"]:
        problems.append(f"{tag}: the panel's Diff: shown {st['pdiffShown']}, editor shown {st['taShown']}, {st['pdiff'][:200]!r}")
    if name == "hello" and "\\ No newline at end of file" not in st["pdiff"]:
        problems.append(f"{tag}: the diff does not say the last line lost its newline: {st['pdiff'][-200:]!r}")
    page.screenshot(path=str(shots / f"{stem}-pdiff-{scheme}.png"))
    srcedit_press(page, "#ed_diffbtn", phone)
    wait_for(lambda: srcedit_state(page)["taShown"], 5)

    # 5. While the Machine tab shows the VM, keys typed in the editor stay there.
    if name == "hello" and not phone:
        show_tab(page, "machine")
        page.click("#ed_text")
        page.keyboard.press("Control+Home")
        page.keyboard.type("xy")
        st = srcedit_state(page)
        if not st["value"].startswith("xy"):
            problems.append(f"{tag}: typed with the Machine tab shown, the editor has {st['value'][:20]!r}")
        page.keyboard.press("Backspace")
        page.keyboard.press("Backspace")
        wait_for(lambda: srcedit_state(page)["value"] == want, 5)

    # 6. The Source tab: the file listed with its counts, its diff, the patch.
    show_tab(page, "source")
    wait_for(lambda: srcedit_state(page)["list"], 10)
    st = srcedit_state(page)
    add, dele = srcedit_counts(orig, want)
    r["list"] = st["list"]
    if st["list"] != [{"path": path, "add": add, "del": dele}]:
        problems.append(f"{tag}: the Source tab lists {st['list']}, want {path} +{add} -{dele}")
    srcedit_press(page, f'#ed_list li[data-path="{path}"] button.eddiffbtn', phone)
    wait_for(lambda: srcedit_state(page)["diffShown"], 5)
    st = srcedit_state(page)
    if "+++ b/" + path not in st["diff"]:
        problems.append(f"{tag}: the Source tab's Diff of {path}: {st['diff'][:200]!r}")
    srcedit_fit(page, f"{tag} Source tab", problems)
    srcedit_shots(page, shots, f"{stem}-list", scheme)
    fname, patch = srcedit_download(page, phone, tmp, stem + "-1")
    r["download"] = fname
    if not fname.endswith(".patch") or name not in fname:
        problems.append(f"{tag}: the patch is called {fname!r}")
    ok, msg, after = srcedit_git_apply(tmp, {path: orig}, patch)
    if not ok or after.get(path) != want:
        problems.append(f"{tag}: git apply of the downloaded patch: {msg or 'other text'}")
    ov = srcedit_overlay(page)
    if ov["paths"] != [path] or ov["files"][path] != [want, orig]:
        problems.append(f"{tag}: srcOverlay: paths {ov['paths']}, read/original equal {[x == y for x, y in zip(ov['files'].get(path, []), [want, orig])]}")

    # 7. An uploaded patch (made by git on the edited tree): all files or none.
    files = {path: want}
    change = {path: want.replace("/* E2 edit */", "/* E2 edit, patched */")}
    if header:
        hdr = srcedit_orig(entry, header)
        files[header] = hdr
        change[header] = hdr.replace("\n", "\n/* E2 header */\n", 1)
        change[newfile] = "#define E2_NEW 1\n"
    good = srcedit_git_patch(tmp, files, change, stem + "-up")
    # The same changes but this file's hunk made on the shipped text at the
    # edited line: it does not match, so no file changes.
    line_end = orig.index("\n", at)
    stale = dict(change, **{path: orig[:line_end] + " /* stale */" + orig[line_end:]})
    bad = srcedit_git_patch(tmp, dict(files, **{path: orig}), stale, stem + "-bad")
    if name == "hello":
        # Not a patch, and a rename: refused with the reason, nothing changed.
        for stem2, body, why in (("junk", "hello\n", "not a unified diff"),
                                 ("rename", f"diff --git a/{path} b/x.c\nsimilarity index 100%\nrename from {path}\nrename to x.c\n", "renames")):
            junk = tmp / f"{stem}-{stem2}.patch"
            junk.write_text(body)
            state, text = srcedit_upload(page, phone, junk)
            if state != "refused" or why not in text or srcedit_overlay(page) != ov:
                problems.append(f"{tag}: the {stem2} upload: {state} {text!r}")
    state, text = srcedit_upload(page, phone, bad)
    r["refused"] = text
    ov2 = srcedit_overlay(page)
    if state != "refused" or path not in text or ov2 != ov:
        problems.append(f"{tag}: a patch with a hunk that does not match: {state} {text!r}; overlay changed {ov2 != ov}")
    state, text = srcedit_upload(page, phone, good)
    r["applied"] = text
    ov = srcedit_overlay(page)
    want_paths = sorted(change)
    if state != "applied" or ov["paths"] != want_paths or any(ov["files"][p][0] != change[p] for p in want_paths):
        problems.append(f"{tag}: the git patch: {state} {text!r}; paths {ov['paths']} vs {want_paths}; "
                        f"text equal {[ov['files'].get(p, [None])[0] == change[p] for p in want_paths]}")
    if newfile and ov["files"].get(newfile, [0, 0])[1] is not None:
        problems.append(f"{tag}: the new file {newfile} has an original: {ov['files'].get(newfile)}")
    st = srcedit_state(page)
    if [x["path"] for x in st["list"]] != want_paths:
        problems.append(f"{tag}: after the patch the Source tab lists {st['list']}")

    # 8. A reload keeps the edits; the editor opens on them.
    page.reload()
    srcedit_ready(page, name)
    ov3 = srcedit_overlay(page)
    if ov3 != ov:
        problems.append(f"{tag}: after a reload srcOverlay has {ov3['paths']}, before {ov['paths']}")
    # Not editing, the panel says the file has edits; its Diff shows them.
    page.evaluate("(p) => MKB.openSourceFile(p)", path)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 60)
    wait_for(lambda: srcedit_state(page)["barShown"], 10)
    st = srcedit_state(page)
    if not st["barShown"] or "You changed this file" not in st["state"] or st["editing"]:
        problems.append(f"{tag}: after a reload the panel does not say {path} has edits: {st['state']!r}")
    srcedit_press(page, "#ed_diffbtn", phone)
    wait_for(lambda: srcedit_state(page)["pdiffShown"], 10)
    st = srcedit_state(page)
    if not st["pdiffShown"] or "/* E2 edit, patched */" not in st["pdiff"]:
        problems.append(f"{tag}: Diff without Edit first: shown {st['pdiffShown']}, {st['pdiff'][:120]!r}")
    srcedit_press(page, "#ed_diffbtn", phone)
    ok, _ = srcedit_open(page, path, phone)
    st = srcedit_state(page)
    if not ok or st["value"] != change[path]:
        problems.append(f"{tag}: after a reload Edit shows other text ({ok})")
    # A second download: git apply on the shipped files gives every file.
    shipped = {path: orig}
    if header:
        shipped[header] = srcedit_orig(entry, header)
    _, patch2 = srcedit_download(page, phone, tmp, stem + "-2")
    ok, msg, after = srcedit_git_apply(tmp, shipped, patch2)
    if not ok or any(after.get(p) != change[p] for p in want_paths):
        problems.append(f"{tag}: git apply of the second patch: {msg or [after.get(p) == change[p] for p in want_paths]}")

    # 9. Typed back to the shipped text: no longer an edit.
    if name == "hello":
        srcedit_open(page, path, phone)
        page.fill("#ed_text", orig)
        if not wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10):
            problems.append(f"{tag}: the shipped text typed back is still an edit: {srcedit_overlay(page)['paths']}")
    if header:
        # The Source tab's file name: the panel's editor moves to that file.
        show_tab(page, "source")
        srcedit_press(page, f'#ed_list li[data-path="{header}"] button.edopen', phone)
        wait_for(lambda: (lambda s: s["editing"] and s["value"] == change[header])(srcedit_state(page)), 15)
        st = srcedit_state(page)
        if not st["editing"] or st["value"] != change[header]:
            problems.append(f"{tag}: the list's {header} did not open in the editor: editing {st['editing']}, {st['value'][:60]!r}")
        page.evaluate("(p) => MKB.openSourceFile(p)", path)
        wait_for(lambda: srcedit_state(page)["value"] == change[path], 15)

    # 10. Revert (press twice) one file, then Revert all.
    if path in srcedit_overlay(page)["paths"]:
        # The panel's Revert: the editor gets the shipped text back.
        srcedit_press(page, "#ed_revert", phone)
        srcedit_press(page, "#ed_revert", phone)
        wait_for(lambda: path not in srcedit_overlay(page)["paths"], 10)
        st = srcedit_state(page)
        if path in srcedit_overlay(page)["paths"] or st["value"] != orig:
            problems.append(f"{tag}: the panel's Revert of {path}: edited {srcedit_overlay(page)['paths']}, the editor has "
                            f"{st['value'][:60]!r}, status {st['status']!r}, bar {st['state']!r}")
    show_tab(page, "source")
    left = srcedit_overlay(page)["paths"]
    if left:
        one = left[0]
        sel = f'#ed_list li[data-path="{one}"] button.edrevert'
        srcedit_press(page, sel, phone)
        armed = page.text_content(sel)
        if srcedit_overlay(page)["paths"] != left:
            problems.append(f"{tag}: one press of Revert reverted {one}")
        srcedit_press(page, sel, phone)
        wait_for(lambda: one not in srcedit_overlay(page)["paths"], 10)
        if one in srcedit_overlay(page)["paths"] or "again" not in (armed or ""):
            problems.append(f"{tag}: Revert of {one}: armed label {armed!r}, still edited {one in srcedit_overlay(page)['paths']}")
    if srcedit_overlay(page)["paths"]:
        srcedit_press(page, "#ed_revert_all", phone)
        srcedit_press(page, "#ed_revert_all", phone)
        wait_for(lambda: srcedit_overlay(page)["paths"] == [], 10)
    st = srcedit_state(page)
    if srcedit_overlay(page)["paths"] or st["list"] or st["download"]:
        problems.append(f"{tag}: after Revert all: {srcedit_overlay(page)['paths']} {st['list']} download {st['download']}")
    if errors:
        problems.append(f"{tag}: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def srcedit_shots(page, shots, stem, scheme):
    for s in ("light", "dark"):
        page.emulate_media(color_scheme=s)
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"{stem}-{s}.png"))
    page.emulate_media(color_scheme=scheme)


def srcedit_nostore_run(browser, base, shots):
    """Without IndexedDB the edits stay in the page until a reload, and it says so."""
    problems, r, errors = [], {}, []
    entry = kernel_entries()["hello"]
    path = SRCEDIT_FILES["hello"][0]
    orig = srcedit_orig(entry, path)
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script("Object.defineProperty(window, 'indexedDB', { get() { throw new Error('storage blocked'); } })")
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#source")
    srcedit_ready(page, "hello")
    ok, _ = srcedit_open(page, path, False)
    page.evaluate("(at) => { const t = document.getElementById('ed_text'); t.focus(); t.setSelectionRange(at, at); }", 0)
    page.keyboard.type("// x\n")
    wait_for(lambda: "reload" in srcedit_state(page)["state"], 10)
    st = srcedit_state(page)
    r["state"] = st["state"]
    ov = srcedit_overlay(page)
    if not ok or "reload" not in st["state"] or ov["paths"] != [path] or ov["files"][path][0] != "// x\n" + orig:
        problems.append(f"no IndexedDB: state {st['state']!r}, overlay {ov['paths']}")
    show_tab(page, "source")
    if "reload" not in page.text_content("#ed_count") + page.text_content("#ed_status"):
        problems.append(f"no IndexedDB: the Source tab does not say the edits go with a reload: {page.text_content('#ed_count')!r}")
    # A revert's first press expires: the label comes back, nothing reverted.
    page.click("#ed_revert_all")
    armed = page.text_content("#ed_revert_all")
    time.sleep(5.5)
    if "again" not in armed or page.text_content("#ed_revert_all") != "Revert all" or srcedit_overlay(page)["paths"] != [path]:
        problems.append(f"no IndexedDB: the armed Revert all ({armed!r}) did not expire: {page.text_content('#ed_revert_all')!r}")
    # The API: write (a line taken out: a deletion-only diff), patch, revert.
    api = page.evaluate("""async ([p, t]) => { await MKB.srcOverlay.write(p, t); const patch = MKB.srcOverlay.patch();
      await MKB.srcOverlay.revert(p); return [patch, MKB.srcOverlay.paths(), await MKB.srcOverlay.read(p) === await MKB.srcOverlay.original(p)]; }""",
                        [path, orig.split("\n", 1)[1]])
    first = orig.split("\n", 1)[0]
    added = [x for x in api[0].split("\n") if x.startswith("+") and not x.startswith("+++")]
    if f"-{first}\n" not in api[0] or added or api[1] != [] or not api[2]:
        problems.append(f"no IndexedDB: srcOverlay write/patch/revert: {api[0][:200]!r} {api[1]} {api[2]}")
    # Edit opened at a picked line: the caret is on it.
    page.click("#src_edit")
    wait_for(lambda: not srcedit_state(page)["editing"], 5)
    page.evaluate("(p) => MKB.openSourceLine(p, 12)", path)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 30)
    page.click("#src_edit")
    wait_for(lambda: "line 12," in srcedit_state(page)["state"], 10)
    if "line 12," not in srcedit_state(page)["state"]:
        problems.append(f"no IndexedDB: opened at line 12, the editor says {srcedit_state(page)['state']!r}")
    # Upload through its button (the file chooser).
    junk = shots / "srcedit-junk.patch"
    junk.write_text("not a patch\n")
    page.evaluate("delete document.getElementById('ed_status').dataset.state")
    with page.expect_file_chooser() as fc:
        page.click("#ed_upload_btn")
    fc.value.set_files(str(junk))
    wait_for(lambda: page.evaluate("document.getElementById('ed_status').dataset.state") == "refused", 10)
    if page.evaluate("document.getElementById('ed_status').dataset.state") != "refused":
        problems.append(f"no IndexedDB: Upload a patch: {page.text_content('#ed_status')!r}")
    junk.unlink()
    if errors:
        problems.append(f"no IndexedDB: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SRCEDIT_IDB_FAIL = {
    # IndexedDB's open of the editor's database fails (onerror).
    "open": """(() => { const open = IDBFactory.prototype.open;
      IDBFactory.prototype.open = function (name, v) {
        if (name !== 'mkb-srcedit') return open.call(this, name, v);
        const r = { error: new DOMException('test: open refused', 'UnknownError') };
        setTimeout(() => r.onerror && r.onerror(), 0);
        return r; }; })()""",
    # It opens, but reading the stored edits fails.
    "getAll": """(() => { const getAll = IDBObjectStore.prototype.getAll;
      IDBObjectStore.prototype.getAll = function (...a) {
        if (this.name !== 'edits') return getAll.apply(this, a);
        const r = { error: new DOMException('test: getAll refused', 'UnknownError') };
        setTimeout(() => r.onerror && r.onerror(), 0);
        return r; }; })()""",
}


def srcedit_paths_run(browser, base, tmp):
    """hello, the paths the main run does not take. Uploaded patches refused
    for each reason the reader and the overlay give (nothing changes); one
    applied with a hunk three lines off and the last newline taken away;
    one deleting the file open in the editor. srcOverlay patches of a
    two-place edit, a deleted file with an empty new one, and a rewrite
    past ED_DIFF_MAX (one hunk): git apply on the shipped file gives the
    same text. A file the patch made, open in the editor, reverted: the
    editor closes. Edit kept on while the panel moves to a file with no
    text: Edit turns off. Ctrl+S stores at once. IndexedDB not opening, or
    the stored edits not loading: the Source tab says why the edits go
    with a reload."""
    problems, r, errors = [], {}, []
    entry = kernel_entries()["hello"]
    path = SRCEDIT_FILES["hello"][0]
    orig = srcedit_orig(entry, path)
    lines = orig.splitlines(True)
    n = len(lines)
    if not orig.endswith("\n") or n < 20:
        return [f"{path}: want a file of >= 20 lines ending in a newline ({n} lines)"], r
    k = next(i for i in range(5, n - 5) if lines.count(lines[i]) == 1 and lines[i].strip())
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#source")
    srcedit_ready(page, "hello")
    clean = srcedit_overlay(page)

    def upload(stem, body):
        p = tmp / f"srcedit-paths-{stem}.patch"
        p.write_text(body)
        return srcedit_upload(page, False, p)

    # 1. Refused, each with its reason; the overlay stays as it was.
    hdr = f"--- a/{path}\n+++ b/{path}\n"
    refusals = [
        ("quoted", f'--- "a/{path} x"\n+++ "b/{path} x"\n@@ -1 +1 @@\n-a\n+b\n', "quoted file names are not supported"),
        ("nohead", "@@ -1 +1 @@\n-a\n+b\n", "a hunk without a file header"),
        ("cut", hdr + f"@@ -1,3 +1,3 @@\n {lines[0]}", f"{path}: the patch ends inside @@ -1,3 +1,3 @@"),
        ("badline", hdr + f"@@ -1,2 +1,2 @@\n {lines[0]}*{lines[1]}", "starts with neither space, - nor +"),
        ("noname", "--- /dev/null\n+++ /dev/null\n@@ -0,0 +1 @@\n+x\n", "a file without a name"),
        ("exists", f"diff --git a/{path} b/{path}\nnew file mode 100644\n--- /dev/null\n+++ b/{path}\n@@ -0,0 +1 @@\n+x\n",
         f"{path}: the patch makes it, but it is in the tree"),
        ("missing", "--- a/no/such.c\n+++ b/no/such.c\n@@ -1 +1 @@\n-a\n+b\n", "no/such.c: not in this kernel's source"),
        ("partdelete", f"diff --git a/{path} b/{path}\ndeleted file mode 100644\n--- a/{path}\n+++ /dev/null\n@@ -1 +0,0 @@\n-{lines[0]}",
         f"{path}: the patch deletes it, but its lines are not the whole file"),
    ]
    for stem, body, why in refusals:
        state, text = upload(stem, body)
        if state != "refused" or why not in text or srcedit_overlay(page) != clean:
            problems.append(f"upload {stem}: {state} {text!r}, want refused with {why!r} and nothing changed")
    # 2. A hunk three lines below its text (found above, as patch does) and
    # the last line's newline taken ("\ No newline at end of file").
    moved = lines[k].rstrip("\n") + " /* moved hunk */\n"
    body = hdr + f"@@ -{k + 4} +{k + 4} @@\n-{lines[k]}+{moved}" + \
        f"@@ -{n} +{n} @@\n-{lines[-1]}+{lines[-1]}\\ No newline at end of file\n"
    want = "".join(lines[:k]) + moved + "".join(lines[k + 1:])[:-1]
    state, text = upload("offset", body)
    got = page.evaluate("(p) => MKB.srcOverlay.read(p)", path)
    if state != "applied" or got != want:
        problems.append(f"upload offset: {state} {text!r}, text as wanted {got == want}")
    page.evaluate("(p) => MKB.srcOverlay.revert(p)", path)

    # 3. The API's patches, each through git apply on the shipped file.
    two = lines[:]
    two[2], two[n - 3] = "/* E2 two-a */\n", "/* E2 two-b */\n"
    # Every line kept, 70 new ones after each: more than ED_DIFF_MAX (2000)
    # differences, so one hunk of the whole middle (Myers would give one
    # hunk per kept line).
    big = "".join(x + "".join(f"/* E2 rewrite {i} {j} */\n" for j in range(70)) for i, x in enumerate(lines))
    if 70 * (n - 1) <= 2000:
        problems.append(f"{path}: {n} lines are too few for more than 2000 differences")
    api = page.evaluate("""async ([p, two, big]) => { const out = {};
      await MKB.srcOverlay.write(p, two); out.two = MKB.srcOverlay.patch(); await MKB.srcOverlay.revert(p);
      await MKB.srcOverlay.write(p, null); await MKB.srcOverlay.write('e2/empty.h', ''); out.del = MKB.srcOverlay.patch();
      out.delPaths = MKB.srcOverlay.paths(); await MKB.srcOverlay.revert(p); await MKB.srcOverlay.revert('e2/empty.h');
      const t0 = performance.now(); await MKB.srcOverlay.write(p, big); out.big = MKB.srcOverlay.patch(); out.bigMs = performance.now() - t0;
      await MKB.srcOverlay.revert(p); out.left = MKB.srcOverlay.paths(); return out; }""", [path, "".join(two), big])
    r["rewrite patch ms"] = round(api["bigMs"])
    for stem, want_after in (("two", {path: "".join(two)}), ("del", {"e2/empty.h": ""}), ("big", {path: big})):
        p = tmp / f"srcedit-api-{stem}.patch"
        p.write_text(api[stem])
        ok, msg, after = srcedit_git_apply(tmp, {path: orig}, p)
        if not ok or after != want_after:
            problems.append(f"srcOverlay.patch() {stem}: git apply {msg or 'gives other files'} ({sorted(after)})")
    if "deleted file mode 100644" not in api["del"] or "new file mode 100644" not in api["del"] or api["delPaths"] != ["boot/hello.c", "e2/empty.h"]:
        problems.append(f"srcOverlay delete + empty new file: paths {api['delPaths']}, patch {api['del'][:160]!r}")
    if api["big"].count("\n@@ ") != 1 or api["left"]:
        problems.append(f"rewrite past ED_DIFF_MAX: {api['big'].count(chr(10) + '@@ ')} hunks (want 1), left {api['left']}")

    # 4. The file open in the editor deleted by an upload; a new file the
    # patch made, open in the editor, reverted.
    ok, _ = srcedit_open(page, path, False)
    state, text = upload("delete", f"diff --git a/{path} b/{path}\ndeleted file mode 100644\n--- a/{path}\n+++ /dev/null\n"
                         f"@@ -1,{n} +0,0 @@\n" + "".join("-" + x for x in lines))
    ov = srcedit_overlay(page)
    if not ok or state != "applied" or ov["paths"] != [path] or ov["files"][path][0] is not None:
        problems.append(f"upload deleting the file in the editor: {state} {text!r}, overlay {ov['paths']}")
    page.evaluate("(p) => MKB.srcOverlay.revert(p)", path)
    state, text = upload("new", "diff --git a/e2/new.h b/e2/new.h\nnew file mode 100644\n--- /dev/null\n+++ b/e2/new.h\n@@ -0,0 +1 @@\n+#define E2 1\n")
    page.evaluate("(p) => MKB.openSourceFile(p)", "e2/new.h")
    if not wait_for(lambda: (lambda s: s["editing"] and s["value"] == "#define E2 1\n")(srcedit_state(page)), 15):
        problems.append(f"the uploaded new file in the editor: {state} {text!r}, {srcedit_state(page)['value'][:40]!r}")
    page.evaluate("MKB.srcOverlay.revert('e2/new.h')")
    if not wait_for(lambda: not srcedit_state(page)["editing"], 5) or srcedit_overlay(page) != clean:
        problems.append(f"reverting the new file in the editor: editing {srcedit_state(page)['editing']}, {srcedit_overlay(page)['paths']}")

    # 5. Edit on, the panel moves to a file with no text: Edit turns off and
    # stays off on the next file.
    ok, _ = srcedit_open(page, path, False)
    page.evaluate("(p) => MKB.openSourceFile(p)", "no/such.c")
    wait_for(lambda: page.get_attribute("#src_body", "data-file") == "no/such.c" and not srcedit_state(page)["editing"], 15)
    st = srcedit_state(page)
    page.evaluate("(p) => MKB.openSourceFile(p)", path)
    wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.get_attribute("#src_body", "data-file") == path, 30)
    st2 = srcedit_state(page)
    if not ok or st["editing"] or st["pressed"] != "false" or st2["editing"]:
        problems.append(f"Edit on, then a file with no text: editing {st['editing']} (pressed {st['pressed']}), then {path}: {st2['editing']}")

    # 6. Ctrl+S in the editor stores what was typed before the typing pause.
    srcedit_open(page, path, False)
    cs = page.evaluate("""(p) => { const ta = document.getElementById('ed_text'); ta.setRangeText('/* S */', 0, 0, 'end');
      ta.dispatchEvent(new Event('input')); const pending = MKB.ED.timer !== 0 && !MKB.srcOverlay.paths().includes(p);
      const ev = new KeyboardEvent('keydown', { key: 's', ctrlKey: true, cancelable: true, bubbles: true }); ta.dispatchEvent(ev);
      return [pending, ev.defaultPrevented, MKB.ED.timer, MKB.srcOverlay.paths().includes(p)]; }""", path)
    if cs != [True, True, 0, True]:
        problems.append(f"Ctrl+S: [typed not yet stored, default prevented, timer, stored] = {cs}")
    page.evaluate("(p) => MKB.srcOverlay.revert(p)", path)
    # Edit pressed (editing off) right after typing, before the pause: what
    # was typed is stored, and the editor's status line is left alone.
    srcedit_open(page, path, False)
    off = page.evaluate("""async (p) => { const ta = document.getElementById('ed_text'); ta.setRangeText('/* off */', 0, 0, 'end');
      ta.dispatchEvent(new Event('input')); const pending = MKB.ED.timer !== 0; document.getElementById('src_edit').click();
      await MKB.srcOverlay.ready; await new Promise((res) => setTimeout(res, 100));
      return [pending, MKB.ED.on, (await MKB.srcOverlay.read(p)).startsWith('/* off */')]; }""", path)
    if off != [True, False, True]:
        problems.append(f"Edit off right after typing: [save pending, editing, stored] = {off}")
    # The Source tab showing a file's diff, that file reverted: the diff goes.
    show_tab(page, "source")
    wait_for(lambda: srcedit_state(page)["list"], 10)
    srcedit_press(page, f'#ed_list li[data-path="{path}"] button.eddiffbtn', False)
    shown = wait_for(lambda: srcedit_state(page)["diffShown"], 5)
    srcedit_press(page, f'#ed_list li[data-path="{path}"] button.edrevert', False)
    srcedit_press(page, f'#ed_list li[data-path="{path}"] button.edrevert', False)
    wait_for(lambda: not srcedit_state(page)["list"], 10)
    st = srcedit_state(page)
    if not shown or st["diffShown"] or st["list"]:
        problems.append(f"revert of the file whose diff the Source tab shows: diff shown before {shown}, after {st['diffShown']}, list {st['list']}")
    if errors:
        problems.append(f"page errors {errors[:3]}")
    ctx.close()

    # 7. IndexedDB refusing: the Source tab says the edits go with a reload, and why.
    for what, why in (("open", "IndexedDB did not open (test: open refused)"),
                      ("getAll", "the stored edits did not load (test: getAll refused)")):
        ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
        ctx.add_init_script(SRCEDIT_IDB_FAIL[what])
        page = ctx.new_page()
        errs = []
        page.on("pageerror", lambda e: errs.append(str(e)[:200]))
        page.goto(f"{base}/index.html?kernel=hello&paused=1#source")
        srcedit_ready(page, "hello")
        page.evaluate("async (p) => MKB.srcOverlay.write(p, '/* idb */\\n' + await MKB.srcOverlay.original(p))", path)
        count = page.text_content("#ed_count")
        r[f"idb {what}"] = count
        if why not in count or "reload" not in count or errs:
            problems.append(f"IndexedDB {what} fails: the Source tab says {count!r} (want {why!r}); page errors {errs[:2]}")
        ctx.close()
    return problems, r


def suite_srcedit(ctx):
    failed = False
    runs = [("hello", "light", False), ("hello", "dark", True)]
    if "minified-linux" in ctx.entries:
        runs += [("minified-linux", "dark", False), ("minified-linux", "light", True)]
    runs += [(n, "light", False) for n in debian_names(ctx) if n in SRCEDIT_FILES]
    with tempfile.TemporaryDirectory() as td:
        for name, scheme, phone in runs:
            t0 = time.monotonic()
            problems, r = srcedit_run(ctx.browser, ctx.base, ctx.shots, name, scheme, phone, pathlib.Path(td))
            failed |= report("srcedit", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                             seconds=round(time.monotonic() - t0))
        failed |= report("srcedit", *srcedit_paths_run(ctx.browser, ctx.base, pathlib.Path(td)), kernel="hello", case="paths")
    failed |= report("srcedit", *srcedit_nostore_run(ctx.browser, ctx.base, ctx.shots), kernel="hello", storage="none")
    return failed
