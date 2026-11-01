# Kernel in the browser: the page

`index.html` boots a bzImage in [v86](https://github.com/copy/v86) (pinned
version and checksums in `THIRD_PARTY.md` and `fetch-v86.sh`). Plain HTML,
JS and CSS with relative URLs; no build step, no libraries besides v86.

Every kernel starts with 2 MB of RAM. The menu also has 4, 18 and 32 MB, and
`?mem=BYTES` asks for any multiple of 4 KiB (e.g. `?mem=1052672` for
1028 KiB). The boot header check warns when the kernel's
`pref_address + init_size` lies past the end of RAM.

## Memory lab

What it does now:

- **Page grid.** One square per 4 KiB page of guest RAM, coloured by region
  (IVT/BDA/EBDA, real-mode setup and command line, VGA window, BIOS ROMs,
  32-bit kernel image, `init_size`, free RAM) and, when
  `kernels/<name>/symbols.json` exists, by ELF section of the kernel (text,
  rodata, data, bss, brk). "Scan memory" reads all of RAM with
  `emulator.read_memory()`: pale squares are all-zero pages, a dot marks the
  pages that changed between the last two scans.
- **Inspector.** Hovering over a page names its region and the symbols in
  it with their ranges, in address order; clicking it shows a hex dump and a
  table of its symbols (name, virtual address, physical range, size,
  section). The dump is laid out like `hexdump -C` (runs of identical rows
  become `*`) with 16, 8 or 4 bytes per row, whichever fits the inspector's
  width; symbol byte ranges are tinted in two alternating colours and the
  symbols starting in a row are named at its end. Bytes that differ from the
  last scan are marked. The asm view shows each instruction's
  `symbol+offset` when there is room. Scans and "live" (re-read every
  500 ms) redraw whichever view is selected. Grid squares with data are
  filled, all-zero pages are hollow.
- **Snapshots.** "Save snapshot" calls `emulator.save_state()`, gzips the
  result in the browser with `CompressionStream("gzip")` (kept raw, and the
  page says so, when the browser lacks it) and keeps the last 8 in memory.
  Each row shows raw and compressed size and the ratio, and has "Restore"
  (`DecompressionStream`, then `emulator.restore_state()`) and "Download".

`symbols.json` comes from `gen-symbols.py ELF OUT [NAME]`: symbols from
`llvm-nm -nS`, sections and physical addresses from the ELF's section and
program headers (paddr = vaddr - 0xC0000000 for the minified Linux, equal for
hello). The workflow writes it for both kernels (`minified/vmlinux`,
`hello/boot/high/kernel.elf`); a kernel without it, or one loaded from disk,
gets the region colours only.

### Snapshot sizes

Measured by `test_browser.py` in headless Chromium, v86 0.5.471, right after
each kernel printed its two lines (sizes in bytes; they vary by a few dozen
bytes between runs):

| kernel | RAM | raw `save_state()` | gzip | ratio | save / gzip time |
|---|---|---|---|---|---|
| hello | 2 MB | 1,490,924 | 94,046 | 15.9 : 1 | 6 / 28 ms |
| minified Linux | 2180 KiB | 1,859,620 | 187,184 | 9.9 : 1 | 9 / 40 ms |
| minified Linux | 18 MB | 2,015,772 | 208,130 | 9.7 : 1 | 13 / 47 ms |

The raw state is smaller than RAM because v86 already leaves out all-zero
pages (`pack_memory`); what remains is the non-zero pages (BIOS leftovers,
the kernel, its page tables), the 256 KiB of VGA memory and device state.
That is still mostly zeros and repeated bytes, so gzip cuts it by about 10x
for 30 to 50 ms: worth doing for anything kept or downloaded.

### Smallest RAM in v86

hello: 1028 KiB. Minified Linux: 2180 KiB boots, 2176 KiB does not (the
header asks for 2264 KiB, but the kernel does not touch all of its brk).
`test_browser.py` boots it at 18 MB and at 2180 KiB.

### Not done yet

- Time travel (recording and stepping back through execution).
- DWARF source mapping (from a byte or an address to a source line).

## Testing

    site/fetch-v86.sh
    site/gen-symbols.py minified/vmlinux site/kernels/minified-linux/symbols.json minified-linux
    site/gen-symbols.py hello/boot/high/kernel.elf site/kernels/hello/symbols.json hello
    site/gen-kernels.sh
    python3 site/test_browser.py

For every kernel case it checks the gate lines on the screen, the page scan,
the hex dump and symbol list of the page at 1 MiB, live re-reads, a
snapshot round trip (save, check gzip is smaller and the ratio is shown,
blank the screen, restore, check both lines are back), that the asm view
survives a scan and auto-scan, that an all-zero page collapses to `*`, and
that neither view nor the page scrolls horizontally at 390, 800, 1000 and
1280 px. A last case serves two "deploys" with `Cache-Control: max-age=600`
(as GitHub Pages does) and checks a reload runs the new `app.js`.
