"""Browser test suite replay (test_browser.py --suites)."""

import json
import pathlib
import shutil
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import (
    bm_close_menu, bm_dialog_save, bm_open_list, bm_tap, machine_do, PHONE, replay_state, replay_wait, report,
    rev_click, show_tab, snap_click, snap_rows, snap_save, wait_for,
)


REPLAY_SAME = ("steps", "counter", "eip", "ram", "clock", "screen")


def replay_context(browser, scheme, phone):
    return browser.new_context(color_scheme=scheme, accept_downloads=True,
                               **(PHONE if phone else {"viewport": {"width": 1280, "height": 900}}))


def replay_same(a, b, tag, problems):
    for k in REPLAY_SAME:
        if a[k] != b[k]:
            problems.append(f"{tag}: {k} differs from the recorded run: {str(b[k])[:300]} vs {str(a[k])[:300]}")


def replay_lines(page):
    return [l.strip() for l in page.inner_text("#screen_container").splitlines()]


def replay_fit(page, tag, problems):
    if page.evaluate("document.documentElement.scrollWidth > innerWidth + 1"):
        problems.append(f"{tag}: horizontal scroll")


def replay_shots(page, shots, stem):
    show_tab(page, "snapshots")
    page.eval_on_selector("#run_box", "e => e.scrollIntoView()")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"{stem}-snapshots.png"))
    show_tab(page, "machine")
    page.eval_on_selector("#screen_container", "e => e.scrollIntoView()")
    time.sleep(0.3)
    page.screenshot(path=str(shots / f"{stem}-machine.png"))


def replay_initrd(browser, base, shots, name, tmp, problems, r):
    """Debian+initrd at J1's full speed: keys typed at the (initramfs) prompt
    while paused (held until Continue), a Step there, a command typed while
    it runs; then Replay in the page. Returns (run file, recorded state)."""
    tag = f"replay {name} jit 1280 light"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=full&count=0&mips=10", timeout=120000)
        if not wait_for(lambda: page.evaluate("!!MKB.stepper && !!MKB.stepper.prompt && MKB.runState === 'paused'"), 900, 1):
            problems.append(f"{tag}: no pause at the (initramfs) prompt")
            return None, None
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.3)
        page.keyboard.type("echo j4")
        held = page.evaluate("typeof window.MKB?.INPUT === 'object' ? MKB.INPUT.pending.length : -1")
        r["held_while_paused"] = held
        if held != 14:
            problems.append(f"{tag}: 7 keys typed while paused: {held} scancodes held for the next slice, want 14")
        # A Step at the prompt: the run goes on stepped for a moment.
        show_tab(page, "debugger")
        page.fill("#dbg_n", "3")
        page.click("#dbg_stepn")
        time.sleep(0.5)
        machine_do(page, "click", "#vm_continue")
        if not wait_for(lambda: any("echo j4" in l for l in replay_lines(page)), 60):
            problems.append(f"{tag}: the keys typed while paused did not reach the shell")
        show_tab(page, "machine")
        page.click("#screen_container")
        time.sleep(0.3)
        page.keyboard.type(" ok; uname -m")
        page.keyboard.press("Enter")
        if not wait_for(lambda: "j4 ok" in replay_lines(page) and any(l in ("i686", "i586", "i386") for l in replay_lines(page)), 90):
            problems.append(f"{tag}: the typed command did not run: {replay_lines(page)[-6:]}")
        time.sleep(2)
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 10)
        a = replay_state(page)
        r["recorded"] = {k: a[k] for k in ("steps", "counter", "eip", "ram", "entries", "keyEntries", "scancodes", "modes", "firstKeyAt", "prompt")}
        if a["keyEntries"] is None or a["scancodes"] < 2 * len("echo j4 ok; uname -m") + 2:
            problems.append(f"{tag}: the log holds {a['scancodes']} scancodes in {a['keyEntries']} entries, want all typed")
        if a["modes"] != ["jit", "stepped", "jit"]:
            problems.append(f"{tag}: the run's modes are {a['modes']}, want JIT, stepped (Step at the prompt), JIT")
        if a["pending"]:
            problems.append(f"{tag}: {a['pending']} scancodes still held after the run")
        if a["taint"]:
            problems.append(f"{tag}: the run is marked not replayable: {a['taint']}")
        # Download the run.
        show_tab(page, "snapshots")
        path = None
        try:
            with page.expect_download(timeout=30000) as d:
                page.click("#run_dl")
            path = tmp / d.value.suggested_filename
            d.value.save_as(path)
            run = json.loads(path.read_text())
            r["file"] = {"name": path.name, "bytes": path.stat().st_size, "entries": len(run.get("log", []))}
            if run.get("format") != "mkb-run" or run.get("kernel") != name or len(run.get("log", [])) != a["entries"] or \
                    (run.get("end") or {}).get("at") != a["steps"]:
                problems.append(f"{tag}: the downloaded run does not match the page: "
                                f"{ {k: run.get(k) for k in ('format', 'kernel', 'end')} }, {len(run.get('log', []))} entries")
        except (PlaywrightError, ValueError, OSError) as e:
            problems.append(f"{tag}: Download run: {e}")
        # A snapshot keeps the run.
        snap_save(page, 1, False)
        sr = page.evaluate("(() => { const s = MKB.snapshots[MKB.snapshots.length - 1]; return s.meta && s.meta.run ? "
                           "{ n: s.meta.run.log.length, steps: s.meta.run.end && s.meta.run.end.at } : null; })()")
        r["snapshot_run"] = sr
        if not sr or sr["n"] != a["entries"] or sr["steps"] != a["steps"]:
            problems.append(f"{tag}: the snapshot's run is {sr}, want {a['entries']} entries ending at {a['steps']}")
        if not page.locator("#snapshots button.snapreplay").count():
            problems.append(f"{tag}: the snapshot row has no Replay")
        # Replay here.
        page.click("#run_replay")
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish ({page.evaluate('MKB.runState')}, "
                            f"{page.evaluate('MKB.stepper && MKB.stepper.steps')} steps)")
        b = replay_state(page)
        r["replayed"] = {k: b[k] for k in ("steps", "eip", "ram", "verdict", "replay", "status")}
        replay_same(a, b, tag, problems)
        if b["verdict"] != "match" or not b["replay"] or b["replay"]["late"]:
            problems.append(f"{tag}: the page's replay verdict: {b['verdict']} {b['replay']} {b['status']!r}")
        if not any("j4 ok" == l for l in b["screen"]):
            problems.append(f"{tag}: the replayed screen lacks the command's output")
        replay_shots(page, shots, "replay-initrd-1280-light")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
        return path, a
    finally:
        ctx.close()


