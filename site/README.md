# Kernel in the browser: the page

`index.html` boots a bzImage in [v86](https://github.com/copy/v86) (pinned
version and checksums in `THIRD_PARTY.md` and `fetch-v86.sh`). Plain HTML,
JS and CSS with relative URLs; no build step, no libraries besides v86.

Every kernel starts with 2 MB of RAM. The menu also has 4, 18 and 32 MB, and
`?mem=BYTES` asks for any multiple of 4 KiB (e.g. `?mem=1052672` for
1028 KiB). The boot header check warns when the kernel's
`pref_address + init_size` lies past the end of RAM.

## Layout

A top bar always shows the kernel and RAM menus, Boot, the status, the VGA
screen, the run controls (Pause, Continue, Step, speed, count) and the boot
counters. Below it are tabs, kept in the URL hash (`#memory`,
`#page/0x100000`, `#debugger`, `#snapshots`, `#about`):

- **Memory.** The region bars and one square per 4 KiB page of guest RAM,
  coloured by region (IVT/BDA/EBDA, real-mode setup and command line, VGA
  window, BIOS ROMs, 32-bit kernel image, `init_size`, free RAM) and, when
  `kernels/<name>/symbols.json` exists, by ELF section of the kernel (text,
  rodata, data, bss, brk). "Scan memory" reads all of RAM with
  `emulator.read_memory()`: filled squares hold data, hollow ones are all
  zero, a dot marks pages that changed between the last two scans. The
  hover line names the page, its region, its first symbol and how many
  more there are. Clicking a page opens it in the Page tab.
- **Page.** One listing of the page in address order, like `objdump -d`
  mixed with `hexdump -C`: where `disasm.json` has an instruction, a line
  `address bytes instruction symbol+offset`; everywhere else hex rows of
  16, 8 or 4 bytes (whichever fits the width), repeated rows collapsed to
  `*`; each symbol start is a `name:` header with its size and section.
  Bytes that changed since the last scan are marked, instructions whose
  bytes differ from the build are highlighted, and the next instruction is
  boxed while the CPU is paused or stepping. Above the listing, a compact
  index of the page's symbols (the only symbol list) scrolls to each one.
  "live" re-reads the page every 500 ms.
- **Debugger.** Step N, Run to address, the registers (EIP, ESP, EBP,
  EAX..EDI, EFLAGS decoded, CS/DS/SS/ES, CR0, CR3, CPL, mode, physical EIP;
  changed ones marked) and the next instructions.
- **Snapshots.** "Save snapshot" calls `emulator.save_state()`, gzips the
  result in the browser with `CompressionStream("gzip")` (kept raw, and the
  page says so, when the browser lacks it) and keeps the last 8 in memory.
  Each row shows raw and compressed size and the ratio, and has "Restore"
  (`DecompressionStream`, then `emulator.restore_state()`) and "Download".
- **About.** What the page is and how the counting works.

## Stepping and boot counters

v86 0.5.471 runs guest code in `main_loop()`, at least 100,003 instructions
per call, mostly through its JIT, and exports no single-step function
(neither `v86.wasm` nor `v86-debug.wasm`; `cycle_internal` and
`do_many_cycles_native` are inlined into `main_loop`). Its release
`v86.wasm` still has a name section, which names two internal functions:
`v86::cpu::cpu::read_imm8` (fetch the byte at EIP through the TLB / page
walk, raise #PF on a fault, EIP += 1) and `v86::gen::interpreter::run`
(execute one instruction given its first byte). `stepper.js` adds both to
the module's export section when the page loads it (`patchV86Wasm`, about
20 ms; the code section is untouched, so the checksum in `fetch-v86.sh`
still covers what runs) and `Stepper.run()` does per instruction what
v86's interpreter loop `jit_run_interpreted` does: `previous_ip = eip`,
`read_imm8`, `run(opcode | is_32 << 8)`, `instruction_counter += 1`.
Timers and interrupts run every 1024 instructions and whenever the CPU
halts, as `main_loop` does. With the v86 loop stopped (`emulator.stop()`),
JavaScript drives the CPU:

- **count** (on by default): every boot is stepped from the reset vector,
  as fast as JavaScript can (about 4-7 million instructions per second in
  headless Chromium; the page shows the time), until "Hello, World!" and
  "Still alive" are both in VGA text memory at 0xb8000 (the page wraps
  v86's MMIO write handlers for 0xa0000-0xbffff and keeps a copy of the
  text page), then runs at the selected speed.
