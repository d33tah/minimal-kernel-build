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
- **Debian sample kernel.** The third kernel in the menu is Debian
  bookworm's `linux-image-6.1.0-50-686` 6.1.176-1 (non-PAE 686; packages,
  hashes and licence in THIRD_PARTY.md), with symbols from its -dbg
  package. The workflow downloads both packages from snapshot.debian.org
  (deb.debian.org as fallback), keeps them in the Actions cache, and runs
  `gen-symbols.py --trim` on the -dbg vmlinux: 46,937 symbols (code and
  global data with a size; local data objects and export/tracing
  bookkeeping dropped), 5,059,585 bytes, 765,158 gzipped (117,357
  symbols, 13 MB, 1.6 MB gzipped untrimmed). No disasm.json or
  sources.json: the listing is disassembled live (below). Its `meta.json` gives the label
  "Debian 6.1 686 (no initrd: ends in a root-fs panic)", the command line
  `nokaslr console=tty0 cryptomgr.notests` and 64 MB of RAM (32 MB does
  not boot: it stays at the SeaBIOS banner). At full speed it prints its
  boot log and stops itself at "VFS: Unable to mount root fs on
  unknown-block(0,0)" after about 13 s. bullseye's 5.10.0-30-686 also
  works; `test_distro_symbols.py --version` checks either.
- **Imported symbols.** "Symbols" next to "From disk" takes a
  `System.map` (`nm -n` text: hex address, type letter, name) or an
  unstripped `vmlinux` (ELF32: section headers, `.symtab`/`.strtab` and
  the PT_LOAD program headers are parsed in the browser, as
  `gen-symbols.py` does). They replace the kernel's `symbols.json` for
  everything that uses symbols (grid colours, Page symbol index, Debugger
  current/previous symbol, Graph, panic detection) and the kernel reboots.
  A System.map has no sizes: a symbol runs to the next one, capped at the
  end of its section; sections come from the type letters (T/t text, R/r
  rodata, D/d data, B/b bss) and the markers `_text`/`_etext`,
  `__start_rodata`, `_sdata`/`_edata`, `__bss_start`/`__bss_stop`,
  `__brk_base`/`__brk_limit`. Physical = virtual - offset; "auto" takes the
  ELF's PT_LOAD, or for a System.map puts `_text` at 0x100000 (where v86
  loads the 32-bit kernel; this is the detected offset shown, as a
  System.map has no bytes to match), else vaddr - 0xC0000000 for addresses
  above the i386 PAGE_OFFSET, else identity; a hex offset can be typed.
  There is no disasm.json or sources.json for imported symbols: the
  listing is disassembled live from guest memory (below). The map is remembered in localStorage
  per bzImage (SHA-256 of the image) when it fits; "forget symbols" drops
  it. A bzImage and a System.map dropped on the page together are loaded
  and booted. A file that is neither gives an error and the previous
  symbols stay. With imported symbols the bzImage's relocation stub, which
  runs at the addresses vmlinux code will occupy, is named after those
  vmlinux symbols (there is no build to compare its bytes with), so the
  call stacks in the Graph tab are one or two levels deeper at the start.
