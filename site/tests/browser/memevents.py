"""Browser test suite memevents (test_browser.py --suites)."""

import time

from tests.browser.common import (
    dbg_shown_once, debian_names, JITDET_CAP, JITDET_WATCH_JS, machine_do, mev_page, replay_wait, report, show_tab, wait_for,
)


# ---- memevents: who touched which physical address, when (M1) --------------
# v86-det.patch reports the CPU's accesses outside RAM (interpreter and
# compiled code, with the exact count) and routes every other RAM access
# through one API in libv86's cpu.js (devices: dma_*, the emulator: emu_*);
# memevents.js keeps them in a columnar ring with region names from the
# standards the guest used (IBM PC map, E820, PCI configuration, ACPI or
# MultiProcessor tables, IA32_APIC_BASE). The guarantee: in checking mode
# every RAM page is hashed at intervals, and a page that changed with no CPU
# write (v86.wasm marks the pages the CPU may write) and no device or
# emulator event fails, on hello, the minified Linux (-O0, -Os) and Debian;
# a write behind the API's back must be caught (page, interval). The events
# repeat: two loads give the same log, a step back lands at an event's very
# instruction (the JIT recorded it) and running on records the same events
# again, and a replay from boot records them too. The cost: Debian at full
# JIT speed with the list open against closed (reported).
MEV_CHECK_RUNS = [("hello", 200000), ("minified-linux", 200000), ("minified-linux-os", 200000), ("debian-6.1-686", 100000000)]
MEV_DIGEST_JS = """(to) => { const c = MKB.MEV.cols, m = MKB.MEV.cap - 1; let h = 0x811c9dc5, n = 0;
  const mix = (v) => { h = Math.imul(h ^ (v >>> 0), 0x01000193); h = Math.imul(h ^ Math.floor(v / 4294967296), 0x01000193); };
  for (const i of MKB.MEV.query(to === null ? {} : { to })) { const k = i & m; mix(c.pos[k]); mix(c.meta[k]); mix(c.addr[k]); mix(c.size[k]);
    mix(c.val[k]); mix(c.valHi[k]); mix(c.eip[k]); mix(c.eipPhys[k]); mix(c.src[k]); for (const ch of MKB.MEV.nameOf(c.region[k]).name) mix(ch.charCodeAt(0)); n++; }
  return { n, hash: (h >>> 0).toString(16), kept: MKB.MEV.n - MKB.MEV.first(), dropped: MKB.MEV.first() }; }"""
MEV_SUMMARY_JS = """() => { const ev = MKB.MEV.query().map((i) => MKB.MEV.event(i)); const by = (f) => ev.filter(f);
  const cpu = by((e) => e.initiator === 'cpu'), emu = by((e) => e.initiator === 'emulator');
  return { n: ev.length, counts: MKB.MEV.counts.slice(), emulator: emu.map((e) => [e.pos, e.addr, e.bytes, e.source, e.regionName]),
    cpuRegions: [...new Set(cpu.map((e) => e.regionName))], unnamed: cpu.filter((e) => !MKB.MEV.nameOf(e.region).known).length,
    vga: cpu.filter((e) => e.write && e.addr >= 0xb8000 && e.addr < 0xc0000).length,
    described: cpu.slice(-3).map((e) => MKB.MEV.describe(e)), facts: MKB.MEV.facts.map((f) => f.text), e820: MKB.MEV.e820.length,
    firmware: MKB.MEV.facts.filter((f) => f.kind === 'firmware').map((f) => f.text),
    check: MKB.MEV.check && { checks: MKB.MEV.check.checks, changed: MKB.MEV.check.changed, failures: MKB.MEV.check.failures } }; }"""


def mev_to_end(page, base, name, q, timeout=600):
    page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10&{q}", timeout=180000)
    return wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), timeout, 0.5)