def replay_from_file(browser, base, shots, path, a, problems, r):
    """The downloaded run in a fresh page of another kernel (390 px, dark):
    Replay a file switches kernels and ends where the run ended."""
    tag = "replay file 390 dark"
    ctx = replay_context(browser, "dark", True)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&mips=10", timeout=120000)
        wait_for(lambda: page.evaluate("!!MKB.stepper && MKB.runState !== 'off'"), 60)
        show_tab(page, "snapshots")
        page.set_input_files("#run_file", str(path))
        if not replay_wait(page, 900):
            problems.append(f"{tag}: the replay did not finish: {page.inner_text('#run_status')[:200]!r}")
        c = replay_state(page)
        r["file_replayed"] = {k: c[k] for k in ("steps", "eip", "ram", "verdict", "status")}
        replay_same(a, c, tag, problems)
        if c["verdict"] != "match":
            problems.append(f"{tag}: the page's replay verdict: {c['verdict']} {c['status']!r}")
        replay_shots(page, shots, "replay-file-390-dark")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


def replay_stepped(browser, base, shots, name, problems, r):
    """Stepped at 1k/s (390 px, light): keys at moments of wall time; a
    snapshot, more keys, a position bookmark; the snapshot's Replay ends at
    the snapshot, the bookmark's at the bookmark."""
    tag = f"replay {name} stepped 390 light"
    ctx = replay_context(browser, "light", True)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel={name}&speed=1000&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no kernel entry")
            return
        show_tab(page, "machine")
        page.tap("#screen_container")
        for keys in ("ab", "1", "cd"):
            time.sleep(0.6)
            page.keyboard.type(keys)
        time.sleep(0.6)
        machine_do(page, "tap", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 10)
        s1 = replay_state(page)
        snap_save(page, 1, True)
        row = snap_rows(page)["rows"][0]["id"]
        # Continue must not rebase the virtual clock (base + count since
        # baseSteps): a rebase rounds the float, and a replay that does not
        # pause there reads another clock (CI 42945681996: 141.214 vs
        # 141.21399999999997 at the bookmark, both inputs "late").
        ab = page.evaluate("async () => { const a = [MKB.CLOCK.base, MKB.CLOCK.baseSteps, MKB.CLOCK.mips]; await MKB.resume(true); "
                           "return [a, [MKB.CLOCK.base, MKB.CLOCK.baseSteps, MKB.CLOCK.mips]]; }")
        r["clock_continue"] = ab
        if ab[0] != ab[1]:
            problems.append(f"{tag}: Continue rebased the virtual clock: {ab[0]} -> {ab[1]}")
        for _ in range(3):  # more pauses than the replay has
            time.sleep(0.4)
            machine_do(page, "tap", "#dbg_pause")
            wait_for(lambda: page.evaluate("MKB.runState") == "paused", 10)
            machine_do(page, "tap", "#dbg_continue")
        show_tab(page, "machine")
        page.tap("#screen_container")
        for keys in ("x", "yz"):
            time.sleep(0.6)
            page.keyboard.type(keys)
        time.sleep(0.6)
        machine_do(page, "tap", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 10)
        s2 = replay_state(page)
        r["stepped"] = {"s1": {k: s1[k] for k in ("steps", "keyEntries", "modes")}, "s2": {k: s2[k] for k in ("steps", "keyEntries", "modes")}}
        if s1["keyEntries"] < 3 or s2["keyEntries"] <= s1["keyEntries"]:
            problems.append(f"{tag}: key entries {s1['keyEntries']} then {s2['keyEntries']}")
        if s2["modes"] != ["stepped"]:
            problems.append(f"{tag}: modes {s2['modes']}, want stepped only")
        if not bm_open_list(page, True) or not page.locator("#bm_here").count():
            problems.append(f"{tag}: no bookmarks list with the position ☆")
        else:
            bm_tap(page, "#bm_here", True)
            b = bm_dialog_save(page, problems, tag, "pos")
            if b and not (b["t"].get("run") and b["t"]["run"]["end"]["at"] == s2["steps"]):
                problems.append(f"{tag}: the position bookmark has no run ending at {s2['steps']}: {str(b['t'])[:200]}")
            bm_close_menu(page)
        # The snapshot's Replay: boots again and ends at the snapshot.
        show_tab(page, "snapshots")
        snap_click(page, f"#snapshots tr[data-id='{row}'] button.snapreplay", True)
        if not replay_wait(page, 600):
            problems.append(f"{tag}: the snapshot's replay did not finish")
        t1 = replay_state(page)
        replay_same(s1, t1, tag + " (snapshot)", problems)
        if t1["verdict"] != "match":
            problems.append(f"{tag}: snapshot replay verdict {t1['verdict']} {t1['status']!r}")
        replay_shots(page, shots, "replay-stepped-390-light-snapshot")
        # The bookmark's Replay: ends at the bookmark.
        if bm_open_list(page, True) and page.locator("#bm_list .bmreplay").count():
            bm_tap(page, "#bm_list .bmreplay", True)
            bm_close_menu(page)
            if not replay_wait(page, 600):
                problems.append(f"{tag}: the bookmark's replay did not finish")
            t2 = replay_state(page)
            replay_same(s2, t2, tag + " (bookmark)", problems)
            if t2["verdict"] != "match":
                problems.append(f"{tag}: bookmark replay verdict {t2['verdict']} {t2['status']!r}")
            r["stepped"]["replays"] = [{k: t[k] for k in ("steps", "ram", "verdict")} for t in (t1, t2)]
        else:
            problems.append(f"{tag}: the position bookmark has no Replay")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


