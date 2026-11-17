"""Part of a browser suite (test_browser.py)."""

import base64
import gzip
import json
import os
import pathlib
import re
import shutil
import subprocess
import time

from tests.browser.common import (
    nav_press, PHONE, run_to_wait, show_tab, shown_tab, snap_click, snap_rows, snap_save, snap_wait_rows, wait_for,
)


# ---- snapdiff: named snapshots, kept across a reload, and their diff (S1) --
# The operator: "dodaj diffowanie snapshotów, ich nazywanie i przechowywanie
# między refresh". Snapshots get a default name the user can rename, are
# stored in IndexedDB per kernel identity (bzImage SHA-256) and survive a
# reload; a diff of two snapshots (or one and now) lists the registers, the
# instruction count, the VGA lines and the 4 KiB pages that changed, with a
# side-by-side hexdump of a page and a link into the Page tab at the
# snapshot's contents. Without storage the page keeps them in memory and
# says so; a full quota is reported and the in-memory copy kept.
SNAPDIFF_STEPS = {"hello": 60, "minified-linux": 3000}

SNAP_JS = """() => { if (window.mkbSnap) return; window.mkbSnap = {
  cpu() { const c = MKB.emulator.v86.cpu, r = c.reg32, s = c.sreg;
    return { eip: (c.instruction_pointer[0] - c.segment_offsets[1]) >>> 0, icount: c.instruction_counter[0] >>> 0,
      regs: { EAX: r[0] >>> 0, ECX: r[1] >>> 0, EDX: r[2] >>> 0, EBX: r[3] >>> 0, ESP: r[4] >>> 0, EBP: r[5] >>> 0,
        ESI: r[6] >>> 0, EDI: r[7] >>> 0, EIP: (c.instruction_pointer[0] - c.segment_offsets[1]) >>> 0,
        EFLAGS: c.get_eflags() >>> 0, ES: s[0], CS: s[1], SS: s[2], DS: s[3], FS: s[4], GS: s[5],
        CR0: c.cr[0] >>> 0, CR2: c.cr[2] >>> 0, CR3: c.cr[3] >>> 0, CR4: c.cr[4] >>> 0 },
      screen: [...document.getElementById('screen_text').children].map((e) => e.textContent.replace(/\\s+$/, '')) }; },
  keep(k) { this[k] = MKB.emulator.read_memory(0, MKB.emuRam).slice(); },
  pages(a, b) { const A = this[a], B = this[b], out = [];
    for (let p = 0; p < A.length / 4096; p++) { let n = 0;
      for (let i = p * 4096; i < p * 4096 + 4096; i++) if (A[i] !== B[i]) n++;
      if (n) out.push([p, n]); }
    return out; },
  hex(k, p) { return Array.from(this[k].subarray(p * 4096, p * 4096 + 4096), (b) => b.toString(16).padStart(2, '0')).join(''); },
  b64(k) { let s = ''; const a = this[k]; for (let i = 0; i < a.length; i += 0x8000) s += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000)); return btoa(s); } }; }"""

SNAP_DIFF_JS = """() => { const o = document.getElementById('snap_diff_out');
  if (!o) return null;
  return { state: o.dataset.state || '', pages: [...o.querySelectorAll('#snap_pages [data-page]')].map((e) => [Number(e.dataset.page), Number(e.dataset.bytes)]),
    regs: [...o.querySelectorAll('#snap_regs [data-reg]')].map((e) => [e.dataset.reg, e.dataset.old, e.dataset.new]),
    screen: [...o.querySelectorAll('#snap_screen [data-line]')].map((e) => Number(e.dataset.line)),
    summary: (document.getElementById('snap_diff_sum') || {}).textContent || '',
    icount: [o.dataset.icountA, o.dataset.icountB], text: o.innerText.slice(0, 4000) }; }"""


