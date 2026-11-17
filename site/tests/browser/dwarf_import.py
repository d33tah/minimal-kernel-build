"""Browser test suite dwarf_import (test_browser.py --suites)."""

import gzip
import json
import os
import pathlib
import re
import tarfile
import tempfile
import time

from tests.browser.common import (
    debug_elf, dwarf_check, DWARF_STEPS, frames_agree, graph_box_xy, import_inputs, kernel_entries, locals_open_all,
    report, show_tab, SITE, source_rows, source_texts, symbolize_frames, wait_for,
)


def import_source_check(page, problems, r, shots):
    """At start_kernel of an imported vmlinux: file:line only until its source is picked; a
    .tar.gz is refused with a reason, a file that is not a tar too; a folder
    without the kernel's files says the line's file is not in it; a pax,
    a GNU and a ustar .tar and a folder picked through its button (each
    holding the tree one level down) give the text of the lines, equal to
    the files' own; the panel opens; its file switcher lists the picked
    folder's files, and one the DWARF does not know opens as a whole file."""
    src = SITE / kernel_entries()["minified-linux"]["src"]
    r["src_status_before"] = page.inner_text("#src_status")
    if page.get_attribute("#src_pick", "hidden") is not None or "file:line only" not in r["src_status_before"] or any(source_texts(page)):
        problems.append(f"import without source: picker hidden or text shown: {r['src_status_before']!r}")
    with tempfile.TemporaryDirectory() as tmp:
        tree = pathlib.Path(tmp) / "linux-min"
        for gz in src.rglob("*.gz"):
            out = tree / gz.relative_to(src).with_suffix("")
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(gzip.decompress(gz.read_bytes()))
        # A path over 100 bytes: a pax "path" record, a GNU "L" entry.
        (tree / ("long-" * 25)).mkdir()
        (tree / ("long-" * 25) / "name.c").write_text("int x;\n")
        tars = {}
        for fmt, name in ((tarfile.PAX_FORMAT, "pax.tar"), (tarfile.GNU_FORMAT, "gnu.tar"), (tarfile.USTAR_FORMAT, "ustar.tar")):
            tars[name] = pathlib.Path(tmp) / name
            with tarfile.open(tars[name], "w", format=fmt) as t:
                t.add(tree, arcname="linux-min")
        tgz = pathlib.Path(tmp) / "src.tar.gz"
        tgz.write_bytes(gzip.compress(tars["pax.tar"].read_bytes()))
        page.set_input_files("#src_tar", str(tgz))
        wait_for(lambda: "compressed" in page.inner_text("#src_status"), 10)
        r["src_status_tgz"] = page.inner_text("#src_status")
        if "compressed" not in r["src_status_tgz"]:
            problems.append(f"a .tar.gz source was not refused with a reason: {r['src_status_tgz']!r}")
        notar = pathlib.Path(tmp) / "notar.tar"
        notar.write_bytes(b"not a tar " * 200)
        page.set_input_files("#src_tar", str(notar))
        wait_for(lambda: "could not read" in page.inner_text("#src_status"), 10)
        r["src_status_notar"] = page.inner_text("#src_status")
        if "could not read notar.tar: not a tar archive (no header at byte 0)" not in r["src_status_notar"]:
            problems.append(f"a file that is not a tar was not refused with a reason: {r['src_status_notar']!r}")
        # A folder without the kernel's files: Code at EIP's line says its file is not there.
        other = pathlib.Path(tmp) / "other"
        (other / "docs").mkdir(parents=True)
        (other / "docs" / "readme.c").write_text("int y;\n")
        page.set_input_files("#src_dir", str(other))
        wait_for(lambda: page.get_attribute("#src_status", "data-state") == "picked", 30)
        _, state, frames = source_rows(page)
        time.sleep(0.5)
        tips = page.evaluate("[...document.querySelectorAll('#context .src:not(.nol)')].map((e) => e.dataset.tip || '')")
        want_tip = f"{frames[0]['file'] if frames else '?'} is not in the folder other"
        r["src_other_folder"] = [state, tips[:1]]
        if not tips or not any(want_tip in t for t in tips) or any(source_texts(page)):
            problems.append(f"a folder without the kernel's files: Code at EIP's C lines say {tips[:2]}, want {want_tip!r}")
        picks = [("pax.tar", "#src_tar", tars["pax.tar"]), ("gnu.tar", "#src_tar", tars["gnu.tar"]),
                 ("ustar.tar", "#src_tar", tars["ustar.tar"]), ("folder", "#src_dir_btn", tree)]
        for label, sel, path in picks:
            if sel == "#src_dir_btn":  # in the Machine tab
                show_tab(page, "machine")
                with page.expect_file_chooser() as fc:
                    page.click(sel)
                fc.value.set_files(str(path))
                show_tab(page, "debugger")
            else:
                page.set_input_files(sel, str(path))
            wait_for(lambda: page.get_attribute("#src_status", "data-state") == "picked" and
                     page.inner_text("#src_status").find(label if label != "folder" else "linux-min") >= 0, 30)
            _, state, frames = source_rows(page)
            texts = source_texts(page)
            want = []
            for f in frames:
                p = tree / f["file"] if f["file"] else None
                lines = p.read_text(errors="replace").split("\n") if p and p.is_file() else []
                want.append(lines[f["line"] - 1].strip() if 0 < f["line"] <= len(lines) else "")
            r[f"src_{label}"] = f"{page.inner_text('#src_status')}: {sum(1 for t in texts if t)} rows with text"
            if state != "ok" or not any(texts) or texts != want:
                problems.append(f"source picked as {label}: rows {texts} != files {want}")
            # Every file indexed (a ustar directory whose path is in the
            # prefix field has an empty name field: not the end of the archive).
            if label == "folder":
                n_want = sum(1 for x in tree.rglob("*") if x.is_file())
            else:
                with tarfile.open(path) as t:
                    n_want = sum(1 for m in t.getmembers() if m.isfile())
            n_got = re.search(r"\(([\d,]+) files\)", page.inner_text("#src_status"))
            if not n_got or int(n_got.group(1).replace(",", "")) != n_want:
                problems.append(f"source picked as {label}: {page.inner_text('#src_status')!r}, the {label} has {n_want} files")
        page.locator("#cur_src .frame button").first.click()
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 10)
        r["src_panel"] = page.inner_text("#src_title")
        if page.get_attribute("#src_body", "data-state") != "ok" or "the folder linux-min" not in page.inner_text("#src_info"):
            problems.append(f"source panel of a picked folder: {r['src_panel']!r} {page.inner_text('#src_info')!r}")
        page.screenshot(path=str(shots / "source-import-1280.png"))
        # The switcher lists the folder's files: one no line table names opens whole.
        extra = "long-" * 25 + "/name.c"
        page.click("#src_files")
        wait_for(lambda: page.is_visible("#src_q"), 5)
        page.fill("#src_q", "long-long-long")
        sel = f'#src_list [role=option][data-path="{extra}"]'
        if not wait_for(lambda: page.locator(sel).count() > 0, 5):
            problems.append(f"the file switcher of a picked folder does not list {extra}: "
                            f"{[x['path'] for x in page.evaluate(SRCFILES_LIST_JS)][:4]}")
        else:
            page.click(sel)
            ok = srcfiles_wait_file(page, extra, timeout=10)
            got = [page.inner_text("#src_title"), page.inner_text("#src_body").strip(), page.evaluate("document.getElementById('src_body').scrollTop")]
            r["src_extra_file"] = got
            if not ok or not re.match(r"\s*1\s+int x;", got[1]) or got[2] != 0:
                problems.append(f"{extra} from the switcher: [title, text, scroll] {got}, want the whole file 'int x;' from the top")
            page.screenshot(path=str(shots / "source-import-extra-1280.png"))
        page.click("#src_close")