# R3: Stepper.run(n) whose n ends on the instruction that halts after the
# (initramfs) prompt must stop there with "prompt", as a longer run does. A
# replay clamps its stepping to the next recorded input; when that input is
# due at the prompt's hlt, run() returned "count", the recorded keys went in
# and woke the CPU, and the stepper ran past the prompt (and past the run's
# switch back to the JIT) to the next halt. hello's cli; hlt stands in for
# the shell's idle halt; the prompt's text is marked seen.
REPLAY_EDGE_JS = """async (mode) => {
  const st = MKB.stepper;
  if (mode === 'find') {
    for (let i = 0; i < 100 && !st.cpu.in_hlt[0]; i++) st.run(1e6, -1, 2000);
    return { steps: st.steps, hlt: st.cpu.in_hlt[0] };
  }
  st.textSeen |= MKB.STEP_CONST.TEXT_PROMPT;
  st.prompt = null;
  const r = st.run(mode);
  return { r, steps: st.steps, hlt: st.cpu.in_hlt[0], prompt: st.prompt && st.prompt.steps };
}"""


def replay_prompt_edge(browser, base, shots, problems, r):
    tag = "replay prompt at the end of a clamped run (hello 1280 light)"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        def start():
            page.goto(f"{base}/index.html?kernel=hello&paused=1&mips=10", timeout=120000)
            return wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper && MKB.runState === 'paused'"), 120, 0.5)
        if not start():
            problems.append(f"{tag}: did not start paused")
            return
        h = page.evaluate(REPLAY_EDGE_JS, "find")
        if not h["hlt"]:
            problems.append(f"{tag}: hello did not halt (test setup): {h}")
            return
        out = {}
        for n in (1, 2):
            if not start():
                problems.append(f"{tag}: did not start paused again")
                return
            left = h["steps"] - n
            for _ in range(100):
                d = left - page.evaluate("MKB.stepper.steps")
                if d <= 0:
                    break
                page.evaluate(f"MKB.stepper.run({d}, -1, 2000)")
            if page.evaluate("MKB.stepper.steps") != left:
                problems.append(f"{tag}: could not step to {left} (test setup)")
                return
            out[n] = page.evaluate(REPLAY_EDGE_JS, n)
        r["prompt_edge"] = {"hlt_at": h["steps"], "run": out}
        for n, o in out.items():
            if o["r"] != "prompt" or o["prompt"] != h["steps"] or o["steps"] != h["steps"]:
                problems.append(f"{tag}: run({n}) ending on the hlt at {h['steps']:,} returned {o['r']!r} at {o['steps']:,} "
                                f"(prompt {o['prompt']}), want 'prompt' there")
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()


