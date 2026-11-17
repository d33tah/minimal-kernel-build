"""Browser test suite dwarf_forms (test_browser.py --suites)."""

import base64
import os
import pathlib
import re
import subprocess
import tempfile

from tests.browser.common import report, wait_for
from tests.browser.cfi import CFI_OFFSET
from tests.browser.dwarf_forms_checks import dwf_broken, dwf_build, DWF_BUILDS, dwf_checks, DWF_JS
from tests.browser.dwarf_forms_ui import dwf_fail_checks, dwf_ui_checks
from tests.browser.dwarf_forms_hand import dwf_hand_checks


# Broken inputs: the errors the Worker answers (open; else each unit,
# the types, each unit's locals), as the import shows them.
DWF_BAD_JS = """async ([b64, offset]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const w = new Worker('dwarfworker.js' + location.search, { type: 'module' });
    const ask = (m, t) => new Promise((res) => { w.onmessage = (e) => { if (e.data.type === t || e.data.type === 'error') res(e.data); };
        w.postMessage(m); });
    try {
        const o = await ask({ type: 'open', file: new File([bytes], 'bad.elf'), offset }, 'open');
        if (o.type === 'error' || !o.ok) return ['open: ' + (o.message || o.why)];
        const errs = [];
        for (let i = 0; i < o.units; i++) {
            const u = await ask({ type: 'unit', i }, 'unit');
            if (u.type === 'error') errs.push('unit ' + i + ': ' + u.message);
        }
        const t = await ask({ type: 'types' }, 'types');
        if (t.type === 'error') return [...errs, 'types: ' + t.message];
        for (let i = 0; i < o.units; i++) {
            const l = await ask({ type: 'locals', i }, 'locals');
            if (l.type === 'error') errs.push('locals ' + i + ': ' + l.message);
        }
        return errs;
    } finally { w.terminate(); }
}"""


def dwarf_forms_case(browser, base, shots, builds=DWF_BUILDS):
    from tests.dwarf.common import symbolize
    from tests.dwarf.locals import gdb_scopes
    problems, r, seen = [], {}, {}
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    page.goto(f"{base}/index.html?kernel=hello&paused=1")
    wait_for(lambda: page.evaluate("typeof window.MKB?.dwFuncProto === 'function' && typeof window.MKB?.cfiRow === 'function' && typeof window.MKB?.dwExprText === 'function'"), 30)
    with tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP")) as tmp:
        tmp = pathlib.Path(tmp)
        for name in builds:
            elf, why = dwf_build(tmp, name)
            if not elf:
                problems.append(f"{name}: the fixture did not build: {why}")
                continue
            nm = {l.split()[2]: int(l.split()[0], 16) for l in
                  subprocess.run(["llvm-nm", str(elf)], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
            m = re.search(r"\.text\s+PROGBITS\s+([0-9a-f]+) [0-9a-f]+ ([0-9a-f]+)",
                          subprocess.run(["readelf", "-SW", str(elf)], capture_output=True, text=True).stdout)
            lo, hi = int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)
            got = page.evaluate(DWF_JS, [base64.b64encode(elf.read_bytes()).decode(), CFI_OFFSET, lo - CFI_OFFSET, hi - CFI_OFFSET, DWF_BUILDS[name][3]])
            if got.get("open") != "ok" or got.get("error"):
                problems.append(f"{name}: the fixture did not open: {str(got)[:300]}")
                continue
            seen[name] = (got, None, lo, hi)
            p = dwf_checks(name, elf, got, lo, hi, nm, symbolize, gdb_scopes, seen)
            r[name] = "ok" if not p else f"{len(p)} problems"
            problems += p
            if name == "clang-dwarf5":
                for what, data, want in dwf_broken(tmp, elf):
                    have = page.evaluate(DWF_BAD_JS, [base64.b64encode(data).decode(), CFI_OFFSET]) if data else None
                    r["broken: " + what] = have
                    if have != want:
                        problems.append(f"broken input, {what}: the import says {have!r}, want {want!r}")
        hd = pathlib.Path(tmp) / "hand"
        hd.mkdir()
        p, r["hand"] = dwf_hand_checks(page, hd, symbolize, gdb_scopes)
        problems += p
        page.close()
        ui = pathlib.Path(tmp) / "ui"
        ui.mkdir()
        p, r["import"] = dwf_ui_checks(browser, base, shots, ui, symbolize, gdb_scopes)
        problems += p
        if (ui / "ui.elf").exists():
            p, r["failures"] = dwf_fail_checks(browser, base, ui / "ui.elf")
            problems += p
    if errors:
        problems.append(f"page errors: {errors[:3]}")
    return problems, r


def suite_dwarf_forms(ctx):
    return report("dwarf_forms", *dwarf_forms_case(ctx.browser, ctx.base, ctx.shots))
