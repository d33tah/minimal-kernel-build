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
  changed ones marked), the current symbol (range, size, offset of EIP),
  the previous symbol (the one the CPU ran before entering the current
  one, and where it left it), a path of the last few symbols
  (`idt_setup_from_table <- setup_arch <- ...`), and the code around EIP:
  about 6 instructions before and 10 after from `disasm.json`, with
  `symbol+offset` and bytes that differ in guest memory marked. Outside
  the kernel image, or where the code is not vmlinux's yet (the bzImage
  relocation stub), it says so and shows the raw bytes at EIP. The symbol
  history is recorded by the stepper only (stepping, slow motion, count,
  Run to), not at full JIT speed. Symbols and lines open in the Page tab.
- **Graph.** Every change of symbol the stepper sees (stepping, slow
  motion, Run to, and the background count with `?speed=full`) is an edge
  old symbol -> new symbol with a count, the number of transitions that
  landed on the symbol's first byte (call-like, drawn solid; the others,
  returns and jumps into the middle, are dashed) and the step it was first
  taken. Nodes are symbols sized by the instructions executed inside and
  coloured by name prefix (symbols.json has no source paths); two grey
  pseudo-nodes stand for BIOS/setup and for kernel-range code with no
  symbol (the relocation stub). The layout is incremental: a node is placed
  once, when first seen, one column right of the node it was entered from,
  so nothing moves. Canvas, redrawn at most 10 times a second; drag, wheel
  and pinch to pan and zoom, tap a node for its callers and callees (with
  counts) and a link to the Page tab; JSON and DOT downloads. Nothing is
  recorded at full JIT speed. After a counted `?speed=full` boot the graph
  has 3 nodes and 2 edges for hello and about 264 nodes and 725 edges for
  the minified Linux (start_kernel entered from i386_start_kernel).
  "hide interrupt handling" folds the interrupt and timer symbols (names
  with irq, interrupt, timer, tick, softirq, rcu, apic, 8259, ...) into
  one node.
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

- **Default boot.** BIOS, option ROM and the real-mode setup are stepped
  as fast as JavaScript can (about 1.4 million instructions, 0.3 s) up to
  the kernel's 32-bit entry at 0x100000 (where v86 puts the 32-bit part,
  whatever `code32_start` says; it is 0x100000 for both kernels). The top
  bar then says "Entered the kernel at 0x100000 after N instructions from
  reset", the speed switches to 1k instructions per second and the
  counters count live from there: hello reaches "Still alive" after 6,150
  instructions, about 6 s; the minified Linux needs about 0.7-1 million,
  about 15 minutes, so the top bar shows an ETA (from the count this
  browser last measured, else a built-in estimate) and the symbol the CPU
  is in.
- **Full speed.** `?speed=full`, or picking "full (JIT)" (the choice is
  kept in `localStorage`; `?speed=` overrides it): with **count** on, the
  whole boot is stepped in the background from the reset vector until
  "Hello, World!" and "Still alive" are both in VGA text memory at 0xb8000
  (the page wraps v86's MMIO write handlers for 0xa0000-0xbffff and keeps
  a copy of the text page), at about 4-7 million instructions per second
  in headless Chromium, then v86's JIT runs.
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

