"""Part of a browser suite (test_browser.py)."""

import base64
import pathlib
import struct
import time

from tests.browser.common import show_tab, shown_tab, snap_click, snap_rows, snap_save, snap_wait_rows, wait_for
from tests.browser.snapdiff_run import SNAP_DIFF_JS, SNAP_JS, snap_parse_state, SNAP_ROW_CODEC_JS, SNAPDIFF_STEPS


# Records put into the snapshot store beside the page's own last one: a
# snapshot of a kernel that is not in the list, one of a listed kernel at a
# RAM size the menu does not offer, and a delta whose base is gone.
SNAP_INJECT_JS = """async (recs) => {
  const s = MKB.snapshots[MKB.snapshots.length - 1];
  for (const x of recs) {
    const rec = { ...MKB.snapRecord(s), id: s.id + x.tag, seq: s.seq + x.n, time: s.time + x.n, name: x.name,
      kernel: x.kernel || s.kernel, kernelKey: x.key || s.kernelKey, ram: x.ram || s.ram, delta: x.delta || null };
    await MKB.snapTx('readwrite', (st) => st.put(rec));
  }
  return s.id; }"""


def lz4_xxh32_short(b):
    """xxh32 (seed 0) of fewer than 16 bytes (github.com/Cyan4973/xxHash,
    doc/xxhash_spec.md): the LZ4 frame descriptor's checksum."""
    p1, p2, p3, p4, p5, m = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1, 0xffffffff
    rotl = lambda x, r: ((x << r) | (x >> (32 - r))) & m
    h, i = (p5 + len(b)) & m, 0
    while i + 4 <= len(b):
        h = rotl((h + struct.unpack_from("<I", b, i)[0] * p3) & m, 17) * p4 & m
        i += 4
    for c in b[i:]:
        h = rotl((h + c * p5) & m, 11) * p1 & m
    h ^= h >> 15
    h = h * p2 & m
    h ^= h >> 13
    h = h * p3 & m
    return h ^ (h >> 16)


def lz4_frame(blocks, flg=0x68, bd=0x70, size=0, hc=None):
    """An LZ4 frame (doc/lz4_Frame_format.md) of blocks [(bytes, stored)],
    its descriptor's checksum computed here unless hc is given."""
    desc = bytes([flg, bd]) + (struct.pack("<Q", size) if flg & 8 else b"")
    out = struct.pack("<I", 0x184D2204) + desc + bytes([(lz4_xxh32_short(desc) >> 8) & 0xff if hc is None else hc])
    for data, stored in blocks:
        out += struct.pack("<I", len(data) | (0x80000000 if stored else 0)) + data
    return out + bytes(4)


# Records written into the snapshot store beside those the page saved: a
# copy of snapshot #1 or #2 (from) with other bytes (b64), codec, size or
# delta base (delta: null makes it a full one).
SNAP_CORRUPT_JS = """async (recs) => {
  const by = Object.fromEntries(MKB.snapshots.map((s) => [s.seq, s]));
  for (const x of recs) {
    const t = by[x.from || 1];
    const data = x.b64 ? new Blob([Uint8Array.from(atob(x.b64), (c) => c.charCodeAt(0))]) : t.data;
    const rec = { ...MKB.snapRecord(t), id: x.id, seq: 100 + x.n, time: t.time + x.n, name: x.name, data, size: data.size,
      codec: x.codec || t.codec, raw: x.size === undefined ? t.raw : x.size, delta: x.delta === undefined ? t.delta : x.delta };
    await MKB.snapTx('readwrite', (st) => st.put(rec));
  }
  return MKB.snapshots.map((s) => ({ seq: s.seq, id: s.id, raw: s.raw, codec: s.codec, delta: s.delta })); }"""
SNAP_MACHINE_JS = """async () => { const c = MKB.emulator.v86.cpu;
  const d = await crypto.subtle.digest('SHA-256', c.mem8.slice(0, MKB.emuRam));
  return { ram: Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, '0')).join(''), icount: c.instruction_counter[0] >>> 0,
    eip: c.instruction_pointer[0] >>> 0, regs: Array.from(c.reg32, (x) => x >>> 0) }; }"""


