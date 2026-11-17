"""Browser test suite deltasnap (test_browser.py --suites)."""

import base64
import re
import time

from tests.browser.common import (
    PHONE, report, run_to_wait, show_tab, snap_click, snap_save, snap_wait_rows, snapdiff_guard, wait_for,
)
from tests.browser.snapdiff_run import (
    snap_check_download, SNAP_DIFF_JS, SNAP_FILL_JS, SNAP_JS, SNAP_LONG_JS, snap_step_to,
)


# ---- deltasnap: delta snapshots (R1) ---------------------------------------
# A snapshot after the first one of a kernel stores only the 4 KiB pages
# whose hash changed since the snapshot before it (its base) plus v86's
# state with no RAM; Restore puts the base's RAM back and the changed pages
# on top. Checked against v86's own RAM and registers at every save, after
# a reload, after deleting snapshots in the middle and at the root of the
# chain, through the diff (which reads a delta's page list) and the
# download (a full v86 state that `lz4 -d` reads).
DELTA_STEPS = {"hello": 15, "minified-linux": 2500}

DELTA_JS = """() => { if (window.mkbDelta) return; window.mkbDelta = {
  shas: {},
  async sha(k) { const d = await crypto.subtle.digest('SHA-256', MKB.emulator.v86.cpu.mem8.slice(0, MKB.emuRam));
    return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join(''); } }; }"""

DELTA_ROWS_JS = """() => [...document.querySelectorAll('#snapshots tbody tr[data-id]')].map((tr) => ({
  id: tr.dataset.id, base: tr.dataset.base || '', depth: Number(tr.dataset.depth || 0), pages: Number(tr.dataset.pages || -1),
  stored: Number(tr.dataset.stored || 0), raw: Number(tr.dataset.raw || 0), full: Number(tr.dataset.full || 0),
  where: tr.dataset.where || '', restore: !!tr.querySelector('button.restore'), text: tr.innerText.replace(/\\s+/g, ' ') }))"""


def delta_rows(page):
    return {x["id"]: x for x in page.evaluate(DELTA_ROWS_JS)}


def delta_restore(page, rid, phone):
    show_tab(page, "snapshots")
    page.evaluate("document.getElementById('snap_status').removeAttribute('data-restored')")
    snap_click(page, f"#snapshots tr[data-id='{rid}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == rid or
             "failed" in page.inner_text("#snap_status"), 60)
    return page.get_attribute("#snap_status", "data-restored") == rid


def delta_check_restore(page, tag, rid, want, phone):
    """Restore rid: RAM SHA-256, registers and instruction count as v86 had them at the save."""
    if not delta_restore(page, rid, phone):
        return [f"{tag}: Restore {rid}: {page.inner_text('#snap_status')[:200]}"]
    got_sha = page.evaluate("mkbDelta.sha()")
    cpu = page.evaluate("mkbSnap.cpu()")
    out = []
    if got_sha != want["sha"]:
        out.append(f"{tag}: after restoring {rid} the RAM SHA-256 is {got_sha[:16]}, at the save it was {want['sha'][:16]}")
    if cpu["regs"] != want["cpu"]["regs"] or cpu["icount"] != want["cpu"]["icount"]:
        diff = {k: (hex(cpu['regs'][k]), hex(want['cpu']['regs'][k])) for k in cpu["regs"] if cpu["regs"][k] != want["cpu"]["regs"].get(k)}
        out.append(f"{tag}: after restoring {rid}: registers differ {diff}, instructions {cpu['icount']} (want {want['cpu']['icount']})")
    return out


