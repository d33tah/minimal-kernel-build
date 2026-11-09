#!/usr/bin/env python3
"""Install the page as a web app in a real (headful) Chromium and launch it.

The pwa suite of test_browser.py runs headless, where Chromium never offers
to install and Page.getInstallabilityErrors answers [] even for a page with
no manifest, so it cannot tell an installable page from one that is not.
This test runs Chromium with a window (under Xvfb), a fresh profile per
case, and collects four pieces of evidence:

  1. beforeinstallprompt fires (--bypass-app-banner-engagement-checks
     skips the "user engaged with the site" wait; the event is caught by
     an init script);
  2. Page.getInstallabilityErrors is empty and Page.getAppManifest parses
     the manifest with no errors;
  3. PWA.install installs it: PWA.getOsAppState knows the manifest id and
     chrome://web-app-internals lists the app with our name, scope,
     start_url and icons;
  4. PWA.launch opens it in its own window, in standalone display mode,
     and hello boots there to "Hello, World!" (an Xvfb screenshot of the
     whole display shows the window).

Plus our own requirement: a service worker controls the page (offline).

Without --base it serves a deployed copy of site/ (test_browser.pwa_deploy)
at http://localhost:PORT/minimal-kernel-build/ and, unless --no-controls,
four broken copies that must FAIL, each for its own reason (otherwise the
test does not discriminate): the manifest link removed, the icons 404, the
service worker never registered, start_url outside the scope.

The PWA domain of the DevTools protocol answers "wasn't found" over
--remote-debugging-port; Playwright's launch talks over a pipe, where it
works. README.md, "Offline and installing".

Usage (needs an X display; Xvfb is enough):
  xvfb-run -a site/test_pwa_install.py                 local copy + controls
  xvfb-run -a site/test_pwa_install.py --base https://d33.pl/minimal-kernel-build
  ... --chromium /usr/bin/chromium                      another Chromium build

Needs: pip install playwright pillow && playwright install chromium
"""

import argparse
import json
import os
import pathlib
import shutil
import sys
import tempfile
import time

from playwright.sync_api import Error as PlaywrightError, sync_playwright

import test_browser as tb

SITE = tb.SITE
SUBPATH = "minimal-kernel-build"

BIP_JS = """
window.__bip = 0;
window.addEventListener('beforeinstallprompt', (e) => {
    e.preventDefault();
    window.__bip++;
    window.__bipEvent = e;
});
"""


def log(*a):
    print(*a, flush=True)


# ---- broken copies (negative controls) -----------------------------------

def edit(path, old, new):
    s = path.read_text()
    if old not in s:
        raise SystemExit(f"control: {old!r} not in {path} (the site changed; update the control)")
    path.write_text(s.replace(old, new))


def control_no_manifest(d):
    edit(d / "index.html", '<link rel="manifest" href="manifest.webmanifest">', "")


def control_icons_404(d):
    shutil.rmtree(d / "icons")


def control_no_sw(d):
    edit(d / "pwa.js", 'PWA.reg = await navigator.serviceWorker.register("sw.js", { updateViaCache: "none" });',
         'throw new Error("registration removed by the no-service-worker control");')


def control_start_outside(d):
    edit(d / "manifest.webmanifest", '"start_url": "./"', '"start_url": "../elsewhere/"')


# name -> (change, what the failure must mention)
CONTROLS = {
    "no-manifest-link": (control_no_manifest, "manifest"),
    "icons-404": (control_icons_404, "icon"),
    "no-service-worker": (control_no_sw, "service worker"),
    "start-url-outside-scope": (control_start_outside, "scope"),
}


def deploy_copy(parent, name, change=None):
    """A deployed copy of site/ at parent/name/minimal-kernel-build."""
    root = parent / name
    d = root / SUBPATH
    d.mkdir(parents=True)
    tb.pwa_deploy(d, tb.PWA_V[0])
    if change:
        change(d)
    return root


# ---- one case --------------------------------------------------------------

# The page draws into shadow roots: inner_text("body") is empty.
WAI_TEXT_JS = """() => {
    const out = [];
    const walk = (n) => {
        if (n.shadowRoot)
            walk(n.shadowRoot);
        for (const ch of n.childNodes)
            if (ch.nodeType === 3) out.push(ch.textContent); else walk(ch);
    };
    walk(document.body);
    return out.join('');
}"""


def web_app_internals(ctx):
    """chrome://web-app-internals as text (a JSON dump of every installed
    app, drawn by a module script)."""
    page = ctx.new_page()
    try:
        page.goto("chrome://web-app-internals")
        text = lambda: page.evaluate(WAI_TEXT_JS)
        tb.wait_for(lambda: "InstalledWebApps" in text(), 15)
        return text()
    except PlaywrightError as e:
        return f"(could not read chrome://web-app-internals: {str(e)[:200]})"
    finally:
        page.close()


def x_screenshot(path):
    try:
        from PIL import ImageGrab
        ImageGrab.grab(xdisplay=os.environ.get("DISPLAY")).save(path)
        return str(path)
    except Exception as e:  # noqa: BLE001 (evidence only; the asserts do not depend on it)
        return f"(no X screenshot: {e})"