def snapdiff_corrupt(browser, base, shots):
    """Stored snapshots whose bytes are not what their record says (a disk
    or another build wrote them; written here into IndexedDB beside two
    the page saved, a full one and a delta of it, then read on a reload):
    Restore of each says what is wrong with it, in the LZ4 frame (no
    frame, flags this page does not write, a header checksum, a block that
    does not decode, a block of another size, a frame shorter than the
    record, one whose content size its block does not decode to), in
    v86's state (another magic, another version) or in the
    chain of deltas (a delta of a "full" one that is itself a delta, one of
    another base), and leaves the machine as it was; the diff of the
    "full" delta and Delete of a base whose delta cannot be rebuilt say so
    too, and keep it. A name confirmed unchanged is no rename. A diff
    with a snapshot of a smaller RAM lists the pages above its end that
    hold bytes, the hexdump of one shows zeros against v86's bytes, all
    256 rows on "all rows", and the Page tab shows it as the snapshot had
    it (zeros, named, renamed). After the site's data is cleared (the page's database
    connection is closed under it) Delete, Rename and Save each say that
    IndexedDB failed, and the snapshot stays in memory. Without the
    snapshot worker's script (404) a save is kept raw and Restore says the
    worker failed."""
    problems, r, errors = [], {}, []
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def start(url):
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
        return ok

    url = f"{base}/index.html?kernel=hello&paused=1#snapshots"
    try:
        if not start(url):
            return ["corrupt: hello did not start paused"], r
        snap_save(page, 1, False)
        show_tab(page, "debugger")
        page.fill("#dbg_n", str(SNAPDIFF_STEPS["hello"]))
        page.click("#dbg_stepn")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
                 page.evaluate("MKB.stepper.steps") >= SNAPDIFF_STEPS["hello"], 60)
        st = snap_save(page, 2, False)
        a_id, b_id = st["rows"][0]["id"], st["rows"][1]["id"]
        saved = page.evaluate("MKB.snapshots.map((s) => ({ id: s.id, raw: s.raw, codec: s.codec, base: s.delta && s.delta.base }))")
        if [x["codec"] for x in saved] != ["lz4", "lz4"] or saved[1]["base"] != a_id:
            return [f"corrupt: two saves are not a full LZ4 snapshot and a delta of it (test setup): {saved}"], r
        b64 = lambda b: base64.b64encode(b).decode()
        stored100 = bytes(range(100))
        aaaa = bytes([0x1A, 0x41, 0x01, 0x00, 0x50]) + b"A" * 5
        v86_v5 = struct.pack("<IiII", 0x86768676, 5, 64, 0) + bytes(48)
        # tag: (record, the reason Restore gives or None for no Restore)
        cases = {
            "nolz4": ({"b64": b64(b"this is not an LZ4 frame"), "size": 1000}, "not an LZ4 frame"),
            "flags": ({"b64": b64(lz4_frame([(stored100, True)], flg=0x48, size=100)), "size": 100},
                      "LZ4 frame with FLG 48: only version 01, independent blocks, no dictionary are read"),
            "checksum": ({"b64": b64(lz4_frame([(stored100, True)], size=100, hc=(((lz4_xxh32_short(bytes([0x68, 0x70]) +
                          struct.pack("<Q", 100)) >> 8) + 1) & 0xff))), "size": 100}, "LZ4 frame header checksum mismatch"),
            # A literal 'A', then a match 65,535 bytes back: before the output's start.
            "block": ({"b64": b64(lz4_frame([(bytes([0x10, 0x41, 0xff, 0xff]), False)], size=100)), "size": 100},
                      "LZ4 block: the offset to copy is not contained in the decompressed buffer"),
            # Twenty 'A's: a literal, a match one byte back, five literals.
            "aaaa": ({"b64": b64(lz4_frame([(aaaa, False)], size=20)), "size": 20}, "not a v86 state (magic 41414141)"),
            "size": ({"b64": b64(lz4_frame([(aaaa, False)], size=21)), "size": 21},
                     "LZ4 block: the expected decompressed size differs, actual 20, expected 21"),
            "short": ({"b64": b64(lz4_frame([(stored100, True)], size=200)), "size": 200}, "LZ4 frame holds 100 bytes, 200 expected"),
            "magic": ({"b64": b64(stored100), "codec": "raw", "size": 100},
                      f"not a v86 state (magic {struct.unpack_from('<I', stored100)[0]:x})"),
            "version": ({"b64": b64(v86_v5), "codec": "raw", "size": len(v86_v5)}, "v86 state version 5, this page reads version 6"),
            # #2 (a delta) recorded as a full snapshot, and a delta of it.
            "fakefull": ({"from": 2, "delta": None}, None),
            "ofdelta": ({"from": 2, "delta": {"base": a_id + "fakefull", "pages": 1}},
                        "the snapshot this delta was taken against is missing"),
            # #1 again under another id, and #2 recorded as a delta of that copy.
            "copy": ({"delta": None}, None),
            "otherbase": ({"from": 2, "delta": {"base": a_id + "copy", "pages": 1}},
                          f"snapshot {a_id}otherbase is not a delta of {a_id}copy"),
        }
        recs = [{**rec, "id": a_id + tag, "n": n, "name": "corrupt " + tag} for n, (tag, (rec, _)) in enumerate(cases.items())]
        page.evaluate(SNAP_CORRUPT_JS, recs)
        start(url)
        show_tab(page, "snapshots")
        snap_wait_rows(page, 2 + len(cases))
        wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
        page.evaluate(SNAP_JS)
        before = page.evaluate(SNAP_MACHINE_JS)
        r["restore"] = {}
        for tag, (_, why) in cases.items():
            if why is None:
                continue
            page.evaluate("document.getElementById('snap_status').textContent = ''")
            snap_click(page, f"#snapshots tr[data-id='{a_id}{tag}'] button.restore", False)
            wait_for(lambda: "failed" in page.text_content("#snap_status") or
                     page.get_attribute("#snap_status", "data-restored") == a_id + tag, 30)
            s = page.text_content("#snap_status")
            after = page.evaluate(SNAP_MACHINE_JS)
            r["restore"][tag] = s[:150]
            if s != f"restore_state failed: Error: {why}":
                problems.append(f"Restore of “corrupt {tag}”: {s!r}, want 'restore_state failed: Error: {why}'")
            if after != before:
                problems.append(f"Restore of “corrupt {tag}” failed but the machine changed: "
                                f"{ {k: (str(before[k])[:40], str(after[k])[:40]) for k in before if before[k] != after[k]} }")
        page.screenshot(path=str(shots / "snapdiff-corrupt-1280-light.png"), full_page=True)

        # The diff of the "full" one that is a delta.
        page.select_option("#snap_a", a_id + "fakefull")
        page.select_option("#snap_b", "now")
        page.click("#snap_diff")
        wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") in ("done", "error"), 30)
        d = page.evaluate(SNAP_DIFF_JS) or {}
        s = page.text_content("#snap_diff_status")
        r["diff fakefull"] = [d.get("state"), s[:120]]
        if d.get("state") != "error" or s != "Diff failed: a delta snapshot read without its base":
            problems.append(f"diff of a delta recorded as full: state {d.get('state')!r}, {s!r}")

        # Delete of a base whose delta cannot be rebuilt without it: kept.
        snap_click(page, f"#snapshots tr[data-id='{a_id}copy'] button.snapdel", False)
        wait_for(lambda: page.text_content("#snap_status").startswith("Not deleted"), 30)
        s = page.text_content("#snap_status")
        ids = [x["id"] for x in snap_rows(page)["rows"]]
        r["delete base"] = [s[:200], a_id + "copy" in ids]
        if s != f"Not deleted: “corrupt copy” is the base of a delta that could not be rebuilt (snapshot {a_id}otherbase " \
                f"is not a delta of {a_id}copy)." or a_id + "copy" not in ids:
            problems.append(f"Delete of a base whose delta cannot be rebuilt: {s!r}, still listed: {a_id + 'copy' in ids}")

        # A name confirmed as it was: no rename, no store.
        name0 = snap_rows(page)["rows"][0]["name"]
        page.evaluate("document.getElementById('snap_status').textContent = 'mark'")
        page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
        page.press("#snapshots input.snapedit", "Enter")
        wait_for(lambda: not page.is_visible("#snapshots input.snapedit"), 10)
        r["same name"] = [snap_rows(page)["rows"][0]["name"] == name0, page.text_content("#snap_status")]
        if r["same name"] != [True, "mark"]:
            problems.append(f"a name confirmed unchanged: same name {r['same name'][0]}, status {r['same name'][1]!r} (want no 'Renamed.')")

        # A diff with a snapshot of 2 MiB on a 4 MiB machine at hello's end:
        # the pages above 2 MiB that hold bytes, each with how many.
        ram_a = page.evaluate("MKB.emuRam")
        page.goto(f"{base}/index.html?kernel=hello&mem={2 * ram_a}#snapshots")
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"hello at {2 * ram_a} bytes of RAM did not end")
        else:
            show_tab(page, "snapshots")
            wait_for(lambda: len(snap_rows(page)["rows"]) >= 2 and all(x["kernel"] for x in snap_rows(page)["rows"]), 30)
            page.select_option("#snap_a", a_id)
            page.select_option("#snap_b", "now")
            page.click("#snap_diff")
            wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") in ("done", "error"), 30)
            d = page.evaluate(SNAP_DIFF_JS) or {}
            want = page.evaluate(f"""(() => {{ const m = MKB.emulator.read_memory(0, MKB.emuRam), out = [];
                for (let p = {ram_a} / 4096; p < MKB.emuRam / 4096; p++) {{ let c = 0;
                  for (let i = p * 4096; i < p * 4096 + 4096; i++) c += m[i] !== 0;
                  if (c) out.push([p, c]); }}
                return out; }})()""")
            got = [x for x in d.get("pages", []) if x[0] >= ram_a // 4096]
            r["diff 2 MiB vs 4 MiB"] = [d.get("state"), len(got), got[:3]]
            if d.get("state") != "done" or not want or got != want:
                problems.append(f"diff of a {ram_a}-byte snapshot with a {2 * ram_a}-byte machine: state {d.get('state')}, "
                                f"pages above {ram_a:#x} {got[:4]} ({len(got)}), RAM has {want[:4]} ({len(want)})")
            # The first of those pages byte by byte: zeros on the 2 MiB side,
            # v86's bytes now; "all rows" shows all 256 rows of 16.
            if got:
                p0 = got[0][0]
                page.click(f"#snap_pages [data-page='{p0}'] button.snaphex")
                wait_for(lambda: page.get_attribute("#snap_hex", "data-page") == str(p0), 15)
                hexrows = "() => [...document.querySelectorAll('#snap_hex pre')].map((e) => e.textContent.split('\\n').filter((l) => l))"
                rows = page.evaluate(hexrows)
                page.click("#snap_hex button:text('all rows')")
                wait_for(lambda: len(page.evaluate(hexrows)[0]) == 256, 10)
                allrows = page.evaluate(hexrows)
                now = page.evaluate(f"Array.from(MKB.emulator.read_memory({p0} * 4096, 4096))")
                parse = lambda lines: {int(l[:8], 16): [int(x, 16) for x in l[9:].split()[:16]] for l in lines}
                a, b = parse(allrows[0]), parse(allrows[1]) if len(allrows) == 2 else {}
                base_p = p0 * 4096
                want_b = {base_p + o: now[o:o + 16] for o in range(0, 4096, 16)}
                changed = sorted(o for o in want_b if any(want_b[o]))
                r["hex beyond"] = [p0, len(rows[0]) if rows else 0, [len(x) for x in allrows]]
                if not rows or sorted(parse(rows[1])) != changed or b != want_b or any(any(v) for v in a.values()) or \
                        [len(x) for x in allrows] != [256, 256]:
                    problems.append(f"the hexdump of page {p0:#x} (beyond the 2 MiB snapshot): rows {r['hex beyond']}, "
                                    f"now's bytes as v86 has them: {b == want_b}, the snapshot's side all zero: {not any(any(v) for v in a.values())}")
                # The page as the 2 MiB snapshot "had" it in the Page tab: zeros,
                # its note naming the snapshot, and after a rename the new name.
                page.click(f"#snap_pages [data-page='{p0}'] button.snapopena")
                wait_for(lambda: shown_tab(page) == "page" and page.is_visible("#page_snap"), 15)
                banner = page.text_content("#page_snap_text")
                zero = page.evaluate("(() => { const d = document.getElementById('listing')._drawn; return !!d && d.bytes.length === 4096 && d.bytes.every((x) => !x); })()")
                show_tab(page, "snapshots")
                page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
                page.fill("#snapshots input.snapedit", "the 2 MiB one")
                page.press("#snapshots input.snapedit", "Enter")
                show_tab(page, "page")
                wait_for(lambda: "the 2 MiB one" in (page.text_content("#page_snap_text") or ""), 10)
                banner2 = page.text_content("#page_snap_text")
                r["page tab beyond"] = [banner[:120], zero, banner2[:120]]
                if banner != f"Showing snapshot “{name0}”; yellow bytes differ from now (when diffed). " or not zero or \
                        not banner2.startswith("Showing snapshot “the 2 MiB one”;"):
                    problems.append(f"page {p0:#x} of the 2 MiB snapshot in the Page tab: {r['page tab beyond']}")
                show_tab(page, "snapshots")
            page.screenshot(path=str(shots / "snapdiff-corrupt-ramsizes.png"), full_page=True)

        # The site's data cleared while the page is open.
        cdp = ctx.new_cdp_session(page)
        cdp.send("Storage.clearDataForOrigin", {"origin": base, "storageTypes": "indexeddb"})
        closing = "The database connection is closing."
        snap_click(page, f"#snapshots tr[data-id='{a_id}version'] button.snapdel", False)
        wait_for(lambda: "IndexedDB failed" in page.text_content("#snap_status"), 15)
        s_del = page.text_content("#snap_status")
        page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
        page.fill("#snapshots input.snapedit", "renamed after clearing")
        page.press("#snapshots input.snapedit", "Enter")
        wait_for(lambda: page.text_content("#snap_status").startswith("Renamed."), 15)
        s_ren = page.text_content("#snap_status")
        n = len(snap_rows(page)["rows"])
        st = snap_save(page, n + 1, False)
        s_save = st["status"]
        where = {x["id"]: x["where"] for x in st["rows"]}
        r["cleared"] = [s_del[:120], s_ren[:160], s_save[-160:], where.get(a_id), list(where.values())[-1]]
        if not s_del.startswith("Deleting from IndexedDB failed: ") or closing not in s_del:
            problems.append(f"Delete after the site's data was cleared: {s_del!r}")
        if not s_ren.startswith("Renamed. Not stored (") or closing not in s_ren or where.get(a_id) != "memory":
            problems.append(f"Rename after the site's data was cleared: {s_ren!r}, the snapshot is {where.get(a_id)!r} (want memory)")
        if "Not stored (" not in s_save or closing not in s_save or list(where.values())[-1] != "memory":
            problems.append(f"Save after the site's data was cleared: {s_save!r}, the new one is {list(where.values())[-1]!r}")
        page.screenshot(path=str(shots / "snapdiff-corrupt-cleared.png"), full_page=True)
        ctx.close()

        # The snapshot worker's script cannot be fetched (no service worker):
        # the save is kept raw, Restore says the worker failed.
        ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
        ctx.add_init_script("if (navigator.serviceWorker) navigator.serviceWorker.register = () => Promise.reject(new DOMException('not allowed by the test', 'SecurityError'));")
        ctx.route("**/snapworker.js*", lambda route: route.fulfill(status=404, body="gone"))
        page = ctx.new_page()
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        st = snap_save(page, 1, False)
        row = st["rows"][0]["id"]
        codec = page.evaluate("MKB.snapshots[0].codec")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: "failed" in page.text_content("#snap_status"), 30)
        r["no worker"] = [st["status"][-80:], codec, page.text_content("#snap_status")]
        if not st["status"].endswith(" Not compressed (the snapshot worker failed).") or codec != "raw" or \
                page.text_content("#snap_status") != "restore_state failed: Error: the snapshot worker failed":
            problems.append(f"no snapshot worker: save {st['status'][-80:]!r} ({codec}), Restore {page.text_content('#snap_status')!r}")
        if errors:
            problems.append(f"corrupt: page errors {errors[:3]}")
    finally:
        ctx.close()
    return problems, r