def deltasnap_chain(browser, base, shots, name, scheme, phone):
    """Save at the kernel entry (full), then step and save 10 more: each is
    a delta of the one before with exactly the pages v86's RAM changed in;
    smaller than the full one; the storage line counts the deltas. After a
    reload every one restores to v86's RAM and registers at its save. The
    diff of a delta and its base compares only the delta's pages and finds
    the same pages as v86's RAM; a diff across the chain too. Deleting a
    snapshot in the middle folds its pages into the next one; deleting the
    root makes the next one full; the rest still restore exactly, also
    after a reload. The download of a delta is a full v86 state."""
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"delta {name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, accept_downloads=True,
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel={name}&paused=1#debugger"

    def start():
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
        page.evaluate(SNAP_JS)
        page.evaluate(DELTA_JS)
        return ok

    def shot(s):
        page.screenshot(path=str(shots / f"deltasnap-{name}-{w}-{scheme}-{s}.png"), full_page=True)

    if not start():
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        problems.append(f"{tag}: Run to the kernel entry stopped at {at:#x}: {status}")
    want, ids, step_pages = [], [], []
    for i in range(11):
        if i:
            snap_step_to(page, page.evaluate("MKB.stepper.steps") + DELTA_STEPS[name] * (1 + i % 3))
            page.evaluate("mkbSnap.keep('B')")
            step_pages.append(page.evaluate("mkbSnap.pages('A', 'B')"))
            page.evaluate("mkbSnap.A = mkbSnap.B")
        else:
            page.evaluate("mkbSnap.keep('A')")
            page.evaluate("mkbSnap.keep('first')")
        want.append({"sha": page.evaluate("mkbDelta.sha()"), "cpu": page.evaluate("mkbSnap.cpu()")})
        st = snap_save(page, i + 1, phone)
        ids.append(st["rows"][-1]["id"])
    page.evaluate("mkbSnap.keep('last')")
    # Read before the reload drops the page's copies.
    pages_0_10 = page.evaluate("mkbSnap.pages('first', 'last')")
    last_ram = base64.b64decode(page.evaluate("mkbSnap.b64('last')"))
    rows = delta_rows(page)
    r["rows"] = [(rows[x]["base"][-4:], rows[x]["depth"], rows[x]["pages"], rows[x]["stored"]) for x in ids]
    r["pages per step"] = [len(p) for p in step_pages]
    first = rows.get(ids[0], {})
    if first.get("base") or first.get("depth"):
        problems.append(f"{tag}: the first snapshot is a delta of {first.get('base')!r} (depth {first.get('depth')})")
    for i in range(1, 11):
        x = rows.get(ids[i], {})
        if x.get("base") != ids[i - 1] or x.get("depth") != i:
            problems.append(f"{tag}: snapshot {i} has base {x.get('base')!r} depth {x.get('depth')} (want {ids[i - 1]}, {i})")
        if x.get("pages") != len(step_pages[i - 1]):
            problems.append(f"{tag}: snapshot {i} stores {x.get('pages')} pages, {len(step_pages[i - 1])} changed in v86's RAM")
        if not x.get("stored") or x["stored"] >= first.get("stored", 0):
            problems.append(f"{tag}: delta {i} is {x.get('stored')} bytes, the full snapshot {first.get('stored')}")
        if not re.search(r"delta of #\d+.*\b" + str(x.get("pages")) + r" pages?", x.get("text", "")):
            problems.append(f"{tag}: row {i} does not say it is a delta and how many pages: {x.get('text', '')[:200]!r}")
        if x.get("full", 0) <= x.get("raw", 0):
            problems.append(f"{tag}: row {i} gives the full state as {x.get('full')} bytes, its own state {x.get('raw')}")
    store = page.inner_text("#snap_store")
    total = sum(x["stored"] for x in rows.values())
    r["store"] = store[:200]
    if not re.search(r"\b10 deltas?\b", store):
        problems.append(f"{tag}: the storage line does not count the 10 deltas: {store[:200]!r}")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll on the Snapshots tab")
    shot("chain")

    # Reload, then restore out of order: every one is v86's state at its save.
    if not start():
        problems.append(f"{tag}: the reload did not start paused")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 11)
    wait_for(lambda: all(x["restore"] for x in delta_rows(page).values()), 30)
    for i in (10, 4, 0, 7, 1):
        problems += delta_check_restore(page, tag, ids[i], want[i], phone)
    r["restore status"] = page.inner_text("#snap_status")[:160]

    # Diff of a delta and its base: only its pages compared; same pages as v86.
    for a, b in ((3, 4), (0, 10)):
        page.select_option("#snap_a", ids[a])
        page.select_option("#snap_b", ids[b])
        snap_click(page, "#snap_diff", phone)
        if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
            problems.append(f"{tag}: the diff {a} -> {b} did not finish")
            continue
        d = page.evaluate(SNAP_DIFF_JS)
        wp = step_pages[a] if b == a + 1 else pages_0_10
        if sorted(map(tuple, d["pages"])) != sorted(map(tuple, wp)):
            problems.append(f"{tag}: diff {a} -> {b} lists {len(d['pages'])} pages, v86's RAM changed in {len(wp)}")
        compared = page.get_attribute("#snap_diff_out", "data-compared")
        st = page.inner_text("#snap_diff_status")
        r[f"diff {a}->{b}"] = [len(d["pages"]), compared, st[:120]]
        if b == a + 1 and (compared != str(len(step_pages[a])) or "delta" not in st):
            problems.append(f"{tag}: the diff of delta {b} and its base compared {compared!r} pages "
                            f"(want the delta's {len(step_pages[a])}): {st[:160]!r}")
    shot("diff")

    # The download of a delta is a full v86 state.
    if not phone:
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{ids[10]}'] button.snapdl").click()
        fname = dl.value.suggested_filename
        if not fname.endswith(".v86state.lz4"):
            problems.append(f"{tag}: the delta's download is named {fname!r}")
        bad, r["download"] = snap_check_download(dl.value.path(), last_ram, "lz4")
        problems += [f"{tag}: download of delta 10: {b}" for b in bad]

    # Delete 5 (middle): 6 takes 5's pages; delete 0 (root): 1 becomes full.
    for gone, nxt, newbase in ((5, 6, 4), (0, 1, None)):
        snap_click(page, f"#snapshots tr[data-id='{ids[gone]}'] button.snapdel", phone)
        wait_for(lambda: ids[gone] not in delta_rows(page), 60)
        wait_for(lambda: delta_rows(page).get(ids[nxt], {}).get("base", "?") == (ids[newbase] if newbase is not None else ""), 60)
        x = delta_rows(page).get(ids[nxt], {})
        if x.get("base", "?") != (ids[newbase] if newbase is not None else ""):
            problems.append(f"{tag}: after deleting {gone}, snapshot {nxt} has base {x.get('base')!r}")
        if newbase is not None:
            union = {p for p, _ in step_pages[gone - 1]} | {p for p, _ in step_pages[nxt - 1]}
            if x.get("pages") != len(union):
                problems.append(f"{tag}: after deleting {gone}, snapshot {nxt} stores {x.get('pages')} pages, "
                                f"{len(union)} changed since {newbase}")
    r["after deletes"] = page.inner_text("#snap_status")[:160]
    if not start():
        problems.append(f"{tag}: the second reload did not start paused")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 9)
    wait_for(lambda: all(x["restore"] for x in delta_rows(page).values()), 30)
    rows = delta_rows(page)
    r["rows after deletes"] = [(rows[x]["base"][-4:], rows[x]["depth"], rows[x]["pages"]) for x in ids if x in rows]
    for i in (6, 10, 1, 3):
        problems += delta_check_restore(page, tag + " after deletes", ids[i], want[i], phone)
    # A save right after restoring 3 is a delta of 3 with no page changed.
    snap_save(page, 10, phone)
    rows = delta_rows(page)
    new = [x for x in rows.values() if x["id"] not in ids]
    if not new or new[0]["base"] != ids[3] or new[0]["pages"] != 0:
        problems.append(f"{tag}: a save right after restoring 3: {new[:1]}")
    shot("deleted")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll after the deletes")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


