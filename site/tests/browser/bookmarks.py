"""Browser test suite bookmarks (test_browser.py --suites)."""

import base64
import json
import os
import pathlib
import re
import shutil
import tempfile
import time
import traceback

from playwright.sync_api import Error as PlaywrightError

from tests.browser.common import (
    BM_DIALOG_JS, bm_dialog_save, bm_open_list, bm_state, kernel_entries, page_center, report, show_tab, snap_click,
    snap_rows, snap_save, snap_wait_rows, snapdiff_guard, wait_for,
)
from tests.browser.snapdiff_more import onecount_run
from tests.browser.bookmarks_run import bookmarks_run


# Storage that fails on demand: setItem throws a QuotaExceededError while
# window.__bmFail is set; every access throws (a browser keeping no data
# for the page) when the URL has nostore=1.
BM_STORAGE_JS = """(() => {
  if (new URLSearchParams(location.search).get('nostore') === '1') {
    Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('no storage here', 'SecurityError'); } });
    return;
  }
  const set = Storage.prototype.setItem;
  Storage.prototype.setItem = function (k, v) {
    if (window.__bmFail) throw new DOMException('full', 'QuotaExceededError');
    return set.call(this, k, v);
  };
})();"""


def bm_status(page):
    return page.text_content("#bm_status") or ""