def replay_refused(browser, base, shots, problems, r):
    """What a replay refuses, and a Step that ends one (the minified Linux at
    1280 light, stepped at 1M/s to its end): a file that is not JSON, a run
    of another format, of format version 0, of a kernel not in the list and
    on a clock the page does not offer each say why and leave the page as
    it was; the run's own replay (at a 20x CPU throttle, so it is still
    stepping when looked at), paused there and stepped, stops following
    its run and says so."""
    tag = "replay refused (minified-linux 1280 light)"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    tmp = pathlib.Path(tempfile.mkdtemp())
    try:
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=1000000&mips=10")
        if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 300, 0.5):
            problems.append(f"{tag}: no kernel entry")
            return
        entry = int(page.get_attribute("#entry_info", "data-steps"))
        # It stops by itself (init exits: a panic) a few seconds later.
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 120)
        run = page.evaluate("MKB.runNow()")
        url = page.url

        def refused(name, body, want):
            f = tmp / name
            f.write_text(body)
            show_tab(page, "snapshots")
            page.set_input_files("#run_file", str(f))
            wait_for(lambda: want in page.text_content("#run_status"), 10)
            st = page.text_content("#run_status")
            if want not in st or page.get_attribute("#run_status", "data-replay") != "error" or \
                    page.url.split("#")[0] != url.split("#")[0]:
                problems.append(f"{tag}: {name}: status {st!r} ({page.get_attribute('#run_status', 'data-replay')}), "
                                f"URL {page.url} (want {want!r}, the page as it was)")
            return st[:120]

        r["refused"] = [
            refused("junk.json", "this is no JSON", "Cannot replay junk.json: not JSON"),
            refused("other.json", json.dumps({"format": "something else"}), "Cannot replay: not a run of this page"),
            refused("v0.json", json.dumps({**run, "version": 0}), "Cannot replay: run format version 0"),
            refused("gone.json", json.dumps({**run, "kernel": "gone-kernel"}),
                    "Cannot replay: “gone-kernel” is not one of the listed kernels"),
            refused("clock.json", json.dumps({**run, "clock": "7"}),
                    "Cannot replay: this page has no clock of 7 instructions per microsecond"),
        ]
        # The run itself, slowed down: pause it while it steps after the
        # kernel entry, then Step.
        page.context.new_cdp_session(page).send("Emulation.setCPUThrottlingRate", {"rate": 20})
        show_tab(page, "snapshots")
        page.click("#run_replay")
        stepping = wait_for(lambda: page.evaluate(f"MKB.replayActive() && MKB.runState === 'stepped' && MKB.stepper.steps > {entry}"), 300, 0.2)
        machine_do(page, "click", "#dbg_pause")
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        at = page.evaluate("MKB.stepper.steps")
        machine_do(page, "click", "#dbg_step")
        wait_for(lambda: "Replay stopped by Step" in page.text_content("#run_status"), 30)
        st = page.text_content("#run_status")
        taint = page.evaluate("MKB.INPUT.taint")
        r["stopped by Step"] = [stepping, at, st, page.get_attribute("#run_status", "data-replay"), taint]
        if not stepping or st != f"Replay stopped by Step at instruction {at:,}." or \
                page.get_attribute("#run_status", "data-replay") != "stopped" or \
                not taint or taint.get("why") != "replay stopped by Step" or taint.get("at") != at:
            problems.append(f"{tag}: a Step during the replay: {r['stopped by Step']}")
        page.screenshot(path=str(shots / "replay-1280-light-stopped.png"))
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()
        shutil.rmtree(tmp, ignore_errors=True)


