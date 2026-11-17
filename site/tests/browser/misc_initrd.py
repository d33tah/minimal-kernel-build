"""Part of a browser suite (test_browser.py)."""

import os
import pathlib
import shutil
import struct
import tempfile
import time

from tests.browser.common import gate_on_screen, kernel_entries, machine_do, show_tab, SITE, wait_for


def initrd_case(browser, base, shots):
    """An initrd reaches guest RAM at 64 MiB; RAM is raised; the map shows it."""
    problems, r = [], {}
    tmp = pathlib.Path(tempfile.mkdtemp())
    cpio = tmp / "scratch.cpio"
    # A newc cpio with one file and the trailer, built here.
    def entry(name, data, mode):
        n = name.encode() + b"\0"
        hdr = "070701" + "".join(f"{v:08x}" for v in (1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(n), 0))
        out = hdr.encode() + n
        out += b"\0" * (-len(out) % 4) + data
        return out + b"\0" * (-len(out) % 4)
    body = entry("init", b"#!/bin/sh\necho initrd-marker-1234\n", 0o100755) + entry("TRAILER!!!", b"", 0)
    cpio.write_bytes(body + b"\0" * (-len(body) % 512))
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("window.oldStepper = MKB.stepper")
    page.set_input_files("#initrd_file", str(cpio))
    # boot() resumes at the default speed; wait for the new machine.
    wait_for(lambda: page.evaluate("!!MKB.stepper && MKB.stepper !== window.oldStepper"), 60)
    machine_do(page, "click", "#dbg_pause") if page.is_enabled("#dbg_pause") else None
    r["ram"] = int(page.eval_on_selector("#memory", "e => e.value"))
    if r["ram"] <= 64 << 20:
        problems.append(f"RAM not raised for the initrd: {r['ram']}")
    head = page.evaluate("Array.from(MKB.emulator.read_memory(64 << 20, 16))")
    if bytes(head) != cpio.read_bytes()[:16]:
        problems.append(f"initrd bytes not at 64 MiB in guest RAM: {bytes(head)!r}")
    r["note"] = page.inner_text("#initrd_status")
    if "ignores a bootloader initrd" not in r["note"]:
        problems.append("no note that our kernels ignore a bootloader initrd: " + r["note"])
    show_tab(page, "memory")
    if "initrd" not in page.inner_text("#map_all"):
        problems.append("the memory map does not show the initrd")
    if not page.evaluate("MKB.pageClass.includes('r-initrd')"):
        problems.append("no grid page is classed as initrd")
    # Too little RAM: the header check says so.
    show_tab(page, "machine")
    page.select_option("#memory", str(32 << 20))
    r["problem"] = page.inner_text("#problems")
    if "64 MiB" not in r["problem"]:
        problems.append("no RAM error for the initrd at 32 MB: " + r["problem"])
    page.select_option("#memory", str(r["ram"]))
    page.set_viewport_size({"width": 390, "height": 900})
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("initrd controls make the page scroll horizontally at 390 px")
    page.screenshot(path=str(shots / "initrd-390.png"))
    page.click("#initrd_clear")
    if page.is_visible("#initrd_clear") or page.inner_text("#initrd_status"):
        problems.append("no initrd did not clear it")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def page_kib(n):
    """app.js kib(): bytes below 1 KiB, else KiB with at most one decimal and thousands commas."""
    if n < 1024:
        return f"{n} bytes"
    t = f"{n / 1024:,.1f}"
    return (t[:-2] if t.endswith(".0") else t) + " KiB"