def bookmarks_edges(browser, base, shots):
    """Bookmarks off the main path, on hello paused at 1280: B in the
    About, Page and Memory tabs; import of a file that is no JSON, of
    JSON without bookmarks, and of a list with one new, one already there
    and one unreadable entry; delete and Undo; another page of the same
    site sees a change at once (the storage event); a link that cannot be
    read; a link from another build of hello (goes there, says so); the
    switch for a kernel that is not listed; storing failing (quota) and a
    browser that keeps no data: the list says so."""
    problems, r = [], {}
    ctx = browser.new_context(viewport={"width": 1280, "height": 1000})
    ctx.add_init_script(BM_STORAGE_JS)
    page = ctx.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    url = f"{base}/index.html?kernel=hello&paused=1"
    page.goto(url + "#machine")
    wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 60)
    tmp = tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP"))
    try:
        # B in three tabs: the machine's position (About: no place of its own;
        # in the Machine tab B is the VM's key), the Page tab's page, the
        # Memory tab's page.
        kinds = {}
        for tab, kind in (("about", "pos"), ("page", None), ("memory", "page")):
            show_tab(page, tab)
            page.click(f'.tabbar [role=tab][data-tab="{tab}"]')
            if tab == "page":
                page.evaluate("MKB.selectPage(0x100000 / 4096)")
                time.sleep(0.5)
            page.mouse.move(5, 5)
            page.evaluate("document.activeElement && document.activeElement.blur && document.activeElement.blur()")
            page.keyboard.press("b")
            got = bm_dialog_save(page, problems, "edges", kind or (page.evaluate(BM_DIALOG_JS) or {}).get("kind", "?"))
            kinds[tab] = got and got["kind"]
        r["b_key"] = kinds
        if kinds.get("about") != "pos" or kinds.get("page") not in ("code", "page") or kinds.get("memory") != "page":
            problems.append(f"B in the About, Page and Memory tabs: {kinds}, want pos, code or page, page")
        st = bm_state(page)
        pos = next((b for b in st["list"] if b["kind"] == "pos"), None)
        if not pos:
            problems.append(f"no machine position bookmark to go on with: {st['list']}")
            return problems, r
        bm_open_list(page, False)
        # Import: no JSON, no bookmarks, a mixed list.
        d = pathlib.Path(tmp.name)
        files = {"junk.json": "this is not JSON", "nolist.json": json.dumps({"format": "x"}),
                 "mixed.json": json.dumps({"bookmarks": [pos, {"name": 3, "kind": "nope"}, {**pos, "id": "bnew1", "name": "another one"}]})}
        want = {"junk.json": "junk.json is not a bookmarks file", "nolist.json": "nolist.json has no bookmarks",
                "mixed.json": "Imported 1 bookmarks from mixed.json, 1 were here already, 1 unreadable."}
        for n, text in files.items():
            (d / n).write_text(text)
            page.set_input_files("#bm_import_file", str(d / n))
            ok = wait_for(lambda: want[n] in bm_status(page), 5)
            r["import_" + n] = bm_status(page)[:100]
            if not ok:
                problems.append(f"import {n}: status {bm_status(page)!r}, want {want[n]!r}")
        n0 = len(bm_state(page)["list"])
        # Another page of the site (same storage) follows.
        other = ctx.new_page()
        other.goto(url + "#machine")
        wait_for(lambda: other.evaluate("typeof window.MKB?.BM === 'object' && MKB.BM.list.length") == n0, 30)
        # Delete, Undo.
        page.click('#bm_list .bmrow[data-id="bnew1"] .bmdel')
        wait_for(lambda: len(bm_state(page)["list"]) == n0 - 1, 3)
        seen = wait_for(lambda: other.evaluate("MKB.BM.list.length") == n0 - 1, 5)
        r["other_page_follows"] = seen
        if not seen:
            problems.append(f"another page of the site still lists {other.evaluate('MKB.BM.list.length')} bookmarks after a delete, want {n0 - 1}")
        page.click("#bm_status button")
        if not wait_for(lambda: len(bm_state(page)["list"]) == n0 and "“another one” is back." in bm_status(page), 3):
            problems.append(f"Undo of a delete: {len(bm_state(page)['list'])} bookmarks, status {bm_status(page)!r}")
        other.close()
        # A bookmark of a kernel the menu does not list: its switch says so.
        (d / "alien.json").write_text(json.dumps([{**pos, "id": "balien", "name": "alien", "kernel": "no-such-kernel", "kernelKey": "f" * 64}]))
        page.set_input_files("#bm_import_file", str(d / "alien.json"))
        wait_for(lambda: page.locator('#bm_list .bmrow[data-id="balien"] .bmswitch').count() > 0, 5)
        page.click('#bm_list .bmrow[data-id="balien"] .bmswitch')
        if "“no-such-kernel” is not one of the listed kernels" not in bm_status(page):
            problems.append(f"switch to a kernel not listed: {bm_status(page)!r}")
        # Storing fails: the list says the bookmarks are not kept.
        page.evaluate("window.__bmFail = true")
        page.click('#bm_list .bmrow[data-id="balien"] .bmdel')
        wait_for(lambda: page.is_visible("#bm_store"), 3)
        r["store_fail"] = page.text_content("#bm_store")
        if "storing them failed (QuotaExceededError)" not in (r["store_fail"] or ""):
            problems.append(f"storing failing: #bm_store {r['store_fail']!r}")
        page.evaluate("window.__bmFail = false")
        page.screenshot(path=str(shots / "bookmarks-edges-1280.png"))
        page.close()
        # Links: one that cannot be read, one from another build of hello.
        p2 = ctx.new_page()
        p2.on("pageerror", lambda e: errors.append(str(e)[:200]))
        p2.goto(url + "#bm=@@not-base64@@")
        if not wait_for(lambda: "The link's bookmark could not be read." in bm_status(p2), 30):
            problems.append(f"a link that cannot be read: status {bm_status(p2)!r}")
        data = {"n": "elsewhere", "k": pos["kind"], "t": pos["t"], "kn": "hello", "kk": "0" * 64}
        b64 = base64.urlsafe_b64encode(json.dumps(data).encode()).decode().rstrip("=")
        p2.goto(url + "#bm=" + b64)
        ok = wait_for(lambda: "“elsewhere” was made with another build of hello" in bm_status(p2), 60)
        r["other_build"] = bm_status(p2)[:120]
        if not ok:
            problems.append(f"a link from another build of hello: status {bm_status(p2)!r}")
        p2.close()
        # A browser that keeps no data for the page.
        p3 = ctx.new_page()
        p3.on("pageerror", lambda e: errors.append(str(e)[:200]))
        p3.goto(f"{base}/index.html?kernel=hello&paused=1&nostore=1#machine")
        wait_for(lambda: p3.evaluate("typeof window.MKB?.BM === 'object'"), 30)
        bm_open_list(p3, False)
        r["no_storage"] = p3.text_content("#bm_store")
        if "this browser keeps no data for the page (SecurityError)" not in (r["no_storage"] or "") or not p3.is_visible("#bm_store"):
            problems.append(f"no storage: #bm_store {r['no_storage']!r}")
        p3.screenshot(path=str(shots / "bookmarks-nostore-1280.png"))
    finally:
        tmp.cleanup()
        if errors:
            problems.append(f"page errors: {errors[:3]}")
        ctx.close()
    return problems, r