def replay_tampered(browser, base, shots, problems, r):
    """Runs edited after the download (hello at 1280 light, to its end on
    the virtual clock): one without its end, one on the real-time clock and
    one with a log entry that is not readable are refused with the reason;
    one recorded for another build of hello, with another RAM hash at its
    end and its last log entry a microsecond later replays and says it
    differs in exactly those (kernel, RAM, one input not at its moment);
    one recorded with 3 MiB of RAM (not in the RAM menu) adds that size to
    the menu, boots with it and differs in RAM."""
    tag = "replay tampered (hello 1280 light)"
    ctx = replay_context(browser, "light", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    tmp = pathlib.Path(tempfile.mkdtemp())
    try:
        page.goto(f"{base}/index.html?kernel=hello&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end in 120 s")
            return
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        show_tab(page, "snapshots")
        with page.expect_download() as dl:
            page.click("#run_dl")
        run = json.loads(pathlib.Path(dl.value.path()).read_text())
        if not run.get("log") or not run.get("kernelKey"):
            problems.append(f"{tag}: hello's run has no log entry or no kernel key (test setup): {str(run)[:200]}")
            return

        def replay(name, body):
            f = tmp / name
            f.write_text(json.dumps(body))
            show_tab(page, "snapshots")
            # The verdict of the replay before is cleared: a wait must see this one's.
            page.evaluate("(() => { const e = document.getElementById('run_status'); e.textContent = ''; delete e.dataset.replay; })()")
            page.set_input_files("#run_file", str(f))

        def replay_end(timeout):
            """This file's replay ended (INPUT.replay still holds the one
            before until the page has read the file and booted: on CI's fast
            runner replay_wait saw that one's end, 43583771338)."""
            return wait_for(lambda: page.get_attribute("#run_status", "data-replay") in ("match", "differ", "stopped", "error"), timeout, 0.5) \
                and replay_wait(page, timeout)

        refused = {"noend.json": ({**run, "end": None}, "Cannot replay: the run has no log or no end."),
                   "realtime.json": ({**run, "clock": "real"},
                                     "Cannot replay: the run was on the real-time clock: its inputs were not recorded."),
                   "unreadable.json": ({**run, "log": [{**run["log"][0], "t": "soon"}] + run["log"][1:]},
                                       "Cannot replay: the log's entry 1 is not readable or out of order.")}
        r["tampered refused"] = {}
        for name, (body, want) in refused.items():
            replay(name, body)
            wait_for(lambda: page.text_content("#run_status"), 10)
            st = page.text_content("#run_status")
            r["tampered refused"][name] = st[:120]
            if st != want or page.get_attribute("#run_status", "data-replay") != "error":
                problems.append(f"{tag}: {name}: {st!r} ({page.get_attribute('#run_status', 'data-replay')}), want {want!r}")

        log = [dict(e) for e in run["log"]]
        log[-1]["t"] += 0.001
        replay("edited.json", {**run, "kernelKey": "sha256:" + "0" * 64, "end": {**run["end"], "ram": "0" * 16}, "log": log})
        if not replay_end(300):
            problems.append(f"{tag}: the edited run's replay did not finish: {page.text_content('#run_status')[:200]!r}")
        c = replay_state(page)
        want = f"Replay differs from the recording (kernel, RAM; 1 input not at its recorded moment) at instruction {run['end']['at']:,}."
        r["tampered differs"] = [c["status"], c["verdict"], c["replay"]]
        if c["status"] != want or c["verdict"] != "differ" or (c["replay"] or {}).get("late") != 1:
            problems.append(f"{tag}: the edited run: {c['status']!r} ({c['verdict']}, {c['replay']}), want {want!r}")
        replay_shots(page, shots, "replay-tampered-1280-light")

        replay("ram3m.json", {**run, "ram": 3 << 20})
        if not replay_end(300):
            problems.append(f"{tag}: the 3 MiB run's replay did not finish: {page.text_content('#run_status')[:200]!r}")
        c = replay_state(page)
        mem = [page.input_value("#memory"), page.evaluate("MKB.emuRam"), page.evaluate("[...document.getElementById('memory').options].map((o) => o.text)")]
        r["tampered 3 MiB"] = [c["status"], c["verdict"], mem[:2]]
        if mem[0] != str(3 << 20) or mem[1] != 3 << 20 or "3,072 KiB" not in mem[2]:
            problems.append(f"{tag}: a run with 3 MiB of RAM: the menu {mem[2]}, chosen {mem[0]}, the machine's RAM {mem[1]}")
        if c["verdict"] != "differ" or not c["status"].startswith("Replay differs from the recording (") or "RAM" not in c["status"]:
            problems.append(f"{tag}: a run recorded on 2 MiB replayed on 3 MiB: {c['status']!r} ({c['verdict']})")
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()
        shutil.rmtree(tmp, ignore_errors=True)


def replay_during(browser, base, shots, problems, r):
    """While a replay runs (hello at 1280 dark, its run stepped from the
    reset vector, replayed with Replay at a 20x CPU throttle): keys typed
    into the screen while it is paused are not given to the guest, and the
    replay, continued, ends matching and says it ignored those 4
    scancodes; Step back while the next replay is paused stops it there
    ("Replay stopped by Step back at instruction N") and steps back one;
    Back to a name that is no symbol says what Run to says for it, with
    Back to for Run to throughout (FAILS on 4e3e1ef8f: "Back to x: no
    symbol of that name. Run to takes ...")."""
    tag = "replay during (hello 1280 dark)"
    ctx = replay_context(browser, "dark", False)
    page = ctx.new_page()
    errs = []
    page.on("pageerror", lambda e: errs.append(str(e)[:200]))
    try:
        page.goto(f"{base}/index.html?kernel=hello&mips=10&fastforward=0", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            problems.append(f"{tag}: hello did not end")
            return
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        end = page.evaluate("MKB.stepper.steps")
        cdp = page.context.new_cdp_session(page)
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 20})

        def replay_paused():
            """Replay; pause it while it steps the BIOS."""
            show_tab(page, "snapshots")
            page.click("#run_replay")
            if not wait_for(lambda: page.evaluate("MKB.replayActive() && MKB.runState === 'stepped' && MKB.stepper.steps > 100000"), 120, 0.05):
                return None
            machine_do(page, "click", "#dbg_pause")
            wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
            return page.evaluate("MKB.replayActive() ? MKB.stepper.steps : null")

        if not replay_paused():
            problems.append(f"{tag}: could not pause the replay while it steps: {page.text_content('#run_status')!r}")
            return
        show_tab(page, "machine")
        page.click("#screen_container")
        page.keyboard.type("ab")
        wait_for(lambda: page.evaluate("MKB.INPUT.replay.ignored") == 4, 10)
        typed = page.evaluate("[MKB.INPUT.replay.ignored, MKB.INPUT.pending.length]")
        machine_do(page, "click", "#dbg_continue")
        if not replay_wait(page, 180):
            problems.append(f"{tag}: the replay did not end: {page.text_content('#run_status')!r}")
            return
        c = replay_state(page)
        want = f"Replay matched: 0 scancodes at the recorded counts, and at instruction {end:,} the same EIP, RAM and screen as recorded. " \
               "4 scancodes typed during the replay were ignored."
        r["typed during"] = [typed, c["status"], c["verdict"], c["keyEntries"]]
        if typed != [4, 0] or c["status"] != want or c["verdict"] != "match" or c["keyEntries"]:
            problems.append(f"{tag}: keys typed during the replay: ignored, held {typed}; {c['status']!r} ({c['verdict']}, "
                            f"{c['keyEntries']} key entries), want {want!r}")

        at = replay_paused()
        if not at:
            problems.append(f"{tag}: could not pause the second replay while it steps")
            return
        show_tab(page, "machine")
        wall, ms, st, state = rev_click(page, "#dbg_back", False)
        run = page.text_content("#run_status")
        r["step back during"] = [at, page.evaluate("MKB.stepper.steps"), st[:100], state, run, page.get_attribute("#run_status", "data-replay")]
        if run != f"Replay stopped by Step back at instruction {at:,}." or page.get_attribute("#run_status", "data-replay") != "stopped" or \
                state != "done" or page.evaluate("MKB.stepper.steps") != at - 1:
            problems.append(f"{tag}: Step back during a replay: {r['step back during']}")
        cdp.send("Emulation.setCPUThrottlingRate", {"rate": 1})

        show_tab(page, "debugger")
        page.fill("#dbg_addr", "no_such_function_here")
        page.click("#dbg_runto")
        wait_for(lambda: page.text_content("#dbg_status").startswith("Run to no_such"), 10)
        run_to = page.text_content("#dbg_status")
        page.click("#dbg_backto")
        wait_for(lambda: page.text_content("#dbg_status").startswith("Back to"), 10)
        back_to = page.text_content("#dbg_status")
        r["back to no symbol"] = [run_to[:120], back_to[:120]]
        if not run_to.startswith("Run to no_such_function_here: ") or back_to != run_to.replace("Run to", "Back to") or \
                page.evaluate("MKB.stepper.steps") != at - 1:
            problems.append(f"{tag}: Back to a name that is no symbol: {back_to!r}, Run to said {run_to!r}")
        page.screenshot(path=str(shots / "replay-during-1280-dark.png"))
        replay_fit(page, tag, problems)
        if errs:
            problems.append(f"{tag}: page errors {errs[:3]}")
    finally:
        ctx.close()