def snapdiff_edges(browser, base, shots):
    """What the table does with records it cannot use as they are, and the
    small controls around it: a store a newer deploy opened at a higher
    version (kept in memory, says why), Delete all's second press timing
    out, a rename shown in the diff, Show live memory after a snapshot's
    page, a snapshot of a kernel not in the list, one of a listed kernel at
    a RAM size the menu lacks (the switch adds it), a delta whose base is
    gone (Download and Restore say so), and Restore at another RAM size."""
    problems, r, errors = [], {}, []

    # A newer deploy's database version: open() fails with a VersionError.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script("indexedDB.open('mkb-snapshots', 2)")
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    st = snap_save(page, 1, False)
    r["version"] = [[x["where"] for x in st["rows"]], st["store"][:200]]
    if [x["where"] for x in st["rows"]] != ["memory"] or "not stored" not in st["store"] or "version" not in st["store"]:
        problems.append(f"database at a newer version: rows {r['version'][0]}, note {st['store']!r} (want in memory, the reason named)")
    page.screenshot(path=str(shots / "snapdiff-edges-version.png"), full_page=True)
    # In memory only the last 8 stay: the ninth save drops the oldest.
    first = st["rows"][0]["id"]
    for n in range(2, 10):
        snap_save(page, min(n, 8), False)
    wait_for(lambda: page.text_content("#snap_status").startswith("Saved #9"), 15)
    ids = [x["id"] for x in snap_rows(page)["rows"]]
    r["in memory, 9 saves"] = [len(ids), first in ids, page.text_content("#snap_status")[:40]]
    if len(ids) != 8 or first in ids:
        problems.append(f"in memory only: 9 saves leave {len(ids)} rows, the first one kept: {first in ids} (want 8, no)")
    ctx.close()

    # No LZ4 module (a failed download of it): the snapshot is kept raw, says
    # so, restores and downloads as v86's plain state.
    # No service worker either (its cache would have the module): register()
    # fails as in a browser that does not allow them.
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    ctx.add_init_script("if (navigator.serviceWorker) navigator.serviceWorker.register = () => Promise.reject(new DOMException('not allowed by the test', 'SecurityError'));")
    ctx.route("**/lz4_wasm_bg.wasm*", lambda route: route.fulfill(status=404, body="gone"))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1#snapshots")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    page.evaluate(SNAP_JS)
    page.evaluate("mkbSnap.keep('A')")
    icount = page.evaluate("mkbSnap.cpu().icount")
    st = snap_save(page, 1, False)
    raw_id = st["rows"][0]["id"]
    row = page.evaluate(SNAP_ROW_CODEC_JS, raw_id) or {}
    r["no lz4"] = [st["status"][-90:], row.get("codec"), row.get("text", "")[-80:]]
    if "Not compressed (" not in st["status"] or "HTTP 404" not in st["status"] or row.get("codec") != "raw" or \
            "not compressed" not in row.get("text", ""):
        problems.append(f"no LZ4 module: status {st['status']!r}, row {row}")
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS["hello"]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != icount and
             page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    show_tab(page, "snapshots")
    page.click(f"#snapshots tr[data-id='{raw_id}'] button.restore")
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == raw_id, 30)
    back = page.evaluate("mkbSnap.cpu().icount")
    with page.expect_download() as dl:
        page.click(f"#snapshots tr[data-id='{raw_id}'] button.snapdl")
    d = dl.value
    data = pathlib.Path(d.path()).read_bytes()
    try:
        ram, _ = snap_parse_state(data)
        same = bytes(ram) == bytes(page.evaluate("Array.from(mkbSnap.A)"))
    except (ValueError, KeyError, IndexError) as e:
        same = str(e)[:100]
    r["no lz4 restore, download"] = [back == icount, d.suggested_filename, len(data), same]
    if back != icount:
        problems.append(f"no LZ4 module: Restore of the raw snapshot gave instruction {back}, want {icount}")
    if not d.suggested_filename.endswith(".v86state") or same is not True:
        problems.append(f"no LZ4 module: the download {d.suggested_filename!r} ({len(data)} bytes) is v86's state "
                        f"with the saved RAM: {same}")
    ctx.close()

    ctx = browser.new_context(viewport={"width": 1280, "height": 1000}, accept_downloads=True)
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))

    def start(url):
        # The same document with another hash would not load again.
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
        return ok

    url = f"{base}/index.html?kernel=hello&paused=1#debugger"
    if not start(url):
        ctx.close()
        return ["edges: hello did not start paused"], r
    ram = page.evaluate("MKB.emuRam")
    st = snap_save(page, 1, False)
    a_id = st["rows"][0]["id"]

    # Delete all: the first press arms it for 5 s, then it is a plain button again.
    page.click("#snap_delall")
    armed = page.text_content("#snap_delall")
    time.sleep(5.5)
    after = page.text_content("#snap_delall")
    r["delete all"] = [armed, after, len(snap_rows(page)["rows"])]
    if "Press again" not in armed or after != "Delete all" or len(snap_rows(page)["rows"]) != 1:
        problems.append(f"Delete all: armed {armed!r}, 5 s later {after!r}, {len(snap_rows(page)['rows'])} rows (want 1)")

    # Step on, diff the snapshot with now; a rename shows in the diff at once.
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS["hello"]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused" and
             page.evaluate("MKB.stepper.steps") >= SNAPDIFF_STEPS["hello"], 60)
    show_tab(page, "snapshots")
    page.select_option("#snap_a", a_id)
    page.select_option("#snap_b", "now")
    page.click("#snap_diff")
    wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60)
    d = page.evaluate(SNAP_DIFF_JS)
    page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
    page.fill("#snapshots input.snapedit", "edge renamed")
    page.press("#snapshots input.snapedit", "Enter")
    renamed = wait_for(lambda: "edge renamed" in (page.evaluate(SNAP_DIFF_JS) or {}).get("text", ""), 10)
    r["rename in diff"] = renamed
    if not renamed:
        problems.append("a rename while the diff is shown: the diff still has the old name")
    if not d["pages"]:
        problems.append(f"stepping {SNAPDIFF_STEPS['hello']} instructions changed no page (test setup)")
    else:
        p0 = d["pages"][0][0]
        page.click(f"#snap_pages [data-page='{p0}'] button.snapopen")
        wait_for(lambda: shown_tab(page) == "page" and page.is_visible("#page_snap"), 15)
        banner = page.text_content("#page_snap_text") or ""
        # Renamed again while the Page tab holds the snapshot's page: its note follows.
        show_tab(page, "snapshots")
        page.click(f"#snapshots tr[data-id='{a_id}'] button.snapname")
        page.fill("#snapshots input.snapedit", "edge renamed twice")
        page.press("#snapshots input.snapedit", "Enter")
        show_tab(page, "page")
        wait_for(lambda: "edge renamed twice" in (page.text_content("#page_snap_text") or ""), 10)
        banner2 = page.text_content("#page_snap_text") or ""
        page.screenshot(path=str(shots / "snapdiff-edges-page.png"))
        page.click("#page_snap_live")
        live = wait_for(lambda: not page.is_visible("#page_snap"), 10)
        same = page.evaluate(f"""(() => {{ const d = document.getElementById('listing')._drawn;
            const m = MKB.emulator.read_memory({p0} * 4096, 4096);
            return !!d && d.bytes.length === m.length && d.bytes.every((b, i) => b === m[i]); }})()""")
        r["live memory"] = [p0, banner[:100], banner2[:100], live, same]
        if "edge renamed" not in banner or "edge renamed twice" not in banner2:
            problems.append(f"the snapshot's page in the Page tab: notes {banner[:100]!r}, after a rename {banner2[:100]!r}")
        if not live or not same:
            problems.append(f"Show live memory: note hidden {live}, listing is live RAM {same}")

    # Three records the page did not write, then a reload reads them.
    gone, odd, orphan = a_id + "g", a_id + "o", a_id + "d"
    page.evaluate(SNAP_INJECT_JS, [
        {"tag": "g", "n": 1, "name": "of a gone kernel", "kernel": "gone-kernel", "key": "sha256:" + "0" * 64},
        {"tag": "o", "n": 2, "name": "hello at 3 MiB", "key": "sha256:" + "1" * 64, "ram": 3 << 20},
        {"tag": "d", "n": 3, "name": "orphan delta", "delta": {"base": "no-such-snapshot", "pages": 3}}])
    start(url)
    show_tab(page, "snapshots")
    snap_wait_rows(page, 4)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    rows = {x["id"]: x for x in snap_rows(page)["rows"]}
    r["injected"] = {k: [rows.get(k, {}).get("kernel"), rows.get(k, {}).get("restore"), rows.get(k, {}).get("sw")]
                     for k in (gone, odd, orphan)}
    if r["injected"] != {gone: ["other", False, True], odd: ["other", False, True], orphan: ["match", True, False]}:
        problems.append(f"stored records after a reload: {r['injected']}")
    page.screenshot(path=str(shots / "snapdiff-edges-rows.png"), full_page=True)

    def status_after(sel, want):
        snap_click(page, sel, False)
        wait_for(lambda: want in page.text_content("#snap_status"), 15)
        return page.text_content("#snap_status")

    s = status_after(f"#snapshots tr[data-id='{orphan}'] button.snapdl", "Download failed")
    r["orphan download"] = s[:160]
    if "Download failed" not in s or "is a delta of a snapshot that is not here" not in s:
        problems.append(f"Download of a delta without its base: {s!r}")
    s = status_after(f"#snapshots tr[data-id='{orphan}'] button.restore", "failed")
    r["orphan restore"] = s[:160]
    if "is a delta of a snapshot that is not here" not in s:
        problems.append(f"Restore of a delta without its base: {s!r}")
    s = status_after(f"#snapshots tr[data-id='{gone}'] button.snapswitch", "not one of the listed kernels")
    r["gone switch"] = s[:160]
    if "“gone-kernel” is not one of the listed kernels" not in s or "load that bzImage from disk" not in s:
        problems.append(f"Switch to a kernel that is not listed: {s!r}")

    # Restore at another RAM size: the page names the size to boot with.
    other = next(m for m in (4 << 20, 2 << 20) if m != ram)
    start(f"{base}/index.html?kernel=hello&mem={other}&paused=1#snapshots")
    show_tab(page, "snapshots")
    snap_wait_rows(page, 4)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    s = status_after(f"#snapshots tr[data-id='{a_id}'] button.restore", "boot with that size")
    r["ram mismatch"] = s[:160]
    want = f"was taken with {page.evaluate(f'MKB.kib({ram})')} of RAM; boot with that size to restore it."
    if want not in s:
        problems.append(f"Restore at {other} bytes of RAM of a {ram}-byte snapshot: {s!r}, want {want!r}")

    # A listed kernel at 3 MiB: the switch adds the size to the menu and boots it.
    snap_click(page, f"#snapshots tr[data-id='{odd}'] button.snapswitch", False)
    booted = wait_for(lambda: page.evaluate("MKB.emuRam === 3145728 && !!MKB.emulator && MKB.emuReady"), 120)
    r["odd ram"] = [page.input_value("#memory"), booted, page.text_content("#snap_status")[:120]]
    if page.input_value("#memory") != "3145728" or not booted:
        problems.append(f"Switch to hello at 3 MiB: memory menu {page.input_value('#memory')!r}, booted {booted}")
    if errors:
        problems.append(f"edges: page errors {errors[:3]}")
    ctx.close()
    return problems, r
