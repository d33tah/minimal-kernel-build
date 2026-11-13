# Third-party files used by the page

`fetch-v86.sh` downloads v86's `libv86.js`, its BIOSes and iced-x86 into
`site/v86/` and `site/iced/` at build time (never committed) and checks
each against a SHA-256 recorded in the script. `gen-v86.sh` builds
`v86.wasm` from v86's source with `v86-det.patch` and checks its SHA-256
(the workflow's `v86` job builds it, the site job checks it again).
lz4-wasm is committed in `site/lz4/`.

| file | source | pin | licence |
|---|---|---|---|
| `libv86.js` | npm package [`v86`](https://www.npmjs.com/package/v86), `build/` | version 0.5.471 (published 2026-10-09) | BSD-2-Clause, copied to `v86/LICENSE.v86` |
| `v86.wasm` | built by `gen-v86.sh` from [copy/v86](https://github.com/copy/v86) with `v86-det.patch` (this repository: hash maps with a fixed hasher, `set_tsc` from the clock, the export `jit_reset_hidden`, R4; the instruction budget `jit_set_budget` and the counters `jit_stats`, R5) in Docker: `ubuntu:24.04@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852`, clang-18, lld-18 and their LLVM libraries at `1:18.1.3-1ubuntu1` and nodejs at `18.19.1+dfsg-6ubuntu5` (the versions of Ubuntu's apt snapshot `20261101T000000Z`; from the archive while it has them, else from that snapshot), Rust 1.90.0, `RUSTFLAGS=-C symbol-mangling-version=v0`; SHA-256 `c4e8e92eab6da3d11c7f62e17230307ac260818030fb83ac0160a22fa8395257` (two builds on one machine, one on another, and the workflow's `v86` job, which builds it on every run and uses its cached build of the same script and patch, SHA-256 checked, only when the build cannot download its toolchain) | commit `94faf3d6b19ecabc53a4010aaa24252fa6a162e2` (the commit npm 0.5.471 was built from) | BSD-2-Clause (v86), the patch under the same terms |
| `iced/iced_x86.js`, `iced/iced_x86_bg.wasm` | npm package [`iced-x86`](https://www.npmjs.com/package/iced-x86) (github.com/icedland/iced), loaded only when a listing needs live disassembly | version 1.21.0 (published 2024-01-20) | MIT, copied to `iced/LICENSE.iced` |
| `lz4/lz4_wasm_bg.wasm` | npm package [`lz4-wasm`](https://www.npmjs.com/package/lz4-wasm) (lz4_flex's LZ4 block compressor built to WebAssembly, github.com/PSeitz/lz4_flex, `lz4-wasm/`; package gitHead `08ab363930b22745fdbf3c015224a1710d2c4369`), unmodified, written by `gen-lz4.py` (sha512 of the tarball and SHA-256 of each file pinned there; `gen-lz4.py --check` runs in CI). The package's JS glue is an ES module for bundlers and is not used: `lz4frame.js` (this repository) instantiates the module and writes the LZ4 frame format around its blocks. Compresses snapshots in `snapworker.js` | version 0.9.2 (published 2022-04-27) | MIT or Apache-2.0, copied to `lz4/LICENSE_MIT` and `lz4/LICENSE_APACHE` (the package's own files; `package.json` says MIT) |
| `seabios.bin`, `vgabios.bin` | [copy/v86](https://github.com/copy/v86) `bios/` | commit `94faf3d6b19ecabc53a4010aaa24252fa6a162e2` (the commit npm 0.5.471 was built from) | SeaBIOS and its VGA BIOS: LGPL-3.0, copied to `v86/COPYING.LESSER` |

To move the pin, change `V86_NPM_VERSION`, `V86_COMMIT` and the checksums in
`fetch-v86.sh` and `V86_COMMIT`, the patch and `SHA256` in `gen-v86.sh`
together (and `JITSNAP_SHA256` in `test_browser.py`), and the table above.

The page itself (`index.html`, `app.js`, `stepper.js`, `livedis.js`, `style.css`, ...) uses
no other libraries. `stepper.js` adds two exports to `v86.wasm` in the
browser after the file is fetched (`read_imm8` and the
interpreter's `run`, found by name in the module's name section); the
code itself is not modified.

## In-browser toolchain (clang.wasm)

`gen-clang-wasm.sh` builds `clang/clang.wasm`: LLVM's multicall binary
(clang, ld.lld, llvm-ar, llvm-nm, llvm-objcopy, llvm-objdump,
llvm-readobj, llvm-strip; argv[0] picks the tool) with the x86 backend
only, for wasm32-wasip1, for rebuilding kernels in the page. Built in CI
by `.github/workflows/clang-wasm.yml` (also called by the Pages workflow),
cached by the script's hash, never committed; every file in `clang/` is
checked against a SHA-256 recorded in the script (`--check`).

| file | source | pin | licence |
|---|---|---|---|
| `clang/clang.wasm` | [YoWASP/llvm-project](https://github.com/YoWASP/llvm-project) (LLVM 22.1.0 with YoWASP's WASI patches), built after YoWASP's `build.sh` recipe (MinSizeRel, no threads, 8 MiB stack, 4 GiB max memory, no LTO) with `LLVM_TARGETS_TO_BUILD=X86` and the llvm-driver, by [wasi-sdk 29](https://github.com/WebAssembly/wasi-sdk/releases/tag/wasi-sdk-29) (`wasi-sdk-29.0-x86_64-linux.tar.gz`, SHA-256 `87d1d1a2879d139cdc624b968efad3d4a97b8078cdff95e63ac88ecafd1a0171`; its wasi-libc `ac020b86fd44`, libc++, libc++abi and compiler-rt builtins from LLVM 21.1.4 are linked in), in Docker `debian:trixie@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6` with cmake 3.31.6-2, ninja-build 1.12.1-1, g++-14 14.2.0-19 and python3 3.13.5-1 (from snapshot.debian.org `20261113T000000Z` when the archive no longer has them); SHA-256 `8b619cdac1f34d4cc29e667b3115ae168a0bdd514866f6e22c5e98020b755827` (the same from the E0 spike build, another container with another tblgen compiler, and from this script on d-claude; the clang-wasm workflow builds it again whenever the script changes) | commit `97196c8eeb1d495fa43bb8af2fb26af5ef5b89fb` (branch `llvmorg-22.1.0+wasm`; `clang --version` says 22.1.0) | Apache-2.0 WITH LLVM-exception: `clang/LICENSE.llvm`, `.clang`, `.lld` and `.libcxx`, `.libcxxabi`, `.compiler-rt` (the wasi-sdk runtime libraries); third-party code in LLVM's Support library: `LICENSE.blake3` (CC0-1.0 or Apache-2.0), `LICENSE.regex` (BSD, Henry Spencer's regex), `LICENSE.xxhash` (BSD-2-Clause), `LICENSE.md5` (public domain or a cut-down BSD licence), `LICENSE.unicode` and `LICENSE.unicode-convertutf` (Unicode licence); wasi-libc: `LICENSE.wasi-libc` (Apache-2.0 WITH LLVM-exception or Apache-2.0 or MIT: `LICENSE.wasi-libc-apache-llvm`, `-apache`, `-mit`; dlmalloc CC0), `LICENSE.wasi-libc-cloudlibc` (BSD-2-Clause), `LICENSE.wasi-libc-musl` (MIT) |

To move the pin, change `LLVM_COMMIT`, the wasi-sdk and Debian pins and the
checksums in `gen-clang-wasm.sh` together, and the table above.

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

## Debian initramfs for the Debian sample

`gen-debian-initrd.sh` (run by the workflow, never committed) builds
`kernels/debian-6.1-686-initrd/initrd.img` with Debian's initramfs-tools
in an i386 `debian:bookworm-slim` container, from the kernel package
above and Debian bookworm packages of the same snapshot.debian.org date
(20260704T211317Z). It writes `packages.txt` next to it: every binary
package whose files are in the initramfs, with its version and source
package. For the current build:

| binary package | version | source package | licence |
|---|---|---|---|
| busybox | 1:1.35.0-4+deb12u1+b1 | [busybox](https://snapshot.debian.org/package/busybox/1%3A1.35.0-4%2Bdeb12u1/) | GPL-2.0 |
| klibc-utils, libklibc | 2.0.12-1 | [klibc](https://snapshot.debian.org/package/klibc/2.0.12-1/) | BSD-3-Clause and GPL-2.0 |
| initramfs-tools-core | 0.142+deb12u3 | [initramfs-tools](https://snapshot.debian.org/package/initramfs-tools/0.142%2Bdeb12u3/) | GPL-2.0+ |
| udev | 252.39-1~deb12u2 | [systemd](https://snapshot.debian.org/package/systemd/252.39-1~deb12u2/) | GPL-2.0+ (udev), LGPL-2.1+ |
| kmod, libkmod2 | 30+20221128-1 | [kmod](https://snapshot.debian.org/package/kmod/30%2B20221128-1/) | GPL-2.0+ (tools), LGPL-2.1+ (library) |
| libc6, libc-bin | 2.36-9+deb12u14 | [glibc](https://snapshot.debian.org/package/glibc/2.36-9%2Bdeb12u14/) | LGPL-2.1+ |
| util-linux, libblkid1 | 2.38.1-5+deb12u3 | [util-linux](https://snapshot.debian.org/package/util-linux/2.38.1-5%2Bdeb12u3/) | GPL-2.0+, LGPL-2.1+ (libblkid) |
| libacl1 | 2.3.1-3 | [acl](https://snapshot.debian.org/package/acl/2.3.1-3/) | LGPL-2.1+ |
| libcap2 | 1:2.66-4+deb12u3+b1 | [libcap2](https://snapshot.debian.org/package/libcap2/1%3A2.66-4%2Bdeb12u3/) | BSD-3-Clause or GPL-2.0 |
| liblzma5 | 5.4.1-1+deb12u2 | [xz-utils](https://snapshot.debian.org/package/xz-utils/5.4.1-1%2Bdeb12u2/) | public domain (liblzma) |
| libpcre2-8-0 | 10.42-1+deb12u2 | [pcre2](https://snapshot.debian.org/package/pcre2/10.42-1%2Bdeb12u2/) | BSD-3-Clause |
| libselinux1 | 3.4-1+b6 | [libselinux](https://snapshot.debian.org/package/libselinux/3.4-1/) | public domain |
| libssl3 | 3.0.20-1~deb12u1 | [openssl](https://snapshot.debian.org/package/openssl/3.0.20-1~deb12u1/) | Apache-2.0 |
| libzstd1 | 1.5.4+dfsg2-5 | [libzstd](https://snapshot.debian.org/package/libzstd/1.5.4%2Bdfsg2-5/) | BSD-3-Clause or GPL-2.0 |
| linux-image-6.1.0-50-686 | 6.1.176-1 | linux (see above) | GPL-2.0 (only modules.* index files, no modules) |

Each package's full copyright file is `/usr/share/doc/<package>/copyright`
in the package; the source packages are linked above (also on
<https://sources.debian.org/>).

