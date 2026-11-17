"""Browser test suite runto_phone (test_browser.py --suites)."""

import json
import time

from tests.browser.common import ALIVE, HELLO, kernel_entries, machine_do, PHONE, report, SITE, wait_for


def phone_flow(browser, base, target, speed="1", phone=True, query="", before=None, scheme="light"):
    """The operator's phone flow: set the speed with the real control,
    tap Boot, tap Pause, type target into the Debugger's Run to field and
    tap Run to. before(page, act) runs between Pause and the Run to.
    Returns (page, context) left open at the end."""
    ctx = browser.new_context(color_scheme=scheme, **(PHONE if phone else {"viewport": {"width": 1100, "height": 1000}}))
    page = ctx.new_page()
    act = page.tap if phone else page.click
    page.goto(f"{base}/index.html?kernel=hello{query}")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") not in (None, "", "off"), 60)
    machine_do(page, "select_option", "#dbg_speed", speed)
    act("#tab_machine")
    act("#boot")
    wait_for(lambda: page.is_enabled("#dbg_pause"), 60)
    act("#dbg_pause")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 30)
    act("#tab_debugger")
    if before:
        before(page, act)
    page.evaluate("window.mkbBefore = document.getElementById('regs').dataset.phys")
    act("#dbg_addr")
    page.fill("#dbg_addr", "")
    page.keyboard.type(target)
    act("#dbg_runto")
    # Settled: paused (not running to it, not fast-forwarding) with the status no longer "Running to".
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
             and not page.inner_text("#dbg_status").startswith(("Running to", "Finding")), 60)
    time.sleep(1)
    return page, ctx


def runto_phone_case(browser, base, shots):
    """Run to on a 390 px touch phone at 1 instruction per second (the
    operator's report: "Run to hello" ran on to "Still alive"), the hello
    kernel. A function name stops at its first instruction; a name that is
    no symbol, data (the "Hello, World!" string) and the middle of an
    instruction are refused where the phone shows it (the paused screen's
    overlay too) and the CPU stays paused; a stepped Run to whose target
    comes right after a stepping slice ended (here: the slice that printed
    "Hello, World!") stops there instead of stepping over it."""
    problems, r = [], {}
    entry = kernel_entries()["hello"]
    syms = json.loads((SITE / entry["symbols"]).read_text())
    fn = {x["name"]: x["paddr"] for x in syms["symbols"]}
    rodata = next(s for s in syms["sections"] if s["name"] == ".rodata")
    main = fn["hello_main"]

    def state(page):
        return {"state": page.get_attribute("#dbg_state", "data-state"),
                "phys": int(page.get_attribute("#regs", "data-phys") or -1),
                "status": page.inner_text("#dbg_status"),
                # Set even while another tab hides the Machine tab's screen (U5).
                "overlay": page.inner_text("#vm_overlay") if not page.evaluate("document.getElementById('vm_overlay').hidden") else "",
                "alive": ALIVE in page.inner_text("#screen_container")}

    def stopped_at(tag, page, pa):
        s = state(page)
        r[tag] = s["status"]
        if s["state"] != "paused" or s["phys"] != pa or s["alive"]:
            problems.append(f"{tag}: want paused at {pa:#x} before {ALIVE!r}, got {s}")

    def refused(tag, page, words):
        s = state(page)
        r[tag] = s["status"]
        if s["state"] != "paused" or s["alive"] or s["phys"] != int(page.evaluate("window.mkbBefore")):
            problems.append(f"{tag}: the CPU ran on: {s}")
        for w in words:
            if w not in s["status"] or w not in s["overlay"]:
                problems.append(f"{tag}: {w!r} missing from the status or the paused overlay: {s}")

    lines = json.loads((SITE / entry["disasm"]).read_text())["insns"]
    # The stores of a character to the VGA cell (movw to memory): -Os has
    # one per inlined put_line, -O0 one in put_line.
    stores = [a for a, _, ins, *rest in lines if ins.startswith("movw") and "(" in ins.split(",")[-1] and a > main]
    # The first instruction of hello_main longer than one byte.
    multi = next(a for a, b, *rest in lines if a >= main and len(b) > 2)
    for phone, speed in ((True, "1"), (True, "10"), (False, "1")):
        tag = ("phone" if phone else "desktop") + "@" + speed
        # At 10/s the Pause comes a few instructions into hello_main: Run
        # to the last character store instead (-Os: of "Still alive", -O0:
        # put_line's only one, first reached for "Hello, World!").
        target, want = ("hello_main", main) if speed == "1" else (hex(stores[-1]), stores[-1])
        page, ctx = phone_flow(browser, base, target, speed, phone)
        stopped_at(f"{tag} {target}", page, want)
        ctx.close()
        page, ctx = phone_flow(browser, base, "hello", speed, phone)
        refused(tag + " hello", page, ["hello", "hello_main"])
        if phone and speed == "1":
            page.screenshot(path=str(shots / "runto-phone-hello-390.png"), full_page=True)
        ctx.close()
    for scheme in ("light", "dark"):
        page, ctx = phone_flow(browser, base, hex(rodata["paddr"]), scheme=scheme)
        refused(f"phone data {scheme}", page, [hex(rodata["paddr"]), ".rodata"])
        page.screenshot(path=str(shots / f"runto-phone-data-390-{scheme}.png"), full_page=True)
        ctx.close()
    page, ctx = phone_flow(browser, base, hex(multi + 1))
    refused("phone mid-instruction", page, [hex(multi + 1), hex(multi)])
    ctx.close()

    # The last character of "Hello, World!": Run to its store, 12 more
    # times (each Run to from the target steps past it), then Run to the
    # loop's next instruction. Stepped (fastforward=0) and with the JIT.
    store = stores[0]
    after = next(a for a, *rest in lines if a > store)

    def to_last_store(page, act):
        for _ in range(len(HELLO)):
            page.fill("#dbg_addr", hex(store))
            page.evaluate("document.getElementById('dbg_status').textContent = ''")
            act("#dbg_runto")
            wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                     and page.inner_text("#dbg_status").startswith("Stopped"), 30)
            time.sleep(0.3)
    for q in ("&fastforward=0", ""):
        page, ctx = phone_flow(browser, base, hex(after), query=q, before=to_last_store)
        stopped_at("phone after the Hello store" + (q or " (JIT)"), page, after)
        if HELLO not in page.inner_text("#screen_container"):
            problems.append(f"after the Hello store{q}: {HELLO!r} not on the screen yet")
        ctx.close()
    return problems, r


def suite_runto_phone(ctx):
    return report("runto_phone", *runto_phone_case(ctx.browser, ctx.base, ctx.shots))