def mev_check_case(browser, base, shots, name, every, problems, r):
    """Checking mode to the end: no page changed without an author."""
    ctx, page, errs = mev_page(browser)
    try:
        if not mev_to_end(page, base, name, f"memcheck={every}"):
            problems.append(f"{name}: no end with ?memcheck")
            return
        time.sleep(0.5)
        page.evaluate("() => { MKB.MEV.check.next = 0; MKB.MEV.tick(MKB.stepper); }")  # the stretch since the last check
        s = page.evaluate(MEV_SUMMARY_JS)
        r[name] = {k: s[k] for k in ("n", "counts", "check", "cpuRegions", "unnamed", "vga", "e820", "firmware", "described")}
        c = s["check"] or {}
        if c.get("failures"):
            problems.append(f"{name}: pages changed with no CPU write and no event: {c['failures'][:5]}")
        if not c.get("checks", 0) >= 3 or not c.get("changed"):
            problems.append(f"{name}: the check ran {c.get('checks')} times, {c.get('changed')} pages changed")
        reasons = [e[3] for e in s["emulator"]]
        for want in ("BIOS image", "video BIOS image", "kernel real-mode part", "kernel protected-mode part", "kernel command line"):
            if want not in reasons:
                problems.append(f"{name}: no emulator event {want!r} (the loader): {s['emulator'][:6]}")
        if any(e[0] != 0 for e in s["emulator"]):
            problems.append(f"{name}: the loader's events are not at count 0: {s['emulator'][:6]}")
        if s["unnamed"]:
            problems.append(f"{name}: {s['unnamed']} CPU events at addresses no standard named: {s['cpuRegions']}")
        if not s["vga"] or "colour text (CGA) video memory" not in s["cpuRegions"]:
            problems.append(f"{name}: no CPU write to the colour text memory named by the IBM PC map: {s['cpuRegions']}")
        if not any(" EIP " in d and ".c:" in d for d in s["described"]):
            problems.append(f"{name}: a CPU event does not name its function and line: {s['described']}")
        if name.startswith("debian"):
            if s["e820"] < 3 or not any("usable RAM" in f for f in s["facts"]):
                problems.append(f"{name}: {s['e820']} E820 answers observed: {s['facts'][:8]}")
            if not any("mass storage controller, IDE (PCI class 01:01)" in f for f in s["facts"]):
                problems.append(f"{name}: the IDE controller's class was not observed: {s['facts'][:8]}")
            if not s["firmware"] or not any(x.startswith("I/O APIC") for x in s["cpuRegions"]):
                problems.append(f"{name}: the I/O APIC is not named from a firmware table: {s['firmware']} {s['cpuRegions']}")
        if errs:
            problems.append(f"{name}: page errors {errs[:3]}")
    finally:
        ctx.close()