def header_check_case(browser, base, shots):
    """bzImages from disk whose boot header v86 cannot use, built here from
    hello's: each problem of the header check (Machine tab) is said, with
    the numbers read here from the file, and Boot still starts the machine
    with "The header check predicts this will not work" after an error,
    unless the kernel or the initrd does not fit in RAM at all (v86 threw a
    RangeError copying it: "Emulator error: offset is out of bounds"): then
    Boot says so and starts nothing. Not a bzImage; protocol 2.01 with
    LOADED_HIGH clear; a 32-bit part larger than 2 MB of RAM; an init_size
    beyond the RAM picked, reaching the initrd at 64 MiB and an
    initrd_addr_max below it; the initrd past 64 MB of RAM; an init_size no
    listed RAM size holds (the largest is picked). A v86.wasm the page
    cannot patch for stepping: hello boots without the debugger."""
    problems, r = [], {}
    hello = (SITE / kernel_entries()["hello"]["path"]).read_bytes()
    tmp = pathlib.Path(tempfile.mkdtemp(dir=os.environ.get("MKB_TMP")))

    def patched(name, data=hello, **fields):
        b = bytearray(data)
        for off, (fmt, v) in fields.items():
            struct.pack_into(fmt, b, int(off[1:], 16), v)
        (tmp / name).write_bytes(b)
        return tmp / name
    u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
    setup = ((hello[0x1f1] or 4) + 1) * 512
    code32 = u32(hello, 0x214)
    big = hello + bytes(1536 * 1024)
    big_payload = len(big) - setup
    init = 80 << 20
    init_end = u32(hello, 0x258) + init
    rd_max = (64 << 20) - 1
    cpio = tmp / "rd.cpio"
    cpio.write_bytes(bytes(512))
    rd_end = (64 << 20) + cpio.stat().st_size
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello")
    wait_for(lambda: gate_on_screen(page), 60)
    show_tab(page, "machine")
    mem_opts = page.evaluate("[...document.getElementById('memory').options].map((o) => Number(o.value))")
    # The file dialog cancelled (no file): hello stays.
    page.set_input_files("#file", [])
    if page.evaluate("MKB.image.name") != "hello":
        problems.append(f"no file picked: the kernel is now {page.evaluate('MKB.image.name')!r}")
    (tmp / "not-a-bzimage.bin").write_bytes(bytes(8192))
    patched("old-low.bin", o206=("<H", 0x201), o211=("<B", hello[0x211] & 0xfe))
    patched("big.bin", big)
    patched("init-size.bin", o260=("<I", init), o22c=("<I", rd_max))
    # (file, initrd, RAM picked then, the header check's lines, the status after Boot)
    predict = "The header check predicts this will not work"
    no_room = lambda name, ram: f"Not booted: {name} or its initrd does not fit in {page_kib(ram)} of RAM (the header check says why). Pick more RAM."
    cases = [
        ("not-a-bzimage.bin", None, None, [
            "No \"HdrS\" signature at 0x202: this is not a bzImage.",
            "Boot flag at 0x1fe is 0x0, not 0xaa55: v86 refuses the image (\"Bad checksum1\")."], predict),
        ("old-low.bin", None, None, [
            "Boot protocol 2.01 is older than 2.02, which v86 assumes.",
            f"LOADED_HIGH is clear: the kernel expects its 32-bit part at {code32:#x}, but v86 ignores loadflags and copies it "
            "to 1 MiB, so it will not start here. (qemu -kernel honours the flag.)"], predict),
        ("big.bin", None, 2 << 20, [
            f"v86 copies the {page_kib(big_payload)} 32-bit part to 1 MiB, but RAM ends at 0x200000. The loader cannot place "
            "the kernel: pick more RAM."], no_room("big.bin", 2 << 20)),
        ("init-size.bin", None, 18 << 20, [
            f"The kernel says it needs memory up to {init_end:#x} (pref_address + init_size), but RAM ends at {18 << 20:#x} "
            f"({page_kib(18 << 20)}). init_size covers bss and brk in full; a kernel that does not touch all of them can still "
            f"boot, otherwise it fails early, typically with no output. {page_kib(init_end)} is enough."], None),
        ("init-size.bin", cpio, None, [
            f"The kernel accepts an initrd only below {rd_max:#x} (initrd_addr_max), but v86 puts it at {64 << 20:#x}\u2013{rd_end - 1:#x}.",
            f"The kernel's memory (up to {init_end:#x}) reaches the initrd at 64 MiB."], None),
        ("init-size.bin", None, 64 << 20, [
            f"The kernel says it needs memory up to {init_end:#x} (pref_address + init_size), but RAM ends at {64 << 20:#x} "
            f"({page_kib(64 << 20)}). init_size covers bss and brk in full; a kernel that does not touch all of them can still "
            f"boot, otherwise it fails early, typically with no output. {page_kib(init_end)} is enough.",
            f"v86 puts the initrd ({page_kib(512)}) at 64 MiB, so RAM must be larger than {page_kib(rd_end)}; it is "
            f"{page_kib(64 << 20)}. Pick more RAM.",
            f"The kernel accepts an initrd only below {rd_max:#x} (initrd_addr_max), but v86 puts it at {64 << 20:#x}\u2013{rd_end - 1:#x}.",
            f"The kernel's memory (up to {init_end:#x}) reaches the initrd at 64 MiB."], no_room("init-size.bin", 64 << 20)),
    ]
    said = lambda: page.evaluate("[...document.querySelectorAll('#problems > div')].map((d) => [d.className, d.textContent])")
    for k, (name, rd, mem, want, after) in enumerate(cases):
        tag = name + (" with an initrd" if rd or k == len(cases) - 1 else "") + (f" at {page_kib(mem)}" if mem else "")
        if rd:
            page.set_input_files("#initrd_file", str(rd))
        elif k and cases[k - 1][0] == name:
            pass
        else:
            page.set_input_files("#file", str(tmp / name))
            wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == name, 10)
        if name == "init-size.bin" and not rd and mem == 18 << 20:
            # useImage picks the smallest listed RAM that holds init_size.
            need = min(v for v in mem_opts if v >= init_end)
            r["init_size_ram"] = page.input_value("#memory")
            if page.input_value("#memory") != str(need):
                problems.append(f"{tag}: RAM {page.input_value('#memory')}, want the smallest that holds {init_end:#x}: {need}")
        if mem:
            wait_for(lambda: "Running " + name in (page.text_content("#status") or ""), 30)
            page.select_option("#memory", str(mem))
            page.click("#boot")
        ram = int(page.input_value("#memory"))
        wait_for(lambda: (after == (page.text_content("#status") or "") if after and after.startswith("Not booted")
                          else f"Running {name} with {page_kib(ram)}" in (page.text_content("#status") or ""))
                 and [t for _, t in said()] == want, 30)
        got = said()
        r[tag] = [t[:60] for _, t in got]
        if [t for _, t in got] != want:
            problems.append(f"{tag}: the header check says {[t for _, t in got]}, want {want}")
        if [c for c, _ in got] != [("error" if t.startswith(("No ", "Boot flag", "LOADED_HIGH", "v86 ")) else "warn") for t in want]:
            problems.append(f"{tag}: problem classes {[c for c, _ in got]}")
        status = page.text_content("#status") or ""
        r[tag + ": status"] = status
        if after and after.startswith("Not booted"):
            if status != after or page.evaluate("!!MKB.emulator || MKB.runState !== 'off'"):
                problems.append(f"{tag}: status {status!r}, emulator {page.evaluate('!!MKB.emulator')}, want {after!r} and no machine")
        elif (after == predict) != (predict in status):
            problems.append(f"{tag}: status {status!r}, want {'a' if after else 'no'} prediction that it will not work")
        if name == "not-a-bzimage.bin":
            page.screenshot(path=str(shots / "header-check-1280.png"))
        if name == "big.bin":
            # "Not booted": light 1280 and dark 390 px, no sideways scroll.
            page.screenshot(path=str(shots / "header-check-1280-light-not-booted.png"))
            page.emulate_media(color_scheme="dark")
            page.set_viewport_size({"width": 390, "height": 900})
            time.sleep(0.3)
            if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
                problems.append(f"{tag}: horizontal scroll at 390 px")
            page.screenshot(path=str(shots / "header-check-390-dark-not-booted.png"))
            page.emulate_media(color_scheme="light")
            page.set_viewport_size({"width": 1280, "height": 900})
    initrd_kinds(page, patched("noversion.bin", o20e=("<H", 0)), tmp, problems, r)
    page.click("#initrd_clear")
    # A v86.wasm the page cannot patch for stepping (another v86 build):
    # hello still boots, without the debugger, which says why.
    page.set_input_files("#file", str(SITE / kernel_entries()["hello"]["path"]))
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == "bzImage", 10)
    # The machine state of the stepped machine before, open in the Debugger.
    show_tab(page, "debugger")
    page.evaluate("document.querySelectorAll('#cpu_state details').forEach((d) => d.open = true)")
    wait_for(lambda: page.locator("#cpu_segments [data-reg='CS']").count() > 0, 10)
    page.evaluate("() => { window.__patch = MKB.patchV86Wasm; MKB.patchV86Wasm = () => { throw new Error('test: not the v86.wasm this page patches'); }; }")
    show_tab(page, "machine")
    page.click("#boot")
    booted = wait_for(lambda: gate_on_screen(page), 60)
    r["unpatched v86.wasm"] = [booted, page.inner_text("#dbg_status"), page.evaluate("MKB.stepper === null")]
    if r["unpatched v86.wasm"] != [True, "No debugger: Error: test: not the v86.wasm this page patches", True]:
        problems.append(f"v86.wasm not patched: [booted, debugger status, no stepper] {r['unpatched v86.wasm']}")
    # Without a debugger the Debugger shows no machine state (not the previous machine's).
    show_tab(page, "debugger")
    r["unpatched cpu state"] = page.inner_text("#cpu_state")[:80]
    if page.inner_text("#cpu_state").strip():
        problems.append(f"no debugger: the Debugger still shows the previous machine's state: {r['unpatched cpu state']!r}")
    # With the debugger again (v86.wasm patched): the sections are back and read this machine.
    page.evaluate("() => { MKB.patchV86Wasm = window.__patch; }")
    show_tab(page, "machine")
    page.click("#boot")
    wait_for(lambda: gate_on_screen(page) and page.evaluate("MKB.stepper !== null"), 60)
    show_tab(page, "debugger")
    page.evaluate("document.querySelectorAll('#cpu_state details').forEach((d) => d.open = true)")
    wait_for(lambda: page.locator("#cpu_control [data-reg='CR0']").count() > 0, 10)
    cr0 = page.inner_text("#cpu_control [data-reg='CR0']") if page.locator("#cpu_control [data-reg='CR0']").count() else ""
    want_cr0 = page.evaluate("'CR0 0x' + (MKB.stepper.cpu.cr[0] >>> 0).toString(16).padStart(8, '0')")
    r["patched again cpu state"] = cr0
    if not cr0.startswith(want_cr0):
        problems.append(f"the debugger back: the CR0 row {cr0!r}, the CPU's {want_cr0!r}")
    page.evaluate("() => { MKB.patchV86Wasm = () => { throw new Error('test: not the v86.wasm this page patches'); }; }")
    # No listed RAM size holds it: the largest is picked.
    page.set_input_files("#file", str(patched("huge-init.bin", o260=("<I", 1 << 30))))
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == "huge-init.bin", 10)
    r["huge_init_ram"] = page.input_value("#memory")
    if page.input_value("#memory") != str(max(mem_opts)):
        problems.append(f"init_size 1 GiB: RAM {page.input_value('#memory')}, want the largest listed, {max(mem_opts)}")
    page.close()
    shutil.rmtree(tmp, ignore_errors=True)
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def initrd_kinds(page, kernel, tmp, problems, r):
    """What the initrd note says of initrds the page cannot fully read, for
    a bzImage from disk without a kernel_version (hello's with its offset
    0): a zstd one and one whose gzip stream is broken, each for the kernel
    its file name says (bits from it too); a cpio cut inside its first
    header; a cpio without its trailer, read up to its end (its
    lib/modules release). The file dialog cancelled keeps the initrd and
    does not boot again."""
    page.set_input_files("#file", str(kernel))
    wait_for(lambda: page.evaluate("MKB.image && MKB.image.name") == kernel.name, 10)
    def entry(name, data, mode):
        n = name.encode() + b"\0"
        out = ("070701" + "".join(f"{v:08x}" for v in (1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(n), 0))).encode() + n
        out += b"\0" * (-len(out) % 4) + data
        return out + b"\0" * (-len(out) % 4)
    kinds = [
        ("initrd.img-6.1.0-13-amd64", b"\x28\xb5\x2f\xfd" + bytes(60),
         " It is for 6.1.0-13-amd64 (64-bit) (from its file name; zstd is not decoded here)."),
        ("initrd.img-5.10.0-30-686.gz", b"\x1f\x8b\x08\x00" + b"\xff" * 60,
         " It is for 5.10.0-30-686 (32-bit) (from its file name; gzip is not decoded here)."),
        ("cut.cpio", b"070701" + bytes(58), ""),
        ("notrailer.cpio", entry("lib/modules/6.1.0-50-686", b"", 0o40755) + entry("init", b"#!/bin/sh\n", 0o100755),
         " It is for 6.1.0-50-686."),
    ]
    r["initrd_kinds"] = {}
    for name, data, says in kinds:
        (tmp / name).write_bytes(data)
        page.evaluate("window.__stepper = MKB.stepper")
        page.set_input_files("#initrd_file", str(tmp / name))
        want = f"{name}: {page_kib(len(data))}, v86 puts it at 0x4000000.{says}"
        # The note is written before the boot with this initrd starts: wait for its machine too.
        wait_for(lambda: page.inner_text("#initrd_status") == want and page.evaluate("!!MKB.stepper && MKB.stepper !== window.__stepper"), 30)
        got = page.inner_text("#initrd_status")
        r["initrd_kinds"][name] = got
        if got != want or page.get_attribute("#initrd_status", "data-mismatch"):
            problems.append(f"initrd {name} for a kernel without kernel_version: {got!r}, want {want!r} and no mismatch")
    page.evaluate("window.__stepper = MKB.stepper")
    page.set_input_files("#initrd_file", [])
    time.sleep(1)
    if not page.inner_text("#initrd_status").startswith("notrailer.cpio:") or not page.evaluate("MKB.stepper === window.__stepper"):
        problems.append(f"no initrd file picked: {page.inner_text('#initrd_status')!r}, booted again {page.evaluate('MKB.stepper !== window.__stepper')}")