def snap_parse_state(raw):
    """v86 0.5.471 save_state() (src/state.js): 16-byte header (magic
    0x86768676, version 6, total length, info length), the JSON info block
    {buffer_infos, state} padded to 4, then the buffers, each aligned to 4.
    The CPU's state[77] is the non-zero pages packed, state[78] the bitmap
    (bit p of byte p >> 3) of which pages they are, state[0] the RAM size."""
    import struct
    magic, version, total, info_len = struct.unpack_from("<iiii", raw, 0)
    if magic & 0xffffffff != 0x86768676 or version != 6 or total != len(raw):
        raise ValueError(f"not a v86 state: {magic & 0xffffffff:#x} v{version} {total}/{len(raw)}")
    info = json.loads(raw[16:16 + info_len])
    start = (16 + info_len + 3) & ~3
    def buf(o):
        bi = info["buffer_infos"][o["buffer_id"]]
        return raw[start + bi["offset"]:start + bi["offset"] + bi["length"]]

    st = info["state"]
    size, packed, bitmap = st[0], buf(st[77]), buf(st[78])
    ram = bytearray(size)
    k = 0
    for p in range(size // 4096):
        if bitmap[p >> 3] >> (p & 7) & 1:
            ram[p * 4096:p * 4096 + 4096] = packed[k * 4096:k * 4096 + 4096]
            k += 1
    return ram, st


def lz4_frame_decode(data):
    """The LZ4 frame format (lz4/doc/lz4_Frame_format.md, version 01) and
    its block format, decoded here independently of the page's lz4frame.js:
    magic 0x184D2204, FLG and BD bytes, an optional content size and
    dictionary ID, the header checksum, then blocks (a 32-bit size, the
    high bit set for a stored block; an optional block checksum) up to a
    zero size, and an optional content checksum. Blocks may refer back
    into earlier blocks (FLG bit 5 clear), so the output is one history.
    The block format's end rules are enforced as the reference decoder
    does for a full block: the last match starts at least 12 bytes before
    the block's end and the last 5 bytes are literals (lz4js 0.2.0 broke
    them and `lz4 -d` refused its 4 MiB blocks)."""
    import struct
    if struct.unpack_from("<I", data, 0)[0] != 0x184D2204:
        raise ValueError(f"not an LZ4 frame: magic {data[:4].hex()}")
    flg = data[4]
    if flg >> 6 != 1:
        raise ValueError(f"LZ4 frame version {flg >> 6}")
    i = 6 + (8 if flg & 8 else 0) + (4 if flg & 1 else 0) + 1
    out = bytearray()
    while True:
        size = struct.unpack_from("<I", data, i)[0]
        i += 4
        if size == 0:
            break
        n = size & 0x7fffffff
        block = data[i:i + n]
        i += n + (4 if flg & 0x10 else 0)
        if size & 0x80000000:
            out += block
            continue
        j, base, last_start, last_end = 0, len(out), 0, 0
        while j < n:
            token = block[j]
            j += 1
            lit = token >> 4
            if lit == 15:
                while True:
                    b = block[j]
                    j += 1
                    lit += b
                    if b != 255:
                        break
            out += block[j:j + lit]
            j += lit
            if j >= n:
                break
            off = block[j] | block[j + 1] << 8
            j += 2
            ml = token & 15
            if ml == 15:
                while True:
                    b = block[j]
                    j += 1
                    ml += b
                    if b != 255:
                        break
            ml += 4
            if off == 0 or off > len(out):
                raise ValueError(f"LZ4 match offset {off} outside the {len(out)} bytes decoded")
            start = len(out) - off
            last_start, last_end = len(out) - base, len(out) - base + ml
            if off >= ml:
                out += out[start:start + ml]
            else:
                for k in range(ml):
                    out.append(out[start + k])
        size_out = len(out) - base
        if last_end and (last_start > size_out - 12 or last_end > size_out - 5):
            raise ValueError(f"LZ4 block of {size_out} bytes: a match at {last_start}..{last_end} breaks the end rules "
                             "(last match 12 bytes before the end, last 5 bytes literals)")
    return bytes(out)


def snap_check_download(path, want_ram, codec):
    """A downloaded snapshot: LZ4 frame (or gzip for an S1 one) of v86's
    state, decoded in Python (and by the lz4 tool when it is installed;
    CI installs it). Returns problems and the decoded sizes."""
    problems = []
    data = pathlib.Path(path).read_bytes()
    try:
        raw = lz4_frame_decode(data) if codec == "lz4" else gzip.decompress(data)
    except (ValueError, OSError, IndexError, EOFError) as e:
        return [f"the {codec} download ({len(data)} bytes, starts {data[:6].hex()}) does not decode: {e}"], [len(data)]
    if codec == "lz4":
        tool = shutil.which("lz4")
        if tool:
            cli = subprocess.run([tool, "-d", "-c", str(path)], capture_output=True)
            if cli.returncode or cli.stdout != raw:
                problems.append(f"lz4 -d of the download: exit {cli.returncode}, {len(cli.stdout)} bytes, "
                                f"same as the Python decoder: {cli.stdout == raw} {cli.stderr[:200]!r}")
        elif os.environ.get("CI"):
            problems.append("no lz4 tool on the CI runner to check the download with")
    ram, _ = snap_parse_state(raw)
    if bytes(ram) != want_ram:
        problems.append(f"the downloaded {codec} state's RAM differs from v86's RAM")
    return problems, [len(data), len(raw), len(ram)]


SNAP_ROW_CODEC_JS = """(id) => { const tr = document.querySelector(`#snapshots tr[data-id='${id}']`);
  return tr ? { codec: tr.dataset.codec || '', text: tr.innerText.replace(/\\s+/g, ' ') } : null; }"""


def snap_row_codec(page, tag, row_id, codec):
    """The row names the compression, its level and how long it took."""
    x = page.evaluate(SNAP_ROW_CODEC_JS, row_id) or {"codec": "", "text": ""}
    want = {"lz4": r"LZ4 level 1\b.*\d+ ms", "gzip": r"gzip level 6\b"}[codec]
    if x["codec"] != codec or not re.search(want, x["text"]):
        return [f"{tag}: row {row_id} says compression {x['codec']!r}: {x['text'][:200]!r} (want {codec}, /{want}/)"]
    return []


def snapdiff_run(browser, base, shots, name, scheme, phone):
    problems, r, errors = [], {}, []
    w = 390 if phone else 1280
    tag = f"{name} {w} {scheme}"
    ctx = browser.new_context(color_scheme=scheme, accept_downloads=True,
                              **(PHONE if phone else {"viewport": {"width": 1280, "height": 1000}}))
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    def shot(s):
        page.screenshot(path=str(shots / f"snapdiff-{name}-{w}-{scheme}-{s}.png"), full_page=True)

    url = f"{base}/index.html?kernel={name}&paused=1#debugger"

    def start():
        page.goto(url) if not page.url.startswith(url.split("#")[0]) else page.reload()
        ok = wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && !!MKB.emulator"), 30)
        page.evaluate(SNAP_JS)
        return ok

    if not start():
        ctx.close()
        return [f"{tag}: did not start paused"], r
    show_tab(page, "debugger")
    page.fill("#dbg_addr", "0x100000")
    page.click("#dbg_runto")
    ok, at, status = run_to_wait(page, {0x100000}, 300)
    if not ok:
        problems.append(f"{tag}: Run to the kernel entry stopped at {at:#x}: {status}")
    page.evaluate("mkbSnap.keep('A')")
    cpu_a = page.evaluate("mkbSnap.cpu()")
    st = snap_save(page, 1, phone)
    if len(st["rows"]) != 1:
        problems.append(f"{tag}: Save snapshot gave {len(st['rows'])} rows: {st['status']}")
        ctx.close()
        return problems, r
    a = st["rows"][0]
    r["A"] = a["name"]
    # The default name: kernel, instruction count, symbol at EIP, time.
    if name not in a["name"] or not re.search(r"\d", a["name"]):
        problems.append(f"{tag}: default name {a['name']!r} lacks the kernel or the instruction count")
    # Step N.
    show_tab(page, "debugger")
    page.fill("#dbg_n", str(SNAPDIFF_STEPS[name]))
    page.click("#dbg_stepn")
    wait_for(lambda: page.evaluate("mkbSnap.cpu().icount") != cpu_a["icount"] and
             page.get_attribute("#dbg_state", "data-state") == "paused", 120)
    time.sleep(1)
    page.evaluate("mkbSnap.keep('B')")
    cpu_b = page.evaluate("mkbSnap.cpu()")
    want_pages = page.evaluate("mkbSnap.pages('A', 'B')")
    want_regs = sorted(k for k in cpu_a["regs"] if cpu_a["regs"][k] != cpu_b["regs"][k])
    want_screen = [i for i in range(max(len(cpu_a["screen"]), len(cpu_b["screen"])))
                   if (cpu_a["screen"] + [""] * 99)[i] != (cpu_b["screen"] + [""] * 99)[i]]
    b_hex = {p: page.evaluate(f"mkbSnap.hex('B', {p})") for p, _ in want_pages[:6]}
    r["want"] = {"pages": len(want_pages), "regs": want_regs, "screen": want_screen,
                 "icount": [cpu_a["icount"], cpu_b["icount"]]}
    if not want_pages or not want_regs:
        problems.append(f"{tag}: stepping {SNAPDIFF_STEPS[name]} changed no page or register (test setup)")
    st = snap_save(page, 2, phone)
    if len(st["rows"]) != 2:
        problems.append(f"{tag}: the second save gave {len(st['rows'])} rows")
        ctx.close()
        return problems, r
    a_id, b_id = st["rows"][0]["id"], st["rows"][1]["id"]
    if st["rows"][0]["name"] == st["rows"][1]["name"]:
        problems.append(f"{tag}: two snapshots got the same default name {st['rows'][0]['name']!r}")

    # Rename B (Enter saves), start renaming A and press Esc (cancels).
    new_name = f"after {SNAPDIFF_STEPS[name]} steps ({scheme})"
    snap_click(page, f"#snapshots tr[data-id='{b_id}'] .snapname", phone)
    inp = page.locator(f"#snapshots tr[data-id='{b_id}'] input.snapedit")
    if not inp.count():
        problems.append(f"{tag}: tapping the name gives no text field to rename it")
    else:
        inp.fill(new_name)
        inp.press("Enter")
    snap_click(page, f"#snapshots tr[data-id='{a_id}'] .snapname", phone)
    inp = page.locator(f"#snapshots tr[data-id='{a_id}'] input.snapedit")
    if inp.count():
        inp.fill("not this")
        inp.press("Escape")
    time.sleep(0.5)
    st = snap_rows(page)
    names = {x["id"]: x["name"] for x in st["rows"]}
    if names.get(b_id) != new_name or names.get(a_id) != r["A"]:
        problems.append(f"{tag}: after renaming B and cancelling A the names are {names}")
    if f"{name}" not in st["store"] or "stored" not in st["store"].lower():
        problems.append(f"{tag}: the storage line {st['store']!r} does not say what is stored")
    for rid in (a_id, b_id):
        problems += snap_row_codec(page, tag, rid, "lz4")
    shot("saved")

    # The download is an LZ4 frame of v86's state (C1): decoded and parsed
    # here independently, its RAM is what v86 had at B.
    if name == "hello" and not phone:
        with page.expect_download() as dl:
            page.locator(f"#snapshots tr[data-id='{b_id}'] button.snapdl").click()
        fname = dl.value.suggested_filename
        if not fname.endswith(".v86state.lz4"):
            problems.append(f"{tag}: the download is named {fname!r}, want *.v86state.lz4")
        bad, r["download"] = snap_check_download(dl.value.path(), base64.b64decode(page.evaluate("mkbSnap.b64('B')")), "lz4")
        problems += [f"{tag}: {b}" for b in bad]

    # Reload: both are listed with names, sizes and a matching kernel.
    if not start():
        problems.append(f"{tag}: the reload did not start paused")
    show_tab(page, "snapshots")
    st = snap_wait_rows(page, 2)
    wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
    st = snap_rows(page)
    r["after reload"] = [(x["name"], x["kernel"], x["where"], x["stored"]) for x in st["rows"]]
    names = {x["id"]: x["name"] for x in st["rows"]}
    if names.get(a_id) != r["A"] or names.get(b_id) != new_name:
        problems.append(f"{tag}: after the reload the snapshots are {names} (want {a_id}: {r['A']!r}, {b_id}: {new_name!r})")
    for x in st["rows"]:
        if x["kernel"] != "match" or x["where"] != "stored" or not x["stored"] or not x["restore"]:
            problems.append(f"{tag}: after the reload row {x['id']} says kernel {x['kernel']!r}, kept {x['where']!r}, "
                            f"{x['stored']} bytes, Restore {x['restore']}")
        if not re.search(r"\d[\d,.]* ?(KiB|MiB|bytes)", x["text"]):
            problems.append(f"{tag}: row {x['id']} shows no size: {x['text'][:120]}")
    if not re.search(r"\d+ snapshots?", st["store"]) or not re.search(r"(KiB|MiB|bytes)", st["store"]):
        problems.append(f"{tag}: the storage line {st['store']!r} lacks the count or the total size")
    if st["hscroll"]:
        problems.append(f"{tag}: horizontal scroll on the Snapshots tab")

    # Diff A -> B.
    page.select_option("#snap_a", a_id)
    page.select_option("#snap_b", b_id)
    snap_click(page, "#snap_diff", phone)
    if not wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60):
        problems.append(f"{tag}: the diff did not finish: {page.evaluate(SNAP_DIFF_JS)}")
        ctx.close()
        return problems, r
    d = page.evaluate(SNAP_DIFF_JS)
    r["diff"] = {"pages": len(d["pages"]), "regs": [x[0] for x in d["regs"]], "screen": d["screen"], "summary": d["summary"][:200]}
    if sorted(map(tuple, d["pages"])) != sorted(map(tuple, want_pages)):
        miss = sorted(set(map(tuple, want_pages)) - set(map(tuple, d["pages"])))
        extra = sorted(set(map(tuple, d["pages"])) - set(map(tuple, want_pages)))
        problems.append(f"{tag}: the diff's pages differ from v86's RAM: missing {miss[:6]}, extra {extra[:6]}")
    got_regs = sorted(x[0] for x in d["regs"])
    if got_regs != want_regs:
        problems.append(f"{tag}: the diff lists registers {got_regs}, these changed: {want_regs}")
    for reg, old, new in d["regs"]:
        if reg in cpu_a["regs"] and (int(old, 16) != cpu_a["regs"][reg] or int(new, 16) != cpu_b["regs"][reg]):
            problems.append(f"{tag}: {reg} {old} -> {new}, v86 had {cpu_a['regs'][reg]:#x} -> {cpu_b['regs'][reg]:#x}")
    if d["screen"] != want_screen:
        problems.append(f"{tag}: the diff lists screen lines {d['screen']}, these changed: {want_screen}")
    if d["icount"] != [str(cpu_a["icount"]), str(cpu_b["icount"])]:
        problems.append(f"{tag}: instruction counts {d['icount']}, v86 counted {cpu_a['icount']} and {cpu_b['icount']}")
    for n in (r["A"], new_name):
        if n not in d["text"]:
            problems.append(f"{tag}: the diff does not name {n!r}")
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll with the diff shown")
    shot("diff")

    # The side-by-side hexdump of one page: one mark per changed byte in B's column.
    p0, n0 = want_pages[0]
    snap_click(page, f"#snap_pages [data-page='{p0}'] button.snaphex", phone)
    wait_for(lambda: page.get_attribute("#snap_hex", "data-page") == str(p0), 20)
    marks = page.evaluate("[...document.querySelectorAll('#snap_hex .snapcol-b mark')].reduce((n, m) => n + m.textContent.trim().split(/\\s+/).length, 0)")
    r["hexdump"] = [p0, n0, marks]
    if marks != n0:
        problems.append(f"{tag}: the hexdump of page {p0:#x} marks {marks} bytes, {n0} changed")
    shot("hex")
    # The Worker keeps no decoded state (P1d): after the diff, the listed
    # pages of both sides only; a page the diff does not list is read from
    # both snapshots decoded again (the same bytes on both sides).
    held = page.evaluate("MKB.snapWorker({ op: 'held' })")
    r["worker holds"] = held
    if held.get("diffPages") != len(want_pages) or held.get("bytes") != 2 * 4096 * len(want_pages):
        problems.append(f"{tag}: after the diff the snapshot Worker holds {held}, want the {len(want_pages)} listed pages of both sides")
    listed = {p for p, _ in want_pages}
    other = next(p for p in range(1, 4096) if p not in listed)
    same = page.evaluate("""async (p) => { const r = await MKB.snapPages(p);
      return r.a.length === 4096 && r.b.length === 4096 && r.a.every((v, i) => v === r.b[i]); }""", other)
    if not same:
        problems.append(f"{tag}: page {other:#x}, unchanged between the two, differs when read again from the snapshots")
    # "now" against "now" (both pickers allow it): one state, nothing differs.
    # The diff replaces the Worker's kept pages, so the Page tab below reads
    # B's page from the snapshots decoded again.
    nn = page.evaluate("""async () => { const r = await MKB.snapWorker({ op: 'diff', a: { key: 'now', raw: await MKB.emulator.save_state() }, b: { key: 'now' } });
      return [r.pages.length, (await MKB.snapWorker({ op: 'held' })).diffPages]; }""")
    if nn != [0, 0]:
        problems.append(f"{tag}: now against now lists {nn[0]} pages, the Worker keeps {nn[1]}")

    # Open B's page in the Page tab: its bytes as B had them; Back returns.
    p1 = next((p for p, _ in want_pages if p in b_hex), p0)
    snap_click(page, f"#snap_pages [data-page='{p1}'] button.snapopen", phone)
    ok = wait_for(lambda: shown_tab(page) == "page" and page.get_attribute("#listing", "data-page") == str(p1), 15)
    got = page.evaluate("(() => { const d = document.getElementById('listing')._drawn; return d ? Array.from(d.bytes, (b) => b.toString(16).padStart(2, '0')).join('') : ''; })()")
    banner = page.evaluate("(() => { const e = document.getElementById('page_snap'); return e && !e.hidden ? e.textContent : ''; })()")
    r["open"] = [p1, ok, banner[:100]]
    if not ok or got != b_hex.get(p1, got):
        problems.append(f"{tag}: opening page {p1:#x} of B: tab {shown_tab(page)}, listing {page.get_attribute('#listing', 'data-page')}, "
                        f"bytes are B's: {got == b_hex.get(p1)}")
    if new_name not in banner:
        problems.append(f"{tag}: the Page tab does not say it shows {new_name!r}: {banner!r}")
    shot("page")
    nav_press(page, "#nav_back", "tap" if phone else "click")
    if not wait_for(lambda: shown_tab(page) == "snapshots", 5):
        problems.append(f"{tag}: Back from the snapshot's page goes to {shown_tab(page)}")

    # Restore B after the reload: same EIP, registers, instruction count, screen.
    snap_click(page, f"#snapshots tr[data-id='{b_id}'] button.restore", phone)
    wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == b_id, 30)
    wait_for(lambda: page.evaluate("mkbSnap.cpu()")["screen"] == cpu_b["screen"], 10)
    now = page.evaluate("mkbSnap.cpu()")
    r["restored"] = [hex(now["eip"]), now["icount"]]
    if now["eip"] != cpu_b["eip"] or now["icount"] != cpu_b["icount"] or now["regs"] != cpu_b["regs"]:
        problems.append(f"{tag}: restoring B after the reload: EIP {now['eip']:#x} (want {cpu_b['eip']:#x}), "
                        f"instructions {now['icount']} (want {cpu_b['icount']}), regs equal {now['regs'] == cpu_b['regs']}")
    if now["screen"] != cpu_b["screen"]:
        problems.append(f"{tag}: the screen after restoring B differs from B's")
    # v86's set_state leaves last_result (the lazy flags' input), the FPU
    # status word and MXCSR outside wasm memory: the page puts them back.
    views = page.evaluate("(() => { const c = MKB.emulator.v86.cpu; return ['last_result', 'fpu_status_word', 'mxcsr']"
                          ".filter((k) => c[k].buffer !== c.wasm_memory.buffer); })()")
    if views:
        problems.append(f"{tag}: after the restore {views} are not in v86's wasm memory")

    # Diff B -> now is empty right after the restore (RAM and registers).
    page.select_option("#snap_a", b_id)
    page.select_option("#snap_b", "now")
    snap_click(page, "#snap_diff", phone)
    wait_for(lambda: (page.evaluate(SNAP_DIFF_JS) or {}).get("state") == "done", 60)
    d = page.evaluate(SNAP_DIFF_JS)
    r["B -> now"] = [len(d["pages"]), [x[0] for x in d["regs"]]]
    if d["pages"] or d["regs"]:
        problems.append(f"{tag}: right after restoring B, B -> now lists pages {d['pages'][:5]} and registers {d['regs'][:5]}")

    # Delete A: gone, also after a reload.
    snap_click(page, f"#snapshots tr[data-id='{a_id}'] button.snapdel", phone)
    st = snap_wait_rows(page, 1)
    if [x["id"] for x in st["rows"]] != [b_id]:
        problems.append(f"{tag}: after deleting A: {[x['id'] for x in st['rows']]}")
    start()
    show_tab(page, "snapshots")
    time.sleep(1)
    st = snap_wait_rows(page, 1)
    if [(x["id"], x["name"]) for x in st["rows"]] != [(b_id, new_name)]:
        problems.append(f"{tag}: after deleting A and a reload: {[(x['id'], x['name']) for x in st['rows']]}")
    shot("deleted")
    if errors:
        problems.append(f"{tag}: page errors: {errors[:3]}")
    ctx.close()
    return problems, r


