# Third-party files used by the page

None of these are committed. `fetch-v86.sh` downloads them into `site/v86/`
at build time and checks each against a SHA-256 recorded in the script.

| file | source | pin | licence |
|---|---|---|---|
| `libv86.js`, `v86.wasm` | npm package [`v86`](https://www.npmjs.com/package/v86), `build/` | version 0.5.471 (published 2026-10-09) | BSD-2-Clause, copied to `v86/LICENSE.v86` |
| `seabios.bin`, `vgabios.bin` | [copy/v86](https://github.com/copy/v86) `bios/` | commit `94faf3d6b19ecabc53a4010aaa24252fa6a162e2` (the commit npm 0.5.471 was built from) | SeaBIOS and its VGA BIOS: LGPL-3.0, copied to `v86/COPYING.LESSER` |

To move the pin, change `V86_NPM_VERSION`, `V86_COMMIT` and the checksums in
`fetch-v86.sh` together, and the table above.

The page itself (`index.html`, `app.js`, `stepper.js`, `style.css`) uses
no other libraries. `stepper.js` adds two exports to `v86.wasm` in the
browser after the checksum-verified file is fetched (`read_imm8` and the
interpreter's `run`, found by name in the module's name section); the
code itself is not modified.

## Debian sample kernel

The workflow downloads two Debian bookworm packages (from
snapshot.debian.org, with deb.debian.org as fallback; SHA-256 checked;
cached in the Actions cache, never committed) and deploys the kernel image
and a symbol list made from the debug package:

| package | version | SHA-256 | used |
|---|---|---|---|
| `linux-image-6.1.0-50-686` | 6.1.176-1 | `526c469c5148a2b61c4241c3f6575427096575e0ffef9541b6729351c600b3e4` | `boot/vmlinuz-6.1.0-50-686`, deployed as `kernels/debian-6.1-686/bzImage` |
| `linux-image-6.1.0-50-686-dbg` | 6.1.176-1 | `4625e3cd446ff2ade7776492bfdecd60b2fb6a72b8b99a97e04f0e472dfeeb84` | `usr/lib/debug/boot/vmlinux-6.1.0-50-686`, read by `gen-symbols.py --trim` into `kernels/debian-6.1-686/symbols.json` (only symbol names, addresses and sizes) |

Licence: GPL-2.0 (the Linux kernel; Debian's packaging is under the same
terms). Corresponding source: the Debian source package `linux` version
6.1.176-1 (the signed image is built from it through
`linux-signed-i386` 6.1.176+1):
<https://snapshot.debian.org/package/linux/6.1.176-1/> and
<https://sources.debian.org/src/linux/6.1.176-1/>.
