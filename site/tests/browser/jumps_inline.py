"""Part of a browser suite (test_browser.py)."""

import json
import re
import time

from tests.browser.common import SITE, wait_for


# Paths in and out of pa as the page has them (static from disasm.json,
# observed from the stepper): [[at, mask, stat, count], ...] each.
JUMP_PATHS_JS = """(pa) => { const P = MKB.jumpPaths(MKB.jumpsFor(MKB.disasm.insns), MKB.stepper, pa);
    const f = (l) => l.map((e) => [e.at, e.mask, e.stat, e.count]);
    return { into: f(P.into), out: f(P.out), noDirect: P.noDirect, late: P.late }; }"""


# What the inline lines show of an instruction's paths: the "← n paths
# in" line above it (.jsrc) with its named sources (.je), and the
# targets after it (.jdst); the gutter stubs drawn for it.
JUMP_INLINE_JS = """([box, pa]) => { const b = document.querySelector(box);
  const s = b.querySelector('.jsrc[data-addr="' + pa + '"]'), d = b.querySelector('.jdst[data-addr="' + pa + '"]');
  const vis = (e) => { if (!e) return false; const r = e.getBoundingClientRect(), q = b.getBoundingClientRect();
    return r.width > 0 && r.height > 0 && r.top >= q.top - 1 && r.bottom <= q.bottom + 1; };
  const ents = (e) => e ? [...e.querySelectorAll('.je')].map((x) => [+x.dataset.at, x.dataset.where, x.innerText]) : [];
  return { src: s ? s.innerText : null, srcVisible: vis(s), srcEntries: ents(s),
    dst: d ? d.innerText : null, dstEntries: ents(d), stubs: [...b.querySelectorAll('svg.jarcs path.jstub[data-addr="' + pa + '"]')].map((p) => p.dataset.way),
    wraps: s ? s.getClientRects().length : 0, fits: b.scrollWidth <= b.clientWidth + 1 }; }"""

# Sources shown inline before "+n more": at least this many when a
# listing is this wide (jumpview.js jumpInlineMax).
JUMP_INLINE_SHOWN = {1280: 3, 390: 2}


def static_sources(entry):
    """target -> [source] of every direct jump or call in disasm.json, from
    the instruction bytes (test_jumps.decode), not from jumps.js; and the
    mnemonic llvm-objdump prints for each instruction."""
    import test_jumps
    out, mnem = {}, {}
    for pa, raw, text in json.loads((SITE / entry["disasm"]).read_text())["insns"]:
        b = bytes.fromhex(raw)
        mnem[pa] = next((w for w in text.split() if not re.fullmatch(r"lock|rep\w*|notrack|bnd|[c-gs]s|data\d+|addr\d+", w)), "")
        d = test_jumps.decode(b, pa)
        if not d or d[1] < 0 or d[0] in (test_jumps.LJMP, test_jumps.LCALL):
            continue
        if d[0] in (test_jumps.JCC, test_jumps.LOOP) and d[1] == pa + len(b):
            continue
        out.setdefault(d[1], []).append(pa)
    return out, mnem


def code_name(syms, pa):
    """pa as llvm-objdump names a branch target: name or name+0xoff of the
    smallest sized symbol holding it; a sizeless one exactly there; hex."""
    best = None
    for s in syms:
        if s["size"] > 0 and s["paddr"] <= pa < s["paddr"] + s["size"] and (not best or s["size"] < best["size"]):
            best = s
    if best:
        return best["name"] + (f"+{pa - best['paddr']:#x}" if pa > best["paddr"] else ""), best
    marks = [s for s in syms if not s["size"] and s["paddr"] == pa]
    return (marks[0]["name"], None) if marks else (f"{pa:#x}", None)