def mev_negative_case(browser, base, shots, problems, r):
    """A write behind the API's back is caught; the API's writes are not."""
    ctx, page, errs = mev_page(browser)
    try:
        if not mev_to_end(page, base, "hello", "memcheck=200000"):
            problems.append("negative: hello did not end")
            return
        out = page.evaluate("""() => { const cpu = MKB.MEV.cpu; MKB.MEV.check.next = 0; MKB.MEV.tick(MKB.stepper); const before = MKB.MEV.check.failures.length;
          cpu.mem8[0x7123] ^= 0x5a;                                   // behind the API's back
          MKB.emulator.write_memory(new Uint8Array([1, 2, 3]), 0x9000);   // the embedding page: an emulator event
          cpu.dma_write(0x1e << 3, new Uint8Array([4, 5, 6]), 0xa010); // a device (the IDE controller's PCI address)
          cpu.dma_write32(0x1e << 3, 0xb000, 0x12345678);
          const v = cpu.dma_read32s(0x1e << 3, 0xb000), b = cpu.dma_read16(0x10002, 0xb000), z = cpu.dma_read8(0x10002, 0xb003);
          cpu.dma_write16(0x10002, 0xc000, 0xbeef); cpu.dma_zero(0x10002, 0xd000, 4096); const rd = Array.from(cpu.dma_read(0x10002, 0xc000, 2));
          MKB.MEV.check.next = 0; MKB.MEV.tick(MKB.stepper);
          const ev = MKB.MEV.query({ limit: 9 }).map((i) => MKB.MEV.event(i));
          return { before, failures: MKB.MEV.check.failures.slice(before), v: v >>> 0, b, z, rd,
            events: ev.map((e) => [e.initiator, e.write, e.addr, e.size, e.value === undefined ? null : e.value, e.bytes === undefined ? null : e.bytes, e.source]),
            pages: [0xa, 0xb, 0xc, 0xd, 0x9].map((p) => MKB.MEV.query({ page: p }).length) }; }""")
        r["negative"] = out
        got = sorted({f["page"] for f in out["failures"]})
        if got != [7]:
            problems.append(f"negative: the unlogged write to page 7 should be the only failure, got pages {got}: {out['failures'][:4]}")
        if out["v"] != 0x12345678 or out["b"] != 0x5678 or out["z"] != 0x12 or out["rd"] != [0xef, 0xbe]:
            problems.append(f"negative: the device API read back {out['v']:#x} {out['b']:#x} {out['z']:#x} {out['rd']}")
        srcs = [e[6] for e in out["events"]]
        if "embedding page" not in srcs or "PCI 00:1e.0" not in srcs or "ISA DMA (8237) channel 2" not in srcs:
            problems.append(f"negative: the events do not name their sources: {out['events']}")
        if min(out["pages"]) < 1:
            problems.append(f"negative: query by page found {out['pages']} events on pages 0xa, 0xb, 0xc, 0xd, 0x9")
        if errs:
            problems.append(f"negative: page errors {errs[:3]}")
    finally:
        ctx.close()


def mev_repeat_case(browser, base, shots, problems, r):
    """Two loads, a step back to an event recorded by the JIT, the run on,
    and a replay from boot: the same events."""
    name = "minified-linux"
    digests = []
    for i in range(2):
        ctx, page, errs = mev_page(browser, "dark" if i else "light", phone=bool(i))
        try:
            if not mev_to_end(page, base, name, "memevents=1"):
                problems.append(f"repeat {i}: no end")
                return
            time.sleep(0.5)
            digests.append(page.evaluate(MEV_DIGEST_JS, None))
            if i == 0:
                continue
            dbg_shown_once(page)  # #dbg_status (read after each step back) is there once the Debugger tab has been shown
            N = page.evaluate("MKB.stepper.steps")
            entry = page.evaluate("MKB.REV.cps.length ? MKB.REV.cps[0].steps : null")
            r["paused"] = {"steps": N, "history": entry}
            picks = page.evaluate("""(from) => { const ev = MKB.MEV.query({ from, initiator: 'cpu', write: true }).map((i) => MKB.MEV.event(i))
                .filter((e, k, a) => !k || a[k - 1].pos !== e.pos); // one per instruction (a rep movs writes many)
              return [0.1, 0.5, 0.9].map((f) => ev[Math.floor(f * (ev.length - 1))]).filter(Boolean).map((e) => ({ pos: e.pos, eip: e.eip, addr: e.addr })); }""",
                                  (entry or 0) + 1)
            r["picks"] = picks
            if len(picks) < 3:
                problems.append(f"repeat: {len(picks)} CPU writes in the reverse history from {entry}")
                return
            cur = N
            for e in reversed(picks):
                page.evaluate("(n) => MKB.revStepBack(n)", cur - e["pos"])
                wait_for(lambda: page.evaluate("!MKB.REV.busy && MKB.runState === 'paused'"), 300, 0.1)
                at = page.evaluate("""() => ({ steps: MKB.stepper.steps, eip: MKB.stepper.cpu.instruction_pointer[0] >>> 0, last: MKB.MEV.n ? MKB.MEV.event(MKB.MEV.n - 1).pos : -1,
                  status: document.getElementById('dbg_status').textContent.slice(0, 200) })""")
                cur = at["steps"]
                r.setdefault("back", []).append(at)
                if at["steps"] != e["pos"] or at["eip"] != e["eip"]:
                    problems.append(f"repeat: stepped back to {at['steps']:,} EIP {at['eip']:#x}, the event is at {e['pos']:,} EIP {e['eip']:#x}")
                if at["last"] >= e["pos"]:
                    problems.append(f"repeat: after the step back to {e['pos']:,} the log still has an event at {at['last']:,}")
            page.evaluate(f"MKB.doStep({N} - MKB.stepper.steps)")
            wait_for(lambda: page.evaluate(f"MKB.stepper.steps === {N} && !MKB.REV.busy && MKB.runState === 'paused'"), 300, 0.2)
            again = page.evaluate(MEV_DIGEST_JS, None)
            r["forward again"] = again
            if again != digests[1]:
                problems.append(f"repeat: forward to {N:,} again: {again} vs the run {digests[1]}")
            orig = page.evaluate(MEV_DIGEST_JS, N)
            run = page.evaluate("MKB.runNow()")
            page.evaluate("(run) => { MKB.replayRun(run); }", run)
            if not replay_wait(page, 600):
                problems.append("repeat: the replay from boot did not finish")
            else:
                rep = page.evaluate(MEV_DIGEST_JS, N)
                r["replay"] = {"run": orig, "replay": rep, "steps": page.evaluate("MKB.stepper.steps")}
                if rep != orig:
                    problems.append(f"repeat: the replay from boot recorded {rep}, the run {orig}")
            page.screenshot(path=str(shots / "memevents-repeat-390-dark.png"))
            if errs:
                problems.append(f"repeat: page errors {errs[:3]}")
        finally:
            ctx.close()
    r["loads"] = digests
    if len(digests) == 2 and digests[0] != digests[1]:
        problems.append(f"repeat: two loads recorded {digests[0]} and {digests[1]}")


