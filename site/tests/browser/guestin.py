"""Browser test suite guestin (test_browser.py --suites)."""

import datetime
import re
import time

from tests.browser.common import (
    debian_names, machine_do, PHONE, report, show_tab, snap_click, snap_rows, snap_save, snap_wait_rows, wait_for,
)


# ---- guestin: the guest's date and random numbers behind the virtual clock (J2)
# D0 (2026-11-10): on the virtual clock the stepped machine is the same in
# every run except for two inputs from the browser. v86's RTC takes its date
# from Date.now() (SeaBIOS turns it into the BIOS tick count at 0x46C, the
# only RAM difference for the small kernels) and RDRAND takes crypto's random
# numbers (Debian uses it: ~50 pages differ 30 M instructions after its
# entry). With the virtual clock the RTC runs from a start date (?rtc=,
# default GUESTIN_DEFAULT_RTC) on the virtual time, RDRAND and the NIC's MAC
# come from a seeded generator (?seed=), and a snapshot keeps both. No
# init script freezes Date or crypto here.
GUESTIN_DEFAULT_RTC = "2026-01-01T00:00:00Z"
GUESTIN_RTC = "2031-02-03T04:05:06Z"
GUESTIN_DEBIAN_STEPS = 30_000_000
GUESTIN_STATE_JS = """async () => { const m = MKB.emulator.read_memory(0, MKB.emuRam), c = MKB.emulator.v86.cpu;
  const sha = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', m))).map((b) => b.toString(16).padStart(2, '0')).join('');
  const el = document.getElementById('guest_in');
  return { ram: sha.slice(0, 16), bda: Array.from(m.subarray(0x46c, 0x470)), ticks: (m[0x46c] | m[0x46d] << 8 | m[0x46e] << 16 | m[0x46f] << 24) >>> 0,
    eip: (c.instruction_pointer[0] >>> 0).toString(16), steps: MKB.stepper ? MKB.stepper.steps : null, clock: MKB.CLOCK.now(), virtual: MKB.CLOCK.virtual,
    rtcTime: c.devices.rtc.rtc_time, browserNow: Date.now(), line: el ? el.textContent : null, lineShown: !!el && el.offsetParent !== null,
    guest: typeof window.MKB?.GUEST === 'object' ? { start: MKB.GUEST.rtcStart, seed: MKB.GUEST.seed, rng: MKB.GUEST.rng } : null }; }"""
GUESTIN_DATE_RE = re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +\d+ \d\d:\d\d:\d\d UTC \d{4}\b")
# BusyBox hwclock: "Mon Feb  3 04:11:14 2031  0.000000 seconds"
GUESTIN_HWCLOCK_RE = re.compile(r"\b(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) +\d+ \d\d:\d\d:\d\d \d{4}(?= +-?[\d.]+ seconds)")


def iso_ms(iso):
    return int(datetime.datetime.fromisoformat(iso.replace("Z", "+00:00")).timestamp() * 1000)


def bios_ticks(iso):
    """SeaBIOS's tick count at 0x46C for an RTC time of day (clock_setup:
    seconds since midnight x PIT_TICK_RATE / PIT_TICK_INTERVAL)."""
    t = datetime.datetime.fromisoformat(iso.replace("Z", "+00:00"))
    return (t.hour * 3600 + t.minute * 60 + t.second) * 1193180 // 65536


def guestin_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def guestin_hello(browser, base, shots, query, scheme, phone, tag, problems):
    """hello booted on the given query to its halt; the machine state."""
    ctx = guestin_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello{query}", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end in 120 s")
            return None
        time.sleep(0.5)
        out = page.evaluate(GUESTIN_STATE_JS)
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"guestin-{tag}.png"))
        if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
            problems.append(f"{tag}: horizontal scroll")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def guestin_debian_stepped(browser, base, shots, name, scheme, phone, tag, problems):
    """Debian: JIT fast-forward to startup_32, then GUESTIN_DEBIAN_STEPS
    stepped exactly; the machine state."""
    ctx = guestin_context(browser, scheme, phone)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1&mips=10#debugger", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no fast-forward to the kernel entry in 300 s")
            return None
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        target = page.evaluate("MKB.stepper.sinceEntry") + GUESTIN_DEBIAN_STEPS
        for _ in range(400):
            left = target - page.evaluate("MKB.stepper.steps")
            if left <= 0:
                break
            page.evaluate(f"MKB.doStep({min(left, 2_000_000)})")
        out = page.evaluate(GUESTIN_STATE_JS)
        if out["steps"] != target:
            problems.append(f"{tag}: stepped to {out['steps']:,}, not {target:,}")
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"guestin-{tag}.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
        return out
    finally:
        ctx.close()