def jump_inline_check(page, box, pa, label, syms, mnem, stat_src, problems, r, width, must=None, kind=None):
    """The inline sources of pa in box (#listing or #context), with no
    click: "← n path(s) in" with n = the paths jumps.js has (whose static
    ones must be the sources the bytes decode to), the first few of them
    in jumps.js's order, each naming its source as llvm-objdump does
    (symbol+offset), the source's mnemonic (static) or the kind
    (observed), how often it was taken, where it is (in view, above or
    below in the same function, another function) with a gutter stub for
    the ones out of view, and "+k more" for the rest."""
    P = page.evaluate(JUMP_PATHS_JS, pa)
    got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    tag = f"{label} {pa:#x} ({code_name(syms, pa)[0]}) in {box} at {width} px"
    r[f"inline {label} {width}"] = f"{pa:#x}: {got['src']!r}"
    n = len(P["into"])
    if got["src"] is None:
        problems.append(f"{tag}: no inline sources line ({n} paths in)")
        return got
    if not got["srcVisible"] and box == "#listing":
        # A redraw at a stop scrolls to EIP's line: bring the instruction
        # back as "go there" does, its sources line must come with it.
        r[f"inline {label} {width} rescrolled"] = True
        page.evaluate(f"MKB.scrollListingTo(document.querySelector('#listing .ins[data-addr=\"{pa}\"]'))")
        got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    if not got["srcVisible"]:
        problems.append(f"{tag}: the sources line is not in view")
    want_head = f"← {n} path{'s' if n > 1 else ''} in"
    if not got["src"].startswith(want_head):
        problems.append(f"{tag}: the line starts {got['src'][:40]!r}, want {want_head!r}")
    stat = sorted(e[0] for e in P["into"] if e[2])
    if stat != sorted(stat_src.get(pa, [])):
        problems.append(f"{tag}: jumps.js's static sources {[hex(x) for x in stat]} are not the decoded ones {[hex(x) for x in sorted(stat_src.get(pa, []))]}")
    shown = [e[0] for e in got["srcEntries"]]
    k = min(n, JUMP_INLINE_SHOWN[width])
    if shown[:k] != [e[0] for e in P["into"][:k]] or len(shown) > n:
        problems.append(f"{tag}: shows sources {[hex(x) for x in shown]}, jumps.js has them as {[hex(e[0]) for e in P['into'][:len(shown) or k]]}")
    if n > len(shown) and f"+{n - len(shown)} more" not in got["src"]:
        problems.append(f"{tag}: {n} paths, {len(shown)} shown, but no '+{n - len(shown)} more': {got['src']!r}")
    sym_here = code_name(syms, pa)[1]
    view = set(page.eval_on_selector_all(f"{box} .ins", "ls => ls.map((l) => +l.dataset.addr)"))
    jk = page.evaluate("MKB.JK")
    for at, where, text in got["srcEntries"]:
        e = next((x for x in P["into"] if x[0] == at), None)
        if not e:
            continue
        name, s = code_name(syms, at)
        if name not in text:
            problems.append(f"{tag}: source {at:#x} shown as {text!r}, llvm-objdump names it {name}")
        if e[2] and mnem.get(at) and not text.startswith(mnem[at] + " from"):
            problems.append(f"{tag}: source {at:#x} shown as {text!r}, its instruction is {mnem[at]}")
        if not e[2]:
            words = [w for kk, w in (("irq", "interrupt"), ("exc", "exception"), ("ret", "ret"), ("iret", "iret")) if e[1] & 1 << jk[kk]]
            if words and not any(w in text for w in words):
                problems.append(f"{tag}: observed source {at:#x} shown as {text!r}, its kinds are {page.evaluate('MKB.jumpKindsText', e[1])}")
        if e[3] and f"×{e[3]:,}" not in text:
            problems.append(f"{tag}: source {at:#x} taken {e[3]}x, shown as {text!r}")
        want_where = "view" if at in view else ("up" if at < pa else "down") if s and sym_here and s["paddr"] == sym_here["paddr"] else "far"
        if where != want_where:
            problems.append(f"{tag}: source {at:#x} marked {where!r}, it is {want_where!r} (listing shows {len(view)} instructions)")
    ways = {w for _, w, _ in got["srcEntries"] if w != "view"}
    if ways and not ways <= set(got["stubs"]):
        problems.append(f"{tag}: sources out of view {sorted(ways)} but gutter stubs only {got['stubs']}")
    if must is not None and must not in [e[0] for e in P["into"]]:
        problems.append(f"{tag}: {must:#x} is not among the paths in")
    if kind and not any(kind in t for _, _, t in got["srcEntries"]):
        problems.append(f"{tag}: no {kind!r} source shown: {got['src']!r}")
    if not got["fits"]:
        problems.append(f"{tag}: the listing scrolls horizontally")
    return got