def mev_ui_case(browser, base, shots, problems, r):
    """The Events tab: light and dark, 1280 and 390 px, opened from the
    keyboard; before that, and with Record cleared, nothing is recorded."""
    for scheme, phone in (("light", False), ("dark", True)):
        w = 390 if phone else 1280
        ctx, page, errs = mev_page(browser, scheme, phone)
        try:
            page.goto(f"{base}/index.html?kernel=hello&speed=full&count=0&mips=10", timeout=180000)
            wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 300, 0.5)
            show_tab(page, "memory")  # keys go to the VM while the Machine tab is shown
            closed = page.evaluate("({ on: MKB.MEV.on, n: MKB.MEV.n })")
            page.focus("#tab_events")
            page.keyboard.press("Enter")
            wait_for(lambda: page.evaluate("MKB.MEV.on && !document.getElementById('panel_events').hidden"), 10)
            time.sleep(0.6)
            look = page.evaluate("""() => ({ on: MKB.MEV.on, items: document.querySelectorAll('#mev_rows tr').length,
              regions: document.querySelectorAll('#mev_regions tr').length, status: document.getElementById('mev_status').textContent,
              first: (document.querySelector('#mev_rows tr') || {}).textContent || '', hscroll: document.documentElement.scrollWidth > innerWidth + 1 })""")
            page.screenshot(path=str(shots / f"memevents-list-{w}-{scheme}.png"))
            r[f"ui {w} {scheme}"] = {"closed": closed, **{k: look[k] for k in ("items", "regions", "status", "first", "hscroll")}}
            if closed["on"] or closed["n"] > 5:
                problems.append(f"ui {w}: before the Events tab, the page records: {closed}")
            # hello's run has ended: the loader's five events only
            if not look["on"] or look["items"] < 5 or look["regions"] < 8:
                problems.append(f"ui {w}: opened from the keyboard: {look}")
            if "Recording" not in look["status"] or "5 by the emulator" not in look["status"]:
                problems.append(f"ui {w}: status {look['status']!r}")
            if look["hscroll"]:
                problems.append(f"ui {w}: horizontal scroll")
            page.uncheck("#mev_on")
            if not wait_for(lambda: not page.evaluate("MKB.MEV.on"), 10):
                problems.append(f"ui {w}: Record cleared, still recording")
            if errs:
                problems.append(f"ui {w}: page errors {errs[:3]}")
        finally:
            ctx.close()


