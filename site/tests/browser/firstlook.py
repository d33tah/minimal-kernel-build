"""Browser test suite firstlook (test_browser.py --suites)."""

import json
import os
import time

from tests.browser.common import debug_elf, kernel_entries, no_hscroll, report, show_tab, SITE, wait_for


# Where firstlook_case pauses each kernel: a function that has run code,
# locals and (Linux) a task by then.
FIRSTLOOK_POINTS = {"hello": "hello_main", "minified-linux": "tty_write",
                    "debian-6.1-686": "start_kernel", "debian-6.1-686-initrd": "start_kernel"}

# Every element a debug feature (D1-D7, V1-V3) puts on the page and the
# explanation a first-time user can reach for it: a "?" tip (a .tip in it,
# its heading or its row) or a title.
FIRSTLOOK_HELP_JS = """() => {
  const out = [], q = (s) => [...document.querySelectorAll(s)];
  const vis = (e) => !!(e.offsetParent || e.getClientRects().length);
  const tipped = (e) => !!(e.title || e.dataset.tip || e.querySelector('.tip, [title], [data-tip]'));
  for (const t of q('.tip'))
    if (!(t.dataset.tip || t.title || '').trim()) out.push('empty ? tip: ' + t.outerHTML.slice(0, 80));
  for (const h of q('#panel_debugger h3, #panel_source h3')) if (!h.querySelector('.tip')) out.push('heading without ?: ' + h.textContent);
  for (const id of ['cur_sym', 'cur_src', 'cur_task', 'call_stack', 'prev_sym'])
    { const e = document.getElementById(id); if (!e) out.push('no #' + id); else if (!tipped(e)) out.push('#' + id + ' has no title'); }
  for (const e of q('#call_stack button, .fmeth')) if (!e.title && !e.closest('[title]')) out.push('call stack item without title: ' + e.textContent);
  for (const e of q('#locals summary')) if (!tipped(e)) out.push('locals frame without title: ' + e.textContent);
  for (const id of ['show_src', 'src_tar', 'cov_filter', 'cov_export', 'watch_expr', 'sym_offset', 'symfile']) {
    const e = document.getElementById(id);
    if (!e) { out.push('no #' + id); continue; }
    const host = e.closest('label, .srcpick, .scan');
    if (!(host && host.querySelector('.tip'))) out.push('#' + id + ' has no ? next to it');
  }
  const ids = q('[id]').map((e) => e.id), dup = ids.filter((x, i) => ids.indexOf(x) !== i);
  if (dup.length) out.push('duplicate ids: ' + [...new Set(dup)].join(', '));
  return out;
}"""


