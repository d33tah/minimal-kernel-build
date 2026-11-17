"""Browser test suite initramfs (test_browser.py --suites)."""

import gzip
import pathlib
import tempfile
import time

from tests.browser.common import machine_do, report, show_tab, wait_for


def initramfs_case(browser, base, shots, name):
    """A kernel shipped with its initrd (the Debian sample with Debian's
    initramfs) boots to the "(initramfs)" BusyBox shell, which takes
    keyboard input; an initrd for another kernel gets a warning."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    t0 = time.monotonic()
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0")
    ok = wait_for(lambda: "(initramfs)" in page.inner_text("#screen_container"), 300, 1)
    r["seconds_to_shell"] = round(time.monotonic() - t0)
    r["note"] = page.inner_text("#initrd_status")
    r["ram"] = page.eval_on_selector("#memory", "e => e.value")
    if not ok:
        problems.append("no (initramfs) shell: " + page.inner_text("#screen_container")[-300:])
    elif "matches the kernel" not in r["note"]:
        problems.append("the initrd note does not say it matches: " + r["note"])
    if ok:
        # The prompt pauses the VM: overlay with the reason, a counter row.
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        r["state"] = page.get_attribute("#dbg_state", "data-state")
        r["overlay"] = page.inner_text("#vm_overlay") if page.is_visible("#vm_overlay") else None
        r["prompt_row"] = page.get_attribute("#counters tr[data-milestone='prompt']", "data-steps") \
            if page.locator("#counters tr[data-milestone='prompt']").count() else None
        if r["state"] != "paused" or not r["overlay"] or "VM paused" not in r["overlay"] or \
                "(initramfs) prompt" not in r["overlay"] or not page.is_visible("#vm_continue"):
            problems.append(f"no pause with the overlay at the prompt: {r['state']} {r['overlay']!r}")
        if not r["prompt_row"]:
            problems.append("no (initramfs) prompt row in the counters")
        page.set_viewport_size({"width": 390, "height": 900})
        page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
        time.sleep(0.5)
        page.screenshot(path=str(shots / f"initramfs-paused-{name}-390.png"))
        page.set_viewport_size({"width": 1100, "height": 1000})
        machine_do(page, "click", "#vm_continue")
        if not wait_for(lambda: not page.is_visible("#vm_overlay"), 10):
            problems.append("the overlay stays after Continue")
        page.click("#screen_container")
        time.sleep(0.5)
        # Unshifted characters only: Playwright's type() sends no Shift.
        page.keyboard.type("uname -m; echo hi")
        page.keyboard.press("Enter")
        lines = lambda: [l.strip() for l in page.inner_text("#screen_container").splitlines()]
        if not wait_for(lambda: "hi" in lines(), 60):
            problems.append("the shell did not run a typed command")
        r["uname"] = [l for l in lines() if l in ("i686", "i586", "i386")]
        # U5: the screen lives in the Machine tab; after another tab and back
        # the keys typed on the screen still go to the VM.
        show_tab(page, "page")
        time.sleep(0.5)
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.5)
        page.keyboard.type("echo u5back")
        page.keyboard.press("Enter")
        if not wait_for(lambda: "u5back" in lines(), 60):
            problems.append("after the Page tab and back to Machine the shell did not run a typed command")
        r["keys_after_tabs"] = "u5back" in lines()
    page.screenshot(path=str(shots / f"initramfs-{name}.png"))
    # An initrd for another kernel: gzip'd newc cpio with lib/modules of
    # 6.12 amd64 and a 64-bit ELF /bin/busybox, named like Debian's.
    def entry(fname, data, mode):
        n = fname.encode() + b"\0"
        hdr = "070701" + "".join(f"{v:08x}" for v in (1, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(n), 0))
        out = hdr.encode() + n
        out += b"\0" * (-len(out) % 4) + data
        return out + b"\0" * (-len(out) % 4)
    body = (entry("lib/modules/6.12.111+deb13-amd64", b"", 0o40755) +
            entry("bin/busybox", b"\x7fELF\x02\x01\x01" + b"\0" * 57, 0o100755) + entry("TRAILER!!!", b"", 0))
    tmp = pathlib.Path(tempfile.mkdtemp()) / "initrd.img-6.12.111+deb13-amd64"
    tmp.write_bytes(gzip.compress(body))
    page.set_input_files("#initrd_file", str(tmp))
    wait_for(lambda: "Warning" in page.inner_text("#initrd_status"), 20)
    r["mismatch_note"] = page.inner_text("#initrd_status")
    if not all(w in r["mismatch_note"] for w in ("Warning", "6.12.111+deb13-amd64", "64-bit", "6.1.0-50-686", "cannot run")):
        problems.append("no mismatch warning: " + r["mismatch_note"])
    if errors:
        problems.append(f"page errors: {errors}")
    page.close()
    return problems, r


def suite_initramfs(ctx):
    failed = False
    for name in [n for n in ctx.entries if "initrd" in ctx.entries[n] and not ctx.entries[n].get("editable")]:
        failed |= report("initramfs", *initramfs_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed
