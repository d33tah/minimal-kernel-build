"""Browser test suite dwarf (test_browser.py --suites)."""

import json
import re
import time

from tests.browser.common import dwarf_check, DWARF_STEPS, kernel_entries, open_page, report, show_tab, SITE, wait_for


# Per view: [selector of the element holding a C location, needs C text too].
SRC_COLOUR_VIEWS = {
    "Page listing": ("#listing .src:not(.nol)", True),
    "Debugger listing": ("#context .src:not(.nol)", True),
    "Debugger source row": ("#cur_src .frame", True),
    "Source panel": ("#src_body > span", True),
    "Debugger inlined-into row": ("#src_colour_probe .frame.inl", False),
    "call stack": ("#call_stack", False),
    "Graph detail": ("#graph_src", False),
}

# The colours of a view's first .srcloc and .srctext, and the opaque
# background behind each (semi-transparent ancestors composited over it).
SRC_COLOURS_JS = """([sel, text]) => {
    const rgba = (c) => { const m = c.match(/[\\d.]+/g).map(Number); return [m[0], m[1], m[2], m.length > 3 ? m[3] : 1]; };
    const bg = (el) => { const layers = [];
        for (let e = el; e; e = e.parentElement) { const c = rgba(getComputedStyle(e).backgroundColor);
            if (c[3] > 0) { layers.push(c); if (c[3] >= 1) break; } }
        if (!layers.length || layers[layers.length - 1][3] < 1) layers.push(rgba(getComputedStyle(document.body).backgroundColor));
        let out = layers.pop().slice(0, 3);
        while (layers.length) { const [r, g, b, a] = layers.pop(); out = [r * a + out[0] * (1 - a), g * a + out[1] * (1 - a), b * a + out[2] * (1 - a)]; }
        return out; };
    const box = [...document.querySelectorAll(sel)].find((e) => e.querySelector('.srcloc') && (!text || e.querySelector('.srctext')));
    if (!box) return null;
    const one = (cls) => { const e = box.querySelector(cls); if (!e) return null; const c = rgba(getComputedStyle(e).color);
        let o = 1; for (let x = e; x; x = x.parentElement) o *= Number(getComputedStyle(x).opacity);
        return { colour: c.slice(0, 3), alpha: c[3] * o, bg: bg(e), text: e.textContent.trim().slice(0, 40) }; };
    return { loc: one('.srcloc'), text: one('.srctext') };
}"""