def firstlook_case(browser, base, shots, name):
    """A first-time user's look at each kernel (D8): paused at the reset
    vector, Run to a function (JIT and a breakpoint where the page can),
    then every debug feature's element has an explanation (? tip or title),
    no id is used twice (the Source tab's notes once went to the Page
    tab), the entry line says the kernel was entered (it kept saying
    "Stepping BIOS and setup to the kernel entry..."), the symbol history
    says the JIT ran instead of naming the BIOS as the previous symbol,
    the Source tab says its counts are incomplete when the JIT ran kernel
    code and shows its notes, and no tab scrolls sideways at 390 px in
    light or dark."""
    problems, r = [], {}
    entry = kernel_entries()[name]
    fn = FIRSTLOOK_POINTS[name]
    syms = json.loads((SITE / entry["symbols"]).read_text())["symbols"]
    sk = next(s for s in syms if s["name"] == fn)
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel={name}&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 90)
    page.fill("#dbg_addr", hex(sk["vaddr"]))
    page.click("#dbg_runto")
    if not wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused"
                    and page.get_attribute("#regs", "data-phys") == str(sk["paddr"]), 300, 0.5):
        status = page.inner_text("#dbg_status")
        page.close()
        return [f"{name}: Run to {fn} did not stop there: {status}"], r
    page.fill("#dbg_n", "3")
    page.click("#dbg_stepn")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    wait_for(lambda: page.get_attribute("#call_stack", "data-unwind") not in (None, "loading"), 30)
    wait_for(lambda: page.locator("#locals summary").count() > 0, 30)
    r["entry_info"] = page.inner_text("#entry_info")
    if "Stepping BIOS and setup" in r["entry_info"] or "Entered the kernel" not in r["entry_info"]:
        problems.append(f"{name}: paused in {fn}, the entry line says {r['entry_info']!r}")
    jit = page.evaluate("MKB.stepper.jitAfterEntry || MKB.stepper.jitBeforeEntry")
    r["jit"] = jit
    r["path"] = page.inner_text("#sym_info .crumbs")
    # The JIT's gap is in the symbol history (the path shows its newest 6;
    # the history keeps 16, so after many stepped calls it has gone).
    r["history_gap"] = page.evaluate("MKB.stepper.history.some((h) => h.sym === -3)")
    if jit and not r["history_gap"] and page.evaluate("MKB.stepper.history.length") < 16:
        problems.append(f"{name}: the JIT ran but the symbol history has no gap for it: {r['path']!r}")
    if page.evaluate("MKB.stepper.jitAfterEntry") and "JIT ran here" not in r["path"]:
        problems.append(f"{name}: the JIT ran kernel code but the path does not say so: {r['path']!r}")
    help_bad = page.evaluate(FIRSTLOOK_HELP_JS)
    problems += [f"{name}: {b}" for b in help_bad]
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") == "ok"
             and not page.inner_text("#cov_status").startswith("Reading"), 60)
    r["cov_status"] = page.inner_text("#cov_status")
    info = page.evaluate("(document.getElementById('covtree_info') || {}).innerText || ''")
    r["covtree_info"] = info[:80]
    if not info.startswith("Lines:"):
        problems.append(f"{name}: the Source tab has no notes under the tree: {r['covtree_info']!r}")
    if page.evaluate("MKB.stepper.jitAfterEntry") and not r["cov_status"].startswith("Incomplete"):
        problems.append(f"{name}: the JIT ran kernel code, the Source tab does not say it is incomplete: {r['cov_status']!r}")
    if page.is_disabled("#cov_export"):
        problems.append(f"{name}: lcov disabled although the tree is there")
    for scheme in ("light", "dark"):
        page.emulate_media(color_scheme=scheme)
        for w in (1280, 390):
            page.set_viewport_size({"width": w, "height": 900})
            for t in ("debugger", "page", "source", "calls", "memory"):
                show_tab(page, t)
                time.sleep(0.3)
                if not no_hscroll(page, "html"):
                    problems.append(f"{name}: {t} tab scrolls sideways at {w} px {scheme}")
            if w == 390:
                show_tab(page, "debugger")
                page.screenshot(path=str(shots / f"firstlook-{name}-390-{scheme}.png"), full_page=True)
    problems += [f"{name}: page error {e}" for e in errors]
    page.close()
    return problems, r


def firstlook_import_case(browser, base, shots):
    """The imported unstripped vmlinux: same explanations; the Source tab
    says why there is no tree and lcov is disabled."""
    problems, r = [], {}
    elf = debug_elf("minified-linux")
    if not elf:
        return ([] if not os.environ.get("CI") else ["no minified-linux.elf in $MKB_DEBUG_ELF_DIR"]), {"skipped": "no -g ELF"}
    page = browser.new_page(viewport={"width": 1280, "height": 1000})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto(f"{base}/index.html?kernel=minified-linux&paused=1#debugger")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    page.evaluate("MKB.startPaused = true")
    page.set_input_files("#symfile", str(elf))
    wait_for(lambda: "compile units" in page.inner_text("#sym_status"), 60)
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    show_tab(page, "source")
    wait_for(lambda: page.get_attribute("#cov_tree", "data-state") not in (None, "loading"), 30)
    r["cov_status"] = page.inner_text("#cov_status")
    if "imported vmlinux" not in r["cov_status"]:
        problems.append(f"import: the Source tab does not say why there is no tree: {r['cov_status']!r}")
    if not page.is_disabled("#cov_export"):
        problems.append("import: lcov is enabled without a tree")
    problems += [f"import: {b}" for b in page.evaluate(FIRSTLOOK_HELP_JS)
                 if not b.startswith(("no #cur_", "no #call_stack", "no #prev_sym", "#cur_", "#call_stack", "#prev_sym"))]
    problems += [f"import: page error {e}" for e in errors]
    page.close()
    return problems, r


def suite_firstlook(ctx):
    failed = False
    for name in [n for n in FIRSTLOOK_POINTS if n in ctx.entries and "dwarf" in ctx.entries[n]]:
        t0 = time.monotonic()
        p, r = firstlook_case(ctx.browser, ctx.base, ctx.shots, name)
        failed |= report("firstlook", p, r, kernel=name, seconds=round(time.monotonic() - t0))
    return failed | report("firstlook_import", *firstlook_import_case(ctx.browser, ctx.base, ctx.shots))