def run_case(pw, exe, base, label, shots, timeout, boot=True):
    """Install and launch base (no trailing slash). Returns (problems, r)."""
    problems, r = [], {"case": label, "base": base}
    prof = tempfile.mkdtemp(prefix="pwa-profile-", dir=os.environ.get("MKB_TMP"))
    ctx = None
    try:
        ctx = pw.chromium.launch_persistent_context(
            prof, headless=False, executable_path=exe, no_viewport=True,
            args=["--bypass-app-banner-engagement-checks", "--window-size=1280,900", "--no-first-run",
                  "--no-default-browser-check", "--password-store=basic"])
        ctx.add_init_script(BIP_JS)
        page = ctx.pages[0] if ctx.pages else ctx.new_page()
        errors = []
        page.on("pageerror", lambda e: errors.append(str(e)[:200]))
        r["chromium"] = ctx.browser.version if ctx.browser else None
        start = base + "/"
        page.goto(start)
        if not page.evaluate("isSecureContext"):
            problems.append(f"{start} is not a secure context (https or localhost): nothing can be installed")
            return problems, r

        # Our own requirement: a service worker controls the page (pwa.js
        # registers it after the page's own loads; sw.js claims the page).
        r["sw_controls"] = tb.wait_for(lambda: page.evaluate("!!navigator.serviceWorker?.controller"), 60)
        if not r["sw_controls"]:
            problems.append("no service worker controls the page (no offline use)")

        # 1. beforeinstallprompt.
        r["beforeinstallprompt"] = tb.wait_for(lambda: page.evaluate("window.__bip") > 0, 20)
        if not r["beforeinstallprompt"]:
            problems.append("beforeinstallprompt did not fire: Chromium does not offer to install the page")

        # 2. Chromium's installability check and manifest parse.
        cdp = ctx.new_cdp_session(page)
        inst = cdp.send("Page.getInstallabilityErrors").get("installabilityErrors", [])
        r["installability_errors"] = inst
        if inst:
            problems.append(f"Chromium's installability errors: {[e.get('errorId') for e in inst]}")
        am = cdp.send("Page.getAppManifest")
        pm = am.get("manifest") or {}
        r["manifest_url"] = am.get("url")
        r["manifest_errors"] = am.get("errors")
        r["manifest"] = {k: pm.get(k) for k in ("id", "name", "shortName", "display", "startUrl", "scope")}
        r["manifest_icons"] = [(i.get("sizes"), (i.get("url") or "").rsplit("/", 1)[-1]) for i in pm.get("icons", [])]
        if not am.get("url"):
            problems.append("the page links no manifest (Page.getAppManifest has no URL)")
        if am.get("errors"):
            problems.append(f"manifest parse errors/warnings: {[e.get('message') for e in am['errors']]}")
        if am.get("url") and pm.get("scope") != start:
            problems.append(f"manifest scope {pm.get('scope')!r}, want {start!r}")
        if am.get("url") and pm.get("startUrl") != start:
            problems.append(f"manifest start_url {pm.get('startUrl')!r}, want {start!r}")

        # 3. Install.
        mid = pm.get("id") or start
        r["manifest_id"] = mid
        try:
            cdp.send("PWA.install", {"manifestId": mid})
            r["install"] = "ok"
        except PlaywrightError as e:
            r["install"] = str(e).split("\n")[0][-200:]
            problems.append(f"PWA.install failed: {r['install']}")
        try:
            r["os_app_state"] = cdp.send("PWA.getOsAppState", {"manifestId": mid})
        except PlaywrightError as e:
            r["os_app_state"] = str(e).split("\n")[0][-200:]
            if r["install"] == "ok":
                problems.append(f"installed, yet PWA.getOsAppState does not know it: {r['os_app_state']}")
        installed = r["install"] == "ok" and isinstance(r["os_app_state"], dict)
        if installed:
            # PWA.install records the user's choice as "open in a browser
            # tab" (web-app-internals: user_display_mode kBrowser); the
            # install dialog's default is a window. Choose the window, as a
            # user would, so the launch below shows the manifest's display.
            try:
                cdp.send("PWA.changeAppUserSettings", {"manifestId": mid, "displayMode": "standalone"})
            except PlaywrightError as e:
                problems.append(f"PWA.changeAppUserSettings failed: {str(e).splitlines()[0][-200:]}")
        if installed:
            wai = web_app_internals(ctx)
            (shots / f"pwa-install-{label}-web-app-internals.txt").write_text(wai)
            want = {"name": '"!name": "Kernel in the browser"', "scope": f'"scope": "{start}"',
                    "start_url": f'"start_url": "{start}"', "manifest_id": f'"manifest_id": "{start}"',
                    "display standalone": '"display_mode": "standalone"',
                    "icon-192": "icon-192.png", "icon-512": "icon-512.png", "maskable": "icon-maskable-512.png"}
            r["web_app_internals"] = {k: v in wai for k, v in want.items()}
            missing = [k for k, ok in r["web_app_internals"].items() if not ok]
            if missing:
                problems.append(f"chrome://web-app-internals does not show the app's {missing}")

        # 4. Launch in its own window.
        if installed:
            url = start + ("?kernel=hello" if boot else "")
            app = None
            try:
                # The sync API sees the new page only while it waits on
                # Playwright (expect_page), not in a time.sleep loop.
                with ctx.expect_page(timeout=30000) as new_page:
                    r["launch"] = cdp.send("PWA.launch", {"manifestId": mid, "url": url})
                app = new_page.value
                app.wait_for_load_state()
                if not app.url.startswith(start):
                    problems.append(f"PWA.launch opened {app.url}, want {url}")
            except PlaywrightError as e:
                r.setdefault("launch", None)
                r["launch_error"] = str(e).split("\n")[0][-200:]
                problems.append(f"PWA.launch opened no app window: {r['launch_error']}")
                app = None
            if app:
                r["app_url"] = app.url
                r["standalone"] = app.evaluate("matchMedia('(display-mode: standalone)').matches")
                r["browser_tab"] = app.evaluate("matchMedia('(display-mode: browser)').matches")
                # A tab has the tab strip and the address bar above the page.
                r["chrome_px"] = app.evaluate("outerHeight - innerHeight")
                if not r["standalone"]:
                    problems.append("the launched app is not in standalone display mode")
                if boot:
                    r["hello"] = tb.wait_for(lambda: tb.gate_on_screen(app), timeout)
                    if not r["hello"]:
                        problems.append(f"hello did not boot in the app window: {app.inner_text('#status')!r}")
                time.sleep(1)
                app.screenshot(path=str(shots / f"pwa-install-{label}-app.png"))
                r["x_screenshot"] = x_screenshot(shots / f"pwa-install-{label}-display.png")
            try:
                cdp.send("PWA.uninstall", {"manifestId": mid})
            except PlaywrightError as e:
                r["uninstall"] = str(e).split("\n")[0][-200:]
        if errors:
            r["page_errors"] = errors[:5]
    except PlaywrightError as e:
        problems.append(f"browser: {str(e)[:300]}")
    finally:
        if ctx:
            ctx.close()
        shutil.rmtree(prof, ignore_errors=True)
    return problems, r