def contrast(fg, bg, alpha=1):
    """WCAG contrast ratio of fg (alpha over bg) against bg, both [r, g, b] 0-255."""
    fg = [f * alpha + b * (1 - alpha) for f, b in zip(fg, bg)]

    def lum(c):
        c = [x / 255 for x in c]
        c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
        return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]
    hi, lo = sorted((lum(fg), lum(bg)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def src_colour_check(page, name, phys, inl_phys, problems, r, shots):
    """Every view showing C lines colours the location (.srcloc) and the C
    text (.srctext) differently, with the same pair everywhere, each at
    least 4.5:1 against its background, in light and dark. inl_phys: an
    address in inlined code, whose Debugger source rows are rendered into
    a probe box to check the "inlined into" rows too."""
    seen = {}
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 1280, "height": 1000})
        got = {}
        show_tab(page, "memory")
        open_page(page, phys // 4096)
        wait_for(lambda: page.locator("#listing .srcloc").count() > 0, 15)
        got["Page listing"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Page listing"]))
        show_tab(page, "debugger")
        wait_for(lambda: page.locator("#cur_src .srctext").count() > 0, 15)
        for view in ("Debugger listing", "Debugger source row", "call stack"):
            got[view] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS[view]))
        if inl_phys is not None:
            page.evaluate("""(pa) => { const box = document.createElement('div'); box.id = 'src_colour_probe';
                document.getElementById('sym_info').append(box); MKB.renderSourceRows(box, pa, null); }""", inl_phys)
            wait_for(lambda: page.evaluate("MKB.srcText(...JSON.parse(document.querySelector('#src_colour_probe #cur_src').dataset.frames)"
                                           ".slice(-1).map(f => [f.file, f.line])[0]) !== null"), 15)
            page.evaluate("""(pa) => { const box = document.getElementById('src_colour_probe'); box.textContent = '';
                MKB.renderSourceRows(box, pa, null); }""", inl_phys)
            got["Debugger inlined-into row"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Debugger inlined-into row"]))
            page.evaluate("document.getElementById('src_colour_probe').remove()")
        page.evaluate("(pa) => MKB.openSource(pa)", phys)
        wait_for(lambda: page.get_attribute("#src_body", "data-state") == "ok", 20)
        got["Source panel"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Source panel"]))
        page.screenshot(path=str(shots / f"srccolour-{name}-1280-{scheme}.png"))
        page.set_viewport_size({"width": 390, "height": 900})
        time.sleep(0.3)
        page.screenshot(path=str(shots / f"srccolour-{name}-390-{scheme}.png"))
        page.set_viewport_size({"width": 1280, "height": 1000})
        page.click("#src_close")
        show_tab(page, "calls")
        page.evaluate("""() => { const st = MKB.G.stepper; MKB.G.view = 'timeline';
            MKB.G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fr.n - 1; MKB.renderGraphInfo(); }""")
        got["Graph detail"] = page.evaluate(SRC_COLOURS_JS, list(SRC_COLOUR_VIEWS["Graph detail"]))
        show_tab(page, "debugger")
        for view, (_, text) in SRC_COLOUR_VIEWS.items():
            if view == "Debugger inlined-into row" and inl_phys is None:
                continue
            g = got.get(view)
            if not g or not g["loc"] or (text and not g["text"]):
                problems.append(f"{name} {scheme}: {view} has no .srcloc/.srctext pair: {g}")
                continue
            seen.setdefault((scheme, "loc"), set()).add(tuple(g["loc"]["colour"]))
            for part in ("loc", "text") if text else ("loc",):
                c = g[part]
                ratio = contrast(c["colour"], c["bg"], c["alpha"])
                r[f"contrast {scheme} {view} {part}"] = round(ratio, 2)
                if ratio < 4.5:
                    problems.append(f"{name} {scheme}: {view} {part} contrast {ratio:.2f} < 4.5: {c}")
            if text:
                seen.setdefault((scheme, "text"), set()).add(tuple(g["text"]["colour"]))
                if g["loc"]["colour"] == g["text"]["colour"]:
                    problems.append(f"{name} {scheme}: {view} location and C text have the same colour {g['loc']['colour']}")
    page.emulate_media(color_scheme="light")
    for key, colours in seen.items():
        r[f"colour {key[0]} {key[1]}"] = sorted(colours)
        if len(colours) != 1:
            problems.append(f"{name} {key[0]}: .src{key[1]} differs between views: {sorted(colours)}")
    if seen.get(("light", "loc")) == seen.get(("dark", "loc")) or seen.get(("light", "text")) == seen.get(("dark", "text")):
        problems.append(f"{name}: the C line colours do not change with the theme: {seen}")


def dwarf_case(browser, base, shots, name):
    """file:line and inline chains from the shipped DWARF shards: Debugger
    source rows and call stack against llvm-symbolizer, C lines and "no
    line information" in the listings, screenshots light/dark at 1280/390."""
    problems, r = [], {}
    syms = json.loads((SITE / kernel_entries()[name]["symbols"]).read_text())["symbols"]
    fn = "start_kernel" if name != "hello" else "hello_main"
    sk = next(s for s in syms if s["name"] == fn)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    bins = set()  # DWARF shards fetched
    page.on("response", lambda resp: "/dwarf/" in resp.url and resp.url.split("?")[0].endswith(".bin") and
            bins.add(resp.url.split("/dwarf/", 1)[1].split("?")[0]))
    # A watch list in this browser that is not JSON: the panel starts with no watches.
    page.add_init_script(f"localStorage.setItem('mkb-watch:{name}', '[\"init_task\", oops')")
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 120):
        page.close()
        return [f"Run to {fn} did not stop there"], r
    steps = DWARF_STEPS if name != "hello" else (0, 3, 7, 11, 19)
    samples = dwarf_check(page, name, sk["vaddr"] - sk["paddr"], problems, r, steps, fn)
    if name != "hello" and not r.get("dwarf_inlined"):
        problems.append(f"{name}: no sample EIP was in inlined code: {r}")
    page.screenshot(path=str(shots / f"dwarf-debugger-{name}-1280-light.png"))
    # Calls tab: the detail of the innermost open frame names its call site and entry with file:line.
    show_tab(page, "calls")
    page.evaluate("""() => { const st = MKB.G.stepper; MKB.G.view = 'timeline';
        MKB.G.selected = st.stack.length ? st.stack[st.stack.length - 1] : st.fr.n - 1; MKB.renderGraphInfo(); }""")
    wait_for(lambda: "reading DWARF" not in (page.evaluate("(document.getElementById('graph_src') || {}).innerText") or ""), 15)
    r["graph_detail"] = page.evaluate("(document.getElementById('graph_src') || {}).innerText")
    if not r["graph_detail"] or not ("called from 0x" in r["graph_detail"] and re.search(r"\.[chS]:\d+", r["graph_detail"])):
        problems.append(f"{name}: Graph frame detail without DWARF locations: {r['graph_detail']!r}")
    page.screenshot(path=str(shots / f"dwarf-graph-{name}-1280-light.png"), full_page=True)
    # The listing of the page holding the last EIP: C lines with titles.
    phys = samples[-1][0]
    show_tab(page, "memory")
    open_page(page, phys // 4096)
    wait_for(lambda: page.locator("#listing .src:not(.dim)").count() > 0, 15)
    r["listing_c_lines"] = page.locator("#listing .src:not(.nol):not(.dim)").count()
    r["listing_no_line"] = page.locator("#listing .src.nol").count()
    titles = page.eval_on_selector_all("#listing .src", "els => els.map(e => e.dataset.tip || e.title)")
    r["listing_inlined_titles"] = sum(1 for t in titles if "inlined into" in t)
    if r["listing_c_lines"] < 1:
        problems.append(f"{name}: no C lines in the listing of page {phys // 4096:#x}")
    page.screenshot(path=str(shots / f"dwarf-listing-{name}-1280-light.png"))
    page.emulate_media(color_scheme="dark")
    page.screenshot(path=str(shots / f"dwarf-listing-{name}-1280-dark.png"))
    show_tab(page, "debugger")
    page.screenshot(path=str(shots / f"dwarf-debugger-{name}-1280-dark.png"))
    for scheme in ("dark", "light"):
        page.emulate_media(color_scheme=scheme)
        page.set_viewport_size({"width": 390, "height": 900})
        show_tab(page, "debugger")
        time.sleep(0.3)
        if not page.evaluate("document.documentElement.scrollWidth <= window.innerWidth + 1"):
            problems.append(f"{name}: the Debugger scrolls horizontally at 390 px ({scheme})")
        page.screenshot(path=str(shots / f"dwarf-debugger-{name}-390-{scheme}.png"), full_page=True)
    page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    src_colour_check(page, name, phys, next((p for p, _, fr in samples if len(fr) > 1), None), problems, r, shots)
    page.close()
    if errors:
        problems.append("page errors: " + "; ".join(errors))
    return problems, r


def suite_dwarf(ctx):
    failed = False
    for name in [n for n in ("minified-linux", "hello") if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        failed |= report("dwarf", *dwarf_case(ctx.browser, ctx.base, ctx.shots, name), kernel=name)
    return failed