def guestin_snapshot(browser, base, shots, problems, r):
    """A snapshot keeps the start date and the generator's state: saved
    with ?rtc=GUESTIN_RTC&seed=5, restored after a reload with another
    date and seed, both come back."""
    tag = "snapshot"
    ctx = guestin_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&rtc={GUESTIN_RTC}&seed=5", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end in 120 s")
            return
        st = snap_save(page, 1, False)
        row = st["rows"][0]["id"]
        saved = page.evaluate("(() => { const m = MKB.snapshots[0].meta; return { rtcStart: m.rtcStart, seed: m.seed, rng: m.rng }; })()")
        before = page.evaluate(GUESTIN_STATE_JS)
        r["snapshot_meta"] = saved
        if saved["rtcStart"] != iso_ms(GUESTIN_RTC) or saved["seed"] != 5 or not isinstance(saved["rng"], int):
            problems.append(f"{tag}: snapshot meta has no start date / seed / generator state: {saved}")
        page.goto(f"{base}/index.html?kernel=hello&rtc=2040-06-07T08:09:10Z&seed=9", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end after the reload")
            return
        other = page.evaluate(GUESTIN_STATE_JS)
        if not other["guest"] or other["guest"]["start"] != iso_ms("2040-06-07T08:09:10Z") or other["guest"]["seed"] != 9:
            problems.append(f"{tag}: after the reload ?rtc= / ?seed= not applied: {other['guest']}")
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or
                 "failed" in page.inner_text("#snap_status"), 60)
        if page.get_attribute("#snap_status", "data-restored") != row:
            problems.append(f"{tag}: Restore: {page.inner_text('#snap_status')[:200]}")
            return
        after = page.evaluate(GUESTIN_STATE_JS)
        r["snapshot_restored"] = after["guest"]
        if after["guest"] != {"start": saved["rtcStart"], "seed": 5, "rng": saved["rng"]}:
            problems.append(f"{tag}: after Restore the guest inputs are {after['guest']}, saved {saved}")
        if after["rtcTime"] != before["rtcTime"]:
            problems.append(f"{tag}: the RTC after Restore reads {after['rtcTime']}, at the save {before['rtcTime']}")
        if "2031-02-03" not in (after["line"] or ""):
            problems.append(f"{tag}: the Machine tab does not show the restored start date: {after['line']!r}")
        show_tab(page, "machine")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / "guestin-snapshot-restored.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
    finally:
        ctx.close()


# The RTC's register B and VMware's GETTIME as a guest driver uses them:
# alarm 11:22:33 (BCD) armed with the periodic and update interrupts, then
# the host time and time zone through the backdoor (EAX magic, ECX 23).
GUESTIN_PORTS_JS = """() => { const c = MKB.emulator.v86.cpu, io = c.io, rtc = c.devices.rtc;
  const cmos = (i, v) => { io.port_write8(0x70, i); io.port_write8(0x71, v); };
  cmos(0x01, 0x33); cmos(0x03, 0x22); cmos(0x05, 0x11);
  const guestNow = MKB.GUEST.dateNow();
  cmos(0x0b, rtc.cmos_b | 0x70);
  c.reg32[0] = 0x564d5868; c.reg32[1] = 23;
  const eax = io.port_read32(0x5658) >>> 0;
  return { guestNow, alarm: rtc.next_interrupt_alarm, periodic: rtc.next_interrupt, update: rtc.update_interrupt_time,
    eax, ebx: c.reg32[3] >>> 0, edx: c.reg32[2] | 0, pageTz: new Date().getTimezoneOffset(), dateIsReal: Date === MKB.RealDate }; }"""