def third_party_case(browser, base, shots, folder):
    """Optional (MKB_THIRD_PARTY=dir with vmlinuz + initramfs): a distro
    kernel with its initrd prints its boot log on the VGA console."""
    folder = pathlib.Path(folder)
    kernel = next(iter(sorted(folder.glob("vmlinuz*"))), None)
    rd = next(iter(sorted(folder.glob("initramfs*"))), None)
    if not kernel or not rd:
        return [f"no vmlinuz*/initramfs* in {folder}"], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0&cmdline=console%3Dtty0")
    wait_for(lambda: gate_on_screen(page), 60)
    page.set_input_files("#initrd_file", str(rd))
    page.set_input_files("#file", str(kernel))
    t0 = time.monotonic()
    ok = wait_for(lambda: "] " in page.inner_text("#screen_container") and "Linux" in page.inner_text("#screen_container")
                  or "[    " in page.inner_text("#screen_container"), 180, 1)
    r = {"kernel": kernel.name, "initrd": rd.name, "seconds": round(time.monotonic() - t0),
         "ram": page.eval_on_selector("#memory", "e => e.value"),
         "screen": page.inner_text("#screen_container").splitlines()[:3]}
    page.screenshot(path=str(shots / "third-party.png"))
    page.close()
    return ([] if ok else ["no kernel log on the screen"]), r