def deltasnap_big(browser, base, shots):
    """64 MiB machine with 24 MiB of data: the second snapshot, a few
    pages later, is a delta under 1% of the full one; saving it makes no
    main-thread task over 200 ms; Restore of the delta gives the RAM back."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&mem={64 << 20}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.evaluate(DELTA_JS)
    page.fill("#dbg_addr", "hello_main")
    page.click("#dbg_runto")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             "hello_main" in page.inner_text("#dbg_status"), 120)
    page.evaluate(SNAP_FILL_JS, 24)
    st = snap_save(page, 1, False)
    full_id = st["rows"][0]["id"]
    # 37 pages of the filled area change, one goes back to zero.
    page.evaluate("(() => { const m = MKB.emulator.v86.cpu.mem8; for (let p = 0; p < 36; p++) m[(9 << 20) + p * 8192 + 5] ^= 0xa5;"
                  " m.fill(0, 12 << 20, (12 << 20) + 4096); })()")
    want = page.evaluate("mkbDelta.sha()")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    st = snap_save(page, 2, False)
    r["save seconds"] = round(time.monotonic() - t0, 2)
    time.sleep(1)
    long = page.evaluate("window.mkbLong")
    r["long tasks ms (save)"] = sorted(long)[-5:]
    if long and max(long) > 200:
        problems.append(f"saving the delta: a {max(long)} ms task on the main thread (want <= 200)")
    rows = delta_rows(page)
    d_id = next(x for x in rows if x != full_id)
    full, d = rows[full_id], rows[d_id]
    r["full stored"], r["delta stored"], r["delta pages"] = full["stored"], d["stored"], d["pages"]
    r["status"] = page.inner_text("#snap_status")[:200]
    if d["base"] != full_id or d["pages"] != 37:
        problems.append(f"the 64 MiB delta has base {d['base']!r} and {d['pages']} pages (want {full_id}, 37)")
    if not d["stored"] or d["stored"] * 100 >= full["stored"]:
        problems.append(f"the 64 MiB delta is {d['stored']} bytes, the full snapshot {full['stored']} (want under 1%)")
    page.evaluate("MKB.emulator.v86.cpu.mem8.fill(0x55, 8 << 20, 9 << 20)")
    page.evaluate(SNAP_LONG_JS)
    t0 = time.monotonic()
    if not delta_restore(page, d_id, False):
        problems.append(f"restoring the 64 MiB delta: {page.inner_text('#snap_status')[:200]}")
    r["restore seconds"] = round(time.monotonic() - t0, 2)
    got = page.evaluate("mkbDelta.sha()")
    if got != want:
        problems.append(f"after restoring the 64 MiB delta the RAM SHA-256 is {got[:16]}, want {want[:16]}")
    page.screenshot(path=str(shots / "deltasnap-big-1280.png"), full_page=True)
    if errors:
        problems.append(f"big delta: page errors {errors[:3]}")
    ctx.close()
    return problems, r


def deltasnap_case(browser, base, shots, linux):
    out = []
    runs = [("hello", "dark", True)] + ([("minified-linux", "light", False)] if linux else [("hello", "light", False)])
    for name, scheme, phone in runs:
        t0 = time.monotonic()
        problems, r = snapdiff_guard(deltasnap_chain, browser, base, shots, name, scheme, phone)
        out.append(report("deltasnap chain", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    problems, r = snapdiff_guard(deltasnap_big, browser, base, shots)
    out.append(report("deltasnap 64 MiB", problems, r))
    return any(out)


def suite_deltasnap(ctx):
    return deltasnap_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)