def mev_speed_case(browser, base, shots, debian, problems, r):
    """Debian at full JIT speed to JITDET_CAP, the list closed and open."""
    if not debian:
        return
    name = debian[0]
    for tag, q in (("closed", ""), ("open", "&memevents=1")):
        ctx, page, errs = mev_page(browser)
        try:
            page.goto(f"{base}/index.html?kernel={name}&paused=1&speed=full&count=0&mips=10{q}", timeout=180000)
            if not wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused'"), 180, 0.5):
                problems.append(f"speed {tag}: not paused at the reset vector")
                continue
            page.evaluate(JITDET_WATCH_JS, [JITDET_CAP, True])
            machine_do(page, "click", "#dbg_continue")
            if not wait_for(lambda: page.evaluate("window.__jd.done"), 400, 0.2):
                problems.append(f"speed {tag}: did not reach {JITDET_CAP:,}")
                continue
            s = page.evaluate("() => { const w = window.__jd; return { mips: +(((w.c - w.c0) >>> 0) / (w.at - w.t0) / 1000).toFixed(1), events: MKB.MEV.n }; }")
            r[f"speed {tag}"] = s
            if errs:
                problems.append(f"speed {tag}: page errors {errs[:3]}")
        finally:
            ctx.close()
    a, b = r.get("speed closed"), r.get("speed open")
    if a and b:
        r["speed open / closed"] = round(b["mips"] / a["mips"], 3)
        if a["events"] > 10:
            problems.append(f"speed: with the list closed {a['events']} events were recorded")
        if not b["events"]:
            problems.append("speed: with the list open nothing was recorded")


MEV_TABLES_JS = """() => { const m = MKB.MEV.cpu.mem8, out = {};
  const w32 = (k, v) => { m[k] = v; m[k + 1] = v >>> 8; m[k + 2] = v >>> 16; m[k + 3] = v >>> 24; };
  const str = (k, s) => [...s].forEach((c, j) => { m[k + j] = c.charCodeAt(0); });
  const named = (a) => { const r = MKB.MEV.nameOf(MKB.MEV.regionOf(a)); return [r.name, r.std, r.known]; };
  // A PCI class the class table does not have.
  out.pci = MKB.pciClassName(0xfe, 0x00);
  // An E820 answer with the carry flag set (an error), and one without the
  // SMAP signature: no entry; then a good one: an entry.
  const e820 = [MKB.MEV.e820.length];
  for (const info of [0x300, 0x000, 0x100]) {
    MKB.MEV.observe({ pos: MKB.stepper.steps, kind: 'e820', buf: 0, ebx: 0, info, bytes: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0] });
    e820.push(MKB.MEV.e820.length);
  }
  out.e820 = e820;
  // Above RAM, before any standard names it.
  out.none = named(0x40000000);
  // Filters of query(): by initiator and by region.
  const all = MKB.MEV.query().map((i) => MKB.MEV.event(i));
  const vga = MKB.MEV.regionOf(0xb8000);
  out.query = [MKB.MEV.query({ initiator: 'cpu' }).length, all.filter((e) => e.initiator === 'cpu').length,
    MKB.MEV.query({ region: vga }).length, all.filter((e) => e.region === vga).length, all.length];
  // ACPI tables (ACPI 6.x 5.2.5-5.2.12): an RSDP in the EBDA's first KiB, its
  // RSDT with a MADT (a processor entry, then an I/O APIC) and an HPET table.
  const ebda = (m[0x40e] | m[0x40f] << 8) << 4, R = ebda || 0xe0000, T = 0x70000, A = 0x70100, H = 0x70200;
  str(R, 'RSD PTR '); w32(R + 16, T);
  str(T, 'RSDT'); w32(T + 4, 44); w32(T + 36, A); w32(T + 40, H);
  str(A, 'APIC'); w32(A + 4, 44 + 8 + 12); w32(A + 36, 0xfee00000);
  m[A + 44] = 0; m[A + 45] = 8;
  m[A + 52] = 1; m[A + 53] = 12; m[A + 54] = 5; w32(A + 56, 0xfec00000); w32(A + 60, 0);
  str(H, 'HPET'); w32(H + 44, 0xfed00000);
  MKB.MEV.acpi = null;
  MKB.MEV.acpiTried = -1e15;
  MKB.MEV.acpiTry(MKB.stepper.steps);
  out.firmware = MKB.MEV.facts.filter((f) => f.kind === 'firmware').map((f) => f.text);
  out.hpet = named(0xfed00010);
  out.ioapic = named(0xfec00010);
  out.lapic = named(0xfee00010);
  return out; }"""