The end of the kernel: both kernels' init ends; hello runs `cli; hlt`,
the minified Linux's init calls exit(0) and the kernel then panics
("Attempted to kill init!", not printed without printk). The stepper
notes the first `int 0x80` with EAX 1 (exit) or 252 (exit_group) and its
EBX, and stops when EIP enters `panic` (from symbols.json) or the CPU
halts with interrupts off. The top bar then shows "Kernel panic (init
exited with code 0) after N instructions from the kernel entry. Machine
stopped here." or "Halted: hlt with interrupts off ...", and stepping
stops there (Continue runs on, e.g. into panic's delay loop).

The counters: instructions from the first instruction at physical
0x100000 (the main column) and from reset until each line appears, until
exit() and until the end (panic or halt), and distinct physical addresses
executed, split into the kernel range (0x100000 to 0x100000 + `init_size`)
and the rest (SeaBIOS, VGA BIOS, the linuxboot option ROM, the real-mode
setup at 0x80000).

What "exact" means here: each count is the number of interpreter calls in
the run on screen. A `rep` string instruction counts once per v86 entry,
and v86 runs at most a page of iterations per entry (qemu's trace below
counts it once).

### Virtual time

v86 takes all device time (PIT, RTC, ACPI and APIC timers, TSC) from one
function, `v86.microtick`, which libv86.js sets at load time to
`performance.now.bind(performance)`. `clock.js`, loaded before libv86.js,
replaces `performance.now` with its own clock (the page times itself with
the saved original). While the stepper drives the CPU the clock is
virtual, like qemu's `-icount`: now = steps / (rate x 1000) ms, with the
rate set by the **clock** menu (10, 100 or 1000 instructions per
microsecond, default 10; `?mips=`; remembered). When the CPU halts with
interrupts on, the stepper moves the clock to the next timer event. A new
machine starts at virtual time 0, so device setup sees the same
timestamps every run. At full JIT speed the clock follows real time,
continuing from the virtual time. "real time" in the menu restores v86's
wall clock while stepping. The top bar shows the time and how many times
the PIT raised IRQ0.

Before (real time): the minified Linux unmasks only IRQ0 (PIT, HZ=250),
so at 1k instructions/s the guest got a timer interrupt every few
instructions, and `calibrate_delay()` (which loops until jiffies advance)
made every count depend on the stepping speed: from the entry to "Hello,
World!" 454,040 and 607,884 in two full-speed runs, 274k single-stepped,
558k-1.18M in earlier runs; SeaBIOS (which waits on time) varied the
from-reset count by thousands. After (virtual, 10/us): the same numbers
at every stepping speed (full background count, 1M/s, 100k/s, 1k/s) and
on every reload; `test_browser.py` checks this. The count depends on the
rate through `calibrate_delay()`: from the entry to "Hello, World!"
652,351 at 10/us, 4,132,455 at 100/us, 24,140,531 at 1000/us. At 1k/s
the Linux boot takes about 11 minutes at 10/us and 69 minutes at 100/us.

Measured in headless Chromium (2 MB, virtual clock 10/us; identical in
every run):

| kernel | until | from 1 MiB | unique EIPs kernel / other | from reset | IRQ0 |
|---|---|---|---|---|---|
| hello | Hello, World! | 6,082 | 16 / 7,770 | 1,411,832 | |
| hello | Still alive | 6,150 | 24 / 7,770 | 1,411,900 | |
| hello | halted | 6,155 | 26 / 7,770 | 1,411,905 | 2 |
| minified Linux | Hello, World! | 652,351 | 10,392 / 8,171 | 2,059,822 | |
| minified Linux | Still alive | 653,080 | 10,477 / 8,171 | 2,060,551 | |
| minified Linux | exit(0) | 653,231 | 10,479 / 8,171 | 2,060,702 | |
| minified Linux | panic | 653,283 | 10,487 / 8,171 | 2,060,754 | 15 |

(The kernel was rebuilt since the qemu cross-check below: 486 build,
panic halts.)

### Cross-check with qemu

The runs below used real time on both sides. The comparable mode to the
page's virtual clock is qemu's `-icount shift=N` (2^N ns per
instruction; shift=7 is 7.8 instructions per microsecond, close to the
page's 10/us), not repeated here.

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

The tab bar wraps on narrow screens (every tab a 32 px target); the touch
case checks every tab is inside a 390 px viewport, the last one opens,
and a touch swipe outside the graph scrolls the page. A determinism case
boots hello at 1k/s and at full speed and Linux at full speed twice and
at 1M/s, and checks the counts are identical.

Cases: hello at 2 MB with the default boot (the entry marker, the switch
to 1k/s, 6,082 instructions from the entry to "Hello, World!", the
"Halted" banner), Linux at 2 MB, 18 MB and 2180 KiB with `?speed=full`
(the panic banner with "init exited with code 0"), and Linux with the default
boot for a few seconds (entry marker, 1k/s, the counter grows by about
3,000 in 3 s, ETA and current symbol in the top bar, the speed choice
remembered across a reload). For every kernel case it checks the gate lines on the screen, the boot counters (both lines
counted, Linux from 1 MiB larger than hello), the page scan, the Page tab
(opened from the grid, in the URL hash, kept on reload; instruction lines
in the 1 MiB page; instruction lines and hex rows in the page where .text
ends; one symbol index and `name:` headers, no second symbol table;
instructions still there after scans and auto-scan; an all-zero page
collapsed to `*`), no horizontal scroll on any tab at 390, 800, 1000 and
1280 px, and a snapshot round trip (save, gzip smaller, blank the screen,
restore, both lines back). For Linux it runs to `start_kernel`, steps
50 at a time and checks the code window's boxed line is EIP, the current
symbol contains EIP and a previous symbol differs from it. For each
kernel it then starts paused
(`?paused=1`): Step 10 changes EIP and marks changed registers and counts
exactly 10, Run to 0x100000 stops there, and 1k/s slow motion advances
the counter over 2 s and flashes at least one written page in the grid.
A last case serves two "deploys" with `Cache-Control: max-age=600` (as
GitHub Pages does) and checks a reload runs the new `app.js`. Screenshots
(desktop and 390 px) go to `site/test-screenshots/`.