- **initrd and cmdline.** "initrd" passes a file to v86 as
  `initrd: {buffer}`, "cmdline" as `cmdline` (empty for the kernels on
  this page; remembered per bzImage SHA-256; `?cmdline=` sets it). v86
  0.5.471 always puts the initrd at 64 MiB (`INITRD_ADDRESS = 64 << 20` in
  src/kernel.js) and writes ramdisk_image/ramdisk_size into the header, so
  RAM must be larger than 64 MiB plus its size: loading one selects the
  smallest RAM size that fits (the menu now goes to 64, 96 and 128 MB),
  the header check reports an error when it does not fit and a warning
  when it lies above the kernel's initrd_addr_max, and the memory map and
  grid show the initrd as its own region. The kernels on this page ignore
  a bootloader initrd (the minified Linux only unpacks its built-in
  initramfs); the page says so. `?count=0` turns off the background count,
  for distro kernels at full speed.

  Tested with Alpine Linux 3.20 x86 netboot (not committed):
  `https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86/netboot/vmlinuz-lts`
  (SHA-256 `ce1f6dad86646efaa980fed238c7ed7e4189fd90c30f99a3788df9c9429cc5c9`,
  7,729,664 bytes, Linux 6.6.134) and `initramfs-lts` (SHA-256
  `894bb68e7e9148149d2f729bec893ce9279b6e653198c62a6361107c2c5c798b`,
  21,751,929 bytes) at 96 MB with `console=tty0`: the kernel log is on
  the VGA console after about 5 s at full speed, then the kernel stops
  in `apic_intr_mode_init` / `setup_IO_APIC` (v86's APIC); with
  `nolapic noapic` it gets to the NMI self-test (`test_nmi_ipi`) and
  hangs there after about 25 s of kernel time. `MKB_THIRD_PARTY=dir`
  runs this case in `test_browser.py`. The same directory has
  `System.map-6.6.134-0-lts` (SHA-256
  `525d25b83b31cf71d6ea5b22fdaf815bbe34009a0e5d71ee9bfbeffdb2a23867`) for
  the symbol import.
- **Live disassembly.** Where there is no disasm.json (imported symbols,
  kernels from disk, the Debian sample, BIOS and setup code) the Page
  listing and the Debugger's code window disassemble guest memory in the
  browser with iced-x86 1.21.0 (MIT; fetched by `fetch-v86.sh` into
  `iced/`, SHA-256 checked, loaded only when needed; its npm build is
  for Node, so `livedis.js` runs it with a small `require()` shim and
  instantiates the wasm asynchronously). AT&T syntax with size suffixes,
  like llvm-objdump. Decoding starts at the containing code symbol's first
  byte so instruction boundaries are right; text outside any symbol
  starts at the page start and its first lines are marked as maybe
  misaligned. Code below the kernel entry, or while the CPU is in real
  mode, decodes as 16-bit; the listing says which. "live decode" in the
  Page tab uses it for our kernels too (it shows what runs, including
  code that differs from the build); on the minified Linux's start_kernel
  all 29 instructions agree with disasm.json up to number formatting. On
  Debian the first instruction of a function is the ftrace call the
  kernel patched into a 5-byte NOP at boot; the live listing shows the
  NOP, llvm-objdump on the -dbg vmlinux the call.
- **C source.** With `kernels/<name>/sources.json` the Page listing and
  the Debugger's code window print the C line above the instructions it
  compiled to, like `objdump -S` (dim, `file.c:123`, printed again when
  the code returns to a line; inlined code names the function it was
  inlined into); "C source" toggles it. The Debugger and the top bar show
  the current C line. Tapping a C line opens the whole function in a side
  panel with live line coverage: a line is marked executed when any
  instruction compiled from it (or inlined from it) has run, from the
  stepper's unique-EIP bitmap. `sources.json` comes from `gen-source.py`:
  `llvm-symbolizer --inlining` over every instruction of `disasm.json`,
  using a `-g` build of the same tree; it keeps the file:line, function and
  inlined-at chain where the location changes, plus only the source lines
  of the functions involved. The workflow builds `-g` copies
  (`KCFLAGS=-g` for the minified Linux, `CC="clang -g"` for hello) next to
  the gate kernels and fails unless their code is byte-identical
  (`llvm-objcopy -O binary --remove-section=.notes`: only the 20-byte GNU
  build-id differs, as it hashes the debug info too). Sizes: minified
  Linux 698,272 bytes (156 KB gzipped; 7,325 locations, 188 files, 1,222
  functions, 14,319 source lines), hello 1,104 bytes. The -g build takes
  about 11 s locally (9 s without -g).
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
- **Graph.** The call structure of the boot, reconstructed from the
  symbol transitions the stepper records (stepping, slow motion, Run to,
  the background count with `?speed=full`; not at full JIT speed): a jump
  to the first byte of a symbol is a call, a jump into the middle of a
  symbol on the stack a return to it, a jump into an entry stub
  (`irq_entries_start`, `asm_*`) an interrupt or exception; a jump into the
  middle of a symbol not on the stack (a tail jump, a task switch)
  replaces the top frame. Code without a symbol (BIOS, setup, the
  relocation stub) gets no frame; its instructions count to the caller's
  self time.

  ESP unwinding (default; `?unwind=0` turns it off): every frame records
  the ESP it arrived with (just below its return address for a call).
  At each transition, the outermost frame of the current stack whose ESP
  lies below the current ESP has returned, and everything above it with
  it; a call arriving at the ESP of an open frame replaces it (a sibling
  that returned elsewhere, or a tail call). Frames are kept per stack
  region (ESP >> 13, Linux's 8 KiB task and IRQ stacks): a stack entered
  from interrupt code or through `call_on_stack` hangs below the code that
  switched, any other stack change (a task switch through
  `__switch_to_asm`, or the early switch to the kernel's virtual stack)
  closes the calls of the stack it leaves and starts the new task at the
  root. Symbol transitions alone missed returns (tail calls, returns into
  functions not on the stack, task switches, interrupts returning on
  another stack) and left frames open. Call depth (max / p99 over all
  calls) on the minified Linux went from 27 / 21 to 13 / 12; on Debian
  5.10.0-30-686 (System.map, `cryptomgr.notests nokaslr`, 2.2 billion
  instructions to its root-fs panic, about 6 million calls) from 216 /
  179 to 63 / 32. start_kernel's inclusive count on the minified Linux
  is 565,589 instructions instead of 588,949: the 23,360 instructions the
  init task's kernel_init thread runs after the first task switch no
  longer count as start_kernel's. Three views:
  - *Timeline* (default): an icicle chart, x = instructions from the
    kernel entry, y = call depth, one box per call, labelled when at least
    40 px wide, coloured by the top directory of its source file
    (sources.json) or a name guess, interrupts grey; a strip with the
    whole run on top (drag in it to move the window), drag to pan, wheel
    or pinch to zoom. "Transpose" puts time downwards and depth across
    (for deep stacks on wide screens), "Flame" puts the root at the
    bottom. "chains" (on) draws a call whose caller has a single call and
    almost no own instructions in the caller's box as "a › b › c". The
    depth slider caps the rows shown and marks deeper calls "+N". The
    search box highlights matching boxes and dims the rest. Tap a box for
    its inclusive/self counts, steps, callers, the callers and callees of
    its function aggregated over all its calls (sandwich), and "Focus",
    which re-roots the view at that call with a breadcrumb back to the
    root (double-click does the same). Rows scrolled out of view keep the
    names of the calls above the window's middle in a strip at the top.
    "follow" keeps the current step and the running call's row in view in
    slow motion, scrolling only when it leaves the visible band; a manual
    pan turns it off.
  - *Call tree*: like `perf report --children` (inclusive %, self %,
    calls), sorted by inclusive count, collapsible, with a search box; the
    root is the whole run from the kernel entry.
  - *Top 30*: the 30 symbols with the largest inclusive count (recursion
    counted once), call edges between them (calls through hidden symbols
    are attributed to the nearest shown caller), layered by how many shown
    symbols are above them; edges pointing up are dashed.
  "hide interrupts" (on by default) hides interrupt and exception frames
  (in the call tree: one "(interrupt and exception handling)" entry).
  JSON has the transitions and the call tree, DOT the transitions. For the
  minified Linux at `?speed=full` (before it dropped calibrate_delay):
  2,024 calls, 264 symbols, 723 transitions; now 1,751 calls, 245
  symbols, 665 transitions.
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
  counters count live from there: hello reaches "Still alive" after 154
  instructions (it prints below the BIOS text instead of clearing the
  screen, see hello/README.md), a fraction of a second; the minified Linux needs about 132,000,
  about 2 minutes 12 s, so the top bar shows an ETA (from the count this
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
EBX, and records when EIP first enters `panic` (from the symbols) as the
"panic() entered" milestone, without stopping: a printk kernel still
prints its panic message. The run ends when the CPU halts with
interrupts off (the minified Linux's panic() halts: "Kernel panic (init
exited with code 0) after N instructions from the kernel entry. Machine
stopped here.", one instruction after the panic() entry; hello: "Halted:
hlt with interrupts off ..."), when "---[ end Kernel panic" appears on
the screen (below), or 50 million instructions after the panic() entry
with neither ("panic() entered, no end-of-panic text ...";
`?panicbudget=` changes the budget).

Distro kernels end with `---[ end Kernel panic - not syncing: ... ]---`
and then spin. The stepper's copy of VGA text memory covers the whole
32 KiB window at 0xb8000 (vgacon scrolls by moving the display start),
and the page looks for that line 4 times a second at any speed, also
under the JIT; when it appears it pauses the machine, stops counting and
shows "Kernel panic: <message> (stopped by the page)". Debian's
5.10.0-30-686 without an initrd stops this way about 25 s after it is
loaded ("VFS: Unable to mount root fs on unknown-block(0,0)").

The header shows which build is deployed: the workflow writes
`version.json` (commit SHA, commit date, subject, run id and number),
fetched with the same `?v=` stamp; without it the page says "local
build". The About tab shows the commit subject.

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
on every reload; `test_browser.py` checks this. The kernel then still ran
`calibrate_delay()`, so its count depended on the rate (652,351 to
"Hello, World!" at 10/us, 4.1 million at 100/us, 24.1 million at
1000/us). The current minified Linux no longer does: 131,287 from the
entry to "Hello, World!" at 10, 100 and 1000 instructions per
microsecond alike (only the from-reset count, SeaBIOS's waits, changes:
1,538,758 / 1,873,311 / 5,228,820). At 1k/s its boot takes about 2
minutes 12 s from the entry.

Measured in headless Chromium (2 MB, virtual clock 10/us; identical in
every run):

| kernel | until | from 1 MiB | unique EIPs kernel / other | from reset | IRQ0 |
|---|---|---|---|---|---|
| hello | Hello, World! | 86 | 17 / 7,770 | 1,405,836 | |
| hello | Still alive | 154 | 25 / 7,770 | 1,405,904 | |
| hello | halted | 159 | 27 / 7,770 | 1,405,909 | 2 |
| minified Linux | Hello, World! | 131,287 | 10,026 / 8,171 | 1,538,758 | |
| minified Linux | Still alive | 132,006 | 10,111 / 8,171 | 1,539,477 | |
| minified Linux | exit(0) | 132,146 | 10,113 / 8,171 | 1,539,617 | |
| minified Linux | panic() entered | 132,198 | 10,121 / 8,171 | 1,539,669 | |
| minified Linux | panic, stopped (hlt) | 132,199 | 10,123 / 8,171 | 1,539,670 | 2 |

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
| hello | Hello, World! | 5,812,951 | 86 | 86 | 17 / 17 |
| hello | Still alive | 5,813,019 | 154 | 154 | 25 / 25 |
| minified Linux | Hello, World! | 6,039,379-6,041,101 | 224,766-227,330 | 273,966 (single steps) | 10,666 / 10,638 |
| minified Linux | Still alive | 6,040,512-6,042,036 | 225,899-228,265 | 274,894 (single steps) | 10,752 / 10,723 |

hello (re-measured after it stopped clearing the screen): the 154 kernel instructions are the same sequence in both. Linux:
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
    (sources.json: see the workflow's "Debug-info builds" step)
    site/gen-kernels.sh
    python3 site/test_browser.py

The tab bar wraps on narrow screens (every tab a 32 px target); the touch
case checks every tab is inside a 390 px viewport, the last one opens,
and a touch swipe outside the graph scrolls the page. A determinism case
boots hello at 1k/s and at full speed and Linux at full speed twice and
at 1M/s, and checks the counts are identical.

Cases: hello at 2 MB with the default boot (the entry marker, the switch
to 1k/s, 86 instructions from the entry to "Hello, World!", the
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
The import case boots the minified Linux with its own System.map
(`llvm-nm -n`) and with the vmlinux ELF instead of symbols.json: Run to
start_kernel names start_kernel in the Debugger as with symbols.json, a
counted boot has a start_kernel frame holding most of the run and detects
the panic, the listing is decoded live, a malformed file gives an error and
keeps the symbols, nothing scrolls sideways at 390 px. The initrd case loads a 512-byte newc cpio built in the test with the
minified Linux and checks RAM was raised, its bytes are at 64 MiB in
guest RAM, the note about our kernels, the initrd region in the map and
grid, the RAM error at 32 MB and "no initrd". The panic-text case writes the panic line through the VGA MMIO handlers
of a running page and checks the machine pauses with the banner; with
`MKB_DISTRO_CACHE` (the cache of `test_distro_symbols.py`) the Debian
kernel is booted without an initrd and must stop itself. When a `debian*` kernel is in kernels.json (CI, or locally after
copying it there) the Debian case checks its defaults, waits for the
end-of-panic stop with "end Kernel panic" on the screen, and, on a fresh
paused boot, runs to start_kernel (about 3.5 minutes of stepping) and
checks the Debugger names it. A last case serves two "deploys" with `Cache-Control: max-age=600` (as
GitHub Pages does) and checks a reload runs the new `app.js`. Screenshots
(desktop and 390 px) go to `site/test-screenshots/`.

## End-to-end check with a real distro kernel

`site/test_distro_symbols.py` downloads Debian bullseye's
`linux-image-5.10.0-30-686` and its `-dbg` package (SHA-256 checked, cached in
`$MKB_DISTRO_CACHE`, ~770 MB), boots the kernel on the page with
`nokaslr console=tty0`, imports the real `System.map` and then the unstripped
`vmlinux`, pauses the CPU four times and requires that every pause inside the
kernel shows the same function as `llvm-symbolizer` on the `-dbg` vmlinux.
Pauses that land in the bzImage's decompressor (below 0xC0000000) are reported
but not counted. Pass `--url https://d33.pl/minimal-kernel-build/` to check a
deploy. It is not in CI because of the download size.