def mev_tables_case(browser, base, shots, problems, r):
    """hello at its end: an unknown PCI class, E820 answers that are errors,
    an address no standard names, query() by initiator and region, and ACPI
    tables (RSDP, RSDT, MADT, HPET) written into RAM, which name the local
    APIC, the I/O APIC and the HPET by ACPI (v86's SeaBIOS has no ACPI)."""
    ctx, page, errs = mev_page(browser)
    try:
        if not mev_to_end(page, base, "hello", "memevents=1"):
            problems.append("tables: hello did not end")
            return
        out = page.evaluate(MEV_TABLES_JS)
        r["tables"] = out
        if out["pci"] != "class fe:00 (PCI class table)":
            problems.append(f"tables: unknown PCI class named {out['pci']!r}")
        e = out["e820"]
        if e[1:] != [e[0], e[0], e[0] + 1]:
            problems.append(f"tables: E820 entries {e[0]}, after an error answer {e[1]}, one without SMAP {e[2]}, a good one {e[3]}")
        if out["none"] != ["no standard the guest used names this address yet", "", False]:
            problems.append(f"tables: 0x40000000 is named {out['none']}")
        q = out["query"]
        if q[0] != q[1] or q[2] != q[3] or not 0 < q[0] < q[4] or not q[2]:
            problems.append(f"tables: query by initiator {q[0]} vs {q[1]}, by region {q[2]} vs {q[3]}, of {q[4]}")
        want = "ACPI MADT and ACPI HPET table: local APIC 0xfee00000; I/O APIC 5 0xfec00000; HPET 0xfed00000"
        if want not in out["firmware"]:
            problems.append(f"tables: the firmware facts {out['firmware']}, want {want!r}")
        # The local APIC at its power-up address is named by the table that
        # says where it is, not by the IA32_APIC_BASE default made first.
        if out["hpet"] != ["HPET", "ACPI HPET table", True] or out["ioapic"] != ["I/O APIC 5 (GSI 0 on)", "ACPI MADT", True] or \
                out["lapic"] != ["local APIC", "ACPI MADT", True]:
            problems.append(f"tables: regions {out['hpet']} {out['ioapic']} {out['lapic']}")
        if errs:
            problems.append(f"tables: page errors {errs[:3]}")
    finally:
        ctx.close()


def suite_memevents(ctx):
    problems, r = [], {}
    mev_tables_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    for name, every in MEV_CHECK_RUNS:
        if name in ctx.entries:
            mev_check_case(ctx.browser, ctx.base, ctx.shots, name, every, problems, r)
    mev_negative_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    if "minified-linux" in ctx.entries:
        mev_repeat_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    mev_ui_case(ctx.browser, ctx.base, ctx.shots, problems, r)
    mev_speed_case(ctx.browser, ctx.base, ctx.shots, debian_names(ctx), problems, r)
    return report("memevents", problems, r)