# ---- main ------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", help="test this site (e.g. https://d33.pl/minimal-kernel-build) instead of a local copy")
    ap.add_argument("--chromium", help="Chromium executable (default: Playwright's Chromium, not the headless shell)")
    ap.add_argument("--no-controls", action="store_true", help="skip the negative controls")
    ap.add_argument("--only", help="comma-separated cases to run (site and/or control names)")
    ap.add_argument("--timeout", type=float, default=120)
    ap.add_argument("--shots", default=str(SITE / "test-screenshots"))
    args = ap.parse_args()
    if not os.environ.get("DISPLAY"):
        raise SystemExit("needs an X display (run under xvfb-run -a): headless Chromium never offers to install")
    shots = pathlib.Path(args.shots)
    shots.mkdir(parents=True, exist_ok=True)
    only = set(args.only.split(",")) if args.only else None
    failed = []
    results = []
    with sync_playwright() as pw:
        exe = args.chromium or pw.chromium.executable_path
        if args.base:
            cases = [("live", args.base.rstrip("/"), None)]
        else:
            tmp = tempfile.TemporaryDirectory(dir=os.environ.get("MKB_TMP"))
            parent = pathlib.Path(tmp.name)
            cases = [("site", None, None)]
            if not args.no_controls:
                cases += [(n, None, c) for n, c in CONTROLS.items()]
        for label, base, control in cases:
            if only and label not in only:
                continue
            httpd = None
            if base is None:
                root = deploy_copy(parent, label, control[0] if control else None)
                httpd, _ = tb.pwa_server(root)
                base = f"http://localhost:{httpd.server_address[1]}/{SUBPATH}"
            log(f"== {label}: {base}")
            try:
                problems, r = run_case(pw, exe, base, label, shots, args.timeout, boot=control is None)
            finally:
                if httpd:
                    httpd.shutdown()
                    httpd.server_close()
            results.append(r)
            log(json.dumps(r, indent=1, default=str))
            if control is None:
                if problems:
                    failed.append(label)
                    log(f"FAIL {label}: " + "; ".join(problems))
                else:
                    log(f"PASS {label}: installable, installed, launched standalone"
                        + (", hello booted" if r.get("hello") else ""))
            else:
                need = control[1]
                mentions = [p for p in problems if need in p]
                if not problems:
                    failed.append(label)
                    log(f"FAIL control {label}: the broken copy passed (the test does not discriminate)")
                elif not mentions:
                    failed.append(label)
                    log(f"FAIL control {label}: failed, but for no reason about {need!r}: " + "; ".join(problems))
                else:
                    log(f"PASS control {label}: rejected: " + "; ".join(problems))
    (shots / "pwa-install-results.json").write_text(json.dumps(results, indent=1, default=str))
    if failed:
        log("FAILED: " + ", ".join(failed))
        sys.exit(1)
    log("all cases as expected")


if __name__ == "__main__":
    main()