def bookmarks_unreachable(browser, base, shots):
    """hello to its end on the virtual clock at 1280 light: the Memory
    tab's ☆ offers the page EIP is on, Shift + right click on its square
    opens no dialog; a position bookmark (with its run),
    a snapshot and the run's file; the bookmark's 🔗 copies its link (the
    clipboard holds it) or, when the browser refuses, says to copy it from
    the context menu; the link opened again finds the bookmark already
    there; Import opens the file dialog and refuses a file that is no
    JSON. Then on hello-os paused at the reset vector (the Page tab's ☆
    offers a line of EIP's page, the Calls tab's with no function picked
    says there is nothing to bookmark) with hello's bzImage answering 404:
    the snapshot's Switch and Replay, Replay a file of hello's run, the
    bookmark's jump and its Replay each say the fetch failed (the Switch
    and the jump also where they were started, and the jump is dropped),
    and hello-os keeps running."""
    problems, r, errors = [], {}, []
    ctx = browser.new_context(viewport={"width": 1280, "height": 900}, accept_downloads=True)
    # No service worker: hello's bzImage from its cache would not fail.
    ctx.add_init_script("if (navigator.serviceWorker) navigator.serviceWorker.register = () => Promise.reject(new DOMException('not allowed by the test', 'SecurityError'));")
    page = ctx.new_page()
    page.on("pageerror", lambda e: errors.append(str(e)[:200]))
    tmp = pathlib.Path(tempfile.mkdtemp())
    tag = "unreachable"
    try:
        page.goto(f"{base}/index.html?kernel=hello&mips=10", timeout=120000)
        if not wait_for(lambda: page.get_attribute("#end_banner", "data-kind"), 120, 0.5):
            return [f"{tag}: hello did not end in 120 s"], r
        wait_for(lambda: page.evaluate("MKB.runState") == "paused", 30)
        show_tab(page, "memory")
        page.click("#bm_add_mem")
        eip_page = page.evaluate("MKB.stepper.nextPhys() >> 12")
        d = page.evaluate(BM_DIALOG_JS) or {}
        r["memory star"] = [d.get("kind"), d.get("what"), eip_page]
        if d.get("kind") != "page" or f"page {eip_page:#x}" not in d.get("what", ""):
            problems.append(f"{tag}: the Memory tab's ☆ offers {d}, want page {eip_page:#x} (EIP's)")
        page.keyboard.press("Escape")
        if not wait_for(lambda: page.evaluate(BM_DIALOG_JS) is None, 5):
            problems.append(f"{tag}: Escape did not close the Memory tab's bookmark dialog")
        # A right click with Shift on a grid square is the browser's own menu: no dialog.
        x, y = page_center(page, eip_page)
        page.keyboard.down("Shift")
        page.mouse.click(x, y, button="right")
        page.keyboard.up("Shift")
        r["shift right click"] = page.evaluate(BM_DIALOG_JS)
        if r["shift right click"]:
            problems.append(f"{tag}: Shift + right click on a page square opened the bookmark dialog: {r['shift right click']}")
            page.keyboard.press("Escape")
        st = snap_save(page, 1, False)
        snap_id = st["rows"][0]["id"]
        with page.expect_download() as dl:
            page.click("#run_dl")
        run_file = tmp / "hello-run.json"
        run_file.write_bytes(pathlib.Path(dl.value.path()).read_bytes())
        bm_open_list(page, False)
        page.click("#bm_here")
        b = bm_dialog_save(page, problems, tag, "pos", name="hello's end")
        if not b or not b["t"].get("run"):
            return problems + [f"{tag}: the position bookmark has no run: {b}"], r
        # The 🔗: refused by the browser, then allowed.
        bm_open_list(page, False)
        row = f"#bm_list .bmrow[data-id='{b['id']}']"
        link = page.get_attribute(f"{row} a.bmlink", "href")
        page.evaluate("() => { navigator.clipboard.writeText = () => Promise.reject(new DOMException('denied by the test', 'NotAllowedError')); }")
        page.click(f"{row} a.bmlink")
        wait_for(lambda: bm_status(page).startswith("Copy"), 5)
        refused = bm_status(page)
        page.reload()
        wait_for(lambda: page.evaluate("typeof window.MKB?.stepper !== 'undefined' && !!MKB.stepper"), 120)
        ctx.grant_permissions(["clipboard-read", "clipboard-write"], origin=base)
        bm_open_list(page, False)
        page.click(f"{row} a.bmlink")
        wait_for(lambda: bm_status(page).startswith("Copied"), 5)
        copied = bm_status(page)
        clip = page.evaluate("navigator.clipboard.readText()")
        r["link"] = [refused, copied, clip == link, page.url]
        if refused != "Copy the link from its context menu.":
            problems.append(f"{tag}: the 🔗 when the clipboard refuses: {refused!r}")
        if copied != "Copied the link to “hello's end”." or clip != link or "#bm=" in page.url:
            problems.append(f"{tag}: the 🔗: {copied!r}, the clipboard {clip[:80]!r} (want {link[:80]!r}), the page went to {page.url}")
        # The link again: the bookmark is there already.
        page.goto(link)
        wait_for(lambda: bm_status(page).startswith("Opened"), 60)
        n = len(bm_state(page)["list"])
        r["link again"] = [bm_status(page), n]
        if bm_status(page) != "Opened “hello's end” from a link." or n != 1:
            problems.append(f"{tag}: the link of a bookmark already listed: {bm_status(page)!r}, {n} bookmarks (want 1)")
        # Import… opens the file dialog. The link's jump lands after its
        # status line and closes the menu (CI run 43600287802: the menu was
        # closed when Import… was clicked): open the list once it landed.
        wait_for(lambda: "#page/" in page.url, 10)
        time.sleep(0.5)
        if not wait_for(lambda: bm_open_list(page, False) and page.is_visible("#bm_import"), 10):
            problems.append(f"{tag}: the bookmarks list with Import… did not open after the link's jump")
        (tmp / "junk.json").write_text("no bookmarks here")
        with page.expect_file_chooser() as fc:
            page.click("#bm_import")
        fc.value.set_files(str(tmp / "junk.json"))
        wait_for(lambda: "junk.json" in bm_status(page), 5)
        r["import dialog"] = bm_status(page)
        if not bm_status(page).startswith("junk.json is not a bookmarks file: "):
            problems.append(f"{tag}: Import through its file dialog: {bm_status(page)!r}")

        # hello's bzImage gone: everything that would load hello says so.
        if "hello-os" not in kernel_entries():
            return problems, r
        ctx.route("**/kernels/hello/bzImage*", lambda route: route.fulfill(status=404, body="gone"))
        page.goto(f"{base}/index.html?kernel=hello-os&paused=1#snapshots")
        wait_for(lambda: page.get_attribute("#dbg_state", "data-state") == "paused", 120)
        # Paused at the reset vector: the Page tab's ☆ offers the line at
        # EIP; the Calls tab's, with no function picked, has nothing to add.
        show_tab(page, "page")
        page.click("#bm_add_page")
        d = page.evaluate(BM_DIALOG_JS) or {}
        page.keyboard.press("Escape")
        eip = page.evaluate("MKB.stepper.nextPhys()")
        show_tab(page, "calls")
        page.click("#bm_add_calls")
        r["page and calls stars"] = [d.get("kind"), d.get("what"), eip, bm_status(page), page.evaluate(BM_DIALOG_JS)]
        m = re.fullmatch(r"instruction: (0x[0-9a-f]+)", d.get("what", ""))
        if d.get("kind") != "code" or not m or int(m.group(1), 16) >> 12 != eip >> 12:
            problems.append(f"{tag}: the Page tab's ☆ paused at the reset vector offers {d}, want a line of EIP's page {eip >> 12:#x}")
        if bm_status(page) != "Nothing to bookmark here: pick a line, a page, a box or a frame first." or page.evaluate(BM_DIALOG_JS):
            problems.append(f"{tag}: the Calls tab's ☆ with no function picked: {bm_status(page)!r}, dialog {page.evaluate(BM_DIALOG_JS)}")
        show_tab(page, "snapshots")
        snap_wait_rows(page, 1)
        wait_for(lambda: all(x["kernel"] for x in snap_rows(page)["rows"]), 15)
        failed = "Error: kernels/hello/bzImage: HTTP 404"
        still = lambda: page.evaluate("MKB.image.name") == "hello-os" and page.get_attribute("#dbg_state", "data-state") == "paused"

        def after(what, click, where, want):
            page.evaluate(f"document.getElementById('{where}').textContent = ''")
            click()
            ok = wait_for(lambda: want in page.text_content(f"#{where}"), 15)
            got = page.text_content(f"#{where}")
            r[what] = got[:120]
            if not ok or not still():
                problems.append(f"{tag}: {what} with hello's bzImage at 404: {got!r} (want {want!r}), "
                                f"still on hello-os: {still()}")
        after("snapshot switch", lambda: snap_click(page, f"#snapshots tr[data-id='{snap_id}'] button.snapswitch", False), "status", failed)
        # The Snapshots tab says so too (FAILS on 0e4bd70a3: "Switching to hello with 2,048 KiB..." stayed).
        want = "Switching to hello failed: kernels/hello/bzImage: HTTP 404."
        wait_for(lambda: page.text_content("#snap_status") == want, 5)
        r["snapshot switch note"] = page.text_content("#snap_status")
        if page.text_content("#snap_status") != want:
            problems.append(f"{tag}: the Snapshots tab after the failed switch: {page.text_content('#snap_status')!r}, want {want!r}")
        show_tab(page, "snapshots")
        after("snapshot replay", lambda: snap_click(page, f"#snapshots tr[data-id='{snap_id}'] button.snapreplay", False),
              "run_status", "Replay failed: " + failed)
        show_tab(page, "snapshots")
        after("replay a file", lambda: page.set_input_files("#run_file", str(run_file)), "run_status", "Replay failed: " + failed)
        bm_open_list(page, False)
        after("bookmark jump", lambda: page.click(f"{row} .bmgo"), "status", failed)
        # The list says so (FAILS on 0e4bd70a3: "Switching to hello for ..." stayed) and the
        # jump is dropped: hello loaded later does not go there.
        want = "Switching to hello for “hello's end” failed: kernels/hello/bzImage: HTTP 404."
        wait_for(lambda: bm_status(page) == want, 5)
        r["bookmark jump note"] = [bm_status(page), page.evaluate("MKB.BM.pending")]
        if r["bookmark jump note"] != [want, None]:
            problems.append(f"{tag}: the bookmark's failed jump to hello: note {bm_status(page)!r} (want {want!r}), "
                            f"pending {page.evaluate('MKB.BM.pending')}")
        bm_open_list(page, False)
        after("bookmark replay", lambda: page.click(f"{row} .bmreplay"), "run_status", "Replay failed: " + failed)
        page.screenshot(path=str(shots / "bookmarks-unreachable-1280-light.png"))
        if errors:
            problems.append(f"{tag}: page errors {errors[:3]}")
    except (PlaywrightError, IndexError, KeyError, TypeError) as e:
        # Name the step, keep what failed before it, and show the page then
        # (CI run 43588908776 reported only "Page.click: Timeout").
        tbk = [x for x in traceback.extract_tb(e.__traceback__) if os.path.dirname(x.filename) == os.path.dirname(__file__)]
        at = f" at {os.path.basename(tbk[-1].filename)}:{tbk[-1].lineno} ({tbk[-1].line})" if tbk else ""
        try:
            state = page.evaluate("({ url: location.href, tab: MKB.tab, dialog: !document.getElementById('bm_dialog').hidden, "
                                  "menu: !document.getElementById('runbadge_menu').hidden, runState: MKB.runState, "
                                  "status: document.getElementById('status').textContent, bm: document.getElementById('bm_status').textContent })")
            page.screenshot(path=str(shots / "bookmarks-unreachable-failed.png"))
        except PlaywrightError as e2:
            state = f"(page not readable: {str(e2)[:80]})"
        problems.append(f"{tag}: {type(e).__name__}{at}: {(str(e).splitlines() or [''])[0][:200]}; the page then: {state}")
    finally:
        ctx.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return problems, r


