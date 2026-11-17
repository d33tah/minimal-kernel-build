"""Browser test suite genimport (test_browser.py --suites)."""

import gzip
import hashlib
import json
import os
import pathlib
import shutil
import tempfile
import time

from tests.browser.common import cov_open_all, cov_rows, debug_elf, kernel_entries, report, show_tab, SITE, wait_for


def genimport_case(browser, base, shots):
    """An imported vmlinux gets the files CI makes for a shipped kernel,
    made in the browser (plan E1: genimport.js, genworker.js, kgen.js with
    clang.wasm's llvm-nm and llvm-objdump): the minified Linux's -g vmlinux
    imported into the page's minified-linux. Its disasm.json and the dwarf/
    files without a file name in them equal the shipped kernel's bytes, the
    kernel switches to them (DWARF shards, static jump paths from
    disasm.json), the coverage tree of a counted boot has the shipped
    kernel's files and lines with code and init/main.c has lines run, a
    picked source tree becomes src/ (the Source panel's text is the file's),
    and all of it is back after a reload (IndexedDB). With an offset typed
    by hand nothing is made; without clang.wasm the status says why and the
    DWARF is read as needed. Screenshots light/dark at 1280 and 390 px, no
    horizontal scroll."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    if not base.startswith("https://") and not (SITE / "clang" / "clang.wasm").exists():
        return ["no site/clang/clang.wasm (the clang-wasm artifact): nothing can be made"], r
    entry = kernel_entries()["minified-linux"]
    kdir = (SITE / entry["dwarf"]).parent
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    nsyms = len(json.loads((SITE / entry["symbols"]).read_text())["symbols"])
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    same = {"disasm.json": sha(SITE / entry["disasm"])}
    for f in ["files.json", "types.json"] + sorted(x.name for x in (SITE / entry["dwarf"]).glob("*.bin")):
        same["dwarf/" + f] = sha(SITE / entry["dwarf"] / f)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    wait_for(lambda: page.evaluate("!!(MKB.symbols && MKB.image)"), 60)
    t0 = time.monotonic()
    page.set_input_files("#symfile", str(elf))
    if not wait_for(lambda: page.get_attribute("#gen_status", "data-state") in ("done", "failed"), 300):
        problems.append(f"the files were not made in 300 s: {page.inner_text('#gen_status')!r}")
    r["made_s"] = round(time.monotonic() - t0, 1)
    r["gen_status"] = page.inner_text("#gen_status")
    wait_for(lambda: page.evaluate("MKB.DBG.mode === 'shards' && !!MKB.DBG.index"), 30)
    st = page.evaluate("""async () => {
      const hex = async (b) => [...new Uint8Array(await crypto.subtle.digest('SHA-256', b))].map((x) => x.toString(16).padStart(2, '0')).join('');
      const sums = {};
      for (const [k, b] of MKB.GEN.files)
        sums[k.slice(MKB.GEN.base.length)] = await hex(b);
      return { mode: MKB.DBG.mode, info: MKB.DBG.info, cfi: MKB.dbgCfiInfo(), insns: MKB.disasm ? MKB.disasm.insns.length : null, syms: MKB.symbols.symbols.length,
               generated: !!(MKB.symbols.imported && MKB.symbols.imported.generated), sym: MKB.$('sym_status').textContent, sums };
    }""")
    r["state"] = {k: v for k, v in st.items() if k != "sums"}
    r["files"] = len(st["sums"])
    if st["mode"] != "shards" or "made in this browser from minified-linux.elf" not in st["info"] or not st["generated"]:
        problems.append(f"the kernel did not switch to the files made: {r['state']}")
    if st["syms"] != nsyms or "No disasm.json" in st["sym"]:
        problems.append(f"symbols: {st['syms']} (shipped {nsyms}); status {st['sym']!r}")
    diff = [k for k, v in same.items() if st["sums"].get(k) != v]
    r["same_as_shipped"] = f"{len(same) - len(diff)} of {len(same)}"
    if diff:
        problems.append(f"files made in the browser differ from the shipped kernel's: {diff[:6]}")
    # The static jump paths are disasm.json's: the Page tab at start_kernel.
    sk = page.evaluate("MKB.symbols.symbols.find((s) => s.name === 'start_kernel').paddr")
    page.evaluate(f"MKB.selectPage({sk >> 12})")
    wait_for(lambda: "(disasm.json)" in page.inner_text("#dump_info"), 30)
    r["dump_info"] = page.inner_text("#dump_info")[:120]
    if "(disasm.json)" not in r["dump_info"]:
        problems.append(f"the listing is not disasm.json's: {r['dump_info']!r}")
    # The rest of the debug files through kfetch: function facts, locals (.gz), types, CFI.
    wait_for(lambda: page.evaluate(f"MKB.dbgFnFacts('start_kernel').state === 'ok' && MKB.dbgLocalGroups({sk}).state === 'ok' && !!(MKB.dbgCfiRow({sk}) || {{}}).cfa"), 30)
    r["debug"] = page.evaluate(f"""({{ facts: MKB.dbgFnFacts('start_kernel').fns.length, locals: MKB.dbgLocalGroups({sk}).G.length,
      types: MKB.dbgTypes().T.types.length, cfi: !!(MKB.dbgCfiRow({sk}) && MKB.dbgCfiRow({sk}).cfa) }})""")
    if not r["debug"]["facts"] or not r["debug"]["locals"] or not r["debug"]["types"] or not r["debug"]["cfi"]:
        problems.append(f"function facts, locals, types or CFI of start_kernel: {r['debug']}")
    # The coverage tree after the counted boot: the shipped kernel's files.
    if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 180):
        problems.append("the counted boot did not end")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and page.get_attribute("#cov_tree", "data-coverage") in ("stepped", "partial"), 60)
    cov_open_all(page)
    rows = cov_rows(page)
    files = {x["path"]: x for x in rows if x["kind"] == "file"}
    root = next((x for x in rows if x["kind"] == "root"), None)
    r["root"] = root
    if not root or root["lines"] != sum(f[1] for f in F["files"]) or len(files) != len(F["files"]):
        problems.append(f"coverage tree {root}, {len(files)} files; shipped files.json: {len(F['files'])} files")
    main = files.get("init/main.c")
    r["init/main.c"] = main
    if not main or not main["linesRun"]:
        problems.append(f"init/main.c has no lines run: {main}")
    # A picked source tree (one directory down) becomes src/.
    with tempfile.TemporaryDirectory() as tmp:
        tree = pathlib.Path(tmp) / "linux-min"
        for gz in (kdir / "src").rglob("*.gz"):
            out = tree / gz.relative_to(kdir / "src").with_suffix("")
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(gzip.decompress(gz.read_bytes()))
        show_tab(page, "machine")
        # Another tree first: a file shorter than the lines the DWARF names.
        wrong = pathlib.Path(tmp) / "wrong"
        shutil.copytree(tree, wrong)
        (wrong / "init" / "main.c").write_text("\n".join((tree / "init" / "main.c").read_text().split("\n")[:10]))
        page.set_input_files("#src_dir", str(wrong))
        wait_for(lambda: "is not the tree the kernel was built from" in page.inner_text("#gen_status"), 60)
        r["wrong_tree"] = [page.evaluate("MKB.SRC.mode"), page.inner_text("#gen_status")[-160:]]
        if r["wrong_tree"][0] != "picked" or "init/main.c" not in r["wrong_tree"][1]:
            problems.append(f"a tree that is not the kernel's: {r['wrong_tree']}")
        # The right tree, twice (the second replaces the first's src/).
        for _ in range(2):
            page.set_input_files("#src_dir", str(tree))
            wait_for(lambda: page.evaluate("MKB.SRC.mode === 'picked'"), 10)
            wait_for(lambda: page.evaluate("MKB.SRC.mode === 'shipped' && !!MKB.SRC.have && /indexed in this browser/.test(MKB.SRC.info || '')"), 60)
        r["src"] = page.evaluate("({ mode: MKB.SRC.mode, info: MKB.SRC.info, have: MKB.SRC.have ? MKB.SRC.have.size : 0 })")
        want = json.loads((kdir / "src" / "index.json").read_text())["files"]
        if r["src"]["mode"] != "shipped" or r["src"]["have"] != len(want):
            problems.append(f"the picked tree did not become src/: {r['src']}, shipped {len(want)} files")
        wait_for(lambda: page.evaluate("MKB.srcFile('init/main.c').state") == "ok", 30)
        text = page.evaluate("MKB.srcFile('init/main.c').lines")
        if text != (tree / "init" / "main.c").read_text().split("\n"):
            problems.append("init/main.c in the Source panel is not the file's text")
    r["gen_status_src"] = page.inner_text("#gen_status")
    if "src/ (" not in r["gen_status_src"]:
        problems.append(f"the status does not list src/: {r['gen_status_src']!r}")
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000})
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            show_tab(page, "machine")
            page.locator("#gen_status").scroll_into_view_if_needed()
            page.screenshot(path=str(shots / f"genimport-{width}-{scheme}.png"))
            if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
                problems.append(f"horizontal scroll at {width} px ({scheme})")
    page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    # A reload: everything is back from IndexedDB, nothing made again.
    page.reload()
    wait_for(lambda: page.evaluate("typeof window.MKB?.GEN !== 'undefined' && MKB.GEN.state === 'done' && MKB.DBG.mode === 'shards' && MKB.SRC.mode === 'shipped'"), 60)
    r["reload"] = page.evaluate("({ gen: MKB.GEN.state, restored: !!MKB.GEN.restored, mode: MKB.DBG.mode, src: MKB.SRC.mode, insns: MKB.disasm ? MKB.disasm.insns.length : null, status: MKB.$('gen_status').textContent })")
    if not r["reload"]["restored"] or r["reload"]["mode"] != "shards" or r["reload"]["src"] != "shipped" or not r["reload"]["insns"]:
        problems.append(f"after a reload: {r['reload']}")
    page.close()
    # Another kernel chosen while the files are made: they are not used.
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.evaluate("!!(MKB.symbols && MKB.image)"), 60)
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.evaluate("MKB.GEN.state === 'running'"), 30)
    page.select_option("#kernel", "hello")
    wait_for(lambda: page.evaluate("MKB.GEN.state === null && MKB.image.name === 'hello'"), 60)
    r["switched"] = page.evaluate("({ gen: MKB.GEN.state, kernel: MKB.image.name, mode: MKB.DBG.mode, imported: !!(MKB.symbols && MKB.symbols.imported) })")
    if r["switched"]["gen"] is not None or r["switched"]["imported"] or r["switched"]["kernel"] != "hello":
        problems.append(f"another kernel while the files were made: {r['switched']}")
    page.close()
    # An offset typed by hand: the files would place code elsewhere, none is made.
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.evaluate("!!(MKB.symbols && MKB.image)"), 60)
    page.fill("#sym_offset", "0xc0000000")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#sym_status", "data-state") == "imported", 30)
    time.sleep(2)
    r["hand_offset"] = page.evaluate("({ gen: MKB.GEN.state, status: MKB.$('gen_status').textContent, mode: MKB.DBG.mode })")
    if r["hand_offset"]["gen"] is not None or r["hand_offset"]["status"] or r["hand_offset"]["mode"] != "import":
        problems.append(f"an offset by hand: {r['hand_offset']}")
    page.close()
    # Without clang.wasm: the reason, and the DWARF read as needed.
    # The context's route: the page's does not see the Worker's requests.
    nc = browser.new_context(viewport={"width": 1280, "height": 1000})
    nc.route("**/clang/clang.wasm", lambda route: route.fulfill(status=404, body="no"))
    page = nc.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.evaluate("!!(MKB.symbols && MKB.image)"), 60)
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: page.get_attribute("#gen_status", "data-state") == "failed", 60)
    r["no_clang"] = page.evaluate("({ gen: MKB.GEN.state, status: MKB.$('gen_status').textContent, mode: MKB.DBG.mode })")
    if r["no_clang"]["gen"] != "failed" or "clang.wasm: HTTP 404" not in r["no_clang"]["status"] or r["no_clang"]["mode"] != "import":
        problems.append(f"without clang.wasm: {r['no_clang']}")
    nc.close()
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_genimport(ctx):
    return report("genimport", *genimport_case(ctx.browser, ctx.base, ctx.shots))