SNAP_LONG_JS = """() => { window.mkbLong = []; new PerformanceObserver((l) => {
    for (const e of l.getEntries()) window.mkbLong.push(Math.round(e.duration)); }).observe({ type: 'longtask' }); }"""


# 24 MiB of guest RAM filled with random runs, repeated runs and zeros
# (gzip -6 keeps about 35% of it, LZ4 level 1 too): a state the size of
# the Debian 64 MiB one at start_kernel (23 MB) that is harder to compress.
SNAP_FILL_JS = """(mib) => { let x = 12345; const rnd = () => (x = (Math.imul(x, 1103515245) + 12345) >>> 0) >>> 8;
  const chunk = new Uint8Array(1 << 20);
  for (let m = 0; m < mib; m++) {
    for (let i = 0; i < chunk.length;) { const n = 16 + (rnd() & 1023), kind = rnd() % 3, v = rnd() & 255;
      for (let k = 0; k < n && i < chunk.length; k++, i++) chunk[i] = kind === 0 ? rnd() & 255 : kind === 1 ? v : 0; }
    MKB.emulator.v86.cpu.mem8.set(chunk, (8 + m) << 20); } }"""


def snap_step_to(page, target):
    """Step (in slices, as the Step button does) until stepper.steps == target."""
    for _ in range(200):
        left = target - page.evaluate("MKB.stepper.steps")
        if left <= 0:
            break
        page.evaluate(f"MKB.doStep({min(left, 400000)})")
    return page.evaluate("MKB.stepper.steps")


# name -> (run, needs the minified Linux). The workflow runs them in
# parallel jobs (--suites); without --suites all run, in this order.
# ---- bookmarks: named bookmarks from many places (K1) ------------------------
# The operator: "chcę móc w różnych miejscach dodawać nazwane zakładki".
# Bookmarks (not tabs: the tab bar has tabs) are added from a listing line,
# Code at EIP, a page of the Memory grid, a line of the Source panel, a
# file of the Source tab, a box of the Calls tab, a frame of the call stack
# and the machine's position, each with a ☆ button, a context menu (right
# click) or long press, and the B key; a dialog asks for the name with a
# default. They are listed in the menu toggle's menu (H1), kept per kernel
# identity (S1's bzImage SHA-256) in localStorage across a reload, jump to
# their place as a history entry (Back returns), carry a ★ mark with a
# popover where they point, export to and import from a JSON file and open
# from a link (#bm=...).
BOOKMARKS_POINTS = {"hello": ("hello_main", 40, "boot/hello.c"),
                    "minified-linux": ("start_kernel", 3000, "init/main.c")}
