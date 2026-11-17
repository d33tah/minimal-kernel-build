"""Browser test suite debian (test_browser.py --suites)."""

import json
import os
import re
import subprocess
import time

from tests.browser.common import (
    cov_rows, debian_names, dwarf_check, kernel_entries, machine_do, open_page, report, show_tab, SITE, wait_for,
)


def debian_case(browser, base, shots, name):
    """The Debian sample: its defaults, the end-of-panic auto-stop with the
    message on the screen, and Run to start_kernel naming start_kernel."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("jit", "stepped"), 60)
    r["cmdline"] = page.input_value("#cmdline")
    r["ram"] = page.eval_on_selector("#memory", "e => e.value")
    r["label"] = page.eval_on_selector("#kernel", "e => e.selectedOptions[0].text")
    if r["cmdline"] != entry.get("cmdline") or str(entry.get("ram")) != r["ram"]:
        problems.append(f"Debian defaults not applied: {r}")
    ok = wait_for(lambda: page.get_attribute("#end_banner", "data-kind") == "panic-text", 300, 1)
    r["seconds_to_panic"] = round(time.monotonic() - t0)
    r["banner"] = page.inner_text("#end_banner")
    screen = page.inner_text("#screen_container")
    if not ok or "Unable to mount root fs" not in r["banner"] or "end Kernel panic" not in screen:
        problems.append(f"no end-of-panic stop: {r['banner']!r}")
    page.screenshot(path=str(shots / "debian-panic.png"))
    # No disasm.json: the listing at start_kernel is decoded live.
    syms0 = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk0 = next(x for x in syms0 if x["name"] == "start_kernel")
    open_page(page, sk0["paddr"] // 4096)
    wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 15)
    open_page(page, sk0["paddr"] // 4096)
    got = listing_insns(page, sk0["paddr"], sk0["paddr"] + 64)
    r["listing_source"] = page.get_attribute("#listing", "data-source")
    r["start_kernel_insns"] = list(got.values())[:3]
    if r["listing_source"] != "live" or not got:
        problems.append(f"no live instructions at start_kernel: {r['listing_source']}")
    vmlinux = os.environ.get("MKB_DEBIAN_VMLINUX")
    if vmlinux and got:
        # The first 5 bytes are the ftrace call the kernel patches to a NOP
        # at boot, so compare from the second instruction on.
        dump = subprocess.run(["llvm-objdump", "-d", "--no-show-raw-insn", f"--start-address={sk0['vaddr']:#x}",
                               f"--stop-address={sk0['vaddr'] + 32:#x}", vmlinux], capture_output=True, text=True).stdout
        want = [norm_insn(l.split(":", 1)[1]) for l in dump.splitlines() if re.match(r"^\s*[0-9a-f]+:", l)]
        live = [got[a] for a in sorted(got)]
        r["objdump"], r["live"] = want[1:3], live[1:3]
        if want[1:3] != live[1:3]:
            problems.append(f"live decode {live[1:3]} != llvm-objdump {want[1:3]}")
    page.close()
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    st32 = next(s for s in syms if s["name"] == "startup_32")
    # Default boot: fast-forward (JIT) through BIOS, setup and the XZ
    # decompressor, stop exactly at startup_32, then 1/s.
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=1#debugger")
    ok = wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5)
    r["ff_seconds"] = round(time.monotonic() - t0, 1)
    machine_do(page, "click", "#dbg_pause")
    info = page.evaluate("MKB.ffInfo && { stop: MKB.ffInfo.stopPhys, steps: MKB.ffInfo.steps, ms: Math.round(MKB.ffInfo.ms) }")
    r["ff"] = info
    if not ok or not info or info["stop"] != st32["paddr"] or r["ff_seconds"] > 120:
        problems.append(f"fast-forward to startup_32 {st32['paddr']:#x}: {info}, {r['ff_seconds']} s")
    # D1: the guest's TSC runs with the virtual clock (v86: TSC = clock x
    # 10^6 per ms - offset, offset 0 from power-on). The fast-forward ends by
    # restoring the slice's snapshot; restore_state sets the offset from the
    # clock it reads, and the clock used to be put back only afterwards, so
    # the TSC lagged by however far the JIT ran past the slice start (in
    # Firefox and WebKit by a varying number of chunks, D0).
    r["tsc_minus_clock"] = page.evaluate("""() => { const c = MKB.emulator.v86.cpu; c.store_current_tsc();
      return c.current_tsc[0] + c.current_tsc[1] * 2 ** 32 - Math.round(MKB.CLOCK.now() * 1e6); }""")
    if abs(r["tsc_minus_clock"]) > 1_000_000:
        problems.append(f"after the fast-forward the TSC is off the virtual clock by {r['tsc_minus_clock']} ticks (1 ms = 10^6)")
    s0 = page.evaluate("MKB.stepper.steps")
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 0.5)
    r["entry_to_start_kernel"] = page.evaluate("MKB.stepper.steps") - s0
    if not ok or r["entry_to_start_kernel"] != 62512:
        problems.append(f"startup_32 -> start_kernel took {r['entry_to_start_kernel']} instructions, not 62,512")
    page.close()
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    src_files = {}  # source files fetched: path -> bytes
    page.on("response", lambda resp: "/src/" in resp.url and resp.url.split("?")[0].endswith(".gz") and
            src_files.__setitem__(resp.url.split("/src/", 1)[1].split("?")[0], int(resp.headers.get("content-length") or 0)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    # A watch list in this browser that is not JSON: the panel starts with no watches.
    page.add_init_script(f"localStorage.setItem('mkb-watch:{name}', '[\"init_task\", oops')")
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    t0 = time.monotonic()
    ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                  and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 1)
    r["runto_seconds"] = round(time.monotonic() - t0)
    r["runto_symbol"] = page.get_attribute("#cur_sym", "data-name")
    r["context_source"] = page.get_attribute("#context", "data-source")
    if r["context_source"] != "live" or page.locator("#context .ins.cur").count() != 1:
        problems.append(f"Debugger code window at EIP: source {r['context_source']!r}, boxed lines {page.locator('#context .ins.cur').count()}")
    if not ok or r["runto_symbol"] != "start_kernel":
        problems.append(f"Run to start_kernel: {r['runto_symbol']!r} ({page.inner_text('#dbg_status')})")
    elif "dwarf" in entry:
        dwarf_check(page, name, sk["vaddr"] - sk["paddr"], problems, r, entry_fn="start_kernel" if "src" in entry else None)
        page.screenshot(path=str(shots / "dwarf-debugger-debian-1280.png"))
        open_page(page, sk["paddr"] // 4096)
        wait_for(lambda: page.locator("#listing .src:not(.dim)").count() > 0, 15)
        r["listing_c_lines"] = page.locator("#listing .src:not(.nol):not(.dim)").count()
        main = page.locator("#listing .src", has_text="main.c").first
        r["listing_main_c"] = main.inner_text() if main.count() else None
        if not r["listing_main_c"]:
            problems.append("Debian: no init/main.c line in the listing of start_kernel")
        page.screenshot(path=str(shots / "dwarf-listing-debian-1280.png"))
        if "src" in entry:
            debian_source_panel(page, page.locator(f'#listing .src[data-addr="{sk["paddr"]}"]'), src_files, problems, r, shots)
        debian_covtree(page, entry, bins, problems, r, shots)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def debian_source_panel(page, line, src_files, problems, r, shots):
    """Debian's C source: the listing's C line at start_kernel's entry shows
    init/main.c's text, tapping it opens start_kernel with lines that ran; only the files of lines shown
    were fetched; screenshots light/dark at 1100 and 390 px."""
    wait_for(lambda: line.count() and not line.locator(".dim").count(), 20)
    r["listing_main_c_text"] = line.inner_text() if line.count() else None
    if not r["listing_main_c_text"] or "main.c:" not in r["listing_main_c_text"] or line.locator(".dim").count():
        problems.append(f"Debian: the init/main.c line of the listing has no text: {r['listing_main_c_text']!r}")
    line.click()
    wait_for(lambda: page.is_visible("#src_panel") and page.get_attribute("#src_body", "data-state") == "ok", 30)
    r["panel"] = page.inner_text("#src_title")
    r["panel_code"] = int(page.get_attribute("#src_body", "data-code") or 0)
    r["panel_executed"] = int(page.get_attribute("#src_body", "data-executed") or 0)
    r["panel_info"] = page.inner_text("#src_info")
    if not r["panel"].startswith("start_kernel, init/main.c:") or not 0 < r["panel_executed"] < r["panel_code"]:
        problems.append(f"Debian source panel: {r['panel']!r}, {r['panel_executed']} of {r['panel_code']} lines executed")
    if "Debian's source package" not in r["panel_info"]:
        problems.append(f"Debian source panel does not say where the source comes from: {r['panel_info']!r}")
    r["src_files_fetched"] = len(src_files)
    r["src_bytes_fetched"] = sum(src_files.values())
    if not src_files or len(src_files) > 100:
        problems.append(f"Debian: {len(src_files)} source files fetched: {sorted(src_files)[:10]}")
    page.screenshot(path=str(shots / "source-debian-1100-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / "source-debian-1100-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"Debian source panel: the page scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"source-debian-390-{scheme}.png"))
    page.set_viewport_size({"width": 1100, "height": 1000})


def debian_covtree(page, entry, bins, problems, r, shots):
    """Debian's Source tab at start_kernel: the whole tree from files.json
    without reading every shard (only those with code that ran), totals
    equal files.json, init/main.c has lines that ran."""
    F = json.loads((SITE / entry["dwarf"] / "files.json").read_text())
    shards = json.loads((SITE / entry["dwarf"] / "index.json").read_text())["shards"]
    page.click("#src_close")
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok" and not page.inner_text("#cov_status").startswith("Reading"), 60)
    page.fill("#cov_filter", "init/main.c")
    rows = cov_rows(page)
    root = next((x for x in rows if x["kind"] == "root"), None)
    main = next((x for x in rows if x["kind"] == "file" and x["path"] == "init/main.c"), None)
    r["covtree_root"], r["covtree_main"], r["covtree_status"] = root, main, page.inner_text("#cov_status")
    r["covtree_shards_fetched"] = [len(bins), len(shards)]
    if not root or root["lines"] != sum(f[1] for f in F["files"]) or root["linesRun"] is None:
        problems.append(f"Debian coverage tree root {root} (files.json: {sum(f[1] for f in F['files'])} lines)")
    if not main or not main["linesRun"] or main["lines"] != F["files"][[f[0] for f in F["files"]].index("init/main.c")][1]:
        problems.append(f"Debian coverage tree init/main.c: {main}")
    if len(bins) >= len(shards) / 2:
        problems.append(f"Debian coverage tree fetched {len(bins)} of {len(shards)} shards")
    page.evaluate("document.getElementById('panel_source').scrollIntoView()")
    page.screenshot(path=str(shots / "covtree-debian-1100.png"))
    # A filter matching more rows than the tree draws (1,500): those and a
    # note with the rest. Rows: the root, every directory above a matching
    # file, the matching files (a path or a function name containing the
    # filter, case-insensitive), and under a file with a matching function
    # its functions that match or all of them if its path matches (only
    # the root open before, as on a fresh page).
    q = "init"
    files = [f[0] for f in F["files"]]
    fns = [[] for _ in files]
    for fn in F["funcs"]:
        if fn[1] >= 0:
            fns[fn[1]].append(fn[0].lower())
    dirs, total = set(), 1
    for p, names in zip(files, fns):
        hit = [n for n in names if q in n]
        if q in p.lower() or hit:
            parts = p.lstrip("/").split("/")
            dirs |= {"/".join(parts[:i + 1]) for i in range(len(parts) - 1)}
            total += 1 + (len(names) if hit and q in p.lower() else len(hit))
    total += len(dirs)
    page.evaluate("() => { MKB.COV.open.clear(); MKB.COV.open.add(''); }")
    page.fill("#cov_filter", q)
    shown = int(page.get_attribute("#cov_tree", "data-rows") or 0)
    note = page.inner_text("#cov_tree > p.note") if page.locator("#cov_tree > p.note").count() else ""
    r["covtree_many"] = [shown, note, total]
    if total <= 1500 or shown != 1500 or note != f"{total - 1500:,} more rows: narrow the filter or close some directories.":
        problems.append(f"Debian coverage tree filtered by {q!r}: {shown} rows and {note!r}, files.json gives {total} rows")
    page.fill("#cov_filter", "")


def norm_insn(text):
    """Instruction text without addresses' symbol names, comments and spacing."""
    text = re.sub(r"<[^>]*>|#.*$", "", text.lower())
    text = re.sub(r"\s+", " ", text).strip().replace(", ", ",")
    # Numbers modulo formatting: -0x3fef0834 and 0xc010f7cc, 9 and 0x9 are equal.
    text = re.sub(r"-?(?:0x[0-9a-f]+|\b\d+\b)", lambda m: hex(int(m.group(0), 0) & 0xffffffff), text)
    # Spellings of the same byte instruction (-O0 code has many): iced-x86's
    # setneb is llvm-objdump's setne, and a byte operation's immediate is
    # one byte ($0xff, llvm-objdump's $-0x1).
    text = re.sub(r"^(set[a-z]+?)b ", r"\1 ", text)
    if re.match(r"^[a-z]+b \$", text):
        text = re.sub(r"^([a-z]+b \$)(0x[0-9a-f]+)", lambda m: m.group(1) + hex(int(m.group(2), 16) & 0xff), text)
    return text


def listing_insns(page, lo, hi):
    """{paddr: normalized instruction} of the Page listing's lines in [lo, hi)."""
    out = {}
    for addr, text in page.evaluate("""() => [...document.querySelectorAll('#listing .ins')].map(e =>
            [Number(e.dataset.addr), e.textContent])"""):
        if lo <= addr < hi:
            # "addr bytes... insn [symbol+off]": drop the address, bytes and symbol+offset
            body = re.sub(r"^[0-9a-f]{8} +(?:[0-9a-f]{2})+\u2026? *", "", text.strip())
            body = re.sub(r" [A-Za-z_][\w.]*\+0x[0-9a-f]+$", "", body)
            out[addr] = norm_insn(body)
    return out


def suite_debian(ctx):
    failed = False
    for name in debian_names(ctx):
        failed |= report("debian", *debian_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed
