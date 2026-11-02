#!/usr/bin/env python3
"""End-to-end check of symbol import with a real distro kernel.

Downloads (once, SHA-256 checked) a Debian 686 kernel (--version: bullseye's 5.10.0-30-686,
the default, or bookworm's 6.1.0-50-686, the page's sample) and its -dbg package, boots it with "nokaslr", imports
the real System.map (and, in a second run, the unstripped vmlinux), pauses
the CPU several times and checks that the symbol the Debugger shows for EIP
is the function llvm-symbolizer finds at that address in the -dbg vmlinux.

  site/test_distro_symbols.py [--url URL] [--cache DIR] [--pauses N] [--version 5.10.0-30-686|6.1.0-50-686]

URL defaults to a local http.server over site/; pass the live site to check
a deploy. The -dbg package is ~720 MB, so this test runs on demand, not in
CI.
"""

import argparse
import functools
import hashlib
import http.server
import os
import pathlib
import subprocess
import sys
import threading
import time
import urllib.request

from playwright.sync_api import sync_playwright

SITE = pathlib.Path(__file__).resolve().parent
MIRROR = "https://deb.debian.org/debian/"
# --version: the kernel ABI name and its two packages (pool path: SHA-256).
VERSIONS = {
    "5.10.0-30-686": {
        "pool/main/l/linux-signed-i386/linux-image-5.10.0-30-686_5.10.218-1_i386.deb":
            "29be9d7ebf1f79048298218f3dc5c746657df07538bf5b7d322c85d80576858d",
        "pool/main/l/linux/linux-image-5.10.0-30-686-dbg_5.10.218-1_i386.deb":
            "b3fc416db5d8f4441860d95a4c6e06d3c0b2be67e259ee20cedabc7b79dc45c6",
    },
    # The page's sample kernel (the Pages workflow pins the same hashes).
    "6.1.0-50-686": {
        "pool/main/l/linux-signed-i386/linux-image-6.1.0-50-686_6.1.176-1_i386.deb":
            "526c469c5148a2b61c4241c3f6575427096575e0ffef9541b6729351c600b3e4",
        "pool/main/l/linux/linux-image-6.1.0-50-686-dbg_6.1.176-1_i386.deb":
            "4625e3cd446ff2ade7776492bfdecd60b2fb6a72b8b99a97e04f0e472dfeeb84",
    },
}
VER = "5.10.0-30-686"
PKGS = VERSIONS[VER]
FILES = {}


def select(ver):
    global VER, PKGS, FILES
    VER = ver
    PKGS = VERSIONS[ver]
    FILES = {
        "bz": "boot/vmlinuz-" + VER,
        "map": "usr/lib/debug/boot/System.map-" + VER,
        "elf": "usr/lib/debug/boot/vmlinux-" + VER,
    }


select(VER)


def fetch(cache):
    cache.mkdir(parents=True, exist_ok=True)
    for path, sha in PKGS.items():
        deb = cache / pathlib.Path(path).name
        if not deb.exists():
            print("downloading", deb.name, flush=True)
            urllib.request.urlretrieve(MIRROR + path, deb)
        h = hashlib.sha256()
        with open(deb, "rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if h.hexdigest() != sha:
            sys.exit(f"{deb.name}: SHA-256 mismatch")
        tar = subprocess.Popen(["dpkg-deb", "--fsys-tarfile", str(deb)], stdout=subprocess.PIPE)
        subprocess.run(["tar", "-x", "-C", str(cache), "--wildcards"] +
                       ["./" + FILES[k] for k in FILES], stdin=tar.stdout,
                       stderr=subprocess.DEVNULL, check=False)
        tar.wait()
    for k, rel in FILES.items():
        if not (cache / rel).exists():
            sys.exit(f"missing {rel} after extraction")
    return {k: cache / rel for k, rel in FILES.items()}


def symbolize(vmlinux, addrs):
    out = subprocess.run(["llvm-symbolizer", "--obj=" + str(vmlinux), "--no-inlines"] +
                         [hex(a) for a in addrs], capture_output=True, text=True, check=True).stdout
    names = [blk.splitlines()[0] for blk in out.strip().split("\n\n")]
    return dict(zip(addrs, names))


def serve():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(SITE))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{srv.server_address[1]}/"


def run(browser, url, files, which, pauses):
    page = browser.new_page(viewport={"width": 1280, "height": 900})
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    # count off: run the distro kernel under the JIT, pause and inspect.
    page.goto(url + "?speed=full")
    page.wait_for_function("typeof stepper !== 'undefined' || document.getElementById('file')", timeout=60000)
    page.fill("#cmdline", "nokaslr console=tty0")
    if page.is_checked("#count"):
        page.uncheck("#count")
    page.set_input_files("#file", str(files["bz"]))
    page.wait_for_timeout(1500)
    if page.input_value("#cmdline") != "nokaslr console=tty0":
        errors.append("cmdline typed before choosing the file was not kept: " + repr(page.input_value("#cmdline")))
        page.fill("#cmdline", "nokaslr console=tty0")
    page.set_input_files("#symfile", str(files[which]))
    t0 = time.time()
    while time.time() - t0 < 120 and not page.inner_text("#sym_status").strip():
        page.wait_for_timeout(500)
    status = page.inner_text("#sym_status")
    page.click("#tab_debugger")
    samples = []
    for i in range(pauses):
        page.wait_for_timeout(4000)
        page.click("#dbg_pause")
        page.wait_for_function("document.getElementById('dbg_state').dataset.state === 'paused'", timeout=30000)
        page.wait_for_timeout(500)
        s = page.evaluate("""(() => { const r = stepper.regs(); const c = document.getElementById('cur_sym');
            return { eip: r.eip >>> 0, cpl: r.cpl, name: c ? c.dataset.name : null }; })()""")
        samples.append(s)
        page.click("#dbg_continue")
    page.screenshot(path=f"/tmp/test_distro_{which}.png")
    page.close()
    return status, samples, errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url")
    ap.add_argument("--cache", default=os.environ.get("MKB_DISTRO_CACHE", "/tmp/mkb-distro"))
    ap.add_argument("--pauses", type=int, default=4)
    ap.add_argument("--version", default="5.10.0-30-686", choices=sorted(VERSIONS),
                    help="Debian kernel ABI name (default %(default)s)")
    a = ap.parse_args()
    select(a.version)
    files = fetch(pathlib.Path(a.cache))
    url = a.url or serve()
    ok = True
    with sync_playwright() as p:
        browser = p.chromium.launch()
        for which in ("map", "elf"):
            status, samples, errors = run(browser, url, files, which, a.pauses)
            kernel = [s for s in samples if s["eip"] >= 0xC0000000]
            truth = symbolize(files["elf"], [s["eip"] for s in kernel]) if kernel else {}
            print(f"[{which}] import: {status[:160]}")
            good = 0
            for s in samples:
                want = truth.get(s["eip"], "(not kernel text)")
                match = s["eip"] >= 0xC0000000 and s["name"] == want
                good += match
                print(f"[{which}] eip={s['eip']:#010x} cpl={s['cpl']} page={s['name']!r} symbolizer={want!r} {'OK' if match else 'MISMATCH'}")
            if errors:
                print(f"[{which}] page errors: {errors}")
            if not kernel or good != len(kernel) or errors:
                ok = False
        browser.close()
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
