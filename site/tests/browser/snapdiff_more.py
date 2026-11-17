"""Part of a browser suite (test_browser.py)."""

import base64
import json
import time

from tests.browser.common import (
    ALIVE, bm_close_menu, bm_dialog_save, bm_open_list, bm_state, bm_tap, kernel_entries, machine_do, PHONE,
    run_to_wait, show_tab, SITE, snap_click, snap_rows, snap_save, snap_wait_rows, wait_for,
)
from tests.browser.snapdiff_run import (
    BOOKMARKS_POINTS, snap_check_download, SNAP_DIFF_JS, SNAP_FILL_JS, SNAP_JS, SNAP_LONG_JS, snap_row_codec,
    snap_step_to,
)


def snapdiff_storage(browser, base, shots):
    """No storage (a private window, blocked site data): snapshots stay in
    memory with a note. A full quota: the save says so and keeps the copy
    in memory. Delete all empties the store."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script("Object.defineProperty(window, 'indexedDB', { get() { throw new DOMException('blocked by the test', 'SecurityError'); } });")
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    r["blocked"] = [(x["where"], x["kernel"]) for x in st["rows"]] + [st["store"][:160]]
    if len(st["rows"]) != 1 or st["rows"][0]["where"] != "memory" or not st["rows"][0]["restore"]:
        problems.append(f"blocked storage: the snapshot is not kept in memory with a Restore: {st['rows']}")
    if "memory" not in st["store"] or "not" not in st["store"]:
        problems.append(f"blocked storage: the note {st['store']!r} does not say the snapshots are only in memory")
    page.screenshot(path=str(shots / "snapdiff-blocked.png"), full_page=True)
    if errors:
        problems.append(f"blocked storage: page errors {errors[:3]}")
    ctx.close()

    # A quota below what the site uses (the service worker's cache alone is
    # megabytes), set before the page opens its database (Chrome checks the
    # quota when the database is opened): the save fails with
    # QuotaExceededError and the snapshot stays in memory.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    cdp = ctx.new_cdp_session(page)
    cdp.send("Storage.overrideQuotaForOrigin", {"origin": base, "quotaSize": 20000})
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    wait_for(lambda: "quota" in snap_rows(page)["status"].lower(), 15)
    st = snap_rows(page)
    r["quota"] = [[x["where"] for x in st["rows"]], st["status"][:200], st["store"][:200]]
    if [x["where"] for x in st["rows"]] != ["memory"] or "quota" not in st["status"].lower():
        problems.append(f"full quota: rows kept {[x['where'] for x in st['rows']]}, status {st['status']!r}")
    if "memory only" not in st["store"]:
        problems.append(f"full quota: the storage line {st['store']!r} does not count the snapshot as in memory only")
    page.screenshot(path=str(shots / "snapdiff-quota.png"), full_page=True)
    cdp.send("Storage.overrideQuotaForOrigin", {"origin": base})
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    show_tab(page, "snapshots")
    time.sleep(2)
    if snap_rows(page)["rows"]:
        problems.append("full quota: the snapshot that was not stored is listed after a reload")
    snap_save(page, 1, False)
    snap_save(page, 2, False)
    st = snap_rows(page)
    if [x["where"] for x in st["rows"]] != ["stored", "stored"]:
        problems.append(f"with the quota back: rows kept {[x['where'] for x in st['rows']]}")
    # Delete all asks again, then empties the store.
    page.click("#snap_delall")
    if len(snap_rows(page)["rows"]) != 2:
        problems.append("Delete all deleted without asking again")
    page.click("#snap_delall")
    st = snap_wait_rows(page, 0)
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    show_tab(page, "snapshots")
    time.sleep(2)
    st = snap_rows(page)
    if st["rows"]:
        problems.append(f"Delete all: {len(st['rows'])} snapshots listed after a reload")
    if errors:
        problems.append(f"quota: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_kernels(browser, base, shots, phone):
    """A snapshot of another kernel is listed as such, without Restore, with
    a switch to its kernel; after the switch it restores."""
    problems, r = [], {}
    ctx = browser.new_context(color_scheme="dark", **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    snap_save(page, 1, phone)
    machine_do(page, "select_option", "#kernel", "hello-os")
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name === 'hello-os' && MKB.emuReady"), 120)
    show_tab(page, "snapshots")
    wait_for(lambda: (snap_rows(page)["rows"] or [{}])[0].get("kernel") == "other", 20)
    st = snap_rows(page)
    x = st["rows"][0]
    r["other"] = [x["kernel"], x["restore"], x["sw"], x["text"][:120]]
    if x["kernel"] != "other" or x["restore"] or not x["sw"] or "another kernel" not in x["text"]:
        problems.append(f"another kernel: the hello snapshot under hello-os shows {r['other']}")
    if st["hscroll"]:
        problems.append("another kernel: horizontal scroll")
    page.screenshot(path=str(shots / f"snapdiff-other-{390 if phone else 1280}.png"), full_page=True)
    snap_click(page, "#snapshots button.snapswitch", phone)
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name === 'hello' && MKB.emuReady"), 120)
    wait_for(lambda: (snap_rows(page)["rows"] or [{}])[0].get("kernel") == "match", 20)
    x = snap_rows(page)["rows"][0]
    if x["kernel"] != "match" or not x["restore"]:
        problems.append(f"another kernel: after the switch the row shows kernel {x['kernel']!r}, Restore {x['restore']}")
    if errors:
        problems.append(f"another kernel: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_perf(browser, base, shots, mem):
    """A diff of two snapshots of mem bytes of RAM, and of one and now, at
    4x CPU throttle: no task on the page's main thread over 200 ms."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={mem}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    run_to_wait(page, {0x100000}, 300)
    snap_save(page, 1, False)
    show_tab(page, "debugger")
    page.fill("#dbg_n", "60")
    page.click("#dbg_stepn")
    time.sleep(2)
    st = snap_save(page, 2, False)
    ids = [x["id"] for x in st["rows"]]
    cdp = ctx.new_cdp_session(page)
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": 4})
    page.evaluate(SNAP_LONG_JS)
    for a, b in ((ids[0], ids[1]), (ids[1], "now")):
        page.select_option("#snap_a", a)
        page.select_option("#snap_b", b)
        t0 = time.monotonic()
        page.click("#snap_diff")
        done = wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 180)
        r[f"{a}->{b} seconds"] = round(time.monotonic() - t0, 1)
        if not done:
            problems.append(f"{mem >> 20} MiB: the diff {a} -> {b} did not finish at 4x throttle")
    # the hexdump and the Page tab of a changed page
    d = page.evaluate(SNAP_DIFF_JS)
    if d and d["pages"]:
        page.click(f"#snap_pages [data-page='{d['pages'][0][0]}'] button.snaphex")
        wait_for(lambda: page.get_attribute("#snap_hex", "data-page") == str(d["pages"][0][0]), 30)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    r["long tasks ms"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"{mem >> 20} MiB at 4x throttle: a {max(long)} ms task on the main thread (want <= 200)")
    cdp.send("Emulation.setCPUThrottlingRate", {"rate": 1})
    if errors:
        problems.append(f"{mem >> 20} MiB: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_LEGACY_JS = """async (id) => {
  // An S1 (gzip) record of the same state, stored as S1 stored it.
  const s = MKB.snapshots.find((x) => x.id === id);
  const raw = await MKB.snapUnpack(s);
  const gz = await new Response(new Blob([raw]).stream().pipeThrough(new CompressionStream('gzip'))).arrayBuffer();
  const rec = { id: s.id + 'gz', seq: s.seq + 1, name: 'S1 gzip snapshot', time: s.time + 1, kernel: s.kernel,
    kernelKey: s.kernelKey, ram: s.ram, raw: raw.byteLength, size: gz.byteLength, gzip: true, saveMs: 5, gzipMs: 25,
    meta: s.meta, data: new Blob([gz]) };
  await MKB.snapTx('readwrite', (st) => st.put(rec));
  return rec.id; }"""


def snapdiff_legacy(browser, base, shots):
    """Snapshots stored by S1 (gzip, no codec field) still list, restore,
    diff against an LZ4 one and download as .gz after the switch to LZ4."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel=hello&paused=1#debugger"
    page.goto(url)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate("mkbSnap.keep('A')")
    cpu_a = page.evaluate("mkbSnap.cpu()")
    st = snap_save(page, 1, False)
    lz_id = st["rows"][0]["id"]
    gz_id = page.evaluate(SNAP_LEGACY_JS, lz_id)
    # Step on, so the restore has something to undo.
    show_tab(page, "debugger")
    page.fill("#dbg_n", "40")
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != cpu_a["icount"] and
             page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    show_tab(page, "snapshots")
    st = snap_wait_rows(page, 2)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    r["rows"] = [(x["name"], x["where"]) for x in snap_rows(page)["rows"]]
    problems += snap_row_codec(page, "S1 gzip row", gz_id, "gzip")
    problems += snap_row_codec(page, "LZ4 row", lz_id, "lz4")
    # Restore the gzip one: v86 is back at A.
    snap_click(page, f"#snapshots tr[data-id='{gz_id}'] button.restore", False)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == gz_id, 30)
    now = page.evaluate("mkbSnap.cpu()")
    r["restored"] = [hex(now["eip"]), now["icount"]]
    if now["regs"] != cpu_a["regs"] or now["icount"] != cpu_a["icount"]:
        problems.append(f"restoring the S1 gzip snapshot: EIP {now['eip']:#x} (want {cpu_a['eip']:#x}), "
                        f"instructions {now['icount']} (want {cpu_a['icount']})")
    page.evaluate("mkbSnap.keep('A2')")
    # The same state twice: the diff gzip -> LZ4 is empty.
    page.select_option("#snap_a", gz_id)
    page.select_option("#snap_b", lz_id)
    page.click("#snap_diff")
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"diff S1 gzip -> LZ4 did not finish: {page.inner_text('#snap_diff_status')!r}")
    else:
        d = page.evaluate(SNAP_DIFF_JS)
        r["gzip -> lz4"] = [len(d["pages"]), d["regs"]]
        if d["pages"] or d["regs"]:
            problems.append(f"diff of the same state as gzip and as LZ4 lists pages {d['pages'][:5]} registers {d['regs'][:5]}")
    # Downloads: .gz for the S1 one, .lz4 for the new one; both are A's RAM.
    mem = base64.b64decode(page.evaluate("mkbSnap.b64('A2')"))
    for rid, codec, ext in ((gz_id, "gzip", ".v86state.gz"), (lz_id, "lz4", ".v86state.lz4")):
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{rid}'] button.snapdl").click()
        if not dl.value.suggested_filename.endswith(ext):
            problems.append(f"the {codec} snapshot downloads as {dl.value.suggested_filename!r}, want *{ext}")
        bad, r[f"download {codec}"] = snap_check_download(dl.value.path(), mem, codec)
        problems += bad
    page.screenshot(path=str(shots / "snapdiff-legacy-1280.png"), full_page=True)
    if errors:
        problems.append(f"S1 gzip: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def snapdiff_bigsave(browser, base, shots, phone):
    """C1: Save snapshot of a 64 MiB machine with 24 MiB of data is
    compressed with LZ4 in a Worker: the row says LZ4 level 1 and its
    times, no task on the page's main thread over 200 ms while saving, the
    snapshot is smaller than the state; Restore gives the RAM back."""
    problems, r = [], {}
    ctx = browser.new_context(color_scheme="dark" if phone else "light",
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={64 << 20}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    # At hello_main, where the 24 MiB of data goes (the power-on save is
    # snapdiff_poweron's).
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate(SNAP_FILL_JS, 24)
    page.evaluate("mkbSnap.keep('F')")
    time.sleep(1)
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    st = snap_save(page, 1, phone)
    r["save seconds"] = round(time.monotonic() - t0, 2)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    s = page.evaluate("(() => { const { data, meta, ...s } = MKB.snapshots[0]; return s; })()")
    r["snapshot"] = {k: s.get(k) for k in ("raw", "size", "codec", "level", "saveMs", "packMs")}
    r["long tasks ms (save)"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"Save snapshot of {s['raw']} bytes: a {max(long)} ms task on the main thread (want <= 200)")
    problems += snap_row_codec(page, "big save", st["rows"][0]["id"], "lz4")
    if not s.get("size") or s["size"] >= s["raw"]:
        problems.append(f"the LZ4 snapshot is {s.get('size')} bytes for {s['raw']} raw")
    page.screenshot(path=str(shots / f"snapdiff-bigsave-{390 if phone else 1280}.png"), full_page=True)
    # Restore after scribbling over the filled RAM.
    page.evaluate("MKB.emulator.v86.cpu.mem8.fill(0x55, 8 << 20, 9 << 20)")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    snap_click(page, f"#snapshots tr[data-id='{st['rows'][0]['id']}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == st["rows"][0]["id"], 60)
    r["restore seconds"] = round(time.monotonic() - t0, 2)
    r["restore status"] = page.inner_text("#snap_status")[:200]
    r["long tasks ms (restore)"] = sorted(page.evaluate("window.mkbLong"))[-5:]
    page.evaluate("mkbSnap.keep('G')")
    if page.evaluate("mkbSnap.pages('F', 'G')"):
        problems.append("after Restore the 64 MiB machine's RAM differs from the snapshot's")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"horizontal scroll at {390 if phone else 1280} px")
    if errors:
        problems.append(f"big save: page errors {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_POWERON_TARGET = {"hello": "hello_main", "minified-linux": "start_kernel"}


def snapdiff_poweron(browser, base, shots, name, scheme, phone):
    """B1: a snapshot saved while paused at power-on (the reset vector, before
    SeaBIOS ran) restores. v86 0.5.471's PCI set_state moved the I/O BARs of
    every BAR slot with bit 0 set, including slots with no BAR (at power-on
    the config space still holds the device's initial bytes there), and
    threw "Cannot read properties of undefined (reading 'original_bar')".
    Save at 0xF000:0xFFF0, boot on to hello_main / start_kernel, Restore:
    the registers, the instruction count and the RAM are the snapshot's;
    then the machine boots on from there to the same target (and hello
    prints its lines). No page error; screenshots."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"power-on {name} {w} {scheme}"
    target = SNAP_POWERON_TARGET[name]
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
    page.evaluate(SNAP_JS)
    cpu_p = page.evaluate("mkbSnap.cpu()")
    r["at save"] = {"CS:EIP": f"{cpu_p['regs']['CS']:#x}:{cpu_p['eip']:#x}", "icount": cpu_p["icount"]}
    if cpu_p["regs"]["CS"] != 0xF000 or cpu_p["eip"] != 0xFFF0:
        problems.append(f"{tag}: not paused at the reset vector: {r['at save']['CS:EIP']} (test setup)")
    # The slots that broke v86's PCI set_state: no BAR, I/O bit set.
    r["BAR slots without a BAR, I/O bit set"] = page.evaluate("""() => { const pci = MKB.emulator.v86.cpu.devices.pci, out = [];
      pci.devices.forEach((d, id) => { if (d && d.pci_bars) for (let i = 0; i < d.pci_bars.length; i++)
        if (!d.pci_bars[i] && pci.device_spaces[id][4 + i] & 1) out.push((d.name || id) + ' BAR' + i); }); return out; }""")
    page.evaluate("mkbSnap.keep('P')")
    st = snap_save(page, 1, phone)
    row = st["rows"][0]["id"]
    r["saved"] = st["status"][:160]

    def run_to(what):
        show_tab(page, "debugger")
        page.fill("#dbg_addr", target)
        page.click("#dbg_runto")
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                      target in page.inner_text("#dbg_status"), 300)
        if not ok:
            problems.append(f"{tag}: Run to {target} {what} did not stop there: {page.inner_text('#dbg_status')[:200]}")
        return ok

    run_to("before Restore")
    show_tab(page, "snapshots")
    snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
             "failed" in page.inner_text("#snap_status"), 60)
    r["restore status"] = page.inner_text("#snap_status")[:200]
    if page.get_attribute("#snap_status", "data-restored") != row:
        problems.append(f"{tag}: Restore of the power-on snapshot: {r['restore status']}")
    page.screenshot(path=str(shots / f"snapdiff-poweron-{name}-{w}-{scheme}-restored.png"), full_page=True)
    cpu_q = page.evaluate("mkbSnap.cpu()")
    r["after restore"] = {"CS:EIP": f"{cpu_q['regs']['CS']:#x}:{cpu_q['eip']:#x}", "icount": cpu_q["icount"]}
    bad = {k: (f"{cpu_p['regs'][k]:#x}", f"{cpu_q['regs'][k]:#x}") for k in cpu_p["regs"] if cpu_p["regs"][k] != cpu_q["regs"][k]}
    if bad:
        problems.append(f"{tag}: registers after Restore differ from the snapshot's (saved, now): {bad}")
    if cpu_q["icount"] != cpu_p["icount"]:
        problems.append(f"{tag}: instruction count after Restore {cpu_q['icount']}, saved {cpu_p['icount']}")
    page.evaluate("mkbSnap.keep('Q')")
    diff = page.evaluate("mkbSnap.pages('P', 'Q')")
    if diff:
        problems.append(f"{tag}: after Restore {len(diff)} RAM pages differ from the snapshot's, first {diff[:4]}")
    if page.get_attribute("#dbg_state", "data-state") != "paused":
        problems.append(f"{tag}: after Restore the machine is {page.get_attribute('#dbg_state', 'data-state')}, want paused")
    # It boots on from the reset vector.
    if run_to("after Restore") and name == "hello":
        machine_do(page, "click", "#dbg_continue")
        if not wait_for(lambda: ALIVE in page.inner_text("#screen_text"), 60):
            problems.append(f"{tag}: after Restore and Continue the screen never showed {ALIVE!r}: "
                            f"{page.inner_text('#screen_text')[:200]!r}")
    r["status"] = page.inner_text("#dbg_status")[:160]
    page.screenshot(path=str(shots / f"snapdiff-poweron-{name}-{w}-{scheme}-booted.png"), full_page=True)
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")
    if errors:
        problems.append(f"{tag}: page errors {errors[:3]}")
    ctx.close()
    return problems, r


# D1: a snapshot is a point on the virtual-clock timeline. Restoring it
# and stepping on must give the same machine as stepping on from it the
# first time: v86's devices keep absolute timestamps (the PIT's counter
# start, the RTC's last update) and restore_state sets the TSC offset from
# the clock it reads at that moment, so the page puts its virtual clock and
# the stepper's count back to the snapshot's before restore_state. Before
# that, the clock ran on from where the machine was when Restore was
# pressed, so the timer interrupts and every TSC/PIT read after a restore
# were shifted by the time stepped in between.
SNAP_REPLAY = {"hello": (600_000, 600_000), "minified-linux": (1_500_000, 500_000)}
SNAP_REPLAY_STATE_JS = """async () => { const c = MKB.emulator.v86.cpu, m = MKB.emulator.read_memory(0, MKB.emuRam);
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  return { steps: MKB.stepper.steps, eip: c.instruction_pointer[0] >>> 0, regs: Array.from(c.reg32, (x) => x >>> 0), eflags: c.get_eflags() >>> 0,
    clock: MKB.CLOCK.now(), irq0: MKB.stepper.irq0, ram: sha }; }"""


def snapdiff_replay(browser, base, shots, name, scheme, phone, point=None):
    """Save at S on the virtual clock, step N, note the machine; Restore,
    step N again: the same EIP, registers, RAM, IRQ0 count (from S) and clock."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    at, n = point or SNAP_REPLAY[name]
    tag = f"replay {name} {w} {scheme}" + (f" at {at:,}" if point else "")
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1&mips=10#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
    if snap_step_to(page, at) != at:
        problems.append(f"{tag}: could not step to {at} (test setup): {page.inner_text('#dbg_status')[:160]}")
    st = snap_save(page, 1, phone)
    row = st["rows"][0]["id"]
    s0 = page.evaluate(SNAP_REPLAY_STATE_JS)
    snap_step_to(page, at + n)
    first = page.evaluate(SNAP_REPLAY_STATE_JS)
    # Some more time passes before the Restore (as when someone reads the screen).
    snap_step_to(page, at + n + 777)
    show_tab(page, "snapshots")
    snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
             "failed" in page.inner_text("#snap_status"), 60)
    if page.get_attribute("#snap_status", "data-restored") != row:
        problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
    s1 = page.evaluate(SNAP_REPLAY_STATE_JS)
    for k in ("steps", "clock"):
        if s1[k] != s0[k]:
            problems.append(f"{tag}: right after Restore {k} is {s1[k]}, at the save it was {s0[k]}")
    snap_step_to(page, s1["steps"] + n)
    again = page.evaluate(SNAP_REPLAY_STATE_JS)
    r["first"] = {k: first[k] for k in ("steps", "eip", "irq0", "clock")} | {"ram": first["ram"][:16]}
    r["again"] = {k: again[k] for k in ("steps", "eip", "irq0", "clock")} | {"ram": again["ram"][:16]}
    first["irq0"] -= s0["irq0"]  # the page's IRQ0 count is not part of the state
    again["irq0"] -= s1["irq0"]
    for k in ("eip", "regs", "eflags", "irq0", "clock", "ram"):
        if first[k] != again[k]:
            problems.append(f"{tag}: {k} after Restore + {n} steps differs from the first time: {str(again[k])[:80]} vs {str(first[k])[:80]}")
    page.screenshot(path=str(shots / f"snapdiff-replay-{name}-{w}-{scheme}{'-' + str(at) if point else ''}.png"), full_page=True)
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")
    if errors:
        problems.append(f"{tag}: page errors {errors[:3]}")
    ctx.close()
    return problems, r


# ---- one count: every place names the Machine tab's count (X2) -------------
# The Machine tab, the counters table, the run-badge menu and the Reverse
# line count the stepper's instructions; v86's own instruction_counter runs
# 86 (hello) or 87 (minified Linux) ahead of it. Snapshot names and rows, the
# diff summary and the "This position" bookmark showed v86's number until X2.
ONECOUNT_JS = """() => { const now = document.querySelector('#counters tr[data-milestone="now"]');
  return { now: now ? Number(now.dataset.steps) : null, text: document.getElementById('count_text').textContent,
    badge: Number(document.getElementById('runbadge').dataset.steps || -1), steps: MKB.stepper.steps,
    icount: MKB.emulator.v86.cpu.instruction_counter[0] >>> 0 }; }"""


def onecount_run(browser, base, shots, name, scheme, phone):
    """Pause, read the Machine tab's count N, save a snapshot and a "This
    position" bookmark: each name, row and the diff summary say N."""
    problems, r, errors = [], {}, []
    fn, steps, _ = BOOKMARKS_POINTS[name]
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]}
    w = 390 if phone else 1280
    tag = f"{name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", hex(syms[fn]["vaddr"]))
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {syms[fn]["paddr"]}, 240)
    if not ok:
        ctx.close()
        return [f"{tag}: Run to {fn} stopped at {at:#x}: {status}"], r
    page.fill("#dbg_n", str(steps))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             page.evaluate("MKB.stepper.steps") > 0, 120)
    time.sleep(1)
    show_tab(page, "machine")
    time.sleep(0.5)
    m = page.evaluate(ONECOUNT_JS)
    n = m["now"]
    r["machine"] = m
    fmt = f"{n:,}" if n is not None else "?"
    if n is None or n != m["steps"] or n != m["badge"] or fmt not in m["text"]:
        problems.append(f"{tag}: the Machine tab does not show one count: counters {n}, run badge {m['badge']}, "
                        f"line {m['text'][:120]!r}, stepper {m['steps']}")
        ctx.close()
        return problems, r
    if m["icount"] == n:
        problems.append(f"{tag}: v86's counter equals the stepper's ({n}): the case no longer tells the counts apart")
    show_tab(page, "machine")
    shot = lambda s: page.screenshot(path=str(shots / f"onecount-{name}-{w}-{scheme}-{s}.png"), full_page=False)
    shot("machine")

    # The snapshot: default name, its row and the diff summary against now.
    st = snap_save(page, 1, phone)
    row = (st["rows"] or [{}])[0]
    r["snapshot"] = [row.get("name"), row.get("text", "")[:160]]
    if "@" + fmt + " " not in row.get("name", ""):
        problems.append(f"{tag}: snapshot default name {row.get('name')!r} lacks the Machine tab's count @{fmt}")
    if "instruction " + fmt + "," not in row.get("text", ""):
        problems.append(f"{tag}: snapshot row {row.get('text', '')[:160]!r} lacks the Machine tab's count {fmt}")
    page.select_option("#snap_a", row["id"])
    page.select_option("#snap_b", "now")
    snap_click(page, "#snap_diff", phone)
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"{tag}: the diff did not finish")
    else:
        summ = page.evaluate(SNAP_DIFF_JS)["summary"]
        r["diff"] = summ[:200]
        if "instruction " + fmt + " → " + fmt + " (" not in summ:
            problems.append(f"{tag}: the diff summary {summ[:160]!r} does not say instruction {fmt} → {fmt}")
    shot("snapshots")

    # The "This position" bookmark: default name, the list row (and its snapshot).
    bm_open_list(page, phone)
    bm_tap(page, "#bm_here", phone)
    b = bm_dialog_save(page, problems, tag, "pos", want_default="@" + fmt + " ")
    if b:
        r["bookmark"] = [b["name"], b["t"]]
        st = bm_state(page)
        bm_open_list(page, phone)
        st = bm_state(page)
        what = next((x["what"] for x in st["rows"] if x["id"] == b["id"]), "")
        r["bookmark row"] = what
        if "after " + fmt + " instructions" not in what:
            problems.append(f"{tag}: the bookmark row {what!r} does not say after {fmt} instructions")
        if b["t"].get("snapName") != row.get("name"):
            problems.append(f"{tag}: the bookmark is not tied to the snapshot of the same count: {b['t'].get('snapName')!r}")
        shot("bookmark")
    bm_close_menu(page)
    for x in (r.get("snapshot", [""])[0] or "", r.get("diff", ""), (r.get("bookmark") or [""])[0], r.get("bookmark row", "")):
        if f"{m['icount']:,}" in x:
            problems.append(f"{tag}: {x[:120]!r} shows v86's counter {m['icount']:,}")
    # A position bookmark stored before X2 (no steps, only v86's icount) still
    # lists after a reload, with the count it was saved with.
    if b:
        page.evaluate("""(id) => { const j = JSON.parse(localStorage.getItem('mkb-bookmarks'));
          const o = JSON.parse(JSON.stringify(j.bookmarks.find((x) => x.id === id)));
          delete o.t.steps; o.id = id + 'old'; o.name = 'pre-X2 position'; j.bookmarks.push(o);
          localStorage.setItem('mkb-bookmarks', JSON.stringify(j)); }""", b["id"])
        page.reload()
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: len(bm_state(page)["rows"]) == 2, 20)
        old = next((x for x in bm_state(page)["rows"] if x["name"] == "pre-X2 position"), None)
        r["pre-X2 bookmark"] = old and old["what"]
        if not old or "after " + f"{m['icount']:,}" + " instructions" not in old["what"]:
            problems.append(f"{tag}: a pre-X2 position bookmark lists as {old}")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r