def guestin_edges(browser, base, shots, problems, r):
    """hello paused on ?rtc=GUESTIN_RTC, the browser in Asia/Kolkata (UTC
    +5:30): the RTC alarm a guest sets through register B is that day at
    the alarm's time, the periodic and update interrupts start at the
    guest's date, VMware's GETTIME gives the guest's date in seconds and
    microseconds with a time zone offset of 0, and the page's own Date is
    the browser's again afterwards. ?rtc= and ?seed= that cannot be read
    are named on the Machine tab and the defaults used. A snapshot saved on
    the real-time clock: restored on real time, the clock goes back to
    the snapshot's; restored after a reload on the virtual clock, the
    guest's date goes on from the browser date the RTC had at the save,
    the run notes that a snapshot without an input log was restored and the
    note over the screen names the Restore."""
    tag = "edges"
    ctx = browser.new_context(color_scheme="dark", viewport={"width": 1280, "height": 900}, timezone_id="Asia/Kolkata")
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10&rtc={GUESTIN_RTC}", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120):
            problems.append(f"{tag}: hello did not start paused")
            return
        x = page.evaluate(GUESTIN_PORTS_JS)
        r["ports"] = x
        start = datetime.datetime.fromisoformat(GUESTIN_RTC.replace("Z", "+00:00"))
        alarm = int(start.replace(hour=11, minute=22, second=33).timestamp() * 1000)
        if x["alarm"] != alarm:
            problems.append(f"{tag}: the RTC alarm 11:22:33 set through register B is at {x['alarm']}, want {alarm} (that day, UTC)")
        if not (iso_ms(GUESTIN_RTC) <= x["guestNow"] < iso_ms(GUESTIN_RTC) + 1000) or x["periodic"] != x["guestNow"] or \
                x["update"] != x["guestNow"]:
            problems.append(f"{tag}: periodic / update interrupts from {x['periodic']} / {x['update']}, the guest's date {x['guestNow']}")
        if x["eax"] != x["guestNow"] // 1000 or x["ebx"] != x["guestNow"] % 1000 * 1000 or x["edx"] != 0:
            problems.append(f"{tag}: VMware GETTIME gave {x['eax']} s + {x['ebx']} us, zone {x['edx']} min; "
                            f"want {x['guestNow'] // 1000} s + {x['guestNow'] % 1000 * 1000} us, zone 0")
        if x["pageTz"] != -330 or not x["dateIsReal"]:
            problems.append(f"{tag}: after the guest's I/O the page's Date has offset {x['pageTz']} (want -330), is the browser's: {x['dateIsReal']}")

        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10&rtc=yesterday&seed=lots", timeout=120000)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        show_tab(page, "machine")
        line = page.text_content("#guest_in_text")
        r["unreadable"] = line
        want = (f"?rtc=yesterday is not a date (ISO 8601, e.g. {GUESTIN_DEFAULT_RTC}). ?seed=lots is not a number. RTC 2026-01-01 00:00:00 UTC "
                "(from 2026-01-01 00:00:00 UTC) · RDRAND seed 1")
        if line != want:
            problems.append(f"{tag}: ?rtc=yesterday&seed=lots: the Machine tab says {line!r}, want {want!r}")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / "guestin-edges-unreadable.png"))

        # A snapshot on the real-time clock.
        page.goto(f"{base}/index.html?kernel=hello&mips=real", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello on real time did not end in 120 s")
            return
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        st = snap_save(page, 1, False)
        row = st["rows"][0]["id"]
        saved = page.evaluate("({ clock: MKB.snapshots[0].meta.clock, time: MKB.snapshots[0].time, rtc: MKB.emulator.v86.cpu.devices.rtc.last_update, "
                              "guest: 'rtcStart' in MKB.snapshots[0].meta })")
        time.sleep(8)
        later = page.evaluate("MKB.CLOCK.now()")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or "failed" in page.text_content("#snap_status"), 30)
        back = page.evaluate("MKB.CLOCK.now()")
        r["real time restore"] = {"saved": saved, "before": later - saved["clock"], "after": back - saved["clock"]}
        if saved["guest"] or page.get_attribute("#snap_status", "data-restored") != row or later - saved["clock"] < 8000 or \
                not (0 <= back - saved["clock"] < 4000):
            problems.append(f"{tag}: restored on real time: the clock {later - saved['clock']:.0f} ms past the snapshot's before, "
                            f"{back - saved['clock']:.0f} ms after (want back at it); status {page.text_content('#snap_status')[:120]!r}")

        page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10", timeout=120000)
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        show_tab(page, "snapshots")
        snap_wait_rows(page, 1)
        wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.restore", False)
        wait_for(lambda: page.get_attribute("#snap_status", "data-restored") == row or "failed" in page.text_content("#snap_status"), 30)
        g = page.evaluate("({ now: MKB.GUEST.dateNow(), clock: MKB.CLOCK.now(), virtual: MKB.CLOCK.virtual, taint: MKB.INPUT.taint, steps: MKB.stepper.steps, "
                          "run: document.getElementById('run_text').textContent })")
        show_tab(page, "machine")
        line = page.text_content("#guest_in_text")
        # The page started paused at the reset vector; the note over the
        # screen is the Restore's now (FAILS on 8c05ba7fe: "Paused at the
        # reset vector." stayed over hello's end).
        reason = page.text_content("#vm_reason") if page.is_visible("#vm_overlay") else None
        r["virtual restore note"] = reason
        if reason != f"Restored the snapshot “{st['rows'][0]['name']}”.":
            problems.append(f"{tag}: after a Restore in a page started paused, the note over the screen says {reason!r}")
        r["virtual restore"] = {"guest_minus_rtc": g["now"] - saved["rtc"], "line": line, "taint": g["taint"]}
        iso = lambda ms: datetime.datetime.fromtimestamp(ms / 1000, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        if page.get_attribute("#snap_status", "data-restored") != row or not g["virtual"] or not (0 <= g["now"] - saved["rtc"] < 1000):
            problems.append(f"{tag}: restored on the virtual clock: the guest's date {g['now']}, the RTC's at the save {saved['rtc']} "
                            f"(want it to go on from there); status {page.text_content('#snap_status')[:120]!r}")
        if f"(from {iso(g['now'] - g['clock'])})" not in line or not line.startswith("RTC " + iso(saved["rtc"])[:16]):
            problems.append(f"{tag}: the Machine tab after that Restore: {line!r}")
        why = "a snapshot without an input log was restored"
        if not g["taint"] or g["taint"]["why"] != why or why not in g["run"]:
            problems.append(f"{tag}: the run after restoring a snapshot without an input log: taint {g['taint']}, {g['run']!r}")
        page.eval_on_selector("#vclock", "e => e.scrollIntoView({block: 'center'})")
        time.sleep(0.3)
        page.screenshot(path=str(shots / "guestin-edges-realtime-restored.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
    finally:
        ctx.close()


def guestin_shell_dates(browser, base, shots, name, problems, r):
    """Debian with its initramfs at full speed on ?rtc=GUESTIN_RTC: at the
    (initramfs) prompt `hwclock -u` (the guest reads the CMOS clock) and
    `date -u` (the kernel's time, set from it at boot) read the start date
    plus the virtual time at that moment, and a second hwclock a few seconds
    later has moved as far as the virtual clock."""
    tag = "shell-date"
    ctx = guestin_context(browser, "dark", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10&rtc={GUESTIN_RTC}", timeout=120000)
        if not wait_for(lambda: "(initramfs)" in page.inner_text("#screen_container"), 400, 1):
            problems.append(f"{tag}: no (initramfs) shell")
            return
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 10)
        if page.is_visible("#vm_continue"):
            machine_do(page, "click", "#vm_continue")
        page.click("#screen_container")
        time.sleep(0.5)
        start = iso_ms(GUESTIN_RTC) / 1000
        r["shell"] = []

        def read(cmd, rx, fmt):
            """The guest's date printed by cmd, and the virtual clock (s)
            just before Enter and just after the line appeared."""
            n = len(rx.findall(page.inner_text("#screen_container")))
            page.keyboard.type(cmd)
            c0 = page.evaluate("MKB.CLOCK.now()") / 1000
            page.keyboard.press("Enter")
            ok = wait_for(lambda: len(rx.findall(page.inner_text("#screen_container"))) > n, 60, 0.05)
            c1 = page.evaluate("MKB.CLOCK.now()") / 1000
            if not ok:
                problems.append(f"{tag}: `{cmd}` printed no date: {page.inner_text('#screen_container')[-200:]!r}")
                return None
            text = re.sub(" +", " ", rx.findall(page.inner_text("#screen_container"))[-1])
            got = datetime.datetime.strptime(text, fmt).replace(tzinfo=datetime.timezone.utc).timestamp()
            r["shell"].append({"cmd": cmd, "printed": text, "minus_start": got - start, "clock": [round(c0, 2), round(c1, 2)]})
            # The guest prints whole seconds.
            if not (c0 - 1 <= got - start <= c1 + 1):
                problems.append(f"{tag}: `{cmd}` printed {text!r}: {got - start:.0f} s after {GUESTIN_RTC}, "
                                f"the virtual clock was at {c0:.1f}..{c1:.1f} s")
            return got, c0, c1

        a = read("hwclock -u", GUESTIN_HWCLOCK_RE, "%a %b %d %H:%M:%S %Y")
        read("date -u", GUESTIN_DATE_RE, "%a %b %d %H:%M:%S UTC %Y")
        time.sleep(3)
        b = read("hwclock -u", GUESTIN_HWCLOCK_RE, "%a %b %d %H:%M:%S %Y")
        page.screenshot(path=str(shots / "guestin-shell-date.png"))
        if a and b and not (b[1] - a[2] - 1 <= b[0] - a[0] <= b[2] - a[1] + 1):
            problems.append(f"{tag}: between two hwclock readings the guest's date moved {b[0] - a[0]:.0f} s, "
                            f"the virtual clock {b[1] - a[2]:.1f}..{b[2] - a[1]:.1f} s")
        if errs:
            problems.append(f"{tag}: page errors {errs[:2]}")
    finally:
        ctx.close()


def guestin_case(browser, base, shots, entries, debian, initrd):
    problems, r = [], {}
    # hello twice: the same machine at its halt, page 0 included.
    runs = [guestin_hello(browser, base, shots, "&mips=10", scheme, phone, f"hello-{scheme}-{390 if phone else 1280}", problems)
            for scheme, phone in (("light", False), ("dark", True))]
    r["hello"] = [x and {k: x[k] for k in ("ram", "bda", "ticks", "line")} for x in runs]
    if None not in runs:
        a, b = runs
        if a["bda"] != b["bda"]:
            problems.append(f"hello: the BIOS tick count at 0x46C differs: {a['bda']} vs {b['bda']}")
        if a["ram"] != b["ram"]:
            problems.append(f"hello: RAM differs: {a['ram']} vs {b['ram']}")
        if not (bios_ticks(GUESTIN_DEFAULT_RTC) <= a["ticks"] <= bios_ticks(GUESTIN_DEFAULT_RTC) + 20):
            problems.append(f"hello: BIOS tick count {a['ticks']}, not the default start date's {bios_ticks(GUESTIN_DEFAULT_RTC)}")
        for x in runs:
            if not x["line"] or "2026-01-01 00:00:00" not in x["line"] or "seed 1" not in x["line"] or not x["lineShown"]:
                problems.append(f"hello: the Machine tab does not show the default start date and seed: {x['line']!r}")
    # ?rtc= and ?seed=: SeaBIOS reads that time of day from the RTC.
    x = guestin_hello(browser, base, shots, f"&mips=10&rtc={GUESTIN_RTC}&seed=7", "light", False, "hello-rtc", problems)
    if x:
        r["hello_rtc"] = {k: x[k] for k in ("ticks", "line", "guest")}
        want = bios_ticks(GUESTIN_RTC)
        if not (want <= x["ticks"] <= want + 20):
            problems.append(f"hello ?rtc={GUESTIN_RTC}: BIOS tick count {x['ticks']}, want {want} (+ a few)")
        if not x["line"] or "2031-02-03 04:05:06" not in x["line"] or "seed 7" not in x["line"]:
            problems.append(f"hello ?rtc= ?seed=: the Machine tab line {x['line']!r}")
    # Real time keeps the browser's date.
    x = guestin_hello(browser, base, shots, "&mips=real", "dark", True, "hello-realtime", problems)
    if x:
        r["hello_real"] = {"rtc_minus_browser_s": round((x["rtcTime"] - x["browserNow"]) / 1000, 1), "line": x["line"]}
        if abs(x["rtcTime"] - x["browserNow"]) > 120_000:
            problems.append(f"hello on real time: the RTC reads {x['rtcTime']}, the browser {x['browserNow']}")
        if not x["line"] or "browser" not in x["line"]:
            problems.append(f"hello on real time: the Machine tab line {x['line']!r}")
    guestin_snapshot(browser, base, shots, problems, r)
    guestin_edges(browser, base, shots, problems, r)
    if debian:
        runs = [guestin_debian_stepped(browser, base, shots, debian[0], scheme, phone, f"debian-{scheme}-{390 if phone else 1280}", problems)
                for scheme, phone in (("light", False), ("dark", True))]
        r["debian"] = [x and {k: x[k] for k in ("steps", "eip", "ram", "clock")} for x in runs]
        if None not in runs:
            a, b = runs
            for k in ("steps", "eip", "ram", "clock"):
                if a[k] != b[k]:
                    problems.append(f"debian {GUESTIN_DEBIAN_STEPS:,} stepped after the entry: {k} differs: {a[k]} vs {b[k]}")
    if initrd:
        guestin_shell_dates(browser, base, shots, initrd[0], problems, r)
    return problems, r


def suite_guestin(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n]]
    return report("guestin", *guestin_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, debian_names(ctx), initrd))