def jump_inline_out_check(page, box, pa, label, syms, problems, r, want_to, kind):
    """The targets shown after the instruction at pa: kind and the name of
    want_to, with no click."""
    got = page.evaluate(JUMP_INLINE_JS, [box, pa])
    r[f"inline out {label}"] = f"{pa:#x}: {got['dst']!r}"
    name = code_name(syms, want_to)[0]
    if kind == "direct":
        ok = want_to in [e[0] for e in got["dstEntries"]]
    else:
        ok = bool(got["dst"]) and re.search(kind + r"\w* to", got["dst"]) and name in got["dst"]
    if not ok:
        problems.append(f"{label} {pa:#x} in {box}: the targets after it are {got['dst']!r}, want {kind} to {name}")
    return got


def jump_inline_case(page, name, entry, want, problems, r, shots):
    """B2: where jumps come from is visible in the asm without a click.
    After the walk (want: its oracle), at 1280 and 390 px: a taken jcc's
    target, a loop head (a backward jump in one function), a function
    entry with callers in other functions, a return site and its ret, an
    interrupt entry (minified Linux), and EIP's line in Code at EIP."""
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    stat_src, mnem = static_sources(entry)
    jk = page.evaluate("MKB.JK")
    func = lambda pa: code_name(syms, pa)[1]
    picks = []
    jcc = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k == jk["jcc"] and d == t), reverse=True)
    if jcc:
        picks.append(("jcc target", jcc[0][2], jcc[0][1], None))
    loops = [(c, f, t) for (f, t), (c, k, d) in want.items() if k in (jk["jcc"], jk["jmp"]) and d == t and t < f and func(f) and func(t) and func(f)["paddr"] == func(t)["paddr"]]
    if not loops:
        loops = [(0, f, t) for t, fs in stat_src.items() for f in fs if t < f and func(f) and func(t) and func(f)["paddr"] == func(t)["paddr"] and mnem.get(f, "").startswith("j")]
    if loops:
        c, f, t = max(loops)
        picks.append(("loop head", t, f, None))
    # The function with the most callers elsewhere: other functions in
    # other pages (out of view in the Page listing).
    entries = [(len([f for f in fs if func(f) and func(f)["paddr"] != t and f // 4096 != t // 4096]), t)
               for t, fs in stat_src.items() if func(t) and func(t)["paddr"] == t]
    callers, fn_entry = max(entries)
    r["function entry"] = f"{code_name(syms, fn_entry)[0]}: {callers} callers in other functions and pages"
    picks.append(("function entry", fn_entry, None, None))
    rets = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k == jk["ret"]), reverse=True)
    if rets:
        picks.append(("return site", rets[0][2], rets[0][1], "ret"))
    else:
        problems.append(f"{name}: no ret in the walk")
    irqs = sorted(((c, f, t) for (f, t), (c, k, d) in want.items() if k is None), reverse=True)
    if name == "minified-linux":
        if irqs:
            picks.append(("interrupt entry", irqs[0][2], irqs[0][1], "interrupt"))
        else:
            problems.append(f"{name}: no interrupt in the walk")
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000 if width > 500 else 844})
        time.sleep(0.5)
        for label, pa, must, kind in picks:
            page.evaluate(f"MKB.jumpGo({pa})")
            page.evaluate("MKB.jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{pa}"]').count() == 1, 20)
            time.sleep(0.3)
            jump_inline_check(page, "#listing", pa, f"{name} {label}", syms, mnem, stat_src, problems, r, width, must, kind)
            if label == "function entry" or (label == "jcc target" and width == 1280):
                for scheme in ("light", "dark"):
                    page.emulate_media(color_scheme=scheme)
                    time.sleep(0.3)
                    page.locator("#listing").screenshot(path=str(shots / f"jumpsrc-{name}-{label.replace(' ', '-')}-{width}-{scheme}.png"))
                page.emulate_media(color_scheme="light")
        if rets:
            c, f, t = rets[0]
            page.evaluate(f"MKB.jumpGo({f})")
            page.evaluate("MKB.jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{f}"]').count() == 1, 20)
            jump_inline_out_check(page, "#listing", f, f"{name} ret {width}", syms, problems, r, t, "ret")
        callers_of = sorted(stat_src.get(fn_entry, []), key=lambda x: (x // 4096 == fn_entry // 4096, x))
        if callers_of:
            page.evaluate(f"MKB.jumpGo({callers_of[0]})")
            page.evaluate("MKB.jumpHit = -1")
            wait_for(lambda: page.locator(f'#listing .ins[data-addr="{callers_of[0]}"]').count() == 1, 20)
            # Out of view the target gets a button and a stub; in view
            # (hello is one page) its name in the text is all there is.
            if page.locator(f'#listing .ins[data-addr="{fn_entry}"]').count():
                r[f"call {width}"] = f"{callers_of[0]:#x} calls {fn_entry:#x} in view"
            else:
                got = jump_inline_out_check(page, "#listing", callers_of[0], f"{name} call {width}", syms, problems, r, fn_entry, "direct")
                if "out-far" not in got["stubs"]:
                    problems.append(f"{name}: the call at {callers_of[0]:#x} to another function has no gutter stub at {width} px: {got['stubs']}")
    page.set_viewport_size({"width": 1280, "height": 1000})
    time.sleep(0.3)
    # "+k more" opens the whole list; a named source goes there.
    page.evaluate(f"MKB.jumpGo({fn_entry})")
    wait_for(lambda: page.locator(f'#listing .jsrc[data-addr="{fn_entry}"]').count() == 1, 20)
    n = len(page.evaluate(JUMP_PATHS_JS, fn_entry)["into"])
    more = page.locator(f'#listing .jsrc[data-addr="{fn_entry}"] .jmore')
    if more.count():
        more.click()
        if not wait_for(lambda: page.is_visible("#jump_pop"), 5) or page.locator("#jump_pop li").count() != n:
            problems.append(f"{name}: '+k more' of {fn_entry:#x} did not list all {n} paths in")
        page.keyboard.press("Escape")
    elif n > JUMP_INLINE_SHOWN[1280]:
        problems.append(f"{name}: {fn_entry:#x} has {n} paths in and no '+k more'")
    far = page.locator(f'#listing .jsrc[data-addr="{fn_entry}"] .je[data-where="far"] button.jto')
    if far.count():
        at = int(far.first.get_attribute("data-go"))
        far.first.click()
        if not wait_for(lambda: page.locator(f'#listing .ins.jhit[data-addr="{at}"]').count() == 1, 10):
            problems.append(f"{name}: the inline source {at:#x} of {fn_entry:#x} did not open there")
        r["inline go there"] = f"{fn_entry:#x} -> {at:#x}"
    else:
        r["inline go there"] = "no source in another page"


def jump_inline_eip_check(page, name, entry, eip, problems, r, shots):
    """Code at EIP at a function's entry: its callers inline above it."""
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    stat_src, mnem = static_sources(entry)
    for width in (1280, 390):
        page.set_viewport_size({"width": width, "height": 1000 if width > 500 else 844})
        page.evaluate("MKB.renderContext()")
        time.sleep(0.5)
        got = jump_inline_check(page, "#context", eip, f"{name} EIP", syms, mnem, stat_src, problems, r, width)
        here = code_name(syms, eip)[1]
        if got["src"] and not any((code_name(syms, at)[1] or {}).get("paddr") != here["paddr"] for at, _, _ in got["srcEntries"]):
            problems.append(f"{name}: Code at EIP {eip:#x} shows no caller from another function: {got['src']!r}")
        for scheme in ("light", "dark"):
            page.emulate_media(color_scheme=scheme)
            time.sleep(0.3)
            page.locator("#context").screenshot(path=str(shots / f"jumpsrc-{name}-eip-{width}-{scheme}.png"))
        page.emulate_media(color_scheme="light")
    page.set_viewport_size({"width": 1280, "height": 1000})
    page.evaluate("MKB.renderContext()")


# Instructions of a box (selector) that carry the ⊘ mark: the .ins line
# before each .jnone (the marks follow their line's span).
NOPATH_MARKS_JS = """(sel) => { const out = [];
  for (const box of document.querySelectorAll(sel))
    for (const n of box.querySelectorAll('.jnone')) {
      let e = n.previousElementSibling;
      while (e && !e.matches('.ins')) e = e.previousElementSibling;
      if (e) out.push([+e.dataset.addr, e.classList.contains('cur'), n.dataset.tip || n.title]); }
  return out; }"""
