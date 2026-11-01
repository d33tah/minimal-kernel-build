# hello: a toy kernel for the browser page

This is a tiny stand-alone kernel that prints two lines on the VGA text
console and halts:

    Hello, World!
    Still alive

It is **not Linux** and not the minimal Linux this repository is about. It
has no ELF loader, no userspace, no init and no syscalls. It exists so the
browser page in `../site/` has a kernel that boots in a fraction of a second
and so the bzImage plumbing (header, loaders, page) can be tested on its own.
The repo's provenance guard (`syscall-uuid-test.sh`) does not cover it.

It is a real bzImage, so the same loaders that boot the minified Linux boot
it: `qemu-system-i386 -kernel`, v86, SYSLINUX, GRUB.

## Layout

| file | what it is |
|---|---|
| `boot/setup.asm` | real-mode setup: boot protocol 2.10 header, 16-bit entry at 0x200 that enables A20 (BIOS call, then port 0x92), loads a flat GDT, enters protected mode and jumps to `code32_start` with ESI = boot_params |
| `boot/head.asm` | 32-bit entry: sets a stack and calls `hello_main` |
| `boot/hello.c` | clears the screen, writes the two lines to 0xb8000 with attribute 0x07, `cli; hlt` forever |
| `boot/hello.lds` | links the 32-bit part as a flat binary at `KERNEL_BASE` |
| `tools/build.c` | host tool modelled on `arch/x86/boot/tools/build.c`: pads the setup to 512-byte sectors (minimum 5, as upstream), patches `setup_sects`, `syssize` and `init_size`, appends the 32-bit part |
| `vmtest.tcl` | the same expect check as the repo's `vmtest.tcl` (qemu curses display, same strings, 60 s) |

Two images are built from the same sources:

| image | 32-bit part at | loadflags | minimum RAM, qemu | minimum RAM, v86 |
|---|---|---|---|---|
| `bzImage` | 0x100000 | LOADED_HIGH | 1028K (1M fails) | 1028 KiB (1 MiB fails) |
| `bzImage.low` | 0x10000 | 0 (zImage layout, setup at 0x90000) | 1020K (960K fails) | does not run |

The page's RAM menu has 2, 4, 18 and 32 MB and starts every kernel with
2 MB; 1028 KiB can be requested with `index.html?mem=1052672`. The header
check warns when the kernel's `pref_address + init_size` does not fit.

qemu honours `LOADED_HIGH` being clear and loads the low image at 0x10000.
v86 ignores loadflags and always copies the 32-bit part to 1 MiB, so v86 and
the page use `bzImage`. Below 1 MiB of RAM qemu accepts `-m` but SeaBIOS
never starts (the screen stays at "VGA Blank mode"); v86 throws
`RangeError: offset is out of bounds` when it cannot copy the 32-bit part to
1 MiB.

## Build and test

Needs the same tools as `../Dockerfile-build-and-run`: build-essential, llvm,
clang, lld, nasm, qemu-system-x86, expect.

    make LLVM=1        # bzImage and bzImage.low
    make test          # qemu + vmtest.tcl for both (bzImage.low with -m 1M)
    make clean

Run it by hand:

    qemu-system-i386 -kernel hello/bzImage
    qemu-system-i386 -kernel hello/bzImage.low -m 1M

`./vmtest.tcl IMAGE MEM` checks any image with any `-m` value.

## The browser page

`../site/` boots kernels in v86. Every `site/kernels/<name>/bzImage` shows up
in the page's kernel list (`site/gen-kernels.sh` writes `kernels.json`), so
swapping in another kernel is one file:

    cp minified/arch/x86/boot/bzImage site/kernels/minified-linux/bzImage
    site/gen-kernels.sh

The page also takes any bzImage from disk through its file input. Locally:

    site/fetch-v86.sh                       # pinned v86 runtime and BIOS
    python3 -m http.server -d site 8000     # then open http://localhost:8000/
    python3 site/test_browser.py            # headless Chromium check

The page also has a small memory lab (page inspector with symbols, and
compressed snapshots); see `../site/README.md` for what it does and the
snapshot sizes measured for this kernel and the minified Linux.

The GitHub Pages workflow (`.github/workflows/pages.yml`) builds both
kernels, runs both qemu gates and the browser test, and deploys `site/`.
