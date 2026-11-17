"""Browser test suite coverage (test_browser.py --suites)."""

import json

from tests.browser.common import kernel_entries, no_hscroll, open_page, report, SITE, wait_for
from tests.browser.debian import listing_insns


def livedecode_case(browser, base, shots):
    """Live decoding (iced-x86) agrees with disasm.json on start_kernel."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == "start_kernel")
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
    wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 60)
    open_page(page, sk["paddr"] // 4096)
    hi = min(sk["paddr"] + sk["size"], (sk["paddr"] // 4096 + 1) * 4096)
    built = listing_insns(page, sk["paddr"], hi)
    page.check("#live_decode")
    wait_for(lambda: page.get_attribute("#listing", "data-source") == "live", 10)
    live = listing_insns(page, sk["paddr"], hi)
    same = [a for a in built if live.get(a) == built[a]]
    r["build"], r["live"], r["same"] = len(built), len(live), len(same)
    r["differ"] = [(hex(a), built[a], live.get(a)) for a in built if live.get(a) != built[a]][:5]
    if not built or len(same) < 0.95 * len(built):
        problems.append(f"live decode disagrees with disasm.json on start_kernel: {r}")
    page.screenshot(path=str(shots / "live-decode.png"))
    page.close()
    return problems, r


def coverage_case(browser, base, shots):
    """The Page listing tints instructions that have not executed in this boot.

    Paused at the reset vector nothing is recorded, so nothing is tinted.
    After a counted boot (?speed=full: stepped to the end, then the JIT)
    start_kernel's first instruction is not tinted, die() (never called in
    a good boot) is, hex rows never are, and the key says how it is recorded.
    """
    problems, r = [], {}
    syms = {s["name"]: s for s in json.loads((SITE / kernel_entries()["minified-linux"]["symbols"]).read_text())["symbols"]}
    sk, die = syms["start_kernel"], syms["die"]
    tinted = "e => [...e.querySelectorAll('.nx')].map(x => [Number(x.dataset.addr), x.classList.contains('ins')])"

    def line_tinted(page, addr):
        return page.eval_on_selector(f"#listing .ins[data-addr='{addr}']", "e => e.classList.contains('nx')")

    def show_listing_line(page, addr):
        page.eval_on_selector(f"#listing .ins[data-addr='{addr}']", """e => {
            const box = document.getElementById('listing');
            box.scrollTop = e.offsetTop - box.offsetTop - 40;
            document.getElementById('page_title').scrollIntoView(); }""")

    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60):
        problems.append("did not pause at the reset vector")
    open_page(page, sk["paddr"] // 4096)
    r["paused"] = {"coverage": page.get_attribute("#listing", "data-coverage"),
                   "tinted": len(page.eval_on_selector("#listing", tinted)), "key": page.inner_text("#cov_info")}
    if r["paused"]["tinted"] or r["paused"]["coverage"] != "none" or "No execution coverage" not in r["paused"]["key"]:
        problems.append(f"tinted before anything ran: {r['paused']}")
    page.close()

    for scheme, width in (("light", 1280), ("dark", 1280), ("dark", 390), ("light", 390)):
        page = browser.new_page(viewport={"width": width, "height": 1000}, color_scheme=scheme)
        page.goto(f"{base}/index.html?kernel=minified-linux&speed=full")
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120):
            problems.append(f"{scheme}/{width}: the counted boot did not end")
        open_page(page, sk["paddr"] // 4096)
        marks = page.eval_on_selector("#listing", tinted)
        cov = page.get_attribute("#listing", "data-coverage")
        key = page.inner_text("#cov_info")
        if width == 1280 and scheme == "light":
            r["start_kernel_page"] = {"coverage": cov, "tinted": len(marks), "unexecuted": page.get_attribute("#listing", "data-unexecuted"),
                                      "insns": page.get_attribute("#listing", "data-insns"), "key": key}
        if cov not in ("stepped", "partial") or "have not executed" not in key:
            problems.append(f"{scheme}/{width}: no coverage key after a counted boot: {cov!r} {key!r}")
        if line_tinted(page, sk["paddr"]):
            problems.append(f"{scheme}/{width}: start_kernel's first instruction is tinted as not executed")
        if any(not ins for _, ins in marks):
            problems.append(f"{scheme}/{width}: a hex row is tinted")
        if not no_hscroll(page, "body"):
            problems.append(f"{scheme}/{width}: horizontal scroll")
        show_listing_line(page, sk["paddr"])
        page.screenshot(path=str(shots / f"coverage-start_kernel-{scheme}-{width}.png"))
        open_page(page, die["paddr"] // 4096)
        if not line_tinted(page, die["paddr"]):
            problems.append(f"{scheme}/{width}: die() is not tinted as not executed")
        if width == 1280 and scheme == "light":
            r["die_page"] = {"tinted": len(page.eval_on_selector("#listing", tinted)), "insns": page.get_attribute("#listing", "data-insns")}
        show_listing_line(page, die["paddr"])
        page.screenshot(path=str(shots / f"coverage-die-{scheme}-{width}.png"))
        page.close()
    return problems, r


def suite_coverage(ctx):
    failed = report("live_decode", *livedecode_case(ctx.browser, ctx.base, ctx.shots))
    return failed | report("coverage", *coverage_case(ctx.browser, ctx.base, ctx.shots))