def dwarf_import_case(browser, base, shots):
    """An imported unstripped vmlinux: its DWARF is read in a Worker one
    compile unit at a time (clang forms: the minified Linux's -g vmlinux;
    GCC forms: Debian's -dbg vmlinux), matches llvm-symbolizer, survives a
    reload from IndexedDB; a vmlinux without DWARF says so."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    url = f"{base}/index.html?kernel=minified-linux&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    # The import reboots the kernel: start that boot paused too (?paused=1 holds for the first boot only).
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported" and
             "compile units" in page.inner_text("#sym_status"), 30)
    r["status"] = page.inner_text("#sym_status")
    if page.get_attribute("#sym_status", "data-dwarf") != "import" or "compile units" not in r["status"]:
        problems.append(f"imported -g vmlinux: DWARF not read: {r['status']!r}")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        problems.append("imported -g vmlinux: Run to start_kernel did not stop there")
    import_source_check(page, problems, r, shots)
    samples = dwarf_check(page, "minified-linux", sk["vaddr"] - sk["paddr"], problems, r, DWARF_STEPS[:6])
    r["imported_first"] = r.get("dwarf_first")
    page.screenshot(path=str(shots / "dwarf-import-1280.png"))
    # Parameters and locals decoded in the browser from the imported DWARF
    # equal the shipped ones (gen-dwarf.js on the same ELF) at the same count.
    at = page.evaluate("MKB.stepper.steps")
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
    imported = locals_open_all(page)
    ship = browser.new_page(viewport={"width": 1280, "height": 1000})
    ship.on("pageerror", lambda e: errors.append(str(e)))
    ship.goto(url)
    wait_for(lambda: ship.get_attribute("#dbg_state", "data-state") == "paused", 60)
    ship.fill("#dbg_addr", hex(sk["vaddr"]))
    ship.click("#dbg_runto")
    wait_for(lambda: ship.get_attribute("#dbg_state", "data-state") == "paused"
             and ship.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120)
    ship.fill("#dbg_n", str(at - ship.evaluate("MKB.stepper.steps")))
    ship.click("#dbg_stepn")
    wait_for(lambda: ship.evaluate("MKB.stepper.steps") == at and ship.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: ship.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 20)
    shipped = locals_open_all(ship)
    ship.close()
    nvars = sum(len(sc["vars"]) for f in imported for sc in f["scopes"])
    r["imported_locals"] = f"{len(imported)} frames, {nvars} variables, equal to the shipped: {imported == shipped}"
    if imported != shipped or not nvars:
        diff = next(((a, b) for a, b in zip(imported, shipped) if a != b), (imported[len(shipped):], shipped[len(imported):]))
        problems.append(f"imported -g vmlinux: parameters and locals at {at} differ from the shipped DWARF's: {str(diff)[:400]}")
    # Without a file list (an import), the function facts' "defined at"
    # opens the function around its code with the declaration's line picked.
    show_tab(page, "calls")
    wait_for(lambda: page.evaluate("MKB.G.rects.length > 0"), 20)
    xy = graph_box_xy(page, "start_kernel")
    if not xy:
        problems.append("imported -g vmlinux: no start_kernel box in the Calls tab")
    else:
        page.mouse.click(xy["x"], xy["y"])
        if not wait_for(lambda: page.locator("#graph_fn .fnloc").count() > 0, 20):
            problems.append("imported -g vmlinux: start_kernel's facts in the Calls tab have no 'defined at' link")
        else:
            loc = page.locator("#graph_fn .fnloc").first
            decl = [loc.get_attribute("data-file"), int(loc.get_attribute("data-line"))]
            loc.click()
            wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok" and page.locator("#src_body .pick").count() > 0, 20)
            got = page.evaluate("""() => { const p = document.querySelector('#src_body .pick');
                return [document.getElementById('src_title').textContent, p ? Number(p.dataset.line) : 0,
                        p ? p.querySelector('.srctext').textContent : '']; }""")
            text = gzip.decompress((SITE / kernel_entries()["minified-linux"]["src"] / (decl[0] + ".gz")).read_bytes()).decode().split("\n")
            r["import_defined_at"] = [decl, got]
            if not got[0].startswith("start_kernel, " + decl[0]) or got[1] != decl[1] or got[2] != text[decl[1] - 1]:
                problems.append(f"imported -g vmlinux: 'defined at' {decl} opened [title, picked line, its text] {got}, "
                                f"want start_kernel's function with line {decl[1]} {text[decl[1] - 1]!r}")
            page.click("#src_close")
    # Reload: the symbols come back from localStorage, the decoded units from IndexedDB.
    page.goto("about:blank")
    page.goto(url)
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached", 60)
    r["reload_status"] = page.inner_text("#sym_status")
    got = page.evaluate("""async (pas) => { const out = [];
        for (const pa of pas) { let f = MKB.dbgFrames(pa);
            for (let i = 0; i < 50 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = MKB.dbgFrames(pa); }
            out.push(f); }
        return out; }""", [p for p, _, _ in samples])
    same = sum(1 for (p, st, fr), g in zip(samples, got) if g["state"] == st and (g.get("frames") or []) == fr)
    r["after_reload"] = f"{same}/{len(samples)} EIPs answer as before the reload"
    if page.get_attribute("#sym_status", "data-dwarf") != "cached" or same != len(samples):
        problems.append(f"after a reload: {r['after_reload']}, status {r['reload_status']!r}")
    # The shipped kernel's dwarf/index.json arriving after the restore began
    # (a slow server; seen in CI as the dwarf_forms suite's reload failing):
    # it must not land in the restored import's state (its index, its info).
    # (In the page: the service worker may answer the fetch, where a route
    # does not see it. The flag is per tab and taken out below.)
    page.add_init_script("""(() => { try { if (!sessionStorage.getItem('mkbSlowIndex')) return; } catch (e) { return; } const f = window.fetch;
      window.fetch = (u, o) => /(^|\\/)kernels\\/minified-linux\\/dwarf\\/index\\.json/.test(String(u))
        ? new Promise((ok) => setTimeout(ok, 3000)).then(() => { document.documentElement.dataset.slowIndex = 'sent'; return f(u, o); })
        : f(u, o); })()""")
    page.evaluate("sessionStorage.setItem('mkbSlowIndex', '1')")
    page.goto("about:blank")
    page.goto(url)
    page.evaluate("sessionStorage.removeItem('mkbSlowIndex')")
    wait_for(lambda: page.get_attribute("#sym_status", "data-dwarf") == "cached"
             and "(remembered for this bzImage)" in page.inner_text("#sym_status"), 60)
    time.sleep(4)  # past the delayed index.json
    r["reload_slow_index"] = page.inner_text("#sym_status")
    if page.get_attribute("html", "data-slow-index") != "sent":
        problems.append("after a reload with the shipped dwarf/index.json late: the page did not ask for it (the case tests nothing)")
    got = page.evaluate("""async (pas) => { const out = [];
        for (const pa of pas) { let f = MKB.dbgFrames(pa);
            for (let i = 0; i < 50 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = MKB.dbgFrames(pa); }
            out.push(f); }
        return out; }""", [p for p, _, _ in samples])
    same = sum(1 for (p, st, fr), g in zip(samples, got) if g["state"] == st and (g.get("frames") or []) == fr)
    if (page.get_attribute("#sym_status", "data-dwarf") != "cached" or "remembered from an earlier import" not in r["reload_slow_index"]
            or "shards" in r["reload_slow_index"] or same != len(samples)):
        problems.append(f"after a reload with the shipped dwarf/index.json late: {same}/{len(samples)} EIPs answer as before, "
                        f"status {r['reload_slow_index']!r}")
    show_tab(page, "machine")
    page.click("#sym_forget")
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") != "imported", 30)
    page.close()
    # A vmlinux without DWARF (the plain build): said so, symbols still work.
    plain = (import_inputs() or [None, None])[1]
    if plain:
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(url)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        page.set_input_files("#symfile", str(plain))
        wait_for(lambda: "no DWARF" in page.inner_text("#sym_status") or "compile units" in page.inner_text("#sym_status"), 30)
        r["plain_status"] = page.inner_text("#sym_status")
        if "no DWARF" not in r["plain_status"]:
            problems.append(f"vmlinux without DWARF: not said so: {r['plain_status']!r}")
        show_tab(page, "machine")
        page.click("#sym_forget")
        page.close()
    # Debian's -dbg vmlinux (GCC forms, 300 MB): only the parts needed are read.
    deb = debug_elf("debian-6.1-686")
    if deb:
        dsyms = json.loads((SITE / kernel_entries()["debian-6.1-686"]["symbols"]).read_text())["symbols"]
        rnd = __import__("random").Random(5)
        fns = [s for s in dsyms if s["type"] in "tT" and s["size"] > 16]
        pas = sorted(s["paddr"] + rnd.randrange(s["size"]) for s in rnd.sample(fns, 40))
        page = browser.new_page(viewport={"width": 1280, "height": 1000})
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(f"{base}/index.html?kernel=debian-6.1-686&paused=1#debugger")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
        t0 = time.monotonic()
        page.set_input_files("#symfile", str(deb))
        wait_for(lambda: "compile units" in page.inner_text("#sym_status") or
                 page.get_attribute("#sym_status", "data-state") == "error", 120)
        r["debian_import_seconds"] = round(time.monotonic() - t0, 1)
        r["debian_status"] = page.inner_text("#sym_status")
        got = page.evaluate("""async (pas) => { const out = [];
            for (const pa of pas) { let f = MKB.dbgFrames(pa);
                for (let i = 0; i < 300 && f.state === 'loading'; i++) { await new Promise((ok) => setTimeout(ok, 100)); f = MKB.dbgFrames(pa); }
                out.push(f); }
            return out; }""", pas)
        want = symbolize_frames(deb, [p + 0xC0000000 for p in pas])
        bad = [(hex(p), g, w) for p, g, w in zip(pas, got, want) if g["state"] != "ok" or not frames_agree(g["frames"], w)]
        r["debian_import"] = f"{len(pas) - len(bad)}/{len(pas)} agree with llvm-symbolizer"
        if bad:
            problems.append(f"Debian vmlinux import: {bad[:3]}")
        show_tab(page, "machine")
        page.click("#sym_forget")
        page.close()
    elif os.environ.get("CI"):
        problems.append("no debian-6.1-686.elf in $MKB_DEBUG_ELF_DIR")
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_dwarf_import(ctx):
    return report("dwarf_import", *dwarf_import_case(ctx.browser, ctx.base, ctx.shots))

SRCFILES_STATE_JS = """() => { const sp = document.getElementById('src_panel'), sb = document.getElementById('src_body');
    const sw = document.getElementById('src_switch');
    return { open: !sp.hidden, title: document.getElementById('src_title').textContent, file: sb.dataset.file || '',
             state: sb.dataset.state || '', code: Number(sb.dataset.code || 0), executed: Number(sb.dataset.executed || 0),
             top: sb.scrollTop, hash: location.hash, switcher: !!sw && !sw.hidden,
             exec: [...sb.querySelectorAll('.exec:not(.cont)')].map(e => Number(e.dataset.line)),
             pick: (sb.querySelector('.pick') || {dataset: {}}).dataset.line || '' }; }"""

SRCFILES_LIST_JS = """() => [...document.querySelectorAll('#src_list [role=option]')].map(e => ({ path: e.dataset.path || '',
    dir: e.dataset.dir || '', group: e.dataset.group || '', executed: e.dataset.executed ?? '', code: e.dataset.code ?? '',
    active: e.classList.contains('active'), text: e.textContent }))"""


def srcfiles_state(page):
    return page.evaluate(SRCFILES_STATE_JS)


def srcfiles_wait_file(page, path, whole=True, timeout=25):
    want = path + " (whole file)" if whole else None
    ok = wait_for(lambda: (lambda s: s["open"] and s["file"] == path and s["state"] == "ok" and
                           (want is None or s["title"] == want))(srcfiles_state(page)), timeout)
    time.sleep(0.4)
    return ok