def replay_case(browser, base, shots, entries, initrd):
    problems, r = [], {}
    tmp = pathlib.Path(tempfile.mkdtemp())
    def guard(fn, *args):
        try:
            return fn(*args)
        except (PlaywrightError, IndexError, KeyError, TypeError) as e:
            problems.append(f"{fn.__name__}: {type(e).__name__}: {(str(e).splitlines() or [''])[0][:300]}")
            return None
    guard(replay_prompt_edge, browser, base, shots, problems, r)
    guard(replay_tampered, browser, base, shots, problems, r)
    guard(replay_during, browser, base, shots, problems, r)
    if "minified-linux" in entries:
        guard(replay_refused, browser, base, shots, problems, r)
        guard(replay_stepped, browser, base, shots, "minified-linux", problems, r)
    if initrd:
        path, a = guard(replay_initrd, browser, base, shots, initrd[0], tmp, problems, r) or (None, None)
        if path and a:
            guard(replay_from_file, browser, base, shots, path, a, problems, r)
    shutil.rmtree(tmp, ignore_errors=True)
    return problems, r


def suite_replay(ctx):
    initrd = [n for n in ctx.entries if n.startswith("debian") and "initrd" in ctx.entries[n] and not ctx.entries[n].get("editable")]
    return report("replay", *replay_case(ctx.browser, ctx.base, ctx.shots, ctx.entries, initrd))