def bookmarks_case(browser, base, shots, linux):
    out = []
    names = kernel_entries()
    runs = [("hello", "light", False, "hello-os" if "hello-os" in names else None), ("hello", "dark", True, None)]
    if linux:
        runs += [("minified-linux", "dark", False, None), ("minified-linux", "light", True, None)]
    for name, scheme, phone, other in runs:
        t0 = time.monotonic()
        problems = []
        try:
            problems, r = bookmarks_run(browser, base, shots, name, scheme, phone, other, problems)
        except Exception as e:  # a missing control times out in Playwright: report it (and where), go on
            tb = traceback.extract_tb(e.__traceback__)
            at = ([f for f in tb if os.path.dirname(f.filename) == os.path.dirname(__file__)] or tb)[-1]
            problems, r = problems + [f"{name} {390 if phone else 1280} {scheme}: {type(e).__name__} at {os.path.basename(at.filename)}:{at.lineno} "
                                      f"({at.line}): {str(e)[:300]}"], {}
        out.append(report("bookmarks", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280,
                          seconds=round(time.monotonic() - t0)))
    out.append(report("bookmarks edges", *bookmarks_edges(browser, base, shots)))
    out.append(report("bookmarks unreachable", *snapdiff_guard(bookmarks_unreachable, browser, base, shots)))
    for name, scheme, phone in [("hello", "light", False)] + ([("minified-linux", "dark", True)] if linux else []):
        problems, r = snapdiff_guard(onecount_run, browser, base, shots, name, scheme, phone)
        out.append(report("bookmarks one count", problems, r, kernel=name, scheme=scheme, width=390 if phone else 1280))
    return any(out)


def suite_bookmarks(ctx):
    return bookmarks_case(ctx.browser, ctx.base, ctx.shots, "minified-linux" in ctx.entries)