- **speed**: full (v86's JIT, not counted) or 1M, 100k, 10k, 1k, 100, 10
  or 1 instructions per second. Stepped speeds update the screen, the
  registers and counters every frame, re-scan RAM every frame (every
  250 ms above 4 MB) so the pages written since the previous frame flash
  in the grid, and keep the Page tab on the page holding EIP.
- **Pause / Continue / Step / Step N / Run to.** Run to takes a hex
  address, physical or kernel-virtual (minus the virtual-physical offset
  of `symbols.json`, 0xC0000000 for the minified Linux, 0 for hello), and
  stops before executing the instruction there. `?paused=1` starts paused
  at the reset vector.

The counters: instructions from reset and from the first instruction at
physical 0x100000 until each line appears, and distinct physical addresses
executed, split into the kernel range (0x100000 to 0x100000 + `init_size`)
and the rest (SeaBIOS, VGA BIOS, the linuxboot option ROM, the real-mode
setup at 0x80000).

What "exact" means here: each count is the number of interpreter calls in
the run on screen. A `rep` string instruction counts once per v86 entry,
and v86 runs at most a page of iterations per entry (qemu's trace below
counts it once). SeaBIOS busy-waits on real time, so the count
before the kernel differs between runs by a few thousand. The minified
Linux runs `calibrate_delay()`, which runs `delay_loop` until the timer
interrupt advances jiffies: how many instructions that takes depends on
how fast the emulator steps against real time, so the Linux count from
1 MiB varies between runs (about 0.6 to 1.1 million at full stepping
speed, 274k when single-stepping from a script); without `delay_loop` and
`calibrate_delay` it is stable. hello's count is the same every run.

Measured in headless Chromium (2 MB, count on, full stepping speed; ranges
over four runs):

| kernel | until | from reset | from 1 MiB | unique EIPs kernel / other |
|---|---|---|---|---|
| hello | Hello, World! | 1,380,660-1,484,527 | 6,082 | 16 / 7,768-7,770 |
| hello | Still alive | +68 | 6,150 | 24 / same |
| minified Linux | Hello, World! | 2,065,933-2,210,066 | 671,562-792,921 | 10,633-10,638 / 8,171 |
| minified Linux | Still alive | +928 | +928 | 10,723 / 8,171 |

### Cross-check with qemu

`qemu-system-i386 -m 2M -kernel bzImage -d exec,nochain -accel
tcg,one-insn-per-tb=on -D fifo` (QEMU 10.0.13) logs one line per executed
instruction with its linear PC. Two qemu runs per kernel; a script read the log from a FIFO (no
file on disk), mapped Linux virtual PCs to physical (minus
0xC0000000) and stopped at the same point as v86: the k-th execution of the
address whose store completed each line in v86. qemu runs its own SeaBIOS
build (Debian's) and v86 its own, so only the kernel part is comparable.

| kernel | until | qemu from reset | qemu from 1 MiB | v86 from 1 MiB | unique kernel EIPs qemu / v86 |
|---|---|---|---|---|---|
| hello | Hello, World! | 5,818,976 | 6,082 | 6,082 | 16 / 16 |
| hello | Still alive | 5,819,044 | 6,150 | 6,150 | 24 / 24 |
| minified Linux | Hello, World! | 6,039,379-6,041,101 | 224,766-227,330 | 273,966 (single steps) | 10,666 / 10,638 |
| minified Linux | Still alive | 6,040,512-6,042,036 | 225,899-228,265 | 274,894 (single steps) | 10,752 / 10,723 |

hello: the 6,150 kernel instructions are the same sequence in both. Linux:
the first instructions at 1 MiB are the bzImage's relocation stub, not
vmlinux; its `std; rep movsl` copies about 97 KB, which v86 enters once
per page (12 entries) and qemu logs once, so the raw sequences part at
instruction 29. With consecutive repeats of one address collapsed, the
two runs are the same for the first 17,558 instructions and part in
`early_identify_cpu` (the emulators report different CPUs). Kernel
addresses only qemu ran: 48 (17 of them in
`determine_cpu_tsc_frequencies`, the rest in the timer tick and paths that
depend on timing); only v86: 19 (8 in `early_identify_cpu`). Without
`calibrate_delay` and `delay_loop`, qemu ran 186,520 kernel instructions
until "Still alive" and v86 161,041; the rest of the gap is timer
interrupts and the CPU and TSC detection. qemu's count before the kernel
is about 4x v86's: different SeaBIOS builds and option ROMs, and both
wait on real time.

The scripts (`v86ref.py`, `qtrace.py`) are not in the repository; the
qemu runs used `timeout 120` and a FIFO, so no trace touched the disk.

## Testing

    site/fetch-v86.sh
    site/gen-symbols.py minified/vmlinux site/kernels/minified-linux/symbols.json minified-linux
    site/gen-symbols.py hello/boot/high/kernel.elf site/kernels/hello/symbols.json hello
    site/gen-kernels.sh
    python3 site/test_browser.py

For every kernel case (hello at 2 MB, Linux at 2 MB, 18 MB and 2180 KiB)
it checks the gate lines on the screen, the boot counters (both lines
counted, Linux from 1 MiB larger than hello), the page scan, the Page tab
(opened from the grid, in the URL hash, kept on reload; instruction lines
in the 1 MiB page; instruction lines and hex rows in the page where .text
ends; one symbol index and `name:` headers, no second symbol table;
instructions still there after scans and auto-scan; an all-zero page
collapsed to `*`), no horizontal scroll on any tab at 390, 800, 1000 and
1280 px, and a snapshot round trip (save, gzip smaller, blank the screen,
restore, both lines back). For each kernel it then starts paused
(`?paused=1`): Step 10 changes EIP and marks changed registers and counts
exactly 10, Run to 0x100000 stops there, and 1k/s slow motion advances
the counter over 2 s and flashes at least one written page in the grid.
A last case serves two "deploys" with `Cache-Control: max-age=600` (as
GitHub Pages does) and checks a reload runs the new `app.js`. Screenshots
(desktop and 390 px) go to `site/test-screenshots/`.
