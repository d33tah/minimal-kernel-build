"""Browser test suite symbols (test_browser.py --suites)."""

import json
import time

from tests.browser.common import counters, kernel_entries, machine_do, report, SITE, wait_for


def symbol_case(browser, base, name, shots):
    """Debugger tab code window and symbol history, from start_kernel."""
    problems, r = [], {}
    entry = kernel_entries().get(name, {})
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next((s for s in syms if s["name"] == "start_kernel"), None)
    if not sk:
        return ["no start_kernel in symbols.json"], r
    page = browser.new_page(viewport={"width": 390, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        return ["did not start paused"], r
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 60):
        return ["Run to start_kernel did not stop there: " + page.inner_text("#dbg_status")], r
    offset = sk["vaddr"] - sk["paddr"]
    for i in range(20):
        page.fill("#dbg_n", "50")
        page.click("#dbg_stepn")
        time.sleep(0.15)
        phys = int(page.get_attribute("#regs", "data-phys"))
        eip = int(page.get_attribute("#regs", "data-eip"))
        cur_line = page.get_attribute("#context", "data-cur")
        lo = page.get_attribute("#cur_sym", "data-lo")
        hi = page.get_attribute("#cur_sym", "data-hi")
        cur = page.get_attribute("#cur_sym", "data-name")
        prev = page.get_attribute("#prev_sym", "data-name")
        if i == 0:
            r["after_step50"] = {"eip": hex(eip), "phys": hex(phys), "current": cur, "previous": prev}
            if cur_line != str(phys) or eip - offset != phys:
                problems.append(f"code window line {cur_line} is not EIP {eip:#x} (phys {phys:#x})")
            if not lo or not (int(lo) <= phys < int(hi)):
                problems.append(f"current symbol {cur} [{lo}, {hi}) does not contain EIP {phys:#x}")
        if prev and cur and prev != cur and page.locator("#context .ins.cur").count() == 1:
            r.update(steps=(i + 1) * 50, current=cur, previous=prev,
                     path=page.inner_text("#sym_info .crumbs"))
            break
    else:
        problems.append(f"no previous symbol different from the current one after 1000 steps: {cur}/{prev}")
    page.screenshot(path=str(shots / f"debugger-symbols-{name}-390.png"))
    if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
        problems.append("debugger tab scrolls horizontally at 390 px")
    page.locator("#prev_sym button").click()
    if page.get_attribute("#tab_page", "aria-selected") != "true":
        problems.append("clicking the previous symbol did not open the Page tab")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def linux_slow_case(browser, base, shots):
    """Default load of Linux: entry marker, 1k/s, counter grows, ETA, symbol."""
    problems, r = [], {}
    page = browser.new_page(viewport={"width": 1100, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux")
    if not wait_for(lambda: page.get_attribute("#entry_info", "data-steps"), 60):
        return ["no entry marker: " + page.inner_text("#entry_info")], r
    r["entry_marker"] = page.inner_text("#entry_info")
    r["speed"] = page.eval_on_selector("#dbg_speed", "e => e.value")
    if r["speed"] != "1000":
        problems.append("speed is not 1k/s after the entry: " + r["speed"])
    time.sleep(0.5)
    a = (counters(page).get("now") or {}).get("entry") or 0
    time.sleep(3)
    b = (counters(page).get("now") or {}).get("entry") or 0
    r["grew_3s"] = b - a
    if not 1500 <= b - a <= 4500:
        problems.append(f"from-entry counter grew by {b - a} in 3 s at 1k/s")
    r["state"] = page.inner_text("#dbg_state")
    r["real_speed"] = int(page.get_attribute("#vclock", "data-speed") or 0)
    if not 500 <= r["real_speed"] <= 2000:
        problems.append(f"measured speed at 1k/s is {r['real_speed']}/s")
    r["eta"] = page.get_attribute("#count_info", "data-eta")
    if " in " not in r["state"]:
        problems.append("top bar does not name the current symbol: " + r["state"])
    if not r["eta"]:
        problems.append("no ETA: " + page.inner_text("#count_text"))
    page.screenshot(path=str(shots / "linux-slow-1100.png"))
    # The choice is remembered: pick full speed, reload, it stays full.
    machine_do(page, "select_option", "#dbg_speed", "full")
    page.reload()
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") in ("stepped", "jit"), 30)
    if page.eval_on_selector("#dbg_speed", "e => e.value") != "full":
        problems.append("the speed choice was not remembered across a reload")
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_symbols(ctx):
    lp, lr = linux_slow_case(ctx.browser, ctx.base, ctx.shots)
    sp, sr = symbol_case(ctx.browser, ctx.base, "minified-linux", ctx.shots)
    return report("linux_slow", lp, lr) | report("symbols", sp, sr)
