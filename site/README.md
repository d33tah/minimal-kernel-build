# Kernel in the browser: the page

`index.html` boots a bzImage in [v86](https://github.com/copy/v86) (pinned
version and checksums in `THIRD_PARTY.md`, `fetch-v86.sh` and
`gen-v86.sh`, which builds `v86.wasm` with `v86-det.patch`). Plain HTML,
JS and CSS with relative URLs; no build step, no libraries besides v86.

Every kernel starts with 2 MB of RAM. The menu also has 4, 18 and 32 MB, and
`?mem=BYTES` asks for any multiple of 4 KiB (e.g. `?mem=1052672` for
1028 KiB). The boot header check warns when the kernel's
`pref_address + init_size` lies past the end of RAM.

## Layout

The top bar holds only the title and the tab bar, so every tab gets the
rest of the height (suite tabs). The run state lives in the menu toggle at
the bottom right of every tab (H1, below). The tabs are each an emoji
(aria-hidden) and its
name, kept in the URL hash (`#machine`, `#memory`, `#page/0x100000`,
`#debugger`, `#calls`, `#source`, `#snapshots`, `#settings`, `#about`; the
old names `#graph` and `#coverage`, and `?tab=` with any of them when the
URL has no hash, open Calls and Source). The emoji show what each tab
holds: 🖥️ Machine (the computer), 🗺️ Memory (the map of RAM), 🔢 Page (one
page's bytes and code), 🐞 Debugger, 📊 Calls (the call chart), 📝 Source,
💾 Snapshots (saved state), ⚙️ Settings, ℹ️ About. ⚙️, ℹ️, 🖥️ and 🗺️ carry
U+FE0F so they never fall back to a text glyph; suite tabs draws each one
on a canvas in the tab's font and in a serif stack and checks it comes out
as a colour emoji (also checked once in Firefox and WebKit):

- **Machine.** Opens first. The status line (which kernel runs with how
  much RAM), warnings about the bzImage, the VGA screen, the run controls
  (Pause, Continue, Step, speed, clock, count, the state line) and the boot
  counters; then what runs and how it boots: the kernel and RAM menus, a
  bzImage from disk, Symbols, C source (for an imported vmlinux), initrd,
  cmdline, offset, Boot, the loaded bzImage's header line, the deployed
  build. All of it was in the top bar once (1,003 px tall at 390 px). The
  VM keeps running while another tab is shown: its screen is hidden, not
  taken out of the page, so v86 keeps drawing into it; keys go to the VM
  only while the Machine tab is shown (Alt+Left/Right go to the jump
  history until the screen is clicked again).
  `?kernel=`, `?mem=`, `?speed=`, `?cmdline=` work as before, and a
  bzImage or symbol file dropped on the page loads from any tab.

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
  Bytes that changed since the last memory scan are yellow (the page
  scans at every stop, every few frames while the CPU runs and on Scan,
  so they show while it runs), instructions whose bytes differ from the
  build are yellow lines that say why (below), and the next instruction is
  boxed while the CPU is paused or stepping. Memory operands are named
  from the DWARF (see "Memory operands"). Above the listing, a compact
  index of the page's symbols (the only symbol list) scrolls to each one.
  "live" re-reads the page every 500 ms.
- **Fast-forward.** With a symbol map (built-in or imported), every boot
  first runs to the kernel's real link entry (`startup_32`, else `_text`:
  0x100000 for the minified Linux, 0x1000000 for Debian) and stops exactly
  there, then continues at the selected speed. The first 2 million
  instructions (SeaBIOS, about 1.4 M) are stepped as before: v86 0.5.471
  cannot restore a snapshot taken while SeaBIOS is setting up PCI
  (pci.js set_state). After that v86's own `main_loop()` (the JIT) runs in
  slices of 20 million instructions, measured with v86's instruction
  counter (wasm memory offset 664; the JIT adds each block's instruction
  count when it leaves the block, the interpreter counts single
  instructions); each slice starts with an in-memory `save_state()`.
  After every `main_loop()` call (about 100,000 instructions) the page
  checks whether EIP is inside the target's code section; when it is, the
  slice's snapshot is restored and the stepper steps exactly to the
  target. The virtual clock follows the instruction counter during JIT
  slices, so timers fire at the same instruction counts. The stepper's
  count and the clock go back to the slice start before `restore_state()`,
  because v86 sets the TSC offset from the clock it reads there (D1). Until
  then they were put back after it, and the guest's TSC lagged the clock by
  however far the JIT had run past the slice start: Debian 1,420,143,700
  ticks (1.42 s, the 14.2 M instructions past it) in Chromium, and a
  varying number of 100,000-instruction chunks in Firefox and WebKit (D0
  saw those runs reach the entry with v86's counter off by multiples of
  about 100,008 and differ in RAM later). Debian 6.1:
  2,320,848,980 instructions to `startup_32` in about 16 s instead of
  214 s stepped, and stepping all of it gives exactly the same count, so
  v86's counter is exact over this span; from there `start_kernel`
  is 62,512 instructions, the same whether stepped or run with the JIT
  and a breakpoint. The minified Linux reaches its entry within the
  stepped part, so its counts do not change. Counts of fast-forwarded
  instructions come from v86's counter; unique EIPs and the timeline
  cover only stepped code (the decompressor run by the JIT is not in
  them). Run to in the Debugger uses the same machinery for kernel code:
  fast-forward to the entry when the kernel has not run yet, then a
  software breakpoint (HLT bytes over the target's first instruction,
  put back when the CPU stops on one; a guest that reads its own code
  there would see them. v86's HLT with interrupts enabled handles a due
  interrupt at once and the handler returns to the next byte, so with a
  single HLT the CPU ran on in the middle of the instruction: Run to
  kernel_execve in the -O0 minified Linux oopsed. The CPU now halts on
  the next HLT and EIP is set back to the target). The
  HLT goes in only once the target holds the build's instruction bytes
  (disasm.json); until then the page steps, stopping at the target. The
  minified Linux's vmlinux runs at 0x100000, where v86 loads its bzImage,
  and its boot code copies the payload into place (a `rep movs` 155
  instructions after the entry): a HLT written at the entry landed in the
  payload, and Run to rest_init ended halted in exc_page_fault after
  72,644 instructions (test_wp_bit's expected fault was no longer fixed
  up). Kernels without disasm.json (the Debian sample, whose vmlinux runs
  at 0x1000000) get the breakpoint right away.
  `?fastforward=0` turns all of it off.
- **Debian sample kernel.** The third kernel in the menu is Debian
  bookworm's `linux-image-6.1.0-50-686` 6.1.176-1 (non-PAE 686; packages,
  hashes and licence in THIRD_PARTY.md), with symbols from its -dbg
  package. The workflow downloads both packages from snapshot.debian.org
  (deb.debian.org as fallback), keeps them in the Actions cache, and runs
  `gen-symbols.py --trim` on the -dbg vmlinux: 48,165 symbols (all code
  and global data with a size, weak definitions such as
  `pgtable_cache_init` included; local data objects and export/tracing
  bookkeeping dropped), 5,201,697 bytes, 781,424 gzipped (117,560
  symbols, 13 MB, 1.6 MB gzipped untrimmed). `gen-dwarf.js` turns the
  same vmlinux's DWARF into `dwarf/` (see Debug info below), and
  `gen-src.js` ships the C source files that DWARF names (`src/`, see C
  source below). No disasm.json: the listing is disassembled live (below). Its `meta.json` gives the label
  "Debian 6.1 686 (no initrd: ends in a root-fs panic)", the command line
  `nokaslr console=tty0 cryptomgr.notests` and 64 MB of RAM (32 MB does
  not boot: it stays at the SeaBIOS banner). At full speed it prints its
  boot log and stops itself at "VFS: Unable to mount root fs on
  unknown-block(0,0)" after about 13 s. bullseye's 5.10.0-30-686 also
  works; `test_distro_symbols.py --version` checks either.
- **Debian sample with its initramfs.** The fourth entry, "Debian 6.1 686
  with its initramfs (BusyBox shell)", is the same bzImage and symbols
  with Debian's own initramfs: `gen-debian-initrd.sh` runs initramfs-tools'
  `mkinitramfs` in an i386 `debian:bookworm-slim` container (a workflow
  job of its own, cached by the script's hash and the kernel package's
  hash) with the kernel package and initramfs-tools, busybox, klibc,
  kmod and udev from the same snapshot.debian.org date, gzip and no
  kernel modules (MODULES=list; v86 has no disk here). The result is
  5.4 MB (Debian's default MODULES=most gives 25.7 MB, which the kernel
  cannot unpack in 128 MB: "No working init found"). The command line
  `nokaslr console=tty0 cryptomgr.notests break` stops Debian's init at
  the premount break, so it drops to the `(initramfs)` BusyBox prompt,
  which takes keyboard input in the screen. At full JIT speed (no count)
  that takes about 40 s from the page load on this machine; 96 and 128 MB
  of RAM both work (the entry uses 128 MB). The packages its files come
  from are listed in `kernels/debian-6.1-686-initrd/packages.txt` and
  THIRD_PARTY.md.
- **Generators in the browser** (plan E1). An imported vmlinux (offset
  auto) gets the files CI makes for the kernels in the list, made in the
  page: `genimport.js` hands the File to `genworker.js` (a Worker), which
  runs `kgen.js`, the generators CI runs too (`gen-dwarf.js` and
  `gen-src.js` are its command lines; `gen-symbols.py` and `gen-disasm.py`
  are ported line for line), with `llvm-nm -nS` and `llvm-objdump -d` of
  the page's `clang/clang.wasm` (E0a) on `wasi.js`: `symbols.json`
  (`--trim` above 20,000 symbols, as CI does for Debian), `disasm.json`
  (code up to 8 MiB; Debian has none, as shipped), `dwarf/` (line shards,
  `files.json`, `types.json`, locals, function facts, CFI shards) and, when
  the source tree is picked, `src/`. The kernel then switches to them
  as if shipped (`kfetch()` serves them to `dbginfo.js` and `source.js`),
  and IndexedDB keeps them for the bzImage, so a reload has them all.
  Measured on d-claude (i7-6700): the minified Linux -O0 in 1.8-2.9 s,
  Debian's 313 MB -dbg vmlinux in 55-77 s (DWARF 48 s Chromium, 67 s
  Firefox, 63 s WebKit; peak browser RSS 3.0-4.8 GB). `test_gen_worker.py`
  (gen-worker job): from the ELFs CI's generators read, every shipped
  kernel's files made in Chromium, Firefox and WebKit equal the site's
  byte for byte, with gzip as the one stated normalisation (`.gz` compared
  unpacked, the gzipped byte counts in the two index.json set to 0: zlib
  in Node and CompressionStream write other bytes for the same data, and
  so do Node 20 and 22). Suite genimport: the import, the coverage tree
  of a counted boot, the picked tree as `src/`, the reload, an offset by
  hand (nothing made) and a missing clang.wasm (the reason; DWARF read as
  needed).
- **Imported symbols.** "Symbols" next to "From disk" takes a
  `System.map` (`nm -n` text: hex address, type letter, name) or an
  unstripped `vmlinux` (ELF32: section headers, `.symtab`/`.strtab` and
  the PT_LOAD program headers are parsed in the browser, as
  `gen-symbols.py` does). They replace the kernel's `symbols.json` for
  everything that uses symbols (grid colours, Page symbol index, Debugger
  current/previous symbol, Calls, panic detection) and the kernel reboots.
  A System.map has no sizes: a symbol runs to the next one, capped at the
  end of its section; sections come from the type letters (T/t text, R/r
  rodata, D/d data, B/b bss; a weak definition W/V takes the section its
  address is in, else text for W and data for V) and the markers `_text`/`_etext`,
  `__start_rodata`, `_sdata`/`_edata`, `__bss_start`/`__bss_stop`,
  `__brk_base`/`__brk_limit`. Physical = virtual - offset; "auto" takes the
  ELF's PT_LOAD, or for a System.map puts `_text` at 0x100000 (where v86
  loads the 32-bit kernel; this is the detected offset shown, as a
  System.map has no bytes to match), else vaddr - 0xC0000000 for addresses
  above the i386 PAGE_OFFSET, else identity; a hex offset can be typed.
  Until the data files below are made, there is no disasm.json or shipped
  C source for imported symbols: the listing is disassembled live from
  guest memory (below). An ELF is read
  in File slices (headers, `.symtab`, `.strtab`), never whole; its DWARF,
  if any, gives file:line (Debug info below). A System.map or a vmlinux
  without `.debug_info` says that it has no line information. The map is remembered in localStorage
  per bzImage (SHA-256 of the image) when it fits; "forget symbols" drops
  it. A bzImage and a System.map dropped on the page together are loaded
  and booted. A file that is neither gives an error and the previous
  symbols stay. With imported symbols the bzImage's relocation stub, which
  runs at the addresses vmlinux code will occupy, is named after those
  vmlinux symbols (there is no build to compare its bytes with), so the
  call stacks in the Calls tab are one or two levels deeper at the start.
- **initrd and cmdline.** "initrd" passes a file to v86 as
  `initrd: {buffer}`, "cmdline" as `cmdline` (empty for the kernels on
  this page; remembered per bzImage SHA-256; `?cmdline=` sets it). v86
  0.5.471 always puts the initrd at 64 MiB (`INITRD_ADDRESS = 64 << 20` in
  src/kernel.js) and writes ramdisk_image/ramdisk_size into the header, so
  RAM must be larger than 64 MiB plus its size: loading one selects the
  smallest RAM size that fits (the menu goes to 64, 96, 128, 256 and 512 MB),
  the header check reports an error when it does not fit and a warning
  when it lies above the kernel's initrd_addr_max, and the memory map and
  grid show the initrd as its own region. hello and the minified Linux
  ignore a bootloader initrd (the minified Linux only unpacks its
  built-in initramfs); the page says so for those two. For other kernels
  it says which kernel the initrd is for and warns when that is not the
  loaded one: it skips the uncompressed early cpio (CPU microcode) Debian
  puts first, recognises gzip, zstd, xz, lz4, lzma and bzip2 by their
  magic, and for plain or gzip cpio (DecompressionStream) reads the
  releases under `lib/modules/` and the ELF class of `bin/busybox` or
  `sbin/udevadm`; otherwise it takes the release from the file name
  (`initrd.img-6.12.111+deb13-amd64`: 64-bit). It compares that with the
  release in the bzImage header's kernel_version string, e.g. "Warning:
  this initrd is for 6.12.111+deb13-amd64 (64-bit) (from its file name;
  zstd is not decoded here); the kernel is 6.1.0-50-686 (32-bit): it
  cannot run its programs." `?count=0` turns off the background count,
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
- **Branch targets.** A direct call, jmp or jcc names its target as
  llvm-objdump does (`calll 0xC10596E0 <__fentry__>`,
  `jae 0xC1D8D012 <hyperv_init+0x304>`) in every listing: disasm.json
  has llvm-objdump's names, and the live decoder adds them from the
  symbols (the smallest sized symbol holding the target, or a sizeless
  one exactly there: asmnames.js `asmTargetName`), so the Debian sample
  and every import get them too. Targets outside every symbol (the
  BIOS, setup code) and real-mode code (segment-relative) get none.
  The name opens the target in the Page tab when clicked; its tooltip,
  made when hovered, is the target's address, inline chain and the
  callee's function facts (prototype, defined at, frame), the same text
  as every other symbol tooltip (`dbgTitle`). One render path
  (vars.js `insnHtml`) for both listings and both text sources.
- **Debug info.** file:line and the inlined-at chain of any address come
  from the kernel's DWARF (`dwarf.js`, one decoder and one lookup for
  everything below). Shipped kernels have `kernels/<name>/dwarf/`, made in
  CI by `gen-dwarf.js` from a -g ELF: every compile unit's line program
  and its function and inlined-call address ranges (with the call
  site's file:line), merged into one table, cut into shards of 64 KiB of
  physical address space (`<hex>.bin`, LEB128, its own string table) plus
  `index.json`. The page fetches a shard the first time it needs an
  address in it. Line rows count only inside their compile unit's address
  ranges, as in llvm-symbolizer, so padding between objects has no line.
  An imported unstripped vmlinux is read by `dwarfworker.js` (a Worker,
  File slices only): the ELF headers and the small `.debug_*` sections
  first, then the one compile unit covering an address (found through
  `.debug_aranges`, or each unit's root DIE where there is none, as with
  clang) and its line program. DWARF 2 to 5 with GCC's forms (strp,
  line_strp, ref4, implicit_const, sec_offset, `.debug_ranges`) and
  clang's (strx1-4, addrx, rnglistx, loclistx, ref_addr and the
  str_offsets/addr/rnglists bases); `.rel.debug_*` is ignored (a vmlinux is
  ET_EXEC). Decoded units are kept in IndexedDB per bzImage, so after a
  reload the addresses decoded before still answer; others ask for the
  file again. Paths are shown relative to the kernel tree (without
  DW_AT_comp_dir, or Debian's prefix-mapped build directory).
  Where it shows: the Debugger's "source" rows (the C line at EIP, then
  "inlined into F at file:line" per level), the "call stack" row (unwound
  at a pause, see Backtraces below, with the frames the stepper
  reconstructed as the fallback; each at its instruction: EIP for the
  top, the call for the others; the title has the whole chain), the titles of "previous" and "path", the
  C lines of both listings, and in the Calls tab the hover text and the
  detail of a timeline frame ("called from" and "entry", each with its
  chain) and the title of a call-tree name. "no line information" is
  said where the line table has no line (padding, assembly without -g,
  line 0); "reading DWARF…" while a shard or unit loads. Wherever a C
  location or line is shown on the page (both listings, the Debugger's
  source rows, the source panel, the call stack, the Calls detail), the
  file:line or line number has one colour and the C text another: one
  class pair, `.srcloc` and `.srctext` (tokens `--src-loc` and
  `--src-text`, light and dark). Hover titles are plain text drawn by the
  browser and cannot be coloured. Every file:line shown outside the Source panel
  is a link to it (one helper, `source.js` srcLinkEl and srcLinkHtml):
  the state line (paused: the function and line at EIP; its text is cut
  with an ellipsis on a narrow screen, the link stays whole), the Run to
  status and its copy in the "VM paused" overlay on the screen, the source row and its inline
  chain, the call stack, the Code at EIP and Page listing C lines, the
  locals' frame headers, the function facts (defined at, starts at) in
  the Debugger, Source and Calls tabs, a watch's or hovered byte's
  "defined at", and the Calls detail. It opens the panel at that line,
  picked, outlined and scrolled into view: in the function around the
  link's address when that line is in its inline chain (the call site of
  an inlined call opens the caller), else the whole file; an imported
  vmlinux has no file list, so a declaration opens in its function. A
  file without text opens too and says why. Links are buttons: Tab,
  Enter or Space (handled before v86, which takes every key on the page
  for the VM), Escape closes the panel and gives the focus back, and the
  focus stays on a link when its view is drawn again. Hover titles are
  browser text and hold no links. Sizes: minified
  Linux 79,141 bytes in 2 shards (42,859 gzipped), hello 105 bytes, Debian
  9,618,203 bytes in 153 shards (4.8 MB gzipped, about 32 KB per shard;
  1,081,481 line rows, 465,287 function and inlined-call ranges;
  `gen-dwarf.js` takes about 11 s and 0.8 GB). `test_dwarf.py` compares a
  generated directory with `llvm-symbolizer --inlines --functions=short`
  at random code addresses (half inside functions, half anywhere): the
  workflow fails on any difference (local runs: minified 19,696/19,696,
  Debian 49,999/49,999).
- **Backtraces (call frame information).** At a pause the Debugger's
  "call stack" is unwound from the registers and the stack (unwind.js,
  one evaluator for every kernel). `gen-dwarf.js` writes the -g ELF's
  `.debug_frame` (or `.eh_frame`) as `dwarf/cfi-<hex>.bin`, the FDEs of
  each 64 KiB with their CIEs and their CFA programs as bytes, fetched
  when a backtrace passes there; an imported vmlinux's is parsed whole by
  the Worker (2 MB for Debian) and kept in IndexedDB. Each frame is found
  by one method and labelled with it: EIP (the registers), CFI (the CFA
  rule and saved registers of the function inside it: DW_CFA_def_cfa*,
  offset, restore, remember/restore_state, register, val_offset,
  expression rules through a DWARF expression evaluator, e.g. GCC's
  realigned-stack `DW_OP_breg5 EBP-4, DW_OP_deref`), pt_regs (where the
  return address is in assembly entry code that called C with ESP at its
  saved registers, i.e. interrupts, exceptions and `int $0x80`: the
  interrupted EIP, ESP and registers come from pt_regs and unwinding goes
  on there; from user mode the last frame is "user space"), entry ESP
  (assembly without CFI whose call the stepper recorded: the return
  address at that ESP), EBP chain (the frame-pointer chain, Debian is
  built with frame pointers; a function without a frame of its own may be
  missing) and recorded (where unwinding stops, the frames below come
  from the stack the stepper recorded, the ESP heuristic). A
  stack switch (IRQ stacks) is allowed, a CFA that goes down on the same
  8 KiB stack stops the unwind; `ret_from_fork` and `startup_32` end it.
  The row's text (as `llvm-dwarfdump` prints it), the CFA and the reason
  for a fallback are in each frame's title; the note after the frames
  says where and why unwinding ended. Caller frames are at their call
  instruction (return address - 1 without a disassembly). Callee-saved
  registers without a rule keep their value, EAX/ECX/EDX are unknown in
  callers. Sizes: minified Linux 9,429 bytes in 2 shards (3,438
  gzipped), hello 23 bytes, Debian 1,511,034 bytes in 153 shards (389,638
  gzipped, 44,711 FDEs, 2,138 CIEs).
- **Variables.** Global and static variables (static locals too) with
  their DWARF types: `gen-dwarf.js` also writes `dwarf/types.json` (dwarf.js
  `DwarfTypes`). Every variable whose location is a fixed address
  (DW_OP_addr or DW_OP_addrx, optionally + DW_OP_plus_uconst) inside the
  loaded image, with its physical address, size, section and whether it is
  external, per-CPU (in `.data..percpu`) or a static local (with its
  function where DWARF names it); and only the types those variables reach,
  each kept once: GCC repeats every type in every compile unit, so named
  structs, unions and enums are keyed by name, size and declaring
  file:line, the rest by structure. A declaration points to the definition
  of the same name. GCC's `long unsigned int` and friends print the way C
  and gdb write them. Debian: 28,757 variables, 16,026 types, 2,411,771
  bytes (592,275 gzipped), 29 s in `gen-dwarf.js`; minified Linux: 134
  variables, 421 types, 29,033 bytes; hello has no C variables. The page
  fetches `types.json` the first time it is needed (the Variables panel,
  a hover in a hex row, the listing of a page with data symbols). For an
  imported vmlinux a second Worker reads every compile unit once (a
  vmlinux has no name index; the first Worker keeps answering file:line
  meanwhile) and builds the same data, kept in IndexedDB per bzImage.
  Where it shows: the Debugger's Variables section (watch expressions:
  `name`, `Function::name`, `file.c::name`, then `.member`, `->member`, `[index]`, a
  leading `*`; structs and arrays open on a tap, at most 64 elements at a
  time; enums by name, char arrays and `char *` as strings, bit fields,
  pointers with what they point at and a tap opens the Page tab there; a
  per-CPU variable shows CPU 0's copy once `__per_cpu_offset[0]` is set,
  else says it shows the template; variables in `.init.*` say they are
  freed after boot; the list is remembered per kernel in this browser),
  the Page listing (pointing at or tapping a byte of a hex row names the
  variable and member path, e.g. `init_task.comm[3]`, its type and value,
  other members of a union at the same bytes, padding and bit fields; a
  tap offers "Watch" for the path) and the label line of each data
  symbol, which carries its type. Values are read from guest RAM when
  drawn. Where each variable is defined: `types.json` (format 2) also
  carries DW_AT_decl_file and DW_AT_decl_line of every variable, each
  attribute through the declaration or abstract origin when the
  definition leaves it out (as gdb reads them; GCC leaves out of a
  definition what its declaration already says); the paths are listed
  once (`files`), each variable adds a file index and a line. The import
  Worker builds the same. A watch says "defined at file:line", a button
  that opens the Source panel at that line (`gen-src.js` also ships the
  files that only hold data, such as init/init_task.c; generated ones
  say they were made during the build); the hovered or tapped byte's text
  in the Page listing and the tooltip of every memory operand the
  listings name (A1) say it too. When several units define a variable at
  its address (a `__weak` definition and the one the linker kept; DWARF
  does not say which is which) all of them are listed. For a name
  several variables share (Debian: 580 names, `__func__` 1,360 times,
  `_acpi_module_name` 107) the watch's note lists the other definitions
  with their file:line and address (20 at most, then how many more), and
  `file.c::name` (also gdb's `'file.c'::name`) picks the one defined in
  that file, by path or unique suffix (`acpica/utglobal.c::name`); a file
  no definition names, or a suffix several files end with, is refused
  with the list, like the Source panel's file:line. `test_dwarf.py --vars
  N` (`gen-dwarf.js --vars`, the page's `dwVarDecls`/`dwVarsPick`) checks
  N variables, half of them sharing their name: gdb's symbol at the
  variable's address (global, file static, or a static local in its
  function's block) is declared at a file:line the page lists, else
  llvm-dwarfdump --name's DIEs at that address name the same places;
  gdb's `&'file.c'::name` is the address the page picks for the path and
  for its shortest unique suffix; an unknown and an ambiguous file are
  refused (Debian 2,000 sampled in CI, minified all 134). Sizes:
  Debian's `types.json` grows by 348,832 bytes (98,169 gzipped; 152
  variables have more than one definition at their address), the
  minified Linux's by 2,478 (817 gzipped); the C source adds the files
  only variables name: 49 for Debian (484,295 bytes gzipped; 17 more are
  made by the build and say so), 7 for the minified Linux (1,811).
  `test_dwarf.py --types N` checks N random bytes inside variables:
  gdb on the -g ELF must evaluate the path the page names to an lvalue
  holding the byte, with the same type (as `whatis` prints it) and size,
  and the variable's type and size must match too; variables gdb cannot
  find by name (statics of inner blocks or of functions that only exist
  inlined, several statics of one name) are checked by the ELF symbol at
  their address instead (local runs: minified 3,000/3,000, Debian
  8,263/8,263 plus 1,737 by ELF symbol).
- **Parameters and locals.** At a pause, every frame the call stack
  unwound gets the parameters and local variables the DWARF has at its
  address (EIP, or a caller's call instruction), in the Debugger's
  "Parameters and locals" section: per frame its inlined calls (innermost
  first) and then the function, like gdb's backtrace, each with the
  variables of the lexical blocks around that address. `gen-dwarf.js`
  writes `dwarf/locals-<hex>.json.gz` per 64 KiB of code (dwarf.js
  `dwarfScopeGroups`): one group per function with its scopes (function,
  lexical blocks, inlined calls; address ranges), each variable's name,
  type (an id of `types.json`, which now also holds the types of
  parameters and locals) and location (a DWARF expression, a location
  list, a constant, or none). The scope rules are gdb's: a lexical block
  without code gives its variables to the block around it, and a
  function, inlined call or block with an abstract origin also has the
  origin's parameters and variables none of its own children refers to
  (optimized out there; clang's blocks without DW_AT_abstract_origin
  match the origin's by position, as gdb does). Values are read from that
  frame's registers and stack (unwind.js `dwLocation`, sharing the DWARF
  expression evaluator with the unwinder): a register, memory at an
  offset of the frame base (DW_OP_call_frame_cfa is the CFA from the call
  frame information; clang's DW_OP_reg4 ESP is ESP's value), a computed
  value (DW_OP_stack_value), a constant (DW_AT_const_value,
  DW_OP_implicit_value) or pieces of these, shown with the Variables
  panel's rows (structs, arrays, pointers); the location follows each
  value. Not shown as values, each with the reason: no location
  (optimized out), no entry in the location list at that address, a
  caller's EAX/ECX/EDX (not saved across calls), a value only described
  at the function's entry (DW_OP_entry_value; with -mregparm=3 the first
  three arguments arrive in EAX, EDX and ECX: gdb finds some of these
  through the caller's call-site information, the page does not),
  implicit pointers, frames below where unwinding stopped. Sizes: Debian
  41,582 functions, 1,029,770 variables, 87,534,189 bytes of JSON in 153
  shards, 16,006,242 gzipped (largest 669,648), only the shards of the
  frames shown are fetched; minified Linux 439 functions, 8,022
  variables, 84,344 bytes in 2 shards; hello 208 bytes. `types.json`
  grew with the local types: Debian 2,411,771 -> 3,415,202 bytes
  (592,275 -> 869,215 gzipped, 30,992 types), minified 29,033 -> 34,443.
  An imported vmlinux: the Worker that reads the variables (every unit,
  now with their locals' types) stays and decodes a compile unit's
  scopes when a frame is in it; they are kept in IndexedDB like the
  units. `test_dwarf.py --locals N` checks N random addresses inside
  functions against gdb's `info scope` on the -g ELF: the same parameters
  and locals for the innermost frame, each with the same kind of
  location there (none, which register, a constant, pieces, other;
  local runs: minified 4,669/4,669 and 4,692/4,692, Debian 4,810/4,810;
  labels, types and enumerators gdb also lists are left out, and
  addresses where gdb's own `info scope` fails).
- **Memory operands.** Every listing (the Page tab's merged hex and asm,
  the Debugger) names the memory operands it can after the instruction,
  in one colour, with a tooltip saying what the name is (asmnames.js
  `asmOperandNames`, one helper for both listings and for
  `gen-dwarf.js --operands`): `movl 0xc011c028, %eax <lpj_fine>`,
  `movw $0x720, -0x3fee3e00 <vc0.vc_video_erase_char>`,
  `movl %fs:0xC1E5E6E0, %eax <__stack_chk_guard>` (a per-CPU variable:
  %fs selects this CPU's copy, the name is the template's),
  `movl (%esp), %eax <type>`, `leal -0x1C(%ebp), %eax <&command_line>`.
  Absolute addresses get the variable and member path there (the
  Variables panel's `dwWhere`), else the ELF symbol (`<sym+0x10>`). A
  stack slot (`disp(%esp)`, `disp(%ebp)`) is named without registers:
  the base register gets a made-up value, the CFA follows from it where
  the CFI row there defines the CFA with that register, and a parameter
  or local whose location there is memory at that address
  (DW_OP_fbreg through the frame base, DW_OP_breg4/5) is named, with the
  member path inside it. At a pause the instruction at EIP is computed
  from the registers (any base and index), and stack slots of EIP's
  function get their real address in the tooltip. A variable's address
  plus registers (`-0x3fef0fa0(,%edx,4)`) is named after the variable at
  the displacement with the registers after it (`<vector_irq[0]+%edx*4>`); a
  `lea` names the address (`<&x>`). Not named: an offset through a
  pointer register (the register's type is not known), callers' stack
  slots, real-mode code (segment-relative), and anything with no
  variable or symbol there (spill slots, outgoing arguments, heap). The
  listing's info line counts the named operands. `test_dwarf.py
  --operands N` checks N random instructions with a memory operand
  against gdb: absolute ones against `info symbol` (variable or symbol
  and offset), stack slots against the address gdb computes for each
  parameter and local on a core with EIP at that instruction (gdb's own
  CFI unwinder, frame base and location lists; ESP and EBP made up far
  apart), and the page's member path at that byte (local runs: minified
  1,948/1,948, 631 absolute and 1,317 on the stack, 494 of those named;
  Debian 2,000/2,000, 370 of 1,000 stack operands named; gdb names none
  the page does not). The operands suite does the same at real pauses:
  minified memblock_add_range+0x2b (`(%esp)` is `type`) and Debian
  start_kernel+0x20 (`-0x1c(%ebp)` is `command_line`), checking every
  name with an address in the Debugger and in the whole Page listing
  against gdb on a core of the pause, and that both listings name the
  same lines alike. No new data files: it uses the types, CFI and locals
  shards the other features fetch.
- **Another file in the Source panel** (F1). The panel's 📂 Files button
  opens a file switcher under its title: with the filter empty it lists
  the files of the call stack at a pause (EIP's inline chain first, then
  every unwound frame's), the files opened recently for this kernel
  (localStorage, eight per kernel) and then every file the kernel's
  debug info (files.json) or its shipped source knows; each row says how
  many of its lines with code ran, counted like the Source tab, or "no
  code", or why its text is missing. Typing filters by basename and path
  (exact name first, then a name prefix, a substring of the name, of the
  path, and the letters in order); the arrows, Page Up/Down and Enter
  pick, Escape closes. The path in the title is a breadcrumb: a
  directory lists its subdirectories and files in the switcher (with a
  row to go up), the file name of a function view opens the whole file.
  A file opens whole, with the same marks and line numbers, scrolled to
  its first line that ran; a line still lists its addresses and Run to.
  Under the title, "ƒ N functions in this file" jumps to a function's
  first line in the file (or to its code in the Page tab when the file
  has no line of it), and "N headers inlined here" opens a file whose
  code is inlined into this file's lines. Each switch and jump is a
  Back/Forward entry, and the hash keeps the file and line
  (`#debugger~src=init/main.c:133`), so a reload or a link opens it
  again. A file without shipped text says why and still lists its
  functions; with an imported vmlinux the switcher lists the C and
  assembly files of the picked source folder or .tar, shown without
  line marks (an import has no file list).
- **Bookmarks** (K1, bookmarks.js). Named places to come back to, added
  from an instruction (Page listing, Code at EIP), a page of the Memory
  grid, a line of the Source panel, a file or function of the Source
  tab, a box of the Calls tab, a frame of the call stack, or the
  machine's position (EIP and the instruction count the Machine tab
  shows, with a snapshot saved alongside if you tick it, or the snapshot
  of the same state if one exists). Each place has a ☆ button; a right click (Shift+right
  click keeps the browser's menu) or a long press on a line, square, box
  or frame, and the B key over it (or on the tab's place) do the same.
  A small dialog asks for the name, with a default: symbol+offset,
  file:line, the page and its region, the function, or kernel @count
  symbol. The list is in the menu toggle's menu (★ Bookmarks), so it is
  one tap away on every tab next to Back and Forward: name, kind and
  target, a filter, rename (✎, Enter saves, Escape cancels), delete (✕,
  with Undo), a link (🔗, copied on a click) and Export / Import of a
  JSON file. A click jumps there as one Back/Forward entry: the Page tab
  at the instruction (outlined) or page, the Source panel at the line or
  the whole file, a function as the Calls tab's double click opens it.
  Bookmarks are kept in localStorage (`mkb-bookmarks`) with the kernel's
  identity from the Snapshots tab (SHA-256 of the bzImage and initrd);
  those of another kernel are listed apart with "Switch to KERNEL",
  which loads it and then jumps. Without storage they last until a
  reload and the list says so. A link (`?kernel=NAME#bm=...`, the
  bookmark as base64url JSON) opens the same place for anyone, after
  the kernel boots, and adds the bookmark to their list. A ★ (gold,
  with a popover naming the bookmark) marks the bookmarked line,
  square, frame, Source tab row and Calls box.
- **Line to address.** Clicking a line in the Source panel (or a C line
  of a listing, which opens the panel at that line) lists the addresses
  of its code above the source: every range whose line table row is that
  line, in every function it was inlined into ("inlined into X"), and
  every range of a function inlined by a call on that line, each a
  button that opens the Page tab there, with its symbol+offset, size and
  which addresses are breakpoint addresses. A line that continues a
  multi-line statement shows its first line's code; a line without code
  says so. "Run to file:line" (the panel's button, or `init/main.c:973`
  or `main.c:973` typed in the Debugger's Run to field) stops at the
  first of the line's breakpoint addresses the kernel reaches: the
  line table's is_stmt rows of that line (one per run of rows of the
  same line, as debuggers do; a row that only continues an is_stmt row
  of the same line in the same sequence is not a new stop) and the start
  of each range of code inlined by a call on it (its first row names
  the inlined function's line). Run to uses the same JIT and breakpoint
  rules as an address (HLT bytes at all of them when they are in the
  entry's code section, only once the build's code is there; otherwise
  the JIT to the entry, then stepping until any of them). Data: the line
  shards (format MKBD 2) carry the is_stmt flag per row and the breakpoint
  addresses inside rows (`dwarf.js dwarfTable`, `dwarfFileCode`); the page
  reads the shards files.json lists for the file. An imported vmlinux:
  the Worker finds the compile units whose line program names the file
  and decodes them one at a time (the result is kept per file until the
  next import). `test_dwarf.py --lines N` (`gen-dwarf.js --lines`) checks
  the lines llvm-symbolizer names at N random addresses: each address is
  in a listed range, the first and last byte of listed ranges and the
  start of listed inlined calls are that line for llvm-symbolizer, every
  breakpoint address is an is_stmt row of the line in llvm-dwarfdump
  --debug-line, and every is_stmt row of the line is a breakpoint address
  unless it continues one (local runs: minified 1,458/1,458 lines,
  Debian 1,878/1,878). Shard sizes grew by 490 B for the minified Linux
  (+459 B gzip) and 99,957 B for Debian (+120,363 B gzip, about 0.8 KB per
  64 KiB shard).
- **Function facts.** What DW_TAG_subprogram says about a function: its
  prototype (return type, parameters with names and types, `...`; `(void)`
  for a prototyped function without parameters), where it is declared
  (DW_AT_decl_file:decl_line, a button that opens the Source panel
  there), external or static (DW_AT_external), whether it was declared
  inline and whether the compiler inlined it (DW_AT_inline; DWARF does not
  record `noinline` or `__always_inline` themselves), whether it returns
  (DW_AT_noreturn), its out-of-line code (one entry per DW_TAG_subprogram
  with code: a static inline helper can have one per compile unit, GCC's
  .isra/.constprop clones are listed with the function they come from, a
  .cold part as a second range) with the line its first instruction
  belongs to (what gdb's `info line NAME` gives) and the frame the call
  frame information gives it (how far below the CFA ESP goes, which
  registers it saves at which CFA offset; for a frame-pointer function,
  where the CFA becomes EBP+8, since the CFI does not say how far below
  EBP its frame goes), and every inlined copy (a DW_TAG_inlined_subroutine
  with code), counted and listed on demand as buttons to the Page tab with
  the function each one is in. Assembly the DWARF only names (GNU as -g)
  says that it has no prototype or declaration. Shown in the Debugger's
  "function" row for every function of the inline chain at EIP (the
  innermost first, the last one the function whose code it is), in the
  Source tab under a function row's arrow, in the Calls tab's detail of
  a tapped box, on the Page listing's label line of a function (its
  prototype, the rest in the title) and in every symbol tooltip that names
  the code at an address (call stack, path, previous, call tree, Calls
  boxes). Data: `dwarf.js DwarfFuncs` collects them with the types (one
  entry per name and declaration, the parameters of the DIE that declares
  them, type ids of types.json); `gen-dwarf.js` writes
  `funcs-<k>.json.gz` shards by a hash of the name (~100 KB of JSON each),
  the page fetches the one a name hashes to; an imported vmlinux gets them
  from the Worker's pass over every unit with the variables (kept in
  IndexedDB with them). The frame is computed on the page from the CFI
  shards (`unwind.js cfiFrameInfo`). `test_dwarf.py --funcs N`
  (`gen-dwarf.js --funcs`) checks N functions, half with code of their own,
  half only inlined: prototypes, declarations and the counts of inlined and
  out-of-line copies against llvm-dwarfdump --name; for the ones with code
  gdb's ptype, its symbol's file:line and whether gdb's global symbol of
  that name is this one; their frames against llvm-dwarfdump
  --debug-frame; up to 20 listed inlined copies each against
  llvm-symbolizer --inlines; and, for the kernels built here, every
  function's inlined copy count against a full llvm-dwarfdump (local runs:
  minified all 1,159 functions, Debian 1,000 sampled, hello 2, no
  problems).
- **Source tab** (coverage; formerly Coverage). All the kernel's code as a tree of directories, files
  and functions, each with lines run / lines with code and instructions
  run / instructions, a bar, sortable (name, lines run %, lines not run,
  lines with code, instructions run %) and filterable; a file opens in the
  Source panel whole, every line with code marked ran or not; a function
  opens the Page tab; "lcov" downloads an lcov tracefile (DA:line,1 or 0,
  ran or not, no counts). The totals come with the kernel:
  `gen-dwarf.js` writes `dwarf/files.json` (every file with its number of
  lines with code and the shards holding them; every DWARF function, i.e.
  outermost range, with its file, its lines in that file, its instruction
  count and its address ranges; instruction starts from `llvm-objdump -d`
  of the same ELF, `$OBJDUMP` overrides). Lines are counted per line row
  of the shards (dwarf.js `dwarfEachLine`), every frame with a line, so a
  line of inlined code and the line of its call both count; the page counts
  what ran the same way, so a file's numbers in the tree and in the Source
  panel are equal. What ran is the stepper's record (`Stepper.seen`, the
  same as the listing tint, with the same rule about code v86's JIT ran);
  the line rows are read only from the shards holding a recorded address.
  A statement written over several lines (`asm volatile("..."` with its
  strings, outputs and clobbers on the next lines, a call with its
  arguments below it) has line rows on some of its lines only, usually the
  first. The Source panel shows its other lines with ┆ and the status of
  the statement (ran when any of its lines with code ran); the function
  view is widened to whole statements. These continuation lines are not
  counted anywhere: the panel, the tree and the lcov export count lines
  with code of their own in the line table, so their numbers stay equal.
  The statements come from the C text (source.js `srcStatements`: ends at
  `;`, `{` or `}` outside parentheses, after an if/for/while/switch
  header, a label, else/do; strings, characters, comments and preprocessor
  lines with backslash continuations skipped; a `{` after `=`, `,` or
  inside parentheses is an initializer or statement expression).
  Debian: 3,183 files, 41,389 functions, 583,569 lines with code,
  3,030,508 instructions (2,941,867 in functions), `files.json` 2,159,580
  bytes (649,441 gzipped), fetched when the tab opens; minified Linux: 188
  files, 439 functions, 4,465 lines, 17,449 instructions, 23,810 bytes. An
  imported vmlinux has the tree once its data files are made in the
  browser (Generators in the browser, below).
- **C source.** "C source" prints the C line above the instructions it
  compiled to in the Page listing and the Debugger's code window, like
  `objdump -S` (dim `file.c:123`, printed again when the code returns to
  a line, "[f inlined into g]" for inlined code, the chain in its title).
  The text of the lines comes from `kernels/<name>/src/`, written by
  `gen-src.js` for every sample kernel: each file the DWARF shards name
  (line rows and inlined-call sites), gzipped (`<path>.gz`), and
  `index.json` with the list, where the tree comes from, why a file is
  missing (a compiler header outside the tree, a file made by the build)
  and how a regenerated one was made. The page fetches a file the first
  time a line of it is shown and gunzips it with DecompressionStream (or
  takes it as is when the server already decoded it); "reading file…"
  until then. `gen-src.js` fails when a line the DWARF names is past the
  end of its file (not the tree the kernel was built from) and prints how
  many inlined calls have a call line naming the inlined function.
  Trees: the minified Linux and hello from the `-g` copies below (minified
  188 files, 748,753 bytes, 279,917 gzipped; hello 1 file); Debian from its
  source package: `gen-debian-source.sh` unpacks `linux_6.1.176.orig.tar.xz`
  (137,945,728 bytes), applies the 165 patches of `debian/patches/series`
  from `linux_6.1.176-1.debian.tar.xz` (the 686 flavour builds featureset
  "none", that series alone; one of them adds
  `drivers/firmware/efi/secureboot.c`) and regenerates the two headers
  the build makes and the DWARF names, with the kernel's own tools:
  `arch/x86/include/generated/asm/syscalls_32.h` (`scripts/syscalltbl.sh
  --abis i386`) and `security/selinux/flask.h` (`genheaders`); both
  tarballs are SHA-256 pinned (from the `.dsc`) and cached. Debian: 3,183
  files, 57,548,912 bytes, 15,548,524 gzipped (about 4.9 KB a file); 85%
  of its 421,041 inlined calls (minified: 84%) have a call line naming
  the function (macros and multi-line calls are the rest); about 10 s in
  CI. For an imported vmlinux (or a bzImage from disk with one) the page
  shows file:line and a "C source" picker: a folder (webkitdirectory) or
  an uncompressed .tar (ustar, pax path records, GNU long names: only the
  512-byte headers are read when it is picked, each file is a File slice
  read when a line of it is shown); the tree may sit one level down
  (`linux-6.1.176/...`). A .tar.gz/.xz is refused with the reason (it
  would have to be decompressed whole). Nothing picked is remembered.
  Tapping a C line opens the whole function in a side panel with live line
  coverage: the panel's lines are those of the function in that file
  (from its DWARF line rows, two lines before the first), a line has
  code when a line row of the function maps to it (directly or as the
  call site of inlined code) and is marked executed when an instruction
  started in one of those rows' address ranges while stepping (the
  stepper's unique-EIP bitmap). The workflow builds `-g` copies
  (`KCFLAGS="-g -O0"` and `KCFLAGS=-g` for the two minified Linux builds,
  `OPT=-O0 CC="clang -g"` and `CC="clang -g"` for the two hello builds) next to
  the gate kernels and fails unless their code is byte-identical
  (`check-code-identity.py`: the `llvm-objcopy -O binary` images and the
  bzImages are equal except inside note sections, i.e. the 20-byte GNU
  build-id, which hashes the debug info too; the plain image must be the
  one inside the shipped bzImage; `--self-test` checks that a flipped code
  byte fails); `gen-dwarf.js` and `gen-src.js` read those, for the minified
  Linux and hello alike. The -g build takes about 11 s locally (9 s
  without -g).
- **Unoptimized hello.** The page's "hello" entry is built with `-O0`
  (`make -C hello LLVM=1 OPT=-O0`; `OPT` is a make variable of
  hello/Makefile, default `-Os`). At `-O0` every parameter and local
  lives in its stack slot for its whole scope, so the Debugger shows a
  value for each of them at every instruction (the hello suite steps
  through all of hello_main and put_line and finds no "optimized out").
  The `-Os` build that hello/Makefile makes by default is the "hello-os"
  entry (labelled as such in the kernel menu): there put_line is inlined
  twice and its `s` has no location, which the page says ("optimized
  out"). Sizes: bzImage 2,784 bytes at `-O0` (`kernel.bin` 215 bytes),
  2,688 at `-Os` (128 bytes).
- **Unoptimized minified Linux.** The page's "minified-linux" entry is
  built by the workflow from the same minified/ sources with `-O0`: a
  copy of the tree gets `site/unoptimized.patch` and `make LLVM=1
  KCFLAGS=-O0` (LTO kept); its own boot in qemu must show both gate
  lines on the screen (`qemu_screen_check.py`). The product build is not touched: `make vm` still builds and
  gates the `-Os` kernel, which the page offers as "minified-linux-os"
  ("minified Linux, -Os (product build; optimized out)"). The patch adds
  two things `-Os` gets from the optimizer: upstream's `#ifdef
  __OPTIMIZE__` guard around `__compiletime_assert` (at `-O0` a
  BUILD_BUG_ON that folds to true still calls the undefined error
  function and the link fails), and definitions of `overflowuid` /
  `overflowgid` (65534) in fs/binfmt_elf.c (their definitions went with
  kernel/sys.c; `-Os` folds the reads away). The C source the page shows
  for this entry is the patched copy. `-O1`, `-Og`, clang 21's
  `-fextend-variable-liveness` and `-fno-inline` variants were measured
  first (ai/plans/site-unoptimized-debug.md, U0): they leave 40 to 53%
  of (variable, statement address) pairs without a location, `-O0`
  none. At `-O0` the parameters and locals of the minified Linux all
  have a location for their whole scope (llvm-dwarfdump --statistics:
  100% of parameter and local bytes covered, 74% / 44% at `-Os`); the
  unoptimized suite runs to start_kernel, setup_arch, kmem_cache_init,
  kmem_cache_alloc, rest_init, kernel_init, kernel_execve and vfs_write,
  single-steps into their calls and finds no "optimized out" at any
  address it reads (the `-Os` entry, walked the same way, shows them).
  Sizes (`-O0` / `-Os`): bzImage 190,256 / 91,568 bytes; vmlinux text
  146,686 / 50,007, data 35,764 / 29,808; symbols.json + disasm.json
  2,169,952 / 813,402 (375,291 / 169,853 gzipped: static inline helpers
  are out of line at `-O0`); locals shards 48,384 / 83,814 gzipped (the
  `-Os` ones carry location lists). The build takes about 8 s locally.
- **Executed code.** The Page listing tints the instructions that have
  not executed in this boot (a lavender background, the "tinted" key next
  to the listing's counts gives how many). An instruction counts as
  executed when the stepper stepped one starting at its physical address
  (the same unique-EIP bitmap as the C source coverage, an O(1) bit test
  per instruction). Only stepped code is recorded: before anything is
  stepped nothing is tinted; pages below the kernel entry are not tinted
  after a fast-forward (BIOS, setup and the decompressor ran on v86's
  JIT); once the JIT has run after the kernel entry (full speed) the key
  says tinted code may have run. Code that ran at the same address before
  the kernel was in place (the bzImage's own code at 1 MiB) counts as
  executed.
- **Jump paths.** Every asm listing (the Page listing and the Debugger's
  code at EIP) shows, after each instruction, its control transfers that
  are not a fall-through to the next instruction: `←n` paths into it,
  `→n` paths out of it, `·k×` the transfers observed along them while
  stepping. A tap lists them (kind, source or target as symbol+offset
  with a button that opens it in the Page tab, its file:line link, static
  or observed with the count). Without a tap, above each instruction a
  path reaches, `← n paths in:` names its first sources (three, two on a
  narrow screen): the source's mnemonic or the kind (interrupt,
  exception), symbol+offset as llvm-objdump writes a target, file:line
  and the count, then `+k more` for the list. `↑` and `↓` mark a source
  earlier or later in the same function but outside the listing, `↗` one
  in another function, and those get a stub at the edge of the arc
  gutter (dotted, into the line from above, below, or from a circle for
  another function). After a jump or call whose target is outside the
  listing a `↑`, `↓` or `↗` button goes there (its name is already in the
  instruction's text); targets known only at run time are named after the
  instruction (`→ retl to allocate_slab+0x18e slub.c:135 ×150`). Kinds: jmp, the taken side of a jcc, call,
  ret, loop/jecxz, int, iret, far jumps, calls and returns,
  sysenter/sysexit, indirect jumps and calls, ud2, and the exceptions and
  interrupts that enter a handler. Static paths come from the
  disassembly (`jumps.js` reads each instruction's text: one reader for
  `disasm.json` and for code decoded live, where it knows only the paths
  inside the decoded range and says so); it runs once per kernel when a
  listing first needs it (about 20 to 40 ms for the `-O0` minified Linux's
  48,069 instructions). ret, iret, indirect jumps and calls and interrupts
  have no static target (`→?` until one is observed). Observed paths are
  recorded by the stepper (`Stepper.jumpRecord`): after each instruction
  it looks at the opcode (prefixes skipped; ModRM.reg for 0xff) and where
  EIP went; a transfer is recorded with its kind at the next fetch, a
  plain instruction whose next EIP is more than 15 bytes away raised an
  exception, an EIP that changed while the timers ran is an interrupt
  (from the last instruction), and an sti with interrupts off counts for
  the instruction after it, which v86 runs in the same step. Code v86's
  JIT runs records nothing; the Page tab's note, each list and the
  Debugger say when the JIT has run. `⊘` marks an instruction no path is
  known to reach: no jump or call goes there, its address is nowhere in
  the kernel, an instruction ends right before it and does not fall
  through (jmp, ret, iret; ud2, int and every call fall through, a call
  to a noreturn function too), and the stepper has not run it, seen a path into it or
  stopped at it. gen-disasm.py lists the addresses the image holds in
  disasm.json's `taken`: aligned 32-bit words of the data sections
  (function pointers, switch jump tables), 32 bits inside another
  instruction (`movl $fn`, `pushl $fn`, `leal fn`), `__ex_table` fixups,
  and the stubs of an array whose start is taken, at a common stride
  after each jmp (early_idt_handler_array + i * 9). Their lists and the
  Debugger say how they are reached. Nothing known before an instruction
  (the first of a section or of a live decode, padding llvm-objdump
  decoded over it) means no `⊘`. What is left is alignment padding after
  a ret or jmp and, in the -Os minified Linux, functions nothing calls or
  points to (107: start_thread, oops_begin, put_pid, ...). Without
  disasm.json (Debian, an imported vmlinux, code outside the build) the
  static paths come only from the instructions decoded live, so `⊘`, the
  lists and the Debugger then say that jumps and calls from other code
  are not known, and the first instruction of each function symbol counts
  as reached (its callers are outside the decode). The stubs of Linux's
  entry stub arrays are known there by name (early_idt_handler_array,
  irq_entries_start, spurious_entries_start: jumps.js JUMP_STUB_ARRAYS):
  the first instruction after each jmp inside one has no `⊘`, and the
  Debugger and its list say "stub k of early_idt_handler_array, entered
  as early_idt_handler_array + k × 9" and which function puts those
  addresses in the IDT (with disasm.json too). The marks of the
  listings explain themselves in the page's popover (the one the "?"
  markers use, data-tip instead of a title attribute, which a touch
  screen never shows): a tap, Enter or Space on `⊘`, a gutter stub, the
  EIP mark or the tint key shows it, a tap elsewhere or Esc closes it;
  buttons that act on a tap (↑ ↓ ↗, names, ←n, file:line) show theirs on
  hover, Tab focus and a long press. The listeners are delegated, so
  marks drawn later work too, and an open popover moves to the same mark
  when its listing is drawn again. Tab moves the focus through the page
  unless the VM screen was clicked last (v86 otherwise takes it).
  Jumps (jmp, jcc,
  loop, indirect jmp) between two
  instructions of the same listing are drawn as arcs left of the
  addresses, like `objdump --visualize-jumps`: shorter arcs nearer the
  code, grey when not taken while stepping, in the accent colour when
  taken, dashed when observed only; up to 10 columns (4 on a phone), and
  the note says how many did not fit. Under the code at EIP the Debugger
  lists EIP's own paths in and out. The Calls tab lists no instructions,
  so it has no arcs.
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
- **Machine state (Debugger tab, cpustate.js).** Collapsible sections
  read from the pinned v86's CPU object and devices: segment registers
  with v86's hidden descriptor cache (base, limit in bytes, access byte;
  D/B only for CS and SS; v86 keeps no granularity bit, and no access byte
  for TR and LDTR); GDTR, IDTR, LDTR, TR with "view GDT" and "view IDT"
  (descriptors decoded from guest memory through the page tables; IDT
  gates with the handler's symbol, e.g. gate 0x80 -> entry_INT80_32);
  CR0, CR2, CR3, CR4 decoded, EFER; DR0-DR7 decoded (v86 stores them but
  raises no debug exceptions from them); the MSRs v86 implements
  (SYSENTER_CS/ESP/EIP, APIC_BASE, PAT, EFER; MISC_ENABLE is a fixed 1;
  MTRRs are not emulated; the TSC is computed on each rdtsc, so it is
  shown only when a state save stored it); x87 control/status/tag words,
  ST0-ST7, FIP/FDP, MM0-MM7, XMM0-XMM7, MXCSR ("not used yet" while
  empty); interrupts and timers: IF, both 8259 PICs (IMR, IRR, ISR, vector
  base, ELCR; v86 keeps the enabled mask, the IMR is its inverse), the
  8254 channels (mode, reload with its rate, the count it last started
  from: v86 computes the running count from the elapsed time), a note
  that there is no local APIC or IOAPIC (v86 has them only with ACPI,
  which the page does not enable), the RTC; paging: the mode, a translate box (hex address or
  symbol name -> each level's entry and flags -> physical address and
  symbol) and a page-directory viewer (present PDEs as runs). At the end
  of the minified Linux's boot: CR4 PSE PGE, the GDT's 0x60 is the flat
  32-bit kernel code segment, the master PIC's IMR 0xfa equals the
  kernel's cached_irq_mask, PIT channel 0 reloads 4773 (HZ=250). The
  runto suite also checks the tools on the paused machine: translate by
  name, the page-directory run holding start_kernel and a hole in it, PAE
  (no shipped kernel enables it) on hand-made tables in RAM (a 4 KiB and
  a 2 MiB page, a missing PDPTE, PDE and PTE, a page table past the end
  of RAM), a hand-made 2-level page directory whose entries make runs,
  paging off, the GDT and IDT views of a table at an unmapped address, an
  IDT with a gate to code no symbol covers, a gate not present and a
  trap gate of DPL 3, MMX and SSE rows once a register holds a value.

- **Calls** (formerly Graph). The call structure of the boot, reconstructed from the
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
  region (ESP >> 13, Linux's 8 KiB task and IRQ stacks): a stack change
  inside a task (IRQ stacks, the early switch from the boot stack to the
  kernel's own) hangs the new stack's calls below the code that switched;
  a task switch (through `__switch_to_asm`) suspends the task it leaves.
  A task is identified by the kernel stack it switches away on (ESP >>
  13; the boot task keeps its boot-stack calls). Its open calls end at
  the switch and their chain is saved; when the task runs again, the
  chain is reopened as continuation frames (same symbols, ESPs and
  stacks), so what it runs next hangs under its real callers. A
  continuation counts its instructions like the call it continues but is
  not another call in the call tree, Top 30 or the sandwich. A new task
  starts at the root (ret_from_fork); a task that switches away from
  `do_task_dead` is gone, and a new task may reuse its stack. Every other
  open frame also ends at a switch, so tasks never overlap. Before, a
  task switch closed the calls of the task it left and the task resumed
  without callers: on Debian 6.1 (to its root-fs panic, 265 million
  instructions from the entry) 8,242 root frames, among them 1,150
  `__switch_to`, 1,071 `__schedule` and 430 `__cond_resched`, and 18 of
  the 19 `__trace_early_add_event_dirs` calls detached from
  `event_trace_init`; now 80 tasks with one root each (79 `ret_from_fork`
  and `startup_32`) plus 1,089 continuation roots (the same in every run on the virtual clock), no scheduler code as a
  root, all 19 calls under `event_trace_init`, and the roots cover all
  but 5,216 of the 265.3 million instructions (no overlap). A jump into the middle of
  a symbol that is not on the stack (a cold path, a function falling
  through into the next symbol) goes below the running call when that call
  lives above ESP, and replaces it otherwise: replacing it always split
  start_kernel into pieces at its first such jump (in
  `determine_cpu_tsc_frequencies` -> `tsc_early_init`) and left holes in
  the timeline. Symbol transitions alone missed returns (tail calls, returns into
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
    40 px wide, coloured by the top directory of the source file of its
    first instruction (DWARF, where its shard is loaded already: colouring
    never fetches, and the minified Linux and hello load their few shards
    up front) or a name guess, interrupts grey. Calls narrower than
    a pixel are merged per pixel column (in the colour of the call that
    covers most of it, more opaque the more of it is covered) instead of
    being dropped, which made dense stretches of tiny calls look empty
    until zoomed in; the covered columns of every row are the same as at
    8x zoom (a test checks this). Hovering a merged block shows how many
    calls it holds, and tapping it zooms into them; a strip with the
    whole run on top (drag in it to move the window), drag to pan, wheel
    or pinch to zoom. "Transpose" puts time downwards and depth across
    (for deep stacks on wide screens), "Flame" puts the root at the
    bottom. All tasks share the rows (one CPU: they never overlap), and a
    resumed task's continuations line up under the same callers. A thin
    slate-blue line marks each task switch, with the incoming task's
    name on a strip above the rows (see *Task names* below; labels that
    would overlap are left out; hover or tap one for the task and where
    its name comes from); switches from inside interrupt handling are
    hidden with "hide interrupts".
    "chains" (on) draws a call whose caller has a single call and
    almost no own instructions in the caller's box as "a › b › c". The
    depth slider caps the rows shown and marks deeper calls "+N". The
    search box highlights matching boxes and dims the rest. Tap a box for
    its inclusive/self counts, steps, callers, the callers and callees of
    its function aggregated over all its calls (sandwich), and "Focus",
    which re-roots the view at that call with a breadcrumb back to the
    root. A double click on a box (a double tap on a touch screen, or
    Enter on the chart with a box selected; the arrow keys move the
    selection: Up the caller, Down a callee, Left and Right along the
    row) opens its function in the code browser: the Page tab at its
    first instruction with the symbol's header line above it, and the
    Source panel at the function's line when the DWARF has one (the
    frame of the inline chain that is the function itself), as one
    history entry (one open per double click or tap, also when the open
    itself is slow), so Back returns to the Calls tab with the same
    window, scroll, selection, focus and orientation. Boxes with no
    symbol (BIOS and setup code, kernel code without a symbol), the
    merged columns (a press zooms into them) and the empty area do
    nothing. Rows scrolled out of view keep the
    names of the calls above the window's middle in a strip at the top.
    "follow" keeps the current step and the running call's row in view in
    slow motion, scrolling only when it leaves the visible band; a manual
    pan turns it off.
  - *Call tree*: like `perf report --children` (inclusive %, self %,
    calls), sorted by inclusive count, collapsible, with a search box; the
    root is the whole run from the kernel entry. With more than one task
    the outermost calls sit under one row per task ("task 1: init (pid
    1)", like perf's `--sort comm`); the JSON export carries the task
    number on those nodes.
  - *Top 30*: the 30 symbols with the largest inclusive count (recursion
    counted once), call edges between them (calls through hidden symbols
    are attributed to the nearest shown caller), layered by how many shown
    symbols are above them; edges pointing up are dashed. A double
    click on a symbol opens it as in the timeline.
  *Task names*: a task is named "comm (pid N)" from its `task_struct`,
  read from guest RAM with the member offsets from the kernel's DWARF
  (the types of D3: `current_task`'s type, so a kernel whose
  `task_struct` was cut down still works). The running task is read
  through the per-CPU `current_task` (CPU 0's copy). When a task switches
  away the stepper notes the `task_struct` that `current_task` still
  names at that moment (`__switch_to` sets the next task after the stack
  switch the stepper sees); while the task lives (its stack region still
  belongs to it) that `task_struct` is read again at every look, so a
  name changed by exec shows (the minified Linux's pid 1 starts with its
  parent's name "swapper" and is "init" once it has run /init). Tasks
  that switched away before the types were loaded are found in the task
  list (`init_task.tasks`, by `task_struct.stack`) when the kernel has
  that list (Debian yes; the minified Linux's `task_struct` has no
  `tasks` member, but its two tasks are the boot task, which is
  `init_task`, and the running one). A task that exited keeps the name
  it had when it last switched away. Without DWARF types (hello, an
  imported System.map) or before the kernel's own code runs, the old
  guess is shown and the tooltip says why: the boot task, else the
  task's thread function. The Debugger shows the running task on a
  "task" row above the call stack (comm, pid, `task_struct` address,
  "Watch" puts `*current_task` in Variables). The browser test reads the
  same fields with gdb's offsets (`ptype` of the -g ELF) from guest RAM
  itself and compares: the running task, every live task, and for
  Debian every task on the task list.
  "hide interrupts" (on by default) hides interrupt and exception frames
  (in the call tree: one "(interrupt and exception handling)" entry).
  JSON has the transitions and the call tree, DOT the transitions. For the
  minified Linux at `?speed=full` (before it dropped calibrate_delay):
  2,024 calls, 264 symbols, 723 transitions; now 1,751 calls, 245
  symbols, 665 transitions.
- **Snapshots.** "Save snapshot" calls `emulator.save_state()`, sends the
  state (transferred, not copied) to snapworker.js, which compresses it
  with LZ4 and stores it in IndexedDB (`mkb-snapshots`, one record per
  snapshot with the compressed state as a Blob), so snapshots survive a
  reload (snapshots.js). The LZ4 blocks come from lz4-wasm 0.9.2 (lz4_flex
  in WebAssembly, vendored in `site/lz4/`); lz4frame.js writes the LZ4
  frame format around them (4 MiB independent blocks, content size), so a
  download, `.v86state.lz4`, is read by `lz4 -d`. lz4_flex has one
  compressor, LZ4's fast one: what `lz4 -1`, the tool's default, runs; each
  row says "LZ4 level 1, N ms; save_state M ms" and the status line repeats
  it. Snapshots stored before this change are gzip
  (`CompressionStream("gzip")`, zlib's default level 6, which the stream
  API cannot change); they are still listed ("gzip level 6"), restored,
  diffed and downloaded as `.v86state.gz`. If the Worker cannot compress,
  the state is kept raw and the status says why. lz4js 0.2.0, the pure-JS
  LZ4 tried first, was dropped: `lz4 -d` refuses its 4 MiB blocks (a
  match too close to a block's end) and it compressed at a third of this
  speed.
  Measured (C1; Chromium 145 and Firefox 146 under Playwright on the 8-core
  test host, which another browser shared; medians of 3; state taken at
  Run to `hello_main` / `start_kernel`; compress and decompress times are
  the Worker's, gzip's were on the page):

  | kernel, RAM | state | gzip: size, compress, decompress | LZ4: size, compress, decompress | Save to listed (Chromium) |
  |---|---|---|---|---|
  | hello, 2 MiB | 1,495,016 | 94,168, 24 ms, 8 ms | 122,717, 2 ms, 8 ms | 46 -> 25 ms |
  | minified Linux, 2 MiB | 1,785,880 | 191,331, 36 ms, 13 ms | 266,922, 7 ms, 9 ms | 55 -> 30 ms |
  | Debian, 64 MiB | 23,009,252 | 14,107,828, 949 ms, 160 ms | 16,806,605, 148 ms, 113 ms | 1,116 -> 228 ms |
  | Debian + initrd, 128 MiB | 28,450,788 | 19,513,050, 1,190 ms, 185 ms | 22,264,056, 155 ms, 138 ms | 1,348 -> 284 ms |

  Firefox 146: Debian 64 MiB gzip 1,699 ms, LZ4 567 ms (Save to listed
  1,972 -> 884 ms); decompress 270 and 284 ms. LZ4 files are 19% (Debian)
  to 40% (minified Linux) larger. `save_state()` itself took 46 to 74 ms
  (Chromium) and Restore's `restore_state()` 0.4 to 0.6 s for Debian with
  either codec; no Save made a main-thread task over 50 ms with either. Each gets a default name
  (kernel, the instruction count, the symbol at EIP, the time); tapping
  the name renames it (Enter or leaving the field saves, Esc cancels).
  Each is tied to the kernel it was taken with: the SHA-256 of the bzImage
  (and of the initrd, if any). Snapshots of another kernel are listed with
  "Switch to NAME" instead of Restore. Each row shows size, raw size and
  ratio, and has Restore (decompressed in the Worker; the stepper's count
  and the page's clock set back to the snapshot's, then
  `emulator.restore_state()`, then v86's instruction counter set back to the
  snapshot's: v86 does not keep it in the state), Download and Delete; "Delete all" asks twice.
  Every count the page shows is the stepper's: the Machine tab's count line
  and counters table, the run badge's menu, the Reverse line, snapshot
  names and rows, the diff summary and the "This position" bookmark (X2).
  v86's instruction_counter runs a few dozen ahead of it (86 or 87 at the
  first Hello lines of hello and the minified Linux); a snapshot and a
  position bookmark keep it too, for Restore, Replay and to tie a bookmark
  to the snapshot of the same state. Until X2 the snapshot and bookmark
  names showed v86's number; names saved then keep it, and a bookmark
  saved then (it has no stepper count) lists with v86's number. The
  line under Save shows how many are stored, their total, the count for
  this kernel and `navigator.storage.estimate()`; the first store calls
  `navigator.storage.persist()`. A write that fails (QuotaExceededError, or
  no IndexedDB at all: a private window, blocked site data) keeps the
  snapshot in memory (the last 8) and says so.
  Restoring found a v86 0.5.471 bug: `CPU.set_state` assigns
  `last_result`, `fpu_status_word` and `mxcsr` to plain arrays instead of
  writing them into wasm memory, so the lazily computed flags (ZF, SF, PF)
  of a restored CPU came from the result before the restore (EFLAGS 0x2
  instead of 0x57 in the test). `v86RestoreFix` (cpustate.js) copies them
  into wasm memory after every `restore_state()`, also the fast-forward's.
  A second one (B1): `PCI.set_state` moves the I/O ports of every BAR slot
  whose saved value has bit 0 set through `pci_bars[slot]`, but the IDE
  controller has no BAR for a channel without a disk and its config space
  keeps the initial I/O bit there until SeaBIOS writes the BAR. A state
  saved at power-on (the reset vector) threw "Cannot read properties of
  undefined (reading 'original_bar')". `v86PciFix` (cpustate.js) puts an
  empty BAR in each missing slot while `set_state` runs; `v86Restore` does
  both fixes around every `restore_state()`.
  A snapshot is a point on the virtual-clock timeline (D1): v86's PIT and
  RTC keep absolute times in the state and `set_tsc` derives the TSC offset
  from the clock at the moment of the restore, so the snapshot records
  `CLOCK.now()` and the stepper's count, and Restore puts both back before
  `restore_state()`. Before that the clock ran on from where the machine
  was when Restore was pressed, and stepping on from a restored snapshot
  gave other timer interrupts than the first time (minified Linux, 500,000
  steps after a restore: 2 IRQ0 instead of 1). Snapshots saved before D1
  have no clock and restore as before.
  Delta snapshots (R1). The page keeps two 32-bit hashes per 4 KiB page
  (FNV-1a and a multiply-xorshift over the page's 1,024 words) of the last
  snapshot saved or restored. The Worker computes them for every save from
  the state itself, so the page's main thread does no more than
  `save_state()`. The next save of the same kernel and RAM size compares
  the hashes and stores a delta: v86's state with `[77]`/`[78]` holding
  only the pages whose hashes changed (a page that became zero included)
  and the info block's `mkb_delta_of` naming the base; CPU, devices and
  VGA memory stay v86's own buffers. A save is full when there is no base
  (the first snapshot after a load, another kernel, another RAM size, a
  base kept in memory only while this browser stores), after 31 deltas in
  a chain, or when more pages changed than the state has non-zero pages.
  Restore, Download and the diff rebuild a delta in the Worker: the full
  snapshot's RAM, each delta's pages on top in order, then the delta's own
  state with v86's `[77]`/`[78]` for that RAM, which `restore_state()`
  reads, so a download is a full state that `lz4 -d` reads. The hashes of
  the restored RAM come back with it, and the next save is a delta of the
  restored snapshot. A diff of a delta and its base compares only the
  delta's pages. Deleting a base first rebuilds each delta of it: a delta
  of the base's base with the pages that differ from it, or a full
  snapshot when the deleted one was full. The row says "delta of #N: K
  pages changed" and the raw size of the delta's state next to the full
  state's; the storage line counts the deltas. Snapshots stored before R1
  are full and stay as they are.
  Measured (R1; Chromium under Playwright on d-claude, 4 cores; Debian with
  64 MiB, JIT fast-forward to `startup_32`, then stepped; one save after
  every 10 M instructions, then every 1 M): the full snapshot is 17,282,308
  bytes stored (23.5 MB state, Worker 97 ms); the ten deltas 10 M apart
  are 40 KB to 5.3 MB (18 to 1,878 pages, median 226), 13.0 MB together;
  the same ten saves as full snapshots (before R1) were 16.8 to 17.4 MB
  each, 172.0 MB together. Worker 15 to 83 ms per delta, 79 to 105 ms per
  full save; the
  deltas 1 M apart are 31 to 33 KB (14 to 18 pages). Restore: 322 ms for
  the full snapshot, 335 ms one delta deep, 433 ms 5 deep, 531 ms 10 deep,
  557 ms 15 deep, each with the RAM's SHA-256 as at the save. The download
  of the 15th delta (the full state rebuilt and compressed again) takes
  316 ms in the Worker and is 17,404,608 bytes.
- Reverse execution in stepped mode (R2, reverse.js): Step back (the run
  bar, and the Debugger's Step back with the instruction count) and Back to
  (reverse continue to the Run to field's function, address or file:line).
  While the page steps the CPU on the virtual clock it keeps a checkpoint
  before a slice whenever half a second of stepping has run since the last
  one (`?revk=N`: every N instructions): v86's `save_state()` packed in the
  snapshot Worker as an R1 delta of the checkpoint before (every 8th one
  full), plus what lives outside v86's state (the count, the clock's base,
  count and rate exactly, J2's RTC start and generator, v86's instruction
  counter, the VGA text and lines the stepper mirrors, idle time, IRQ0
  count, the input log's length). The last 32 (at most 256 MiB packed) stay
  in memory; the oldest group of 8 goes first. Step back restores the last
  checkpoint at or before the target and steps forward to it; Back to runs
  each stretch between checkpoints again from the newest back, watching for
  the addresses, and stops at the last hit before now (the start of the
  history when there is none). Below the furthest count reached the
  stepper re-runs (Stepper.rerun) without recording again: the coverage,
  jump paths, symbols and Calls stay as they were at the furthest count and
  are not counted twice; the end, the prompt, panic() entered, exit and the
  milestone lines come back as the count reaches them, with the stops they
  made. While stepped back the Debugger's "previous", path and recorded
  call stack (the frames below the unwound ones) are not shown, and the
  Calls timeline still ends at the furthest count with now marked. Keys
  typed in the stretch go in again at their counts (replay.js INPUT.redo).
  Typing, the JIT or a clock change after stepping back go another way from
  there: the checkpoints, calls and task switches recorded ahead are
  dropped and the open calls closed there (a gap, as for a stretch the JIT
  runs). The history starts anew after a Run to with breakpoints or the
  fast-forward ran the JIT, a replay ran it, a snapshot restored, a clock
  change, a new machine; real time keeps none. Stepped checkpoints are not
  kept for the count at full speed (the JIT takes over from it). v86 0.5.471's `set_tsc` sets a restored
  TSC off by the offset in effect before the restore: after stepping back,
  SeaBIOS's TSC calibration came out one different and a byte of its data
  differed further on. And as shipped a restored machine does not run on
  under the JIT as the original did (R3: v86's JIT walks a page's entry
  points in hash-map order, and every new map takes the next hash key, so
  the order depends on how many maps came before; compiled code that is
  already there changes where the JIT's slices end too, so timer interrupts
  land elsewhere). The page's `v86.wasm` is built from v86's source with
  `v86-det.patch` (`gen-v86.sh`, R4): hash maps with a fixed hasher,
  `set_tsc` from the clock (a restore with the clock put back first gives
  the TSC as saved; its extrapolation of repeated readings off from there) and `jit_reset_hidden()`, which puts the JIT's cache,
  maps and free table indices, the TLB and the TSC's extrapolation into one
  canonical state. The page calls it after every restore (stepping back, a
  snapshot's Restore, the fast-forward's slice) and after every snapshot it
  saves, and logs it in the run (J4) so a replay resets at the same count.
  From a snapshot the JIT then runs as it did from there the first time:
  every slice end equal from Debian at 1.0e9 to 2.55e9 and Debian+initrd at
  2.5e9 to 3.3e9, restored in the same page, a reloaded one or a fresh page
  that booted to the point itself (test suite `jitsnap`). A JIT run after
  stepping back or after a Restore no longer taints the run's replay.
- Reverse execution at JIT speed (R5). While the JIT runs on the virtual
  clock (jitLoop) the page keeps checkpoints too: one where a stretch the
  JIT runs starts after stepping, then one about every 3 s of JIT running
  (`?revjitk=N`: every N instructions), each a `save_state()` followed by
  `jit_reset_hidden()`, logged with the run (J4) as a snapshot's reset is.
  The 3 s is an estimate from what the JIT did (2.2 ms per million
  instructions, 1.2 ms per thousand calls of compiled code, from
  `v86-det.patch`'s `jit_stats()`, and at most 1.5e9 instructions apart):
  with measured time the resets, and with them the JIT's slice ends, came
  at other counts every run. Until R6 the estimate took 4 ms per million
  and at most 500 M: the bzImage decompressor (2.2 ms per million on
  d-claude) got a checkpoint every second.
  The page also notes which stretches of the run the JIT ran and which
  were stepped (`REV.segs`). Step back restores the last checkpoint at or
  before the target and runs each stretch again the way it ran: the JIT
  stopped at the very instruction by the patch's instruction budget
  (`jit_set_budget`: compiled code does not enter a basic block that would
  cross the target, cycle_internal interprets the rest with v86's counter
  left where compiled code reads the clock from, and main_loop runs no
  timers there unless the slice ends there anyway), or stepping. Step and
  Continue while stepped back where the JIT ran ahead go forward on the
  recorded run the same way (the stepper would run another timeline); Run
  to and typing go another way from there. Back to looks for its addresses
  in the stretches that were stepped only and says how many instructions
  the JIT ran unwatched. Measured (gate, d-claude, Chromium): from a
  checkpoint at 2.0e9 on Debian and at 3.0e9 on Debian+initrd (initramfs
  user mode, where R3's stepper reproduced 0 of 150 JIT slices) to 9
  targets each (the slice end minus 1 and minus 1,000, and 7 inside the
  window), the state reached by restore plus budget equals a second such
  run, a page that ran straight there without a restore, and a run with the
  budget at the start of the stop's compiled call followed by v86's
  interpreter in JS (registers, EFLAGS, segment registers, CR0-4, RAM,
  clock, and the whole `save_state()`); the budget at a slice end gives the
  live run's state there. The budget costs the JIT about 8% (Debian reset to
  2.5e9: 366 and 367 MIPS without, 340 and 338 with), the slice ends and
  RAM unchanged.
  Diff: pick two snapshots of this kernel, or one and "now" (a
  `save_state()` taken for the diff). snapworker.js decompresses and
  compares them in a Worker (the page's main thread only does what Save
  does). It reads v86's state layout (src/state.js of v86 0.5.471): a
  16-byte header of four int32 (magic 0x86768676, version 6, total length,
  JSON length), the JSON `{ buffer_infos: [{ offset, length }], state }`,
  then the buffers from the next 4-byte boundary at their offsets; a typed
  array in `state` is `{ __state_type__, buffer_id }`. The CPU's array
  (src/cpu.js `get_state`): [0] RAM size, [2] segment bases, [10] CR0-7,
  [26]/[27] flags and the pending-flags mask, [37] the linear EIP, [39] the
  eight general registers, [40] the selectors, [77] the non-zero pages
  packed in order, [78] their bitmap (bit p & 7 of byte p >> 3). RAM and
  the registers come from the state; EFLAGS from `get_eflags()` at save
  time when flags are pending; the instruction count, EIP's symbol and the
  VGA text lines from what the page read at save time. The diff shows the
  instruction counts and EIP symbols, the registers that changed (old and
  new), the screen lines that changed, and the changed 4 KiB pages grouped
  by memory-map region or ELF section with their symbols and the bytes
  changed per page. "hexdump" shows the page's two sides next to each other
  (rows without a change left out; yellow bytes differ); "Page tab" opens
  the page with that side's bytes (the boxed line is that side's EIP, yellow
  bytes differ from the other side) and a note with "Show live memory";
  Back returns to the diff. With 64 MiB of RAM at a 4x CPU throttle the
  longest main-thread task of a diff was 51 ms (suite snapdiff).
- **Settings.** Everything the page remembers in this browser, each with
  an explanation:
  - "Compact tabs: emoji only" (`mkb-compact-tabs`): the tab buttons show
    only their emoji, the name is their tooltip (hover, keyboard focus,
    long press) and their accessible name. On by default at 400 px and
    less, where the names take three rows; there the nine emoji share one
    row (38 px each at 390 px, 30 px at 320 px). Off on wider screens.
  - Speed after the kernel entry (`mkb-speed`), clock (`mkb-mips`) and
    count (`mkb-count`): the same controls as in the Machine tab, which
    remembers a change made there too; `?speed=`, `?mips=` and `?count=0`
    override them for one visit.
  - Remembered for each kernel, listed from localStorage with a Forget
    button each: imported symbols (`mkb-map-<SHA-256>`; forgetting the
    current kernel's reboots it with its own symbols), command lines
    (`mkb-cmdline-<SHA-256>`; the current kernel's goes back to its
    default for the next Boot), watch expressions (`mkb-watch:<kernel>`),
    the measured instruction count to Still alive for the ETA
    (`mkb-ref-<kernel>-<clock>`, written once per boot), and the decoded
    DWARF of imported vmlinux files in IndexedDB (`mkb-dwarf`). Any other
    `mkb-` key is listed too, as kept by this page.
  - "Reset all settings" forgets all of it and goes back to the defaults.
    The service worker's offline copy of the site stays.
- **About.** What the page is, how the counting works, and the legend:
  a sample of every colour and mark of the page drawn with the classes
  the page itself uses (so light and dark and later style changes stay
  in step), with what it means and where it appears. The memory region
  and section swatches and the Calls tab's subsystem colours are drawn from
  the same tables as the Memory tab and the Calls tab. Code at EIP and the
  Page listing link to it ("Legend"); the link is a jump, so Back returns.
  `check-legend.py` (run with the site build) fails when style.css gives
  a class a colour, background, outline, stroke, dash, decoration,
  opacity or italics and the legend has no sample of it, unless the
  script names it page furniture with a reason, or when a colour token
  is defined and unused.

**Yellow lines.** An instruction line in Code at EIP or the Page listing
is yellow when guest memory holds other bytes there than the build's
disassembly (disasm.json). Its bytes are a mark with a popover (hover,
tap, Enter) that names the case, decided in `diffTip` (app.js):
vmlinux is not in place yet: the minified Linux's bzImage loads a
121-byte relocation stub at 0x100000 with vmlinux right after it, and
the stub moves vmlinux down over itself right after the kernel entry,
so until then every instruction of the kernel is yellow (the build's
bytes are found 0x79 bytes higher; the shift is read once from the
bzImage and disasm.json, 0 for hello); a Run to breakpoint: the fast Run
to writes HLT bytes over the target instruction while the JIT runs to
it (fastforward.js `ffBreaks`) and puts them back when it stops; zero
memory; anything else is a change at run time (patched code, freed
`.init` memory reused, a stray write). The probe behind this: on hello
nothing ever differs; on both minified builds 47,521 of 48,069
instructions differ at the reset vector and at the entry, none at
start_kernel or at the end of the boot (nothing is patched and
`.init.text` is not overwritten). Yellow bytes on a line that is otherwise the
build's (or in a hex row) changed since the last memory scan and say
so; while stepping the Page tab follows EIP, so they show for example
in slow motion over the relocation stub, right after it moved vmlinux. Live decode compares nothing (it
shows what is in memory), and a kernel without disasm.json (Debian, an
imported vmlinux) has no yellow lines. Yellow registers in the Debugger
changed since they were last shown.

**Back and Forward.** Every jump you make is an entry of the browser's
history: a symbol, address or branch target link, a jump source's "go
there", a file:line (Source panel), a Source panel address, the symbol
index, the page arrows, a page of the Memory grid, a call stack frame, a
Code at EIP line, a Source tab function or file, a Calls node, Run to
file:line from the Source panel. Each entry keeps the view you left: the
tab, the page, the listing's scroll and marked instruction, the Source
panel (open or closed, file, picked line, scroll) and the window's
scroll; it is captured again when you leave it, so Forward returns to it
as you left it. Back and Forward are in the menu of the toggle at the
bottom right (H1, below), enabled once there is a view to go to, and
their tooltips name the view they go to (`Back to
hello_main+0x1e`). Alt+Left and Alt+Right do the same unless the VM
screen was clicked last (then the keys are the VM's), and so do the
mouse's back button and a phone's back gesture: all of them go through
`popstate` in the same document, so the VM keeps running. The first jump
also turns the entry the page was loaded in into a guard: going back past
the first view shows it again instead of leaving the page; a second back
leaves. Following EIP while stepping only replaces the hash and adds no
entry. The navhist suite (test_browser.py) makes one jump of each kind in
hello, goes back through all of them by every means and forward again,
compares each view, and checks stepping, the VM screen's keys, back past
the start with the VM running, and a 390 px phone: after each jump and
each Back, and with the listing scrolled into the strip above the open
Source panel, the buttons cover no text of a listing line on screen (light
and dark). On 7967b0fc8 they covered the right end of the lines there.

**The menu toggle (H1).** One small fixed button at the bottom right of
every tab (`#runbadge_toggle`; left of the Source panel, on a phone above
it, and the listings keep its column free) shows at a glance what the VM
does: three bars (it opens a menu), a dot and a word. The dot is green and
pulses while the CPU runs, with the speed under it (1/s up to full); blue
and pulsing while the boot fast-forwards with the JIT (JIT); hollow when
paused (paused); a grey square when the kernel halted at the end of its run
(halted); a red square at a kernel panic (panic); dotted before a kernel
boots (off). Under `prefers-reduced-motion` the dot does not pulse; the
shape, the colour and the word still tell the states apart. It is drawn
with the Debugger's redraw (drawDebugUi), so it adds no work per
instruction. A click or tap opens its menu: the run state in words with the
instructions stepped from reset (a click opens the Machine tab), Pause or
Continue, the "new version" notice of the PWA with Reload (the toggle gets
a pink dot then, so the notice shows on every tab), and Back and Forward.
The toggle has aria-expanded; Esc closes the menu and gives the focus back
to the toggle, and so does a press anywhere outside it. It replaces the
always-visible Back and Forward box (N1) and the one-line run status next
to the tab bar (U5). The About tab's legend has a sample of each state.
Suite runbadge checks it.

**EIP in the Memory tab (M1).** The square of the page that holds the next
instruction gets a blue box (`#pages_eip`, the listings' EIP colour with a
ring of the background, so it reads on every region colour) and the page
ESP points into a dashed one (`#pages_esp`, the stack). Both addresses are
physical, the one the Debugger shows: CS base + EIP (SS base + ESP) through
the page tables once paging is on, the linear address before. An EIP and an
ESP label sit left of their rows (in a gutter, away from the menu toggle),
with a popover on hover, tap or Enter. The region bars get a frame around
the region holding EIP and a tick where it is. The line above the grid says
`EIP 0xc0126280 (phys 0x00126280) start_kernel, page 0x126 · ESP ...`; the
symbol+offset and page is a button that opens the Page tab at EIP (Back
returns), and the stack's page opens the Page tab at ESP. The boxes are
elements over the canvas: following EIP moves them and redraws nothing
else; they are drawn with the Debugger's redraw (drawDebugUi) and only while
the Memory tab is shown. At full speed (JIT) the line says so and the marks
come back when the CPU stops. The About tab's legend has a sample of each
mark. Suite memeip checks it.

## Offline and installing

The page is a Progressive Web App: browsers offer to install it (Chrome's
"Install app", "Add to Home Screen" on a phone), and after one visit it
works without a network.

- `manifest.webmanifest`: name, start_url and scope `./` (the site's
  directory, `/minimal-kernel-build/` on Pages), standalone display, the
  light theme's background colour, icons of 192 and 512 px and a maskable
  512 px one (`icons/`, drawn by `gen-icons.py`: a chip with a prompt in
  the dark theme's colours). index.html has a theme-color for light and
  one for dark.
- `sw.js`, the service worker, at the site's root so its scope is the
  whole site. At install it keeps the app shell (index.html, the scripts,
  style.css, kernels.json, v86 with its wasm and BIOSes, the manifest and
  icons) and the bzImage, symbols.json and disasm.json of hello, hello-os
  and both minified Linux builds, about 6.6 MB: those kernels boot offline
  even if only one of them was opened. Four requests at a time, so the
  precache does not crowd out the page's own loads. Everything else (DWARF shards, C
  source, Debian's bzImage and initrd, the iced disassembler) is kept when
  the page first fetches it, unless the origin's storage is 80 % full
  (then the file is not kept and nothing else changes).
- One cache pair per deploy (`mkb-shell-<commit>`, `mkb-data-<commit>`).
  The workflow's `stamp.py` puts the commit into sw.js (`const V`) and
  `?v=<commit>` on every script and stylesheet of index.html, so a deploy
  is a new service worker; once it takes over it deletes the other
  deploys' caches. Files with this deploy's stamp come from the cache
  first; the page itself (a navigation), version.json and anything
  unstamped come from the network first, the cache only offline, so a
  deploy is never hidden behind a cached page. A local checkout (`V =
  "dev"`, nothing stamped) is network first for every file. A Range
  request answered from the cache gets the bytes it asks for (206).
- Offline, a file that was never fetched online gets a 503 from the
  worker; the page says "... is not available offline yet" in the top
  bar (`#pwa_notes`) and, for a kernel, in the status line.
- Updates (`pwa.js`): the page compares version.json with its own stamp
  when it loads, when it comes back online or into view, and every 5
  minutes. A newer deploy shows "A new version of this page is deployed
  (build ...)" with Reload, which lets the new worker take over (its
  caches replace the old ones) and reloads. A page that is already the
  new deploy lets the new worker take over silently.
- `check-pwa.py` (workflow, site job, after stamping): every precached
  file exists in the site, every script and stylesheet of index.html is
  precached with the stamp the page asks for, sw.js's commit is
  version.json's, and the manifest is installable under the Pages
  subpath (start_url and scope resolve to the site's directory, each icon
  a PNG of the size it claims).

### Installing it for real (test_pwa_install.py)

Headless Chromium never offers to install a page, and its
`Page.getInstallabilityErrors` answers `[]` even for a page with no
manifest, so the `pwa` suite cannot tell an installable page from one
that is not. `test_pwa_install.py` runs Chromium with a window under Xvfb,
a fresh profile per case, `--bypass-app-banner-engagement-checks`, and
asks for four pieces of evidence:

1. `beforeinstallprompt` fires (caught by an init script);
2. `Page.getInstallabilityErrors` is empty and `Page.getAppManifest`
   parses the manifest with no errors or warnings, scope and start_url
   the site's directory;
3. `PWA.install` installs it, `PWA.getOsAppState` knows its manifest id,
   and `chrome://web-app-internals` lists it with our name, scope,
   start_url, manifest id, display standalone and the three icons;
4. `PWA.launch` opens it in its own window: `(display-mode: standalone)`
   matches there and hello boots to "Hello, World!". The case saves the
   app's page and an Xvfb screenshot of the whole display
   (`test-screenshots/pwa-install-*`), which shows the app window with
   its own title bar and no tab strip or address bar.

Plus the site's own requirement: a service worker controls the page.
Two details of the DevTools protocol: the `PWA` domain answers "wasn't
found" over `--remote-debugging-port` (it works over Playwright's pipe),
and `PWA.install` records the user's choice as "open in a browser tab", so
the test sets "open in a window" (`PWA.changeAppUserSettings`) before the
launch, as the install dialog does by default.

Without `--base` it serves a deployed copy of the site (as the `pwa`
suite does) at `http://localhost:PORT/minimal-kernel-build/` and four
broken copies, each of which must fail for its own reason, or the test
fails as not discriminating:

| copy | Chromium 148 and 156 | the test says |
|---|---|---|
| the site | installs, launches standalone | pass |
| manifest link removed | no beforeinstallprompt, installability error `no-manifest`, no manifest URL | fail |
| icons 404 | no beforeinstallprompt, installability error `no-acceptable-icon` (`PWA.install` still installs it with a generated icon); the service worker does not install (its precache has the icons) | fail |
| service worker never registered | installs and launches: Chromium no longer needs a service worker or fetch handler to offer the install | fail on our own check (no service worker controls the page) |
| start_url `../elsewhere/` | manifest warning "property 'scope' ignored", scope and start_url /elsewhere/, `PWA.launch` refuses the site's URL | fail |

    xvfb-run -a site/test_pwa_install.py
    xvfb-run -a site/test_pwa_install.py --base https://d33.pl/minimal-kernel-build
    xvfb-run -a site/test_pwa_install.py --chromium /usr/bin/chromium --only site,icons-404

The workflow's `pwa-install` job runs both (the local copies with
Playwright's Chromium, then the live site once deployed) with
`xvfb-run`. Checked on host d-claude (Debian 13) with Debian's Chromium
156.0.8078.79 and Playwright 1.60's Chromium 148.0.7778.96, the same
results with both; that host has no colour emoji font, so the tab emoji
are boxes in its screenshots.

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
20 ms; the code section is untouched, so the checksum in `gen-v86.sh`
still covers what runs; the build keeps npm's v0 symbol names, which these
lookups need) and `Stepper.run()` does per instruction what
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
  counters count live from there: hello reaches "Still alive" after 432
  instructions (154 for the -Os hello-os; it prints below the BIOS text instead of clearing the
  screen, see hello/README.md), a fraction of a second; the minified Linux needs about 723,000
  at -O0 (about 12 minutes; the -Os minified-linux-os about 132,000, 2 minutes 12 s),
  so the Machine tab shows an ETA (from the count this
  browser last measured, else a built-in estimate) and the symbol the CPU
  is in.
- **Full speed.** `?speed=full`, or picking "full (JIT)" (the choice is
  kept in `localStorage`; `?speed=` overrides it): with **count** on, the
  whole boot is stepped in the background from the reset vector until
  "Hello, World!" and "Still alive" are both in VGA text memory at 0xb8000
  (the page wraps v86's MMIO write handlers for 0xa0000-0xbffff and keeps
  a copy of the text page), at about 4-7 million instructions per second
  in headless Chromium, then v86's JIT runs.
- **speed**: full (v86's JIT; counted on the virtual clock) or 10M, 1M, 100k, 10k, 1k,
  100, 10 or 1 instructions per second. The Machine tab shows the speed
  actually reached, from v86's instruction counter (JIT and stepping
  alike) sampled 4 times a second, over a window (4 s in slow motion,
  where one drawing of a new page's listing can hold a phone's main
  thread for 2 s, 1 s otherwise) that starts
  at the last Boot, Pause, Continue, speed change, kernel entry or the
  tab being shown again; for the first half of the window it says
  "measuring" (a one-second window showed "0/s" at 1/s while running,
  and the fast-forward's rate right after the entry). Continue and a speed
  picked while running run the first instruction at once. JavaScript stepping tops out around 5M/s on a
  fast desktop, so 10M/s shows about that; 1M/s and up run in the message
  loop and redraw 10 times a second. In a hidden tab (no animation
  frames) the slow stepping runs on a timer with the step credit of the
  time that passed, so it keeps the set rate where the browser runs the
  timer at least once a second; when a phone freezes the tab it stops
  and goes on when shown again. The step credit counts the real time
  since the last frame, however long drawing took (at most a second's
  worth at once). Stepped speeds update the screen, the registers and
  counters when an instruction has executed (and 4 times a second for
  the clock); the views that follow EIP (the grid's flashes, the Page
  tab on the page holding EIP, Code at EIP) are drawn when an
  instruction has executed since their last drawing, at most 5 times a
  second and after a pause as long as the last drawing took, so drawing
  takes at most half of the main thread. Stepping as fast as it can (a
  Run to that steps, counting, the way to the kernel entry) draws them
  too, every 300 ms or twice as long as the last drawing took, so Code at
  EIP and the Page tab never keep the box where the run started while the
  count goes on. RAM is re-scanned then (every 250 ms above 4 MB) so the
  pages written since flash in the grid. Where EIP is inside an
  instruction of the build's disassembly (the bytes there differ: the
  bzImage's relocation stub runs over vmlinux's startup_32 right after
  the entry) the Page tab boxes that instruction with a dashed outline and
  its tooltip says why; Code at EIP decodes those bytes live.
  Inside one page the Page tab moves its EIP mark and untints what has
  executed; it draws the whole listing again (observed counts) every
  4 s or six times as long as a drawing took, and when the page or its
  bytes change. When the run still falls below half the set speed the
  state line says so: "slow motion 1/s (reaching 0.4/s: too fast for
  this device)". At full speed (the JIT) the kernel's end, hlt with
  interrupts off, pauses the page with the end as stepping does (it
  showed "running (full speed, JIT)" at 0/s), and a fast-forward shows
  "speed: measuring" until it has a rate.
- **Pause / Continue / Step / Step N / Run to.** Run to takes a hex
  address, physical or kernel-virtual (minus the virtual-physical offset
  of `symbols.json`, 0xC0000000 for the minified Linux, 0 for hello), and
  stops before executing the instruction there; or a source line,
  file:line, and stops at the first of its breakpoint addresses (see
  "Line to address"); or a function name, and stops at the first
  instruction of every code symbol with that name. A target the CPU
  cannot stop at is refused and the CPU stays paused: a name that is no
  symbol (the message lists similar names: `hello` names `hello_main`),
  an address in the kernel's data (`.rodata`, `.data`, `.bss`) or inside
  an instruction of the build's disassembly. These used to run on to the
  end of the boot without a word (on a phone the screen then showed
  "Still alive"). A Run to that reaches the end without its target says
  so, and the paused screen's overlay shows the same note as the status
  line. Run to from a target goes on to its next hit (it used to stay put
  with the JIT). A stepped Run to checks the first instruction of every
  stepping slice: a slice that ended right before the target (time
  budget, or the slice that put "Hello, World!" on the screen) used to
  step over it. Suite `runto_phone` checks all of this on a 390 px touch
  phone at 1 and 10 instructions a second and on a desktop, and
  `--base URL` runs it against the deployed site. `?paused=1` starts
  paused at the reset vector.

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
and its write hook checks each character as it is written: the "-" that
completes the line, the ":" after "Kernel panic - not syncing" and the
space after "(initramfs)" at the start of a row (Debian's initramfs
shell). Stepping stops at the instruction that writes the panic line's
last character, pauses, stops counting and shows "Kernel panic:
<message> (stopped where its last line was written)". The prompt pauses
the machine once, at the first halt after it (the shell waits for input),
with an "(initramfs) prompt" row in the counters. At full JIT speed on the
virtual clock the page runs the stepper from the slice (about 100,000
instructions) in which "Kernel panic - not syncing" appears, so the stop
is the same instruction; the prompt stops at the end of the first slice
that ends halted after it, or the stepper runs on to the next halt. So
the counts at these stops are the same in every run (test suite
`exactstop`); until J3 the page polled the screen 4 times a second and
the stop varied by up to ~1 M instructions (D0). With the "real time"
clock at full speed (v86's own loop) the page still checks 4 times a
second and stops where the run has got to ("(stopped by the page)").
Debian's 6.1 686 without an initrd ("VFS: Unable to mount root fs on
unknown-block(0,0)") stops 264,979,927 instructions after its entry when
stepped and 261,561,134 at full JIT speed (the JIT delivers timer
interrupts at its slice ends, so it is another timeline); with its
initramfs the prompt stops at 1,116,037,045 and 1,002,205,228. Under the
JIT the "panic() entered" row says "run by the JIT": the stepper takes over
inside panic().

The Machine tab shows which build is deployed: the workflow writes
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
timestamps every run. At full JIT speed on the virtual clock the page runs
v86's `main_loop()` itself, one slice (about 100,000 instructions) at a
time, the clock follows v86's instruction counter, and compiled code goes
live only between two slices (v86 instantiates its wasm asynchronously, so
as shipped a block goes live at a moment of wall time, which moves where
slices end and so where timer interrupts land). The page compiles each
block with `new WebAssembly.Module()` when v86 hands it over, so it is
ready when the next slice starts (R6: waiting for the asynchronous
instantiation there took about 1.4 s of an 8 s Debian boot). A full-speed run from the
same start repeats exactly, at the same speed as v86's own loop or faster
(Debian to 2.5e9 instructions in Chromium: 270-284 vs 239 MIPS; test suite
`jitdet`, which runs Debian on to its exact end-of-panic stop). "real time" in the menu restores v86's wall clock while stepping
and v86's own loop at full speed. The Machine tab shows the time and how many times
the PIT raised IRQ0.

The guest's other inputs from the browser follow the virtual clock too
(`clock.js` GUEST). v86's RTC reads `Date.now()` (at construction, in every
`timer()` call and on register B writes); on the virtual clock it runs from
a start date (`?rtc=` ISO 8601, default 2026-01-01T00:00:00Z) plus the
virtual time, so SeaBIOS's tick count at 0x46C and the kernel's date are the
same every run. RDRAND (v86's `get_rand_int`, `crypto.getRandomValues()`) and
the NE2000's MAC address come from a generator (mulberry32) started from
`?seed=` (default 1). The Machine tab shows the RTC, its start date and the
seed; a snapshot keeps the start date and the generator's state, and Restore
puts them back. With the real-time clock the guest gets the browser's date
and random numbers. Before this (D0) the stepped small kernels differed only
in the BIOS tick count, and Debian 30 M instructions after its entry in about
50 pages of RAM (test suite `guestin`).

The keyboard is the last input that comes at a moment of wall time: v86's
keyboard adapter hands each scancode to the PS/2 controller during the DOM
event. On the virtual clock `replay.js` (J4) holds the scancodes and puts
them in at the next boundary: before the next JIT slice (`jitLoop`), the next
stepper slice (`driverTick`, Step, the exact stops) or a Continue. Each batch
is logged with the stepper's count and the virtual time (a halted CPU lets
time pass without instructions, so the count alone does not name the
moment), and the log notes where the run switched between the JIT and
stepping (two different timelines, D1). A run is the boot settings (kernel,
RAM, command line, clock, speed, count, the RTC start date and seed), the log
and where it ended (count, clock, v86's counter, EIP, a hash of RAM and of
the VGA text). The Snapshots tab downloads it as `.mkbrun.json` and replays
it or a file; snapshots and position bookmarks keep the run up to them and
have their own Replay. Replay boots the same kernel with those settings,
stops each stepper slice at the next logged input (JIT slices end at those
counts anyway), goes on past the stops the run went on past (the initramfs
prompt), stops at the run's end and says whether count, EIP, RAM and screen
match. A JIT Run to (breakpoints) or a clock change mark the run as
replayable only up to there. The log also holds where v86's hidden JIT
state was reset (a snapshot saved or restored, stepping back: R4), and a
replay resets it at the same count. Runs recorded before R4 (version 1,
npm's v86 build) do not replay. With the
real-time clock nothing is recorded (test suite `replay`).

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
| hello (-O0) | Hello, World! | 234 | 43 / 7,770 | 1,405,984 | |
| hello (-O0) | Still alive | 432 | 52 / 7,770 | 1,406,182 | |
| hello (-O0) | halted | 441 | not measured | 1,406,191 | 2 |
| hello-os (-Os) | Hello, World! | 86 | 17 / 7,770 | 1,405,836 | |
| hello-os (-Os) | Still alive | 154 | 25 / 7,770 | 1,405,904 | |
| hello-os (-Os) | halted | 159 | 27 / 7,770 | 1,405,909 | 2 |
| minified Linux (-O0) | Hello, World! | 721,292 | 29,236 / 8,176 | 2,128,763 | |
| minified Linux (-O0) | Still alive | 723,158 | 29,377 / 8,181 | 2,130,629 | |
| minified Linux (-O0) | exit(0) | 723,554 | 29,377 / 8,183 | 2,131,025 | |
| minified Linux (-O0) | panic, stopped (hlt) | 723,660 | 29,403 / 8,184 | 2,131,131 | |
| minified Linux | Hello, World! | 131,287 | 10,026 / 8,171 | 1,538,758 | |
| minified Linux | Still alive | 132,006 | 10,111 / 8,171 | 1,539,477 | |
| minified Linux | exit(0) | 132,146 | 10,113 / 8,171 | 1,539,617 | |
| minified Linux | panic() entered | 132,198 | 10,121 / 8,171 | 1,539,669 | |
| minified Linux | panic, stopped (hlt) | 132,199 | 10,123 / 8,171 | 1,539,670 | 2 |

(The "minified Linux" rows without -O0 are the -Os product build, now
the minified-linux-os entry. The kernel was rebuilt since the qemu
cross-check below: 486 build, panic halts.)

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
| hello-os (-Os) | Hello, World! | 5,812,951 | 86 | 86 | 17 / 17 |
| hello-os (-Os) | Still alive | 5,813,019 | 154 | 154 | 25 / 25 |
| minified Linux | Hello, World! | 6,039,379-6,041,101 | 224,766-227,330 | 273,966 (single steps) | 10,666 / 10,638 |
| minified Linux | Still alive | 6,040,512-6,042,036 | 225,899-228,265 | 274,894 (single steps) | 10,752 / 10,723 |

hello (re-measured after it stopped clearing the screen; the -Os build, now hello-os): the 154 kernel instructions are the same sequence in both. Linux:
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

## Memory events (M1)

Who touches which physical address, and when. `memevents.js` keeps a log of
three kinds of events:

- **cpu**: the CPU's accesses to physical addresses outside RAM
  (memory-mapped I/O: the legacy video window, the local APIC and I/O APIC,
  PCI BARs and expansion ROMs, the SVGA framebuffer, BIOS aliases). They
  come from `v86.wasm` (`src/rust/mev.rs` in `v86-det.patch`), reported from
  the interpreter and from compiled code alike, with the value read or
  written, EIP and EIP's physical address (shown as function and
  `file:line` where the kernel has symbols and DWARF).
- **device**: every RAM access of a device: DMA in both directions, the
  descriptors and rings a device reads and writes (IDE's PRD table,
  virtio's queues) and the 8237's transfers. v86's devices reach RAM only
  through `cpu.js`'s `dma_*` functions now (the patch's JS part: no device
  touches `mem8` itself); bulk transfers carry the byte count and a hash.
  v86 emulates no MSI.
- **emulator**: RAM written by the emulator itself through `emu_*`: the
  BIOS images, the kernel loader (real-mode part, protected-mode part,
  command line, initrd), a snapshot's restore and the page's own writes
  (`write_memory`, the fast Run to's breakpoints). The loader's writes are
  the first events, at count 0.

The position is the stepper's instruction count before the instruction
that caused the event. Compiled code adds a whole block to v86's counter,
so the patch has it store how many instructions of the current call ran
before an instruction that calls out (a memory slow path, an instruction
implemented by a function, port I/O); `mev_now()` is then exact in compiled
code, interpreted runs, the budget's interpreted tail and stepping. A Step
back to an event's position stops right before its instruction. The cost
with the list closed: one store before such calls (no call, no event); the
memory events code itself is off.

Region names come only from standards the guest itself uses, and each says
which: the IBM PC memory map (video memory windows, video BIOS, option
ROMs, system BIOS, interrupt vector table, BIOS data area), the BIOS's INT
15h E820h answers (observed at the real-mode `iret` of the guest's call),
PCI configuration mechanism #1 on ports 0xCF8/0xCFC (vendor and device,
the class code named after the PCI class table, BARs with their all-ones
size probe, expansion ROMs), the ACPI MADT and HPET tables or the Intel
MultiProcessor Specification's table when the firmware has them (v86's
SeaBIOS has the MP table, no ACPI), and IA32_APIC_BASE's power-up value.
An address no standard has named yet says so. What the guest told is kept
as observations with their positions (`MEV.obs`, `MEV.facts`): the order
in which the OS sized each BAR, every E820 entry.

The log is columnar and a ring (up to 262,144 events, the oldest dropped
first) in position order. `MEV.query({ from, to, page, initiator, write,
region, limit })` gives the matching indices, `MEV.event(i)` one event.
A Step back or a restored snapshot drops the events from the restored
count on (and the observations, so region names are as they were then);
running on records the same events again.

The Memory tab's "Memory events" list (closed by default) records while it
is open: the newest 100 events, the regions known now and the last
observations. `?memevents=1` opens it at load.

**Checking mode** (`?memcheck=N`): every N instructions every 4 KiB RAM page
is hashed (`mev_hash_pages`, in wasm). A page that changed with no CPU
write and no device or emulator event in that interval is a failure
(`MEV.check.failures`: page, interval). The CPU's writes: `v86.wasm` marks
the pages for which it makes a TLB entry for writing (every CPU write to
RAM needs one, compiled code too) and its physical writes (page tables'
accessed and dirty bits, the BIOS alias at 0xFFF00000), and empties the
TLB at each interval. The test (`test_browser.py`, suite `memevents`) runs
it to the end of hello, the minified Linux (-O0 and -Os) and Debian, and
writes one byte behind the API's back, which must be the only failure.

## Live memory map (M2)

The Memory tab's "Live memory map" (`memmap.js`, closed by default, its
state remembered in this browser; `?memmap=1` opens it) draws the whole
32-bit physical address space on a Hilbert curve of 128 x 128 cells, so
addresses next to each other stay next to each other on the square. It is
cut at every region `MEV` knows and at the end of RAM: each region gets at
least 16 cells, RAM shares 60% of the rest by size and the other regions
the remainder by the square root of their size (a 16 MiB framebuffer
would otherwise dwarf 2 MiB of RAM), and stretches no standard names get
24 hatched cells each. Every label names its standard.

While it is open the page records memory events (the same log as the
list) and lights each event's cells in its initiator's colour, fading in
about a second: the CPU outside RAM with a pulse from the cell of the code
that did it (a ring marks it), a device in RAM from its PCI region (or the
map's edge), the emulator as a drop poured along the curve (the loader
places the kernel before the first instruction). The CPU's RAM writes are
not events; the map reads v86.wasm's checking-mode bitmap every frame (the
pages that got a TLB entry made for writing, which only a write makes;
`mev_track_begin` empties the TLB) and lights those pages too. Beside the
map, the colour text memory at 0xB8000 is drawn from the log's writes (CGA
layout, code page 437, the CGA palette): the characters appear as the
guest paints them, and the window follows the last row written. The story
lists, in position order, who touched each region first (per initiator
and source: the region of the code, the device, the emulator's reason)
with the function and file:line, and what the guest learned from the
standards (`MEV.facts`).

Time: live, the map follows the log; the slider moves through it event by
event; Replay plays it from the first event (the whole log in 20 s at 1x;
0.25x and 4x); Live goes back. A step back cuts the log and the map
follows. A click on a cell (or the keyboard: arrows, PageUp/PageDown by
region, Home/End, Enter) lists its events with "go there" (a step back to
the instruction) and file:line links. "Only first touch" lights each
region once per initiator and source, with callouts for the newest.

Cost: closed, nothing is recorded or drawn (the jitdet floor is unchanged).
Shown, live drawing is limited to about 20 frames a second; Debian at full
JIT speed to 2.5e9 instructions on d-claude ran at 0.91 times the speed
with the map closed (330.7 against 363.1 MIPS, 3.4 ms a frame). The test
(`test_browser.py`, suite `memmap`) checks the layout, the text rebuilt
from the log against the stepper's mirror of the screen, the story on the
minified Linux and Debian, Replay's drop, the slider, a click, the
keyboard, go there, only first touch, closing, that the events equal the
list's, and the cost.


## Debug info: help, sizes, times and tests

Every element the debug features put on the page has an explanation a
first-time user can reach: a "?" next to it (tap or hover; the same text
for keyboard users) or a title on the row or item. The Debugger's
"source", "task", "call stack", "previous" and "path" rows, the method tag
of each call stack frame and of each "Parameters and locals" frame (EIP,
CFI, pt_regs, entry ESP, EBP chain, recorded), the C source toggle and
picker, the Variables watch field, the Source tab's filter and lcov export,
the Source panel's legend (its first line), and the symbol offset field.
The firstlook suite checks this for every kernel (see below).

Things a first-time user runs into, and what the page says:
- Run to while paused at the reset vector runs BIOS, setup and the
  decompressor on v86's JIT, then sets a breakpoint (code in the entry's
  section) or steps from the kernel entry (other sections such as
  .init.text: start_kernel; a breakpoint there could hit memory that is
  freed and reused after boot). Debian's Run to start_kernel took 18 to
  30 s here instead of about 4 minutes of stepping.
- Code the JIT ran is not recorded: the symbol history marks the gap
  ("(v86's JIT ran here: not recorded)") and the Source tab says its
  counts are incomplete when the JIT ran kernel code.
- An imported vmlinux has no coverage tree (the tab says why; lcov is
  disabled) until its data files are made in the browser, and no C text
  until its source folder or .tar is picked.

Download sizes per kernel and feature (CI run 42270272424; bytes on disk,
"gzip" is `gzip -6` of the file, about what Pages sends; `.json.gz` and
`src/*.gz` are already compressed). Everything under `dwarf/` and `src/`
is fetched lazily, a shard or file the first time an address or line in
it is shown.

hello rows: the -O0 build (the -Os hello-os: bzImage 2,688 bytes, symbols.json + disasm.json 1,876, CFI / locals shards 23 / 208).

| kernel | file | files | bytes | gzip | fetched when |
|---|---|---:|---:|---:|---|
| hello | bzImage | 1 | 2,784 | 438 | boot |
| hello | symbols.json + disasm.json | 2 | 3,376 | 1,107 | boot |
| hello | line and inline shards (D1) | 2 | 553 | 394 | first address shown |
| hello | C source (D2) | 2 | 675 | 669 | first line shown |
| hello | types.json (D3) | 1 | 139 | 138 | Variables, hover, data page |
| hello | files.json (V2) | 1 | 199 | 184 | Source tab |
| hello | CFI (D5) / locals (D4) shards | 1 / 1 | 51 / 134 | 73 / 134 | a pause |
| hello | function facts (A3) | 1 | 160 | 137 | a function's facts shown (Debugger, Source, Calls, tooltips) |
| minified Linux | bzImage | 1 | 91,568 | 36,993 | boot |
| minified Linux | symbols.json + disasm.json | 2 | 813,399 | 172,920 | boot |
| minified Linux | line and inline shards | 3 | 79,521 | 42,914 | first address shown |
| minified Linux | C source | 196 | 287,038 | 283,377 | per file shown |
| minified Linux | types.json | 1 | 37,048 | 12,379 | Variables, hover, data page |
| minified Linux | files.json | 1 | 24,152 | 8,478 | Source tab |
| minified Linux | CFI shards | 2 | 9,429 | 3,674 | a pause |
| minified Linux | locals shards | 2 | 84,605 | 84,605 | a pause |
| minified Linux | function facts shards | 1 | 107,313 | 28,960 | a function's facts shown |
| Debian 6.1 686 | bzImage | 1 | 5,707,712 | 5,681,498 | boot |
| Debian 6.1 686 | symbols.json | 1 | 5,179,791 | 804,023 | boot |
| Debian 6.1 686 | line and inline shards | 154 | 9,620,444 | 4,885,707 | per 64 KiB of code shown |
| Debian 6.1 686 | C source | 3,233 | 16,098,216 | 16,026,134 | per file shown |
| Debian 6.1 686 | types.json | 1 | 3,766,465 | 978,361 | Variables, hover, data page |
| Debian 6.1 686 | files.json | 1 | 2,159,580 | 651,741 | Source tab |
| Debian 6.1 686 | CFI shards | 153 | 1,511,034 | 455,261 | per frame of a pause |
| Debian 6.1 686 | locals shards | 153 | 16,069,116 | 16,069,116 | per frame of a pause |
| Debian 6.1 686 | function facts shards | 83 | 9,305,209 | 3,442,705 | per function name shown (32 to 61 KB each) |
| Debian with its initramfs | initrd.img (the rest is the Debian entry's) | 1 | 5,435,993 | 5,402,522 | boot |

What a typical first session fetches (local measurement: open the page
paused, Run to a function, Watch the first quick variable, open the Page
tab and the Source panel at EIP, then the Source tab; files and gzip
bytes of the debug data, then the total with bzImage, symbols and
disassembly):

| kernel, Run to | line shards | C source | types.json | CFI | locals | files.json | total gzip |
|---|---:|---:|---:|---:|---:|---:|---:|
| hello, hello_main | 2: 334 | 2: 669 | 119 | 1: 41 | 1: 210 | 153 | 2.6 KB |
| minified Linux, tty_write | 3: 42,914 | 44: 68,607 | 11,562 | 1: 2,480 | 1: 65,364 | 8,478 | 409 KB |
| Debian 6.1 686, start_kernel | 5: 113,038 | 24: 112,153 | 879,672 | 2: 5,276 | 2: 108,505 | 651,741 | 8.4 MB (5.7 MB of it the bzImage) |

Times. CI (same run): the kernels job 2.6 min, of which the Debian step
70 s (gen-dwarf.js: decode 2.2 s, table 1.0 s, writing 153 line shards
25 s, files.json 7.3 s, types.json and locals 17.6 s, CFI 0.1 s;
gen-debian-source.sh and gen-src.js the rest) and the -g builds 4 s
(minified) and under 1 s (hello); the site job 22 s; the four
browser-test jobs 4.4, 4.8, 4.5 and 6.2 min wall (suites: debian 160 s,
tasks 312 s, types 167 s, boot 99 s, the others under 45 s each).
Imports (CI, Chromium): an unstripped minified -g vmlinux (676 KB) gives
file:line at once and its Variables types pass takes 1.0 s; Debian's
-dbg vmlinux (313 MB) gives file:line after 3.3 s (only the ELF headers,
the small sections and one compile unit are read) and the Variables
types pass takes 20.1 s in its own Worker; both are kept in IndexedDB.

Which test covers which feature (CI job: `kernels`, `site`, or the
browser-test matrix job named by its suites):

| feature | tests | CI job |
|---|---|---|
| D1 file:line and inline chains | `test_dwarf.py` (Debian 5,000, minified 5,000, hello 500 random addresses vs llvm-symbolizer --inlines); suites dwarf (minified, hello: Debugger, listing, call stack, Calls vs llvm-symbolizer), debian (start_kernel 12 EIPs), dwarf_import (imported minified -g and Debian -dbg vmlinux, after a reload from IndexedDB; the minified one's parameters and locals at start_kernel + 6 steps, decoded in the browser, equal the shipped DWARF's) | kernels; debian,dwarf,dwarf_import,... |
| D2 C source | `gen-src.js` (fails on a line past EOF), `test_kernel_data.py` (every src/ file listed); suites dwarf (line texts), debian (Source panel, files fetched), dwarf_import (a pax, a GNU and a ustar tar with a directory path in the ustar prefix field, each indexing as many files as Python's tarfile lists, a folder through its button; a .tar.gz and a file that is not a tar refused with the reason, a folder without the kernel's files named in Code at EIP's line; the Source panel's switcher lists a picked file no line table names and opens it whole; start_kernel's "defined at" in the Calls tab opens its function with the declaration line, whose text is the file's) | kernels; site; debian,dwarf,dwarf_import,... |
| D3 types and globals | `test_dwarf.py --types` (vs gdb; on hello too, whose only variables are unnamed string literals: it says there is nothing to check); suite types (watches vs gdb, among them `current_task->comm` and `init_task.comm[1]`; a member that does not exist, `->` on a number, a member of an int and an unreadable suffix each say why in the watch's row; hover byte paths, pointer to page, Debian import JSON-identical; `vars_edges`: a bit field and a member inside anonymous members vs gdb, a row of a two-dimensional array opened (its elements vs gdb), an index past the end, past the end of RAM, a pointer to a function (gdb's whatis), `*p` and `p[0]`, a static local by its function, an .init.data, a per-CPU (template or CPU 0's copy as gdb's `__per_cpu_offset[0]` says) and a flexible array, a variable whose DIE has no file, the errors of a non-name, an unknown name, a NULL pointer and a member of a bit field; the task row's Watch, a + suggestion, Enter, an empty field, an opened row closed staying closed after Refresh, at most 40 name suggestions, a stored watch list that is not JSON; in the Page listing the ASCII column, a byte in no variable, a union's other member, a per-CPU template and a click that unpins) | kernels; types,covtree,unwind |
| V1 C line colours | suite dwarf (`src_colour_check`: loc and text colours differ, contrast at least 4.5, light and dark, every view) | debian,dwarf,dwarf_import,... |
| V2 coverage browser | suites covtree (tree sums, files.json, Source panel whole file, lcov; each sort order; paused at the reset vector: totals only; line shards held back: the tree and the lcov export wait, then the export without the shard that answered HTTP 404 and the tree saying why the lines run are unknown), debian (Debian tree, shards fetched; a filter matching more than 1,500 rows draws 1,500 and says how many more, as files.json counts them) | types,covtree,unwind; debian,... |
| V3 multi-line statements | suite covtree (`multiline_case`: asm statements in string_32.c and irqflags.h, counts equal in panel, tree, lcov) | types,covtree,unwind |
| D5 CFI unwinding | `test_dwarf.py --cfi` (vs llvm-dwarfdump rows), `--eh-frame`; suite unwind (frames vs gdb backtrace of a core, import); suite cfi (a hand-written assembly fixture, .eh_frame and .debug_frame, with the CFA rules and operations compilers rarely emit, opened through dwarfworker.js like an import: every row vs llvm-dwarfdump, the frame summary, unwind() through each kind of rule and its stop reasons (entry code whose pt_regs cannot be read or are not pt_regs too); .eh_frame built byte by byte into a copy: a version 1 "zPLR" CIE, uleb128, sleb128, pc-relative sdata2 and udata2 FDE pointers, DW_CFA_expression and DW_CFA_restore of a CIE rule (rows vs llvm-dwarfdump, unwind through them), DW_CFA_GNU_negative_offset_extended (vs GNU readelf), CFA expressions with DW_OP_const_type and seven that give no CFA, each with its reason, constant CFAs on two stacks that make a loop, DW_OP_deref in a CFA and in a register rule, an unknown opcode, an unsupported augmentation, pointer encoding and application, 64-bit CFI and no CFI at all) | kernels; types,covtree,unwind,...,cfi |
| V3 DWARF across compile units | suite dwarf_lto (a GCC link-time-optimized fixture, a.c's helper inlined into b.c's entry: the code's unit refers to each function's and variable's DIE in its source file's unit with DW_FORM_ref_addr; opened through dwarfworker.js like an import: entry's and helper's file:line and prototype, helper's inlined call, counter's file:line, type and address, entry's static local calls (its DIE in entry's nameless concrete DIE: named by its function, at gdb's address, declared where llvm-dwarfdump says), the parameters and locals at the inlined call (names, types, optimized out) equal gdb's, llvm-dwarfdump's and llvm-nm's. With subprogramAt, declAt or typeAt of dwarf.js returning nothing it fails) | types,covtree,unwind,...,cfi,dwarf_lto |
| V3 DWARF 2 to 4 and GCC's extensions | suite dwarf_forms (one C file built by GCC as DWARF 2, 3, 4 and 5 (also with DW_LLE_GNU_view_pair location lists, which neither gdb nor llvm-dwarfdump reads: the page must say what it says for the same code without them) and by clang as DWARF 4 and 5, and a C++ file by g++ (methods and a static data member defined outside their class: DW_AT_specification; their file:line and address equal gdb's, the varargs method is one), with bit fields, an enum, a union, qualified arrays, a varargs and a noreturn function, a declared then defined variable, an inlined call with a block, a static local, long long and double values, -mregparm=3 entry values and SSE registers; each opened through dwarfworker.js like an import: the frames at every byte of its code equal llvm-symbolizer --inlines, the code of each line of m.c is that line, every CFI row equals llvm-dwarfdump's, each function's type, file:line and noreturn/varargs flags, each variable's type (a pointer to a varargs function too), address and file:line, the static local picked by its function's name, the bit field layout, enumerator values and the member at every byte of a struct equal gdb's, the parameters and locals at every byte equal gdb's info scope by kind of location, every location expression's text equals llvm-dwarfdump's; a file the line tables do not name and locals asked before the types say so; DWARF written by hand (llvm-mc: DW_FORM_indirect, .debug_loc and .debug_ranges lists with a base address selection, DW_FORM_block4 with DW_OP_plus_uconst, an enumerator value as a block, DW_LNS_fixed_advance_pc, DW_LNS_set_isa and an opcode the header declares, a DWARF 5 unit without line program, DW_FORM_ref_addr, DW_LLE_start_length and DW_LLE_default_location, DW_FORM_data16): frames, code of each line, f's type and file:line, second's address, an enum's values, every location and the locals as llvm-symbolizer, gdb and llvm-dwarfdump read them, the values of variables located by DW_OP_implicit_value and by pieces of a register and a constant equal gdb's on a core file, a 4-bit piece, DW_OP_regx and an unknown operation give no value in both, and without .debug_loc or .debug_loclists the variables have no location; broken inputs (not an ELF, an x86-64 object, no -g, DWARF version 6, 64-bit DWARF, an unknown form, a bad abbreviation code, unknown range and location list entries) give their error; the import as the page uses it (dbginfo.js, "Symbols" with offset 0xc0000000, a fixture of two units): frames at every byte of three functions equal llvm-symbolizer's, the code of a line, a line without code, an unknown file, the parameters and locals (gdb's info scope), the tooltip's inline chain and function facts, the CFI source; after a reload with one unit, no variables, no CFI read: what each lookup says; after IndexedDB lost the units; a broken unit's frames, line code and variables, a DWARF version 6 import; after a reload with the variables, the locals of m.c and the CFI read: a unit's locals not read, the CFI row back; when its debug info cannot be had: each shipped file failing (HTTP 404), index.json still on its way or without some parts, a bzImage from disk, the reader Worker's script not loading (first or second Worker), no IndexedDB; hello_main's rows by dbgFunction, none for assembly or no code) | types,covtree,unwind,...,cfi,dwarf_lto,dwarf_forms |
| D4 parameters and locals | `test_dwarf.py --locals` (vs gdb info scope; where gdb also lists, as optimized out, the variables of a block whose concrete copy clang emits without DW_AT_abstract_origin, out of scope or twice, llvm-dwarfdump decides), `--inline-blocks` (a block's variables listed exactly inside the block, at every instruction of an inlined call built here); suites unwind (values vs gdb on a core, import), hello | kernels; types,covtree,unwind; boot,... |
| D6 task names | suites tasks (Debian, every task vs task_struct read with gdb's offsets), tasks_minified (also with types.json held back until the end of the run: the same names once it arrives, and a byte of init_task in the Page listing says the variables are being read, then names its member) | tasks; boot,... |
| Panic budget while stepping | suite misc (`panic_budget_stepped`: the minified Linux stepped from the reset vector with `?panicbudget=2` stops 2 instructions after panic() is entered (the counters' panic() entry plus 2) and the banner says so; stepped back 3,000 and forward again, the re-run stops at "Hello, World!", "Still alive" and that end, at the counters' counts, with the same banner) | boot,... |
| Calls tab controls | suite tasks_minified (`graph_controls`, the minified Linux's whole run, every canvas text recorded: a click on the task switch label shows the switch's count from the entry and the task drawn on the label; a click on merged sub-pixel calls zooms to their range plus 5% and draws them as boxes; Fit shows the run; the wheel zooms about the pointer and a pinch from 100 to 300 px zooms 3x, neither in Top 30; a cancelled touch leaves no pointer; a 60 px drag upwards draws the top row's call as the first sticky label; transposed with depth 3, the "+N" marks and no deeper rows; a transposed drag moves time by the dragged share of the height and scrolls the columns; "hide interrupts" off draws the interrupt boxes and drops the selection; "chains" off draws no "a › b" label; Enter with nothing selected opens nothing; arrow keys from no selection, Up at the top row and Ctrl+Left; the info's Page tab button; two presses 15 px apart open the function and the click right after it does not reach the page; a call tree toggle opens and closes, a search matching more than 2,000 paths shows 2,001 rows and says so; a right click on a Top 30 box bookmarks its function, drawn with a ★; the JSON export's blob URL no longer loads after 10 s); suite misc (`follow_case`: at 1k/s inside one function, a drag stops following and ticking "follow" again moves the window with the run, its span kept; the call tree is rebuilt after 2 s and not before); suite graphopen (at the reset vector, 1280 px, an arrow key on the empty timeline selects nothing) | boot,...; speedasm,... |
| D7 minified and hello parity | `check-code-identity.py --self-test` (-g build's code equals the shipped kernel), `test_kernel_data.py` (every kernel has every feature's files); suite hello | kernels; site; boot,... |
| A1 memory operands named | `test_dwarf.py --operands` (absolute vs gdb info symbol, stack slots vs gdb on a core at the instruction); suite operands (minified and Debian pauses: names with an address vs gdb on a core of the pause, Debugger and Page tab alike, tooltips, 390 px) | kernels; tasks,operands |
| A2 line to address | `test_dwarf.py --lines` (listed ranges and inlined calls vs llvm-symbolizer, breakpoint addresses vs llvm-dwarfdump is_stmt rows; Debian 2,000, minified 3,000, hello 200 sampled addresses), `test_kernel_data.py` (shards in MKBD 2, breakpoint count); suite lineaddr (minified and Debian: Source panel list, its Run to and a typed main.c:LINE stop where llvm-symbolizer names the line, incl. a line whose code is all inlined; list jumps to the Page tab; the listing's C line lists the same; current.h's get_current() line (78 inlined copies) lists 40 and counts the rest, every copy that line for llvm-symbolizer; import of the minified -g vmlinux gives the same breakpoints and Run to works; 390 px light and dark) | kernels; site; types,covtree,unwind,lineaddr |
| A3 function facts | `test_dwarf.py --funcs` (prototypes, declarations, copy counts vs llvm-dwarfdump --name; ptype, file:line, external vs gdb; frames vs llvm-dwarfdump --debug-frame; inlined copies vs llvm-symbolizer; all copy counts vs a full llvm-dwarfdump for the kernels built here; Debian 1,000, minified 2,000, hello 200 functions), `test_kernel_data.py` (every funcs shard, function count); suite fnfacts (minified and Debian: start_kernel's row vs gdb ptype, symbol, global symbol, info line and llvm-dwarfdump's frame; an inlined copy in it: every function of the chain as llvm-symbolizer names it, the inlined one's prototype and copy count vs llvm-dwarfdump, its copy listed; defined-at opens the Source panel; call stack tooltip, Page label line, Source tab detail and Calls detail say the same; import of the minified -g vmlinux gives identical facts; 390 px light and dark) | kernels; site; types,covtree,unwind,lineaddr,fnfacts |
| A5 variables defined at | `test_dwarf.py --vars` (defined-at vs gdb's symbol at the address, static locals gdb cannot find by name vs llvm-dwarfdump --name; `&'file'::name` vs the page's pick by path and shortest suffix; unknown and ambiguous files refused; Debian 2,000, minified 134, hello: nothing to check), `test_kernel_data.py` (types.json format 2, every file index valid, every file a variable is defined in shipped in src/ or said to be missing); suite types (watch "defined at" vs gdb and the Source panel line, other definitions listed, file::name vs gdb, refusals, hover text, operand tooltips, Worker's import JSON-identical incl. files; boxed EIP line's underscores drawn, light and dark; 390 px) | kernels; site; types,covtree,unwind,lineaddr,fnfacts,branches |
| A4 branch targets named | suite branches (Debian start_kernel and setup_arch pages, minified with live decode on, imported minified -g vmlinux: every direct branch to a symbol's code named, equal to gdb info symbol; live names equal disasm.json's llvm-objdump names line for line; Debugger and Page tab alike; callee tooltip prototype vs gdb ptype; click opens the target; BIOS page unnamed; 390 px light and dark) | types,covtree,unwind,lineaddr,fnfacts,branches |
| D8 help and first look | `check-ids.py` (no duplicate ids); suite firstlook (every kernel incl. Debian with its initramfs and an imported vmlinux: explanations present, entry line, JIT gap, Source tab notes, no sideways scroll at 390 px light and dark) | site; boot,...,firstlook |
| Run to on a phone | suite runto_phone (hello at 390 px touch, 1/s, 10/s and desktop: Boot, Pause, Run to a function stops at its first instruction; `hello`, `.rodata` and mid-instruction targets refused in the status and the paused overlay with the CPU still paused; a Run to right after the slice that printed "Hello, World!" stops there, stepped and with the JIT; light and dark screenshots); `--base https://d33.pl/minimal-kernel-build` for the live site | tasks,operands,runto_phone,srclinks |
| C1 file:line links | suite srclinks (minified Linux paused in code inlined into setup_arch: each kind of link in the Debugger, Page listing, hovered byte, Calls detail and Source tab facts opens its file with its line picked and in view; no plain file:line text anywhere on the page after a Run to and on the Page, Calls and Source tabs (the overlay's copy of the status opens too); after a Run to the state line names the function at EIP with the source row's line; Enter opens one and moves the focus, Escape gives it back; a tap on a 390 px phone, and on the state line's link at a stop in a function with a long name (the link inside the viewport and the state line's box); light and dark at 1280 and 390 px) | tasks,operands,runto_phone,srclinks |
| Screen after a pause | suite debugger, check "screen after a pause" (requestAnimationFrame starved: hello stepped past Hello, World! while paused and minified Linux after its end stop at full speed still show the text on the screen; CI once caught the Debian panic stop without its last line) | boot,symbols,debugger,... |
| B2 jump sources inline | suite jumps (hello and minified Linux, after the walk, at 1280 and 390 px with no click: a taken jcc's target, a loop head, the function with the most callers in other pages (minified Linux: panic, 32 paths in), a return site and its ret, a timer interrupt's entry (minified Linux) and Code at EIP at the Run to's function entry: the line above reads `← n paths in` with n from jumps.js, whose static sources equal the decoded bytes' (test_jumps.decode); the sources shown are jumps.js's first ones, each with llvm-objdump's symbol+offset, the source's mnemonic (static) or kind (observed) and its count, marked in view, up, down or another function, with a gutter stub for each way out of view, and `+k more` for the rest; the ret names `retl to` its observed return site; a call to a function out of view has its stub; `+k more` lists all paths and a named source opens the Page tab there; no horizontal scroll; screenshots light and dark) | tasks,operands,runto_phone,srclinks,unoptimized,jumps |
| J1 jump paths | `test_jumps.py` (every instruction of hello, hello-os and both minified Linux builds: kind, target and fall-through from jumps.js over disasm.json equal a decode of the ELF's bytes from a fresh llvm-objdump run, the decoder agrees with objdump's mnemonics, and every target's static sources are the same); suite jumps (hello: 300 single steps from hello_main; minified Linux: from rest_init to the end of the boot, about 95,000 single steps with timer interrupts, iret and an exception: the recorded paths equal the non-fall-through pairs of the stepper's own EIP sequence, count for count, with a fitting kind; a taken jcc's target shows `←n·k×` and its list (source, count, file:line), go there opens the Page tab at the source, Enter and Escape, a second click on the badge and the list's Close button close it, the paths out of a return list the observed targets or (none observed, minified Linux) say none was seen, the taken arc drawn, underscores of the list's names drawn; EIP's paths at a function entry name the static call; the JIT note; 390 px light and dark) | kernels; tasks,operands,runto_phone,srclinks,unoptimized,jumps |
| J2 live-decode scope | suite jumps, case jumps_live (Debian 6.1 686 and an imported vmlinux on the minified Linux, Run to start_kernel: the ⊘ titles of Code at EIP and the Page listing, the Page note, the list notes and the Debugger's paths at EIP say "no direct path" only with the scope of the instructions decoded live, never "nothing jumps or calls here"; Debian's first text page that no sized code symbol covers (.altinstr_aux) is decoded from its start and says its first lines may be misaligned; hello with disasm.json keeps the whole-disassembly wording) | tasks,operands,runto_phone,srclinks,unoptimized,jumps |
| Z1 ⊘ only where no path leads | `test_jumps.py` (every instruction of the four built kernels: the page's ⊘ set, jumpPaths over disasm.json, equals one computed from the ELF alone: no direct jump or call target, an instruction ends before it and does not fall through, not an aligned data word, not a number in llvm-objdump's operand text, not an `__ex_table` fixup, not a stub of Linux's early_idt_handler_array, irq_entries_start or spurious_entries_start; prints what the marks left are: -O0 minified Linux 1,637, all padding; -Os 440: 333 padding, 107 functions nothing calls); suite nopath (hello, hello-os and both minified Linux builds: paused at the start the EIP line has no ⊘; Run to start_kernel or hello_main, then up to 2,000,000 single steps: no instruction that ran or has an observed path in carries ⊘, Code at EIP at the first stop on an instruction reached only indirectly (minified -O0: the jump table of clear_all_debug_regs, -Os: cpu_init+0xc2) has no ⊘, nor the Page listings of those, and the ⊘ title says what it rests on; screenshots 1280 and 390 px, light and dark); suite jumps, case jumps_live (no ⊘ on the first instruction decoded live nor on function entries). On 7f4ebd019 all of these fail: 87 (-O0) and 55 (-Os) wrong marks, startup_32 and EIP marked | kernels; nopath; jumps |
| L1 legend and yellow lines | `check-legend.py` (every class style.css colours has a sample in the About tab's legend or a page-furniture reason, every colour token is used); suite legend, case legend (minified Linux: entry, Scan, Run to start_kernel, 300 steps, Page listing, Source panel, Source tab, Memory: every coloured class drawn is in the legend, 17 samples compared with the drawn element's computed colour, background, outline, italics, opacity, stroke and dash in light and dark, the memory and Calls swatches equal the page's tables, the Legend links of Code at EIP and the Page tab show it and Back returns; screenshots 1280 and 390 px, light and dark, no horizontal scroll); case yellow (both minified builds, 64 MiB: at the entry every yellow line of Code at EIP and the Page listing names the relocation stub and the 0x79 shift, checked against the shift found in the bzImage file by the test and the build's bytes read from guest memory there; hover, Enter and a 390 px tap show it without moving lines; nothing is yellow after start_kernel; during a fast Run to its HLT breakpoint line is yellow and says so, after it ends it is plain; in slow motion over the relocation stub the Page listing marks the bytes it moved since the last memory scan, and those lines say so). On 6525404db they fail: no legend, no explanation on any yellow line | debian,dwarf,dwarf_import,runto,initramfs,coverage,navhist,tipmarks,legend |
| T1 popovers on the listing marks | suite tipmarks (hello and minified Linux, Run to hello_main / start_kernel and 30 steps: no ⊘, ←n, ↑ ↓ ↗, name, file:line, C line, EIP-inside line or tint key in the Page listing or Code at EIP relies on a title; at 1280 px hover on ⊘ shows the popover, its first line says what ⊘ means, then the whole disassembly and what ran; it fits on screen and moves no line; Enter, Space and Esc on a focused ⊘; a click pins it and it stays on the mark when the listing is drawn again; tint key, ←n (a click still opens its list), EIP mark, Tab focus and hover on a jump end, hover on a gutter stub; at 390 px touch a tap on ⊘, a long press on a jump end shows its popover without going there, a plain tap still goes there, taps on the EIP mark and a stub; screenshots light and dark); case tipmarks_live (Debian at 390 px, Run to start_kernel, a tap on a live-decode ⊘ says only the instructions decoded live are known). On b9cc4387d it fails: no popover on any mark | debian,dwarf,dwarf_import,runto,initramfs,coverage,navhist,tipmarks |
| Z2 stub arrays in a live decode | suite jumps, case jumps_live (Debian 6.1 686 and an imported vmlinux on the minified Linux, Run to start_kernel, then the Page listing at early_idt_handler_array, irq_entries_start and spurious_entries_start when the symbol table has them: no ⊘ on any instruction there that is not int3 or nop padding; for each stub after a jmp Paths at EIP names the array, "array + k × stride" and the function that fills the IDT). On ede4e171f it fails: ⊘ on the stubs of early_idt_handler_array (Debian 0x1d86011, 0x1d8601a, ...; imported 11 stubs) and no text on how the stubs are reached | tasks,operands,runto_phone,srclinks,unoptimized,jumps |
| J3 no em-dashes | `check-text.py` (no U+2014 in the page's .js, .html, .css and README); suite srclinks (the Source panel's title after every file:line click has none) | site; tasks,operands,runto_phone,srclinks,unoptimized,jumps |
| B1 run state under random clicks | suite runstate (minified Linux at 1/s: Boot pressed twice while the boot fast-forwards to the entry, on a desktop in one task and as two taps at 390 px, must end with the new machine running at 1/s and no page error; Run to start_kernel pressed twice at once during that fast-forward must stop at start_kernel; then a seeded random-click fuzzer with fixed seeds, desktop and 390 px touch, hello and minified Linux, speeds 1/s to full, clock, kernel, Boot, Pause, Continue, Step, Run to targets, tabs, the overlay, listing lines, jump lists, file:line links, the Source panel, Memory pages, Snapshots and keys: after every action, paused means Pause disabled, Continue enabled unless the run ended and the overlay shown; running means the opposite and, at 1/s and 10/s, an instruction within 3.5 s; a fast-forward moves within 10 s; "off" lasts under 30 s; no page error. `--fuzz-seeds N --fuzz-from S --fuzz-actions A` runs more seeds locally) | runstate |
| B1 speed line at slow speeds | suite speedline (minified Linux at 1/s on a desktop, at 1/s and 10/s at 390 px touch with the CPU 5x slower: the speed line sampled every 100 ms for about 60 s through Pause and Continue, a speed change, the tab hidden 6 s with the run going on, frozen 8 s and shown again; while running it shows the set rate within a factor of two or "measuring", never 0/s, and the count moves at a third of the set rate or more) | speedline |
| B3 slow motion with an asm listing open | suite speedasm (390 px touch with CDP CPU throttling, the tab opened after the kernel entry and watched for 25 s: the minified Linux's Page tab on the kernel-text page holding EIP, about 1,400 instructions, at 4x and 6x, 1/s and 10/s; its Debugger tab (Code at EIP) at 6x, 1/s and 10/s; hello's Page tab at 4x and 1/s. The speed line never shows 0/s and, once the 4 s speed window has passed since the tab was opened (its first drawing holds the main thread up to 2 s at 6x), the set rate within a factor of two; the count moves at a third of the set rate or more; the listing or Code at EIP is drawn at most once per executed instruction plus a full refresh every few seconds; long tasks take under 70 % of the wall time. On 900b6cc2e the Page tab at 4x made 5 instructions in 25 s at 1/s with "speed 0/s") and suite runstate (its phone runs under 4x CPU throttling, with the Page and Debugger tabs watched 3 to 8 s among the actions, and "speed 0/s" while running is a broken invariant: its seeds 2 and 5 found the JIT left running at 0/s after the kernel's end) | speedasm, runstate |
| R1 the asm views follow EIP | suites followeip and followfuzz. FOLLOW_EIP_JS watches the page: every drawing of Code at EIP and of the Page listing must box the line holding EIP, and once an instruction has executed the view on screen must show the new EIP within max(1 s, twice its last drawing plus 400 ms), while stepping at any speed and paused (not under the JIT or in a hidden tab). Fixed sequences on the minified Linux (-O0 and -Os) and hello, Debugger and Page tabs, CPU 1x and 4x: 1/s from the entry, 10/s, 1k/s, Pause, Step, Step 100, Continue, a Run to that steps to the end of the run, then Boot at full speed (counted, then the JIT) to the end. Then a seeded fuzzer of tab switches, speeds, Pause, Step, Run to, the Source panel, Back and Forward, links, kernel switch, Boot during the fast-forward, a hidden tab and CPU throttling. On d3a2553f0 5 of the 12 sequences fail: Code at EIP kept the box where a stepped Run to or the counted boot had started (0x100000) for over a second while the count went on (minified Linux, CPU 4x), and the Page listing marked nothing while the relocation stub ran inside startup_32's instructions (both minified builds, 1x and 4x) | followeip, followfuzz |
| U3 tab names, emoji, compact tabs, Machine tab | suite tabs (hello, light and dark at 1280 and 390 px: the eight tabs with an aria-hidden emoji each, distinct, and the word as accessible name; no tab or About text calls a tab Graph or Coverage; #graph, #coverage, ?tab=graph and ?tab=coverage open Calls and Source with the hash rewritten; the kernel, RAM, file, symbol, initrd, cmdline and offset controls, Boot, the header and build lines and the setting are in the Machine tab, the screen, run bar, counters, status and state line in the top bar; picking hello-os and Boot with 4 MB from the Machine tab boot it; a bzImage dropped on the About tab boots; compact tabs hide the words, keep aria-label and tooltip, the hover shows the name, a click opens the tab, the choice survives a reload both ways, off by default at 1280 px, on by default at 390 px with the bar in one row; the top bar at 390 px is shorter than the 1,003 px it had before; no horizontal scroll). On 8dc5c89c7 it fails: no emoji, Graph and Coverage, no Machine tab, no compact setting, top bar 1,003 px | debian,...,legend,tabs |
| U4 Settings tab, literal emoji | suite tabs (the nine tabs in order with Settings before About and the emoji 🖥️ 🗺️ 🔢 🐞 📊 📝 💾 ⚙️ ℹ️, each drawn as a colour emoji in two font stacks; #settings and ?tab=settings open Settings; Compact tabs, speed, clock, count, the per-kernel list and Reset are in the Settings tab and the Machine tab has no Settings section or checkbox; with a watch list, a cmdline set by Boot, another bzImage's symbols, an unknown mkb- key and an IndexedDB DWARF entry stored, every mkb- key in localStorage and the IndexedDB entry have a row with an explanation; speed, clock and count set in Settings change the top bar's controls and survive a reload, the top bar's speed shows in Settings; entries whose value is not JSON say so with "?" and "none"; Forget removes each entry with its effect (the watch list empties, the cmdline goes back to the default, the other bzImage's DWARF goes, the running kernel's imported System.map gives way to its own symbols); Reset all settings, also with a key written while it waits for IndexedDB, leaves no mkb- key and no IndexedDB entry and restores compact off, 1k/s, 10/us, count on; the compact bar is one row at 390 px light and dark; no horizontal scroll on Settings; in a browser that keeps no data (localStorage throws, no IndexedDB) Settings says nothing is remembered and its speed, clock, count and compact tabs still apply to the page). On feddeeea8 it fails: no Settings tab, the old emoji, Compact tabs in the Machine tab | debian,...,legend,tabs |
| U5 everything above the tabs in Machine | suite tabs (at 1280 and 390 px, light and dark: no element drawn in the header besides the title, the tab bar and the run status, the header at most 140 px; the first load without a hash opens Machine with the screen showing "Hello, World!", #memory still opens Memory; the status line, warnings, screen, overlay, run bar, state line, counters and notes are in the Machine tab; hello at 10/s: on the Page tab the run status counts on and says 10/s like the VM, the screen is hidden but stays in the page and "Hello, World!" reaches it meanwhile, Pause and Continue in the run status pause and resume the VM, a click on it opens Machine with the new text shown; the minified Linux at 1k/s on the Debugger tab counts on, at least 500 in 2 s) and suite initramfs (after the Page tab and back, keys typed on the screen reach the Debian (initramfs) shell). On d78451a41 it fails: the screen and run bar in the header (373 px at 1280, 660 at 390), Memory first, no run status | debian,...,legend,tabs |
| H1 the menu toggle | suite runbadge (hello at 1280 and 390 px, light and dark: paused, 1/s from the menu's Continue on the Debugger tab with the count rising, paused again from the menu, halted at full speed; the face says paused / 1/s / halted, the dot pulses only while running, its colour differs running and paused, the accessible name says the state; the toggle is fixed in the bottom right corner, 40 to 64 px wide, no horizontal scroll, the header has no run status any more; the menu opens on a click or tap with Back and Forward disabled before any jump (tooltip "no earlier view"), the state in words with the count and Continue or Pause, stays on screen, and closes on Esc (focus back on the toggle) or a tap outside; Enter on the focused toggle opens it; the state in words opens Machine; a new deploy (pwaNewer) puts a mark on the toggle and the notice with Reload in the menu on the About tab; the toggle covers no listing line of the Page tab in light and dark; reduced motion: running at 10/s and paused without animation, still told apart; the minified Linux at 1k/s and then at full speed into its own panic, and Debian at full speed (the JIT) and its panic text, at 1280 light and 390 dark; the About legend has a sample of the six states); suite navhist (Back and Forward through the menu, Alt+Left/Right with the menu closed) and suite tabs (the run status is the toggle). On 941db9ec1 it fails: no menu toggle | speedasm,runbadge; debian,...,navhist,...,tabs |
| M1 EIP in the Memory tab | suite memeip (hello at 1280 and 390 px, light and dark; the minified Linux -O0 at 1280 light and 390 dark, -Os at 1280 dark and 390 light: at the reset vector, the kernel entry (Run to 0x100000), stepping at 1/s and 1k/s on the Memory tab with every drawing of the mark checked in the same task against the CPU, after Pause and Step, after Run to start_kernel, after the end at full speed (the minified Linux's panic, hello's halt): exactly one square has the EIP mark and it is the page of the CPU's EIP read independently (CS base + EIP, then the page tables from guest RAM, PSE and PAE), the ESP mark on the stack's page, the line names EIP, phys, the symbol (or the relocation stub) and the page, one All RAM segment framed with a tick, the First MiB bar marked when EIP is below 1 MiB, the mark's colour is no region or section colour, no horizontal scroll; the EIP label's popover on hover and tap; the line opens the Page tab with EIP boxed and Back returns, its stack link the page of ESP (read from the CPU) and Back returns; the menu toggle covers neither the mark nor its label; a kernel switch and Boot: the mark follows the new machine; the About legend has the samples; case memory controls, hello at 1280 px with 2 MiB + 64 KiB of RAM: Enter and Space on a legend entry highlight its pages and say how many, a click on a bar segment does too, a "?" clicked twice opens and closes its popover, the hover line names a page and is empty over an empty cell of the last row, the Page tab's previous and next buttons, Step after hello's halt says there is nothing to step, a #page~src= link that is not valid percent-encoding opens the Page tab without the Source panel); suite legend (the samples look like the drawn marks, light and dark); suite speedasm (the Memory tab at 10/s, 6x CPU throttling, 390 px: no stall, the mark follows). On 309a260ef it fails: no square has the EIP mark | speedasm,runbadge,memeip; debian,...,legend,... |
| G1 double click opens a function | suite graphopen (hello at 1280 light and 390 dark, the minified Linux at 1280 dark and 390 light: Run to hello_main / start_kernel and 40 / 3,000 steps, then in the Calls tab a single click or tap on the function's box selects it and stays there, its hover text and info mention the double click; a double click (icicle and Flame), a double tap at 390 px, Enter on the chart and a double click in Top 30 open the Page tab at the function's first instruction with its header line in view and the Source panel at it in its file (boot/hello.c, init/main.c), one history entry; Back (the menu's button, Alt+Left, the browser's back) returns to the Calls tab with the same window, scroll, selection, focus and orientation and the panel closed; ArrowDown and ArrowUp move to a callee and back; a double click or tap on the empty area opens nothing; no horizontal scroll; case graphopen loading, hello with its DWARF line shard held back: the double click opens the Page tab at once and the Source panel when the shard arrives with the function's first instruction still in view in the listing (it used to scroll to EIP's line when the shard came after the jump's 1 s scroll hold), and not at all when the user has gone to another tab by then). On b6e78dd9a it fails: the double click re-roots the chart and the Page tab does not open | speedasm,runbadge,memeip,graphopen,srcfiles |
| F1 another file in the Source panel | suite srcfiles (hello at 1280 light and 390 dark, the minified Linux at 1280 dark and 390 light: Run to hello_main / start_kernel and 40 / 3,000 steps, the panel opened from a C line of Code at EIP (at 1280 a click on its C text, not its link), the title's file name opens the whole file and Back returns; the switcher lists the call stack's files with EIP's first, the recent ones with that file, and exactly the files of files.json and the shipped source; filtering "hello.c" / "main.c" lists the file, picking it opens the whole file with one history entry and the hash keeping it; its ● lines equal the lines llvm-symbolizer --inlines gives for every instruction start the stepper recorded, and the switcher's count says the same; a drag selecting a line's text does not pick it (1280); every files.json function of the file is in the outline and a jump puts the line llvm-symbolizer gives for its first instruction in view, its first option goes nowhere; the directory breadcrumb lists exactly that directory's files and subdirectories, its Up row the top directory's and a directory row that one (1280); a header inlined into init/main.c opens; Back returns to the function view with its scroll, Forward to the file; keyboard only (Enter on 📂 Files, typing, arrows, Enter; a filter matching nothing says so and the arrows and Enter do nothing); a reload reopens the file; the switcher fits the panel at 390 px, no horizontal scroll; screenshots light and dark). On 21801480a it fails: the panel has no file switcher | speedasm,runbadge,memeip,graphopen,srcfiles |
| K1 bookmarks | suite bookmarks (hello at 1280 light and 390 dark, the minified Linux at 1280 dark and 390 light: Run to hello_main / start_kernel and 40 / 3,000 steps; bookmarks added from Code at EIP (☆), a call stack frame (its ☆), a Page listing line (right click at 1280, long press at 390, and the B key over a line at 1280), the function's label line in the listing (right click at 1280, long press at 390: a bookmark of the function; FAILED at 390 on 6d112bde5, the touch reached the whole listing), a Memory grid square (right click or long press), a picked Source panel line (☆), the Source tab's file row (☆), the Calls box of the function (selected + ☆ at 1280, long press at 390) and the machine's position (the menu's ☆): each dialog's default name, the stored target equal to EIP, the line, the square, the frame's function's symbols.json address, the file:line; Escape cancels; the ★ with a popover naming it on the line, square, frame, file row and Calls box; a rename in the list; after a reload every bookmark is listed with its name as this kernel's and each jump lands (the address's line on screen in the Page tab, the page, the Source panel line in view, the whole file, the function's first instruction) as one history entry that Back undoes; the filter and Tab, Enter; a delete; the export's JSON imported into a fresh context restores the list; the bookmark's link opens the function in a fresh context; on hello-os the hello bookmarks are listed for another kernel and the switch loads hello; menu and dialog inside the viewport, no horizontal scroll; screenshots light and dark; and on hello paused at 1280: B in the About, Page and Memory tabs, an import of a file that is no JSON, of JSON without bookmarks and of a list with one new, one present and one unreadable entry, delete and Undo, another page of the site following a change (storage event), a link that cannot be read, a link from another build of hello, the switch for a kernel not listed, storing failing (quota) and a browser keeping no data; case unreachable, hello to its end at 1280 light: the Memory tab's ☆ offers EIP's page, a position bookmark, a snapshot and the run's file; the bookmark's 🔗 copies its link (the clipboard holds it) or, refused, says to copy it from the context menu, and its link opened again finds it listed; Shift + right click on a page square opens no dialog; Import opens its file dialog and refuses a file that is no JSON; then hello-os paused (the Page tab's ☆ offers a line of EIP's page, the Calls tab's with nothing picked has nothing to add) with hello's bzImage at 404: the snapshot's Switch and Replay, Replay a file, the bookmark's jump and its Replay each say "kernels/hello/bzImage: HTTP 404" and hello-os keeps running; the Switch and the jump say so where they were started and the jump is dropped, FAILS on 0e4bd70a3: "Switching to hello..." stayed). On b9eeb8822 it fails: there is no ☆ button | snapdiff,bookmarks |
| R1 delta snapshots | suite deltasnap (hello at 390 dark and the minified Linux at 1280 light: a full snapshot at the kernel entry, then 10 saves a few steps apart, each a delta of the one before (base and depth on the row) storing exactly the pages v86's RAM changed in, smaller than the full one, the row saying "delta of #N: K pages changed" and the storage line "10 deltas"; after a reload snapshots 10, 4, 0, 7 and 1 restore to v86's RAM SHA-256, registers and instruction count at their saves; the diff of 3 and its delta 4 compares only the delta's pages and lists the pages v86's RAM changed, as does 0 to 10 across the chain; the download of delta 10 is a full v86 state (Python LZ4 decoder and `lz4 -d`) holding v86's RAM; deleting 5 makes 6 a delta of 4 with the union of both pages, deleting 0 makes 1 full, and after another reload 6, 10, 1 and 3 still restore exactly; a save right after restoring 3 is a delta of 3 with 0 pages; no horizontal scroll. A 64 MiB hello machine with 24 MiB of data: 36 pages changed and one zeroed give a 37-page delta under 1% of the full snapshot, no main-thread task over 200 ms while saving it, and Restore of the delta gives the RAM back. FAILS on 311c0d540: every save is full) | snapdiff,bookmarks,deltasnap |
| C1 snapshots compressed with LZ4 | suite snapdiff (every save of the S1 runs: the row says LZ4 level 1 with its time; hello's download is `*.v86state.lz4`, decoded by an independent Python LZ4 frame decoder that enforces the block end rules and by `lz4 -d` (installed in CI), and its RAM is v86's. A gzip record stored the S1 way still lists as "gzip level 6", restores to its EIP, registers and instruction count, diffs empty against an LZ4 snapshot of the same state, and downloads as `.v86state.gz`. A 64 MiB machine with 24 MiB of filled RAM at 1280 and 390 px: LZ4 level 1 row, no main-thread task over 200 ms while saving, smaller than the state, Restore gives the RAM back. FAILS on a29205d9b: no LZ4 row, no snapUnpack) | snapdiff |
| S1 named snapshots, kept across reloads, diff | suite snapdiff (hello at 1280 light and 390 dark, the minified Linux at 1280 dark and 390 light: Run to the kernel entry, save A, step 60 / 3,000, save B, guest RAM and registers read from v86 at both points; default names name the kernel and the count; rename B with Enter, a rename of A cancelled with Esc; hello: the download (gzip until C1, an LZ4 frame since) parsed in Python (v86 state layout) holds v86's RAM at B; reload: both listed with names, sizes, "stored" and the matching kernel; diff A to B lists exactly the changed pages and bytes per page, the changed registers with v86's values, the changed screen lines and both instruction counts; the hexdump marks exactly the changed bytes; "Page tab" shows B's bytes with a note, Back returns; Restore B after the reload gives B's EIP, registers, instruction count and screen, with last_result, the FPU status word and MXCSR back in wasm memory; B to now is empty right after it; Delete survives a reload; no horizontal scroll. Without IndexedDB the snapshot stays in memory with a note; with a quota below the site's usage the save says QuotaExceededError and keeps it in memory, not listed after a reload; Delete all asks twice and empties the store. A hello snapshot under hello-os says "for another kernel" with no Restore and a switch that brings Restore back, at 1280 and 390 px. 2 and 64 MiB of RAM at a 4x CPU throttle: no main-thread task over 200 ms while diffing two snapshots and one and now. FAILS on 7c1a58dbd. Case edges (hello at 1280): a store at a newer database version keeps the snapshot in memory and names the VersionError; in memory only, 9 saves keep the last 8; without the LZ4 module (404, no service worker) a save is kept raw ("not compressed"), restores and downloads as v86's plain state; Delete all's second press expires after 5 s; a rename shows at once in the diff and in the Page tab's note of a snapshot's page (FAILS on c28854dba: both kept the old name); Show live memory puts back v86's bytes; stored records of a kernel not in the list (its switch says to load it from disk), of hello at 3 MiB (the switch adds 3,072 KiB to the RAM menu and boots it) and of a delta whose base is gone (Download and Restore say so); Restore at another RAM size names the size to boot with. Case corrupt (hello at 1280): records written into IndexedDB beside a saved full snapshot and a delta of it, read after a reload: Restore of each names what is wrong (no LZ4 frame, FLG 48, a header checksum one off, a block whose match points before the output, a content size its block does not decode to, a frame of 100 stored bytes for 200, LZ4 bytes and a raw record that are not a v86 state, a v86 state of version 5, a delta of a "full" record that is a delta, a delta of another base) and leaves v86's RAM, registers and count as they were; the diff of that "full" delta fails with its reason and Delete of a base whose delta cannot be rebuilt keeps it and says why; Enter on an unchanged name is no rename; a 2 MiB snapshot diffed with hello at its end on 4 MiB lists exactly the pages above 2 MiB that hold bytes (counted from v86's RAM) and the hexdump of one shows zeros against v86's bytes, all 256 rows on "all rows", and the Page tab shows that page as the snapshot had it (zeros, its name, renamed at once); after the site's IndexedDB is cleared under the open page, Delete, Rename and Save say the database connection is closing and keep the snapshots in memory; with snapworker.js at 404 a save is kept raw and Restore says the snapshot worker failed) | snapdiff |
| D1 a restored snapshot replays | suite snapdiff, case replay (hello at 390 dark from 600,000 instructions, the minified Linux at 1280 light from 1,500,000, virtual clock 10/us: save, step 600,000 / 500,000, note EIP, registers, EFLAGS, the SHA-256 of RAM, the clock and the IRQ0 count; step 777 more, Restore: the count and the clock are the snapshot's; step as many again: all equal the first time). FAILS on e1730dcaa: count and clock run on from the Restore, the IRQ0 count after it differs (hello 3 vs 1, minified 2 vs 1). Suite debian: after the fast-forward to startup_32 the guest TSC equals the virtual clock (1 ms = 10^6 ticks; was 1,420,143,700 behind on e1730dcaa) | snapdiff,bookmarks; debian,dwarf,dwarf_import,runto,initramfs,coverage,navhist,tipmarks,legend,tabs,pwa |
| J2 the guest's date and random numbers on the virtual clock | suite guestin, no init script freezing Date or crypto (hello twice, 1280 light and 390 dark: the same RAM, the BIOS tick count at 0x46C that of 2026-01-01 00:00:00; `?rtc=2031-02-03T04:05:06Z&seed=7`: tick count 267,744 + a few, the Machine tab shows both; real time: the RTC reads the browser's date; a snapshot saved with `?rtc=`/`?seed=5` and restored after a reload with others puts back the start date, seed and generator state; Debian stepped 30,000,000 instructions after its entry twice: the same EIP, clock and RAM; Debian with its initramfs at full speed: `hwclock -u` and `date -u` at the prompt print the start date plus the virtual time, and two hwclock readings move with the virtual clock; case edges, hello at 1280 dark with the browser in Asia/Kolkata: an RTC alarm set through register B is at that time on the `?rtc=` day, the periodic and update interrupts start at the guest's date, VMware's GETTIME gives the guest's date and a zone offset of 0, the page's Date is the browser's again after; `?rtc=yesterday&seed=lots` are named on the Machine tab and the defaults used; a snapshot saved on real time restores there with the clock back at the snapshot's, and after a reload on the virtual clock the guest's date goes on from the RTC's at the save, the run says a snapshot without an input log was restored and the note over the screen names the Restore, FAILS on 8c05ba7fe: it said "Paused at the reset vector."). FAILS on 01b4e4d15 (tick counts and RAM differ, Debian RAM differs, the shell prints the browser's date); suite jitdet now runs without its init scripts and FAILS on 01b4e4d15 (minified and Debian RAM differ) | jitdet,guestin |
| J3 exact end stops | suite exactstop (Debian and Debian with its initramfs, each 3 times at 1280 light, 390 dark and 1280 dark, counted stepping `speed=full&count=1` and J1's full JIT speed `count=0`, virtual clock 10/us: the stop is the panic text (marked exact) or the prompt with the CPU halted, and v86's count, steps, EIP, RAM SHA-256, IRQ0, virtual time, the end or prompt row and under the JIT the slice-chain hash are the same in all 3; the count line's rate after the fast-forward, hello 1280 light and Debian 390 dark, is at most 100 M/s); suite jitdet runs Debian on to its exact panic-text stop. FAILS on df0fc50ed (stepped and JIT: the counts, EIP and RAM at both stops differ in every run, the prompt stop is not at a halt; the count line says 251.1 M/s for hello and 239263.1 M/s for Debian) | exactstop |
| J4 record and replay guest input | suite replay. Minified Linux stepped at 1k/s (390 light): keys typed at moments of wall time are logged, a snapshot and then a position bookmark keep the run, the snapshot's Replay and the bookmark's Replay boot again and end at the same count, v86 counter, EIP, RAM SHA-256, virtual time and screen. Debian with its initramfs at J1's full speed (1280 light): keys typed while paused at the prompt are held (14 scancodes for 7 keys), a Step there, a command typed while it runs; the log has the modes JIT, stepped, JIT; the downloaded run and the snapshot's run match the page; Replay in the page and the downloaded file replayed in a fresh 390 dark page started on hello end in the same state, with the command's output on the screen and the page's verdict "match". FAILS on 53f6f94f3 (nothing is held or logged, no Replay). Case refused (minified Linux at 1280 light, run to its end at 1M/s): a file that is not JSON, a run of another format, of format version 0, of a kernel not in the list and on a clock the page does not offer each say why and leave the URL as it was (FAILS on 51e78fcd5: the refused clock left the run's settings in the URL, so a reload booted with them); the run's replay, paused while it steps (20x CPU throttle) and then stepped, says "Replay stopped by Step at instruction N" and taints the run there. Case tampered (hello at 1280 light, its run downloaded at its end): edited runs without an end, on the real-time clock and with an unreadable log entry are refused with the reason; one for another build of hello, with another RAM hash at its end and its last log entry a microsecond later replays and says "Replay differs from the recording (kernel, RAM; 1 input not at its recorded moment)"; one recorded on 3 MiB adds 3,072 KiB to the RAM menu, boots with it and differs in RAM. Case during (hello at 1280 dark, its run stepped from the reset vector, replayed at a 20x CPU throttle): 4 scancodes typed while the replay is paused are not given to the guest and the continued replay matches and says it ignored them; Step back while the next replay is paused stops it ("Replay stopped by Step back at instruction N") and steps back one; Back to a name that is no symbol says what Run to says, with Back to throughout (FAILS on 4e3e1ef8f: "Run to takes ..." in the Back to note) | jitdet,guestin,replay |
| R2 reverse execution | suite reverse. hello (390 dark, 1280 light) and the minified Linux (1280 light, 390 dark) stepped from the reset vector with a checkpoint every 20,000 instructions, Debian stepped from `startup_32` (JIT before it) with the page's own interval: forward to E, noting the machine at E-deep, E-1000, E-1 and E and every call in the last 20,000 instructions; Step back 1 (run bar), Step back 999 and Step back to E-deep (Debugger) each give the count, EIP, registers, segment registers, EFLAGS, CR0-CR4, v86's counter, clock, IRQ0 count, halt state, RAM SHA-256 and VGA text of the forward run there, 1 and 1,000 within 1 s; the recordings (coverage, paths, symbols, calls, task switches, milestones) unchanged; forward to E again: the same machine and recordings; back 100 and on to E + 30 (the re-run catches up at E and records on), then back 30 is the machine at E; Back to the function called most, twice: its last two calls with their registers; no jump history entries; a key typed after stepping back drops what was ahead; the JIT drops the history (Step back disabled, the line says why). Debian with its initramfs at the prompt: "echo r2" typed and stepped until r2 is on the screen, Step back to before the keys (the keys waiting, r2 not on the screen), forward again: the same machine and input log. FAILS on 677f8a0fc (no Step back); after stepping back and forward again on hello the RAM differed by one SeaBIOS byte until the TSC fix | reverse |
| R3 every restore has the TSC as saved; a replay keeps the prompt stop | suite snapdiff, case replay tsc (hello at 1280 light saved at 1,300,000, 100,000 steps across SeaBIOS's TSC calibration, Restore, the same steps: the same RAM SHA-256; FAILS on bcb03313f: RAM differs, v86's set_tsc left the TSC off by the offset before the Restore). Suite replay, case prompt edge (hello paused: the step count of its first hlt; reloaded, stepped to 1 and 2 before it, the prompt text marked seen: run(1) / run(2) end on the hlt with "prompt" there; FAILS on bcb03313f: "count", the stop a replay lost when a recorded key was due at the prompt's hlt) and the Debian+initrd JIT replay with the exact fast-forward restore (the prompt then comes from stepping) | snapdiff,bookmarks,deltasnap; jitdet,guestin,replay |
| R4 the patched v86, a snapshot repeats the JIT run | suite jitsnap. The page's `v86.wasm` has the SHA-256 of the `gen-v86.sh` build and exports `jit_reset_hidden`, and stepper.js adds its step exports to it. Debian (1.0e9 to 2.55e9) and Debian+initrd (2.5e9 to 3.3e9) at 1280 light, on J1's det JIT from a pause: the page's Save at A, the run on to B (X1); Restore in the same page (X2), in a reloaded page from IndexedDB (X3), and a fresh page that boots to A, saves and restores (X4), each run to B: every slice end (count, EIP, registers and EFLAGS, IRQ0 count) and the end's RAM, registers and clock equal to X1's, and the guest's TSC on the virtual clock (within 1 ms, read twice at the same count) right after each restore and at the ends (it once came out nearly doubled after a reset: suite debian, CI run 43013630565). Debian+initrd at 390 dark: Pause past 2.5e9, Save (the log gets the reset), Continue to the (initramfs) prompt, Restore, Continue to the prompt again: the same count, EIP, RAM, registers and clock, no taint, Replay from boot matches. On 2f26f8c5d (npm's v86.wasm) it FAILS (see PROGRESS R4) | jitsnap |
| R5 reverse execution at JIT speed | suite jitrev. Debian at full speed paused past 2.5e9 (1280 light, `?revjitk=40000000`), Debian+initrd paused past 3.2e9 in the initramfs scripts (390 dark) and at its (initramfs) prompt (1280 light, the last stretch stepped by the exact stop): Step back is enabled with JIT checkpoints in the history; Step back 1 (run bar) and Step back 999 (Debugger) land at N-1 and N-1,000 within 10 s; Step 999 and Step 1 forward again give the registers, segment registers, EFLAGS, CR0-CR4, v86's counter, clock, IRQ0 count, halt state, RAM SHA-256 and VGA text stepped back to at N-1 and the live run's at the pause N; Back to over the JIT's stretches says it did not look there; forward from the history's start to N-1 again the same; Replay from boot (the Snapshots tab's button) of the run ending at N-1, and of the one ending at N-1,000, matches (EIP, RAM, screen, clock, counter) with the same registers. FAILS on 0eb730a31 (Step back disabled after the JIT ran) | jitrev |
| R6 the det JIT at v86's speed again | suite jitdet: Debian at full speed on the virtual clock (1280 light, 390 dark) waits for none of its compiled blocks in Chromium (FAILS on fcbabc36c: it waited for 112 of 137) and keeps its speed at 0.9 of v86's own loop or more. Suite jitrev, Debian+initrd at the (initramfs) prompt: Step back 1 from the hlt of the idle loop's "sti; hlt" lands on the sti, 2 back, and the status says why (it FAILED once the prompt came at that halt: "Stepped back 0 instructions", the machine still at the hlt) | jitdet,guestin,replay; jitrev |
| X1 the page says what is deterministic | `check-text.py` also fails on stale claims in the page sources (its STALE list: SeaBIOS waiting on real time, the minified Linux calibrating its delay loop, counts that vary from run to run) (FAILS on 8ec1bc839: the count tooltip, the About tab and this README). The count tooltip and the About tab now say that on the virtual clock every count is the same in every run, stepped and at full JIT speed, and what still changes with the real-time clock | site |
| X2 one instruction count | suites snapdiff ("one count": hello 390 dark, the minified Linux 1280 light) and bookmarks ("one count": hello 1280 light, the minified Linux 390 dark): paused after Run to `hello_main` / `start_kernel` and 40 / 3,000 steps, the Machine tab's counters "now" row, its count line and the run badge give one count N (hello 1,405,793, v86's counter 1,405,880; minified 1,467,846, v86's 1,467,933); the snapshot's default name says @N, its row "instruction N", the diff to now "instruction N → N", the "This position" bookmark's default name @N and its row "after N instructions", tied to that snapshot; none shows v86's number; a position bookmark stored without the stepper's count (before X2) lists after a reload with v86's number. The bookmarks suite's position bookmark holds both counts. FAILS on c0bf0be0e (every name and row says v86's count, 87 higher) | snapdiff,bookmarks |
| B1 a power-on snapshot restores | suite snapdiff (hello at 1280 light and 390 dark, the minified Linux at 1280 dark, started paused at the reset vector F000:FFF0: the IDE controller's BAR0 and BAR1 have no BAR and the I/O bit set; Save snapshot there, Run to hello_main / start_kernel, Restore: no error, the registers, the instruction count and every RAM page are the snapshot's and the machine is paused; then Run to the same target stops there and hello prints Still alive; no page error, no horizontal scroll; screenshots after Restore and after the boot). FAILS on eb8579e5c: "restore_state failed: TypeError: Cannot read properties of undefined (reading 'original_bar')", the count and 22 pages wrong, a wasm "unreachable" page error | snapdiff |
| B2 one double tap is one open | suite graphopen (each of its four runs, after the double click or tap above: the same double click (1280 px) or double tap (390 px) on the function's box twice more, once with the open itself taking 900 ms longer (a wrapper around openFunction busy-waits after it) and once with the CPU 10x slower through CDP; each opens the function exactly once and adds one history entry, and Back returns to the Calls tab as it was). FAILS on 250ee59e1: with the 900 ms open every run opens twice, at 390 px that is two history entries (before (0, 2), after (2, 3)); the canvas's dblclick opened it, then the timer of the pointerup double-press detection opened it again because the 600 ms guard counted from the start of the open | speedasm,runbadge,memeip,graphopen,srcfiles |
| V3 no stale machine state | suite misc (`header_check_case`: hello booted with the debugger, the machine-state sections open in the Debugger, then booted again with a v86.wasm the page cannot patch: the Debugger shows no machine state; booted again with the page's v86.wasm: the sections are back and the CR0 row is this CPU's). FAILS on b8cd70deb: the Debugger still showed the previous machine's segments, control registers and FPU | boot,... |
| P1 installable, offline, update notice | suite pwa (locally the site is deployed twice into a temporary directory with `stamp.py`, served with Pages' caching; offline means the server answers nothing and the context is offline): the manifest is linked and parses, start_url and scope resolve to the site's directory, the icons load at their declared sizes (192, 512, maskable), theme-color for light and dark; Chromium's own parse of the manifest (CDP Page.getAppManifest) meets its install criteria (Page.getInstallabilityErrors is checked too, but Chromium 145 answers [] even for a page without a manifest); a service worker controls the page after the first visit and answers fetches; its version and cache names are the deployed commit. Online, hello paused: Run to hello_main, call stack, locals, Page listing and boot/hello.c in the Source panel. Offline: a reload boots hello to "Hello, World!", the minified Linux (never opened online) boots too, the same Debugger, Page and Source checks pass, Debian (never fetched) says "not available offline yet" with no page error. A second deploy (another commit in sw.js, version.json and the stamps, a marker in app.js): coming back online shows the update notice; Reload, pressed while the new worker still installs (one file of its precache is held back until Reload is pressed) and with no version.json check while it reloads, runs the new app.js under the new worker with only the new deploy's caches, and the notice does not show on it; then the new worker answers Range requests from its cache (206, a suffix range, 416 past the end), leaves a POST to the server, does not keep a precached file twice, keeps a fetched file only while the origin's storage has room (CDP quota override), and offline again boots hello and notes a file never fetched, leaves a request to another origin to the network (it fails, no 503) and, with index.html gone from its cache, answers the navigation with its own 503 offline page. Screenshots of the notes light and dark at 1280 and 390 px, no horizontal scroll. With `--base` the offline part aborts every request of the context and the new deploy is a version.json routed to another commit. Locally also (case edges): without sw.js the page warns `service worker:` in the console and boots with no controller and no note; a worker that never answers the version question still takes the page (after 3 s), and a version.json that is not JSON shows no notice; from an insecure origin (http://mkb.test routed to the server: no service worker API) hello boots with no note; a new deploy whose sw.js is missing: the old worker serves its page online without keeping it (offline the old deploy's page boots), and Reload from the old page reaches the new deploy; a second tab of a new deploy (its worker installed and waiting) activates its worker, and the first tab, of the old deploy, shows the notice naming the new build while the new tab does not. On 039c91717 it fails: no manifest, no service worker | debian,...,tabs,pwa |
| P2 a real install | `test_pwa_install.py` in the `pwa-install` job (headful Chromium under Xvfb: beforeinstallprompt, no installability errors or manifest warnings, PWA.install, chrome://web-app-internals, PWA.launch standalone, hello boots in the app window; four broken copies (no manifest link, icons 404, no service worker, start_url outside the scope) must each fail for their own reason), then against the live site | pwa-install |
| K1 boot check reads the screen | `test_qemu_screen_check.py` (the kernels job's -O0 boot output of run 42539920959, where curses drew "H", a backspace, escapes, a cursor-right and "ello, World!": the bytes lack "Hello, World!" and an expect pattern on them ends without a match, while the replayed 80x25 screen shows "Hello, World!" and "Still alive" on rows 9 and 10 for every read size from 1 byte up; output cut before "Still alive", the strings in the wrong order, overwritten or cleared text fail); `qemu_screen_check.py` boots hello and hello-os (18M and the low image at 1M) and the -O0 minified Linux in the workflow | kernels |
| U1/U2 unoptimized builds | qemu boot of the -O0 hello and minified Linux with both gate lines on the screen (`qemu_screen_check.py`); `check-code-identity.py` for each -O0 and -Os pair; `test_dwarf.py` on both builds of each; suite hello (every instruction of hello_main and put_line), suite unoptimized (minified Linux: eight Run to stops, single steps into calls, no "optimized out" at any address read, values vs gdb at each stop; the -Os entry must show some) | kernels; tasks,operands,runto_phone,srclinks,unoptimized; boot,... |

## In-browser toolchain

`clang/clang.wasm` is clang and ld.lld 22.1.0 (with llvm-ar, llvm-nm,
llvm-objcopy, llvm-objdump, llvm-readobj and llvm-strip in the same
multicall module) with the x86 backend, built for wasm32-wasip1 by
`gen-clang-wasm.sh` (pins, licences and SHA-256 in `THIRD_PARTY.md`). It
is the compiler and linker for rebuilding a kernel in the page. The E0
spike (plan site-edit-rebuild, PROGRESS) compiled hello, minified Linux
and Debian 6.1 files and linked their kernels with it in Chromium,
Firefox and Node, byte-identical to a native clang/lld of the same
commit. `clang-wasm.yml` builds it when the script changes (about two
hours), caches it by the script's hash and checks every file
(`gen-clang-wasm.sh --check`, plus a changed licence file that must fail
the check); the Pages workflow calls it and deploys `clang/`. The service
worker does not precache it (77 MB); the page fetches it only when a
`ClangWorker` loads it.

The page runs it in a Worker (plan step E0c). `clang.js` (loaded by
`index.html`) is the API the editor and the rebuild call: `new
ClangWorker()`, `load()` (fetch and compile `clang/clang.wasm`, no deploy
stamp, so the HTTP cache keeps it across deploys), `write([[path,
bytes or text]])`, `read(paths)`, `remove(paths)`, `list()` and
`run(argv, { cwd, env, stdin, outputs })`, which answers the tool's exit
code, stdout, stderr, the requested output files, the Worker's time and
the wasm memory at the exit. `clangworker.js` keeps the in-memory file system
for the Worker's life and runs each tool (argv[0]: clang, ld.lld,
llvm-ar, ...) on a fresh instance. `wasi.js` is the WASI preview1 layer,
written here for this module instead of a third-party shim: the E0 spike's
@bjorn3/browser_wasi_shim copied a whole file on every write that extended
it (43% of a Chromium profile); here a file is a buffer that grows by
doubling plus a length, and the hot calls (fd_read, fd_write, fd_seek,
fd_pread) make no BigInt beyond the 64-bit arguments WebAssembly passes.
The build directory is preopened as "/" and as its absolute path with
PWD set to it, like `check-wasm-objects.mjs` does with Node's WASI, so
relative paths and `DW_AT_comp_dir` are what the native build saw. A
WASI call it does not have answers ENOSYS, so a module that imports more
still instantiates.
llvm-objcopy and llvm-strip write their output and then set its mode
with fchmod, which wasi-libc answers with ENOSYS without asking the WASI
layer, so they failed with "Function not implemented" after writing the
file (found in E0c, under Node's WASI too). `gen-clang-wasm.sh` applies
one patch to the pinned source: on WASI, LLVM's `setPermissions(FD)`
does nothing and succeeds (WASI preview1 has no file modes).

The kernel builds' host tools run in the same Worker (plan step E0d):
`gen-host-tools.py` builds them for wasm32-wasip1 with the wasi-sdk that
`gen-clang-wasm.sh` pins, from each kernel's own source and with the
compile command its build used (kbuild's `.cmd` file of the tool, or
`make -n` for hello): `scripts/kallsyms`, `scripts/sorttable`,
`scripts/recordmcount` (it rewrites Debian's objects after the compile:
the clang build's config has `FTRACE_MCOUNT_USE_RECORDMCOUNT`, where
Debian's GCC build records mcount calls in the compiler),
`arch/x86/tools/relocs`, `arch/x86/boot/tools/build` and
`arch/x86/boot/compressed/mkpiggy` of the Debian tree (the minified
Linux has only the last two), hello's `tools/build`, and nasm 2.16.03
from its release tarball (SHA-256 of the tarball and of `nasm.wasm`
pinned; `--check`). `hosttools/wasi-shim.c` is linked into each: mmap
that writes a shared mapping back at munmap (sorttable sorts vmlinux in
place), a thread function run to its end in `pthread_create` and the one
`popen` sorttable makes (grep and awk on System.map); the kernel sources
stay unchanged. `ClangWorker.load(url, name)` loads such a module under
a name and `run(argv, { module: name })` runs it; `mkdir(paths)` makes
the directories a tool writes into.
`capture-host-tools.py` records, after a full build, what each native
tool read and wrote: it puts a wrapper in place of each tool (and of
nasm, ld.lld, llvm-objcopy and llvm-nm in PATH), deletes the files that
come before the steps of interest, runs the build's own command again
and then checks that the bzImage came out the same. The clang-worker
job runs every recorded run again in the page's Worker in Chromium,
Firefox and WebKit (`test_clang_worker.py --tool-fixtures`): every file
written and the stdout must equal CI's bytes, for hello -O0 and -Os
(nasm, ld.lld, llvm-objcopy, llvm-nm, tools/build), the minified Linux
-O0 and -Os (llvm-objcopy, mkpiggy, tools/build) and Debian built with
clang (recordmcount on `fs/open.o` and `kernel/sched/core.o`, kallsyms,
sorttable, relocs, llvm-objcopy, mkpiggy, tools/build); per tool, its
first run again with its largest input cut to half must fail or write
other bytes.

`test_clang_worker.py` (CI job `clang-worker`, Chromium, Firefox and
WebKit) feeds a page's `ClangWorker` the inputs of CI's own compiles and
checks the objects byte for byte against CI's: `check-wasm-objects.mjs
--own-wasi --fixture=DIR` (kernels and debian-clang jobs) runs each
compile under `wasi.js` in Node, reading the build directory's files as
clang opens them, and writes per object its argv, directory and every
file read, with the SHA-256 of the build's object (or of the native
clang's rerun). Fixtures: hello and hello -Os, plain and -g;
`fs/namei.o` and `init/main.o` of the minified Linux -O0 and -Os, plain
and -g; Debian's `fs/open.o` and `kernel/sched/core.o`. On the first one
it also checks that another `-O` level and an edited source give other
bytes and the source written back gives CI's again, runs ld.lld -r,
llvm-objcopy -O binary to stdout and to a file (the same bytes), llvm-ar
and llvm-nm, and the API's
failures (a run before load, a 404 module, a Worker that does not start,
terminate with a request pending). `testdata/wasi-probe.c`, built in the
job by the wasi-sdk that `gen-clang-wasm.sh` pins, runs in a ClangWorker
of its own and checks the WASI calls the way wasi-libc makes them (stdin,
clocks, files that grow, shrink and have holes, append, seek, directories
and readdir, rename, unlink, rmdir, and the errors: EEXIST, ENOENT,
EISDIR, ENOTDIR, ENOTEMPTY, ENOTSUP for links and sleeping); the kbuild
host tools of step E0d are such programs. Node's WASI fails 5 of its
53 checks (it has symlinks and sleeps), so the probe tells layers apart. It prints the times and the peak RSS
of the browser; `--json` keeps them, `--firefox-profile` profiles
Firefox (Gecko profiler) and prints where the Worker's thread spent its
samples, `--no-automation` starts Firefox (or `--firefox-binary`) without
Playwright on a page that runs the same checks by itself and posts the
result. Measured (PROGRESS of plan step E0c): Debian's
`kernel/sched/core.c` takes 5.0-7.8 s in Chromium, Firefox and WebKit,
and 98% of Firefox's samples are in clang's own wasm code (wasi.js 0.1%).
The Firefox of Playwright 1.60 (Firefox 150, its own build) took 24 s:
it has no optimizing wasm compiler (with the baseline compiler off it
answers "no WebAssembly compiler available"), with or without Playwright
attached; Mozilla's Firefox 150 and 160 and Playwright's newer Firefox
159 do not have that problem.

CI builds the kernels with the native clang/lld 22.1.0 of
`gen-clang-native.sh`: the same source and pins as `clang.wasm`, the same
tools in one llvm-driver binary, a Release build for x86_64 Linux,
written as a reproducible tar whose SHA-256 the script pins (`--check`
also fails when its pins differ from `gen-clang-wasm.sh`'s).
`clang-native.yml` builds and caches it like `clang-wasm.yml`; the
kernels job unpacks it to `/opt/clang-22` and puts its `bin/` first in
PATH (Debian's llvm package stays for llvm-dwarfdump and
llvm-symbolizer). `check-wasm-objects.mjs` runs `clang.wasm` under Node's
WASI on the exact command line of a build's object (kbuild's `.cmd`, or
`make -n` for hello) in the build directory and fails unless the object
is byte-identical: in CI for hello and hello -Os, the minified Linux -O0
and -Os, each plain and -g, and for the `debian-clang` job, which builds
Debian 6.1 686 from its source package and config with the same clang
(`gen-debian-clang.sh`, LLVM=1, bzImage), boots it in qemu to the root-fs
panic and checks four of its objects (kbuild's recordmcount rewrites the
kernel's objects after the compile, so `fs/open.o` and
`kernel/sched/core.o` are compared with the native clang's output of the
same command, `--native-rerun`; the decompressor's `misc.o` and
`string.o` with the build's). Its `--self-test` compiles the first object
at another `-O` level and must see a difference.

### Editing the source (srcedit.js)

The Source panel's Edit turns the file shown into an editor: a
`<textarea>` (no wrapping, Tab inserts a tab, Escape gives the focus back
to Edit, Ctrl+S saves at once) with a column of line numbers scrolled with
it. Measured before choosing: CodeMirror 6 (`codemirror` 6.0.1,
`@codemirror/lang-cpp` 6.0.2, esbuild 0.24.0, minified IIFE) is 518,778 B
(166,662 B gzip -9) with its basic setup and C highlighting, 278,914 B
(90,386 B gzip) with only the view, the state and the default keymap,
against 862,628 B (270,353 B gzip) for all of the page's own scripts. The
textarea costs nothing, keeps the browser's undo, selection, IME and phone
keyboard, and opens Debian's `kernel/sched/core.c` (11,293 lines) in about
0.6 s; what it lacks is highlighting.

Edits are an overlay over the tree the kernel was built from: IndexedDB
`mkb-srcedit`, store `edits`, key [kernel, path], value
`{ kernel, path, base, text }` (base: the text the edit started from, null
for a new file; text null: deleted); a file typed back to its base is no
longer an edit. Without IndexedDB they stay in memory and the page says
they go with a reload. The listings, the Debugger and the panel's own view
keep the built text (their line numbers and coverage belong to that
build); the panel says when its file has edits. The Source tab's "Your
edits" lists the changed files (+added -removed lines) with a diff and a
revert each (a revert takes a second press within 5 s), downloads all of
them as `<kernel>-edits.patch`, a git patch with a/ and b/ paths relative
to the kernel's tree (`git apply` in the tree), and applies an uploaded
unified diff (git diff or diff -u, with or without a/ b/, new and deleted
files, "\ No newline at end of file") to the edits: each hunk at its line
or the nearest place its old lines are, every file or none.

`srcOverlay` is the API for the in-browser build (E3, E4), for the shown
kernel: `ready` (a promise: the stored edits are read), `paths()` (the
changed files, sorted), `read(path)` (the text with the overlay applied;
null when deleted or not in the tree), `original(path)` (the built text;
null for a new file), `write(path, text)`, `revert(path)`, `patch()` and
`apply(patchText)`.

Tests: `test_srcedit.mjs` (site job) runs the diff, the patch writer and
the patch reader against git on random texts and edits (lines added,
removed, changed, the last newline taken or given, new and deleted files):
each patch srcedit.js writes must `git apply` to the edited text, and each
patch git writes must give it through srcedit.js. Suite `srcedit` of
`test_browser.py` edits hello's `boot/hello.c` (at 1280 px light and 390 px
dark), the minified Linux's `init/main.c` plus a header and a new file
(1280 dark, 390 light) and Debian's `kernel/sched/core.c`: the editor holds
the shipped text with one number per line, typed text and Tab land in it
(also while the Machine tab has the VM's keys), the numbers follow the
scroll, the downloaded patch `git apply`s on the shipped files, a git
patch with one stale hunk changes nothing and names the file, the good one
applies, a reload keeps everything, typing the shipped text back drops the
edit, Revert and Revert all need two presses; and once without IndexedDB.

## Testing

    site/fetch-v86.sh
    site/gen-v86.sh    # v86.wasm: Docker, about 5 minutes
    site/gen-clang-wasm.sh    # clang.wasm: Docker, about 2 hours
    site/gen-symbols.py minified/vmlinux site/kernels/minified-linux/symbols.json minified-linux
    site/gen-symbols.py hello/boot/high/kernel.elf site/kernels/hello/symbols.json hello
    (dwarf/ and src/: see the workflow's "Debug-info builds" step)
    site/gen-kernels.sh
    site/test_kernel_data.py
    python3 site/test_browser.py
    python3 site/test_browser.py --suites debian,dwarf,dwarf_import,types

`test_kernel_data.py` (also in the workflow's site job) fails when a
kernel in kernels.json lacks a file one of the debug features fetches:
symbols, line and inline shards, C source, types, the coverage tree's
files.json, CFI and locals shards, and the task_struct layout (hello and
hello-os, which have no tasks, must have none). The hello suite checks the
hello kernels' features in the page. hello (-O0): Run to hello_main, then
one step at a time to its hlt; at each of the addresses no parameter or
local of any frame is optimized out, and at the statement boundaries row,
s and p are the guest's values (the BIOS cursor row, the addresses of the
two strings, the VGA cell) with the call stack put_line <- hello_main <-
startup_32. hello-os (-Os): the values from the DWARF location lists
(registers) and put_line's s shown as optimized out. Both: the Source tab's tree
and the honest "no variables" and "no current_task" lines.

### Coverage of the page's JavaScript (jscov)

With `MKB_JSCOV=DIR` set, test_browser.py and test_pwa_install.py collect
V8 precise coverage (`site/jscov.py`): their Chromium also opens a
DevTools port, and a second CDP connection attaches to every page, popup,
Worker and the service worker before it runs, starts block coverage and
takes it every 30 s, before Playwright closes or navigates a page, when
the page navigates by itself (location.reload(), location.href: its
beforeunload pauses in the debugger while the collector takes; the new
document can be in another renderer process, without the old one's
coverage: in CI run 43438427659 pwaReload showed as never run although the
pwa suite clicks Reload), and before the
page terminates a Worker. Only the site's top-level scripts are
kept (not v86/, iced/, lz4/, nor the node tools gen-dwarf.js and
gen-src.js). `site/jscov/report.js` (monocart-coverage-reports, pinned with
its SHA-512 integrity hashes in `site/jscov/package-lock.json`) merges the
raw files of every job into an HTML report and `summary.json` (lines,
functions, branches per file, every function that never ran and the
uncovered line ranges, `uncoveredLines`):

    MKB_JSCOV=/tmp/jscov python3 site/test_browser.py --suites hello
    npm ci --prefix site/jscov --ignore-scripts
    node site/jscov/report.js --src site --out /tmp/jscov-report /tmp/jscov
    python3 site/test_jscov.py   # self-test on a fixture page

A function V8 reports without its blocks (it ran, but which branches is
not known) counts as run and its lines do not. The collector's close line
counts those per target type, and `raw-*.trace.txt` beside each raw file
lists attaches, detaches, pauses, takes and answers in order. Functions
V8 did not compile itself while block coverage was on report no blocks
(Chromium 158), so Chromium runs with `--v8-cache-options=none` (the
site's service worker caches its scripts at install and Chromium loads
those with a code cache: in CI run 43509415990 every function of a page
reloaded under it came without blocks) and
`--js-flags=--no-compilation-cache` (a document loaded again in the same
renderer reuses the functions compiled for the one before, seen with the
Debugger domain off). The self-test's reload, of a script its service
worker serves from its cache, fails without the first.

The service worker runs when Playwright's own session runs it (the
collector's Runtime.runIfWaitingForDebugger does not start it), and
Playwright runs it at once. When that came before the collector's
coverage start, Chromium had compiled all of sw.js (it compiles a service
worker eagerly) without block counters, and every function of that
worker reported no blocks: 13 of 13 in most coverage jobs of run
43528247799, different jobs in different runs, so sw.js measured 72% in
one run and 92% in another. With MKB_JSCOV set, `install()` loads
`jscov.HOLD_SW_JS` into Playwright's driver (`NODE_OPTIONS=--require`):
Playwright's run of a new service worker waits until the browser has the
collector's coverage start (a file per target in a temporary directory),
at most 20 s; the driver prints `JSCOV_HOOK_OK` when the patch is in. The
self-test handles the service worker's attach a second late and fails
without it (sw.js without block detail: the untaken return covered, the
never-called function not listed). A worker Chromium stops and starts
again loads its V8 code cache from the install and still reports no
blocks; that happens only where a test stops it or restarts the browser
(pwa-install), the same in every run.

Block coverage slows the page's own JavaScript: stepping a Debian boot
again for a Step back took 679 ms instead of 199 ms (3.4x; binary block
coverage, callCount off, 719 ms), so the wall-clock bounds of reverse,
snapdiff, exactstop and tasks fail under it. CI therefore runs every
browser-test group twice: plain, with every assertion, and with coverage,
where a failing test step does not fail the job. The coverage jobs and
pwa-install upload their raw coverage;
the jscov job runs the self-test (a never-called function, one nested in
it, a Worker's and the service worker's functions and an untaken else must
show as uncovered, the called ones as covered, also one a page runs just
before it leaves for another site by itself, and the untaken return of a
function that runs only after a reload), merges, writes the table
into the job summary, uploads the report (artifact `jscov-report`) and
fails when a total is below `site/jscov-baseline.json` or a file's lines
are below its `fileLines` (80). `site/jscov-uncovered.tsv` says, per block,
why each line that never runs in the tests does not (V3: 355 blocks, every
uncovered line of run 43588908776); the job summary names the uncovered
lines it does not explain and the listed lines that ran (`report.js
--uncovered`; `--require-listed` would fail on the first, CI only reports
them, so a new uncovered block shows up there first).

The suites (`SUITES` in test_browser.py) run in four parallel CI jobs
(the workflow's browser-test matrix), grouped by their measured times;
each prints `suite NAME: N s`.

The tab bar wraps on narrow screens (every tab a 32 px target; at 400 px
and less compact tabs keep it to one row); the touch
case checks every tab is inside a 390 px viewport, the last one opens,
and a touch swipe outside the graph scrolls the page. A determinism case
boots hello at 1k/s and at full speed and Linux at full speed twice and
at 1M/s, and checks the counts are identical.
A coverage case opens start_kernel's page paused at the reset vector
(nothing tinted, the key says nothing was stepped), then after a counted
boot (`?speed=full`) in both colour schemes at 1280 and 390 px: the first
instruction of start_kernel is not tinted, die() is, no hex row is, and
the page has no horizontal scroll.

Cases: hello and hello-os at 2 MB with the default boot (the entry marker,
the switch to 1k/s, 234 (-O0) and 86 (-Os) instructions from the entry to
"Hello, World!", the "Halted" banner), Linux at 2 MB, 18 MB and 2180 KiB with `?speed=full`
(the panic banner with "init exited with code 0"), and Linux with the default
boot for a few seconds (entry marker, 1k/s, the counter grows by about
3,000 in 3 s, ETA and current symbol in the Machine tab, the speed choice
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
The runto suite's case also checks Enter in the Step N field and each
reason a Run to is refused (a misspelt name with the similar ones, a
data symbol, a data address typed as a word, free text, an address past
4 GiB), with the machine left where it was.
The import case boots the minified Linux with its own System.map
(`llvm-nm -n`) and with the vmlinux ELF instead of symbols.json: Run to
start_kernel names start_kernel in the Debugger as with symbols.json, a
counted boot has a start_kernel frame holding most of the run and detects
the panic, the listing is decoded live, a malformed file gives an error and
keeps the symbols, nothing scrolls sideways at 390 px. The import-edges
case gives the page symbol files built in the test that it must refuse,
each with its reason (an x86-64 ELF, an ELF cut after 20 bytes, one
without section headers, a stripped one, one whose only symbols are in a
section that is not loaded or in no PT_LOAD, a one-line System.map, an
offset that is not hex), System.maps without section markers placed by
each automatic rule and by hand (every symbol's physical address and
size, a weak one too, and the sections from the symbols' extent), an ELF
with an offset by hand, and a bzImage dropped together with its
System.map, then Forget (with neither symbols nor DWARF the Debugger
names no memory operands). The initrd case loads a 512-byte newc cpio built in the test with the
minified Linux and checks RAM was raised, its bytes are at 64 MiB in
guest RAM, the note about our kernels, the initrd region in the map and
grid, the RAM error at 32 MB and "no initrd". The header-check case
loads bzImages built from hello's with a broken header (not a bzImage,
protocol 2.01 with LOADED_HIGH clear, a 32-bit part larger than 2 MB of
RAM, an init_size past the RAM, an initrd_addr_max below the initrd, an
init_size no listed RAM holds) and checks each line of the header check
against the numbers in the file, the RAM picked for it, and that Boot
starts nothing when the kernel or the initrd does not fit in RAM (v86
threw "offset is out of bounds" there before). With a v86.wasm the page
cannot patch (no debugger) the Debugger shows no machine state, and with
the debugger back the state is this machine's. For a bzImage without a
kernel_version the initrd note says what a zstd initrd and one with a
broken gzip stream are for (from their file names), a cpio cut inside its
first header and one without its trailer (its lib/modules release). The
fetch-failure case
also checks a kernel picked in the Machine tab that answers 404,
kernels.json missing or empty (Boot then does nothing), hello without
symbols.json or disasm.json (missing, not JSON, not listed): it still
runs and Run to says it has no symbols, version.json unreachable ("local
build"), an uncaught exception in the status line, and an entry's own
initrd dropped when another kernel is picked. The panic-text case writes the panic line through the VGA MMIO handlers
of a running page and checks the machine pauses with the banner, and on
the real-time clock at full speed (v86's own loop; the machine spinning
at start_kernel, the clock switched from virtual to real time while the
page's JIT runs) the "(initramfs) " prompt pauses it once and the panic
line stops it, neither exact. A panic line without its end line under
the page's JIT: the stepper takes over to the kernel's own panic, or on
a spin stops after the panic budget (`?panicbudget=`) and says so; Step
10^15 on the spin stops after 5 s; counting gives up past its time
limit, or stops when count is switched off, and the JIT runs the boot to
its end; a v86.wasm the page cannot patch boots without the debugger,
which says why; with
`MKB_DISTRO_CACHE` (the cache of `test_distro_symbols.py`) the Debian
kernel is booted without an initrd and must stop itself. When a `debian*` kernel is in kernels.json (CI, or locally after
copying it there) the Debian case checks its defaults, waits for the
end-of-panic stop with "end Kernel panic" on the screen, and, on a fresh
paused boot, runs to start_kernel (about 3.5 minutes of stepping) and
checks the Debugger names it, then steps 11 times and checks the
source rows against llvm-symbolizer on the -dbg vmlinux and that the
start_kernel listing has init/main.c lines with their text; tapping one
opens start_kernel in the source panel with some but not all lines
executed, and at most 100 of its 3,183 source files were fetched. With the C source
(every kernel built here, and Debian) the DWARF cases also check the
text: the line at the entry of start_kernel / hello_main names it, and
at least half the call lines of inlined code name the inlined function. The DWARF case (minified
Linux, hello) runs to start_kernel / hello_main, steps to 12 (5) EIPs and
compares the Debugger's source rows with `llvm-symbolizer --inlines` on
the -g ELF (`$MKB_DEBUG_ELF_DIR/<kernel>.elf`, required in CI), needs at
least one EIP in inlined code, call stack and timeline frame titles with
file:line, C lines in the listing, and takes screenshots light and dark
at 1280 and 390 px; then, light and dark, in every view with C lines the
computed colours of `.srcloc` and `.srctext` differ, are the same in all
views, change with the theme and are at least 4.5:1 against the
background behind them. The DWARF import case imports the minified Linux's -g
vmlinux, compares at 6 EIPs, reloads and gets the same answers from
IndexedDB, checks a vmlinux without DWARF says so, shows file:line only
and the C source picker until a source is picked, refuses a .tar.gz with
the reason and gets the lines' text (equal to the files') from a pax
.tar, a GNU .tar and a folder that hold the tree one level down, and imports Debian's
300 MB -dbg vmlinux and compares 40 random addresses. The types case
(minified Linux, Debian) runs to start_kernel and checks the Variables
section: watches of init_task.comm, jiffies_64 and others equal gdb's
print of the ELF's own data, init_task opens with `.comm = "swapper"`, a
pointer member opens the Page tab at its target; in the Page listing
init_task's label line says `struct task_struct` and 25 bytes near it,
pointed at with the mouse, name a variable path that gdb evaluates to an
lvalue holding the byte; a tap offers a working watch; screenshots light
and dark at 1280 and 390 px. The same case checks where variables are
defined: init_task's watch says the file:line of gdb's symbol and opens
it in the Source panel on the line that defines init_task; Debian's
`_acpi_module_name` (107 file statics) lists the other definitions, and
two of them picked by path and by unique suffix give the address gdb's
`&'file'::name` gives; `nosuch/file.c::init_task` and a basename two
files share are refused; the hovered byte of init_task and every global
operand on start_kernel's page say "defined at" with types.json's place
(gdb's for up to 8 of them). It also checks the Debugger's boxed EIP line
draws the underscores of its names (`start_kernel+0x0`): in a screenshot
decoded in the page, some pixel row between the box's outline rows has
ink across the cell of every `_`, light and dark. The types import case imports the minified
Linux's and Debian's -g vmlinux and checks the Worker's variables equal
the shipped `types.json`, also after a reload (IndexedDB). The coverage tree case boots the minified Linux counted and checks the
Source tab: the root and every directory are the sums of their files and
equal `files.json`, init/main.c has lines that ran, die() has none,
start_kernel's instructions run equal the untinted instructions of its
listing, sorting and the filter work, the Source panel's whole-file view of
init/main.c has the tree's numbers, the lcov export has the same totals,
a function opens the Page tab; screenshots light and dark at 1280 and 390
px. The multi-line case (same suite) opens the asm statements of strlen
(arch/x86/lib/string_32.c) and native_save_fl (irqflags.h, inlined) in the
Source panel, whole file and function view at a copy that ran: every line
of the statement shows as run, the lines after the first as continuation
lines that are not counted; strlen's own copy, which does not run, shows
the whole statement as not run; the file's counts in the panel, the tree
and the lcov export are equal; `srcStatements` is checked on a snippet
with strings, comments, a #define, headers, a label and initializers. The Debian case also opens the tree at start_kernel and checks it came
without fetching most shards. The unwind case pauses the minified Linux
in memblock_add_range (start_kernel's call chain) and in tty_write (a
system call from user space), and Debian in timer_interrupt (an interrupt
arriving in kernel code), each at the entry and a few steps in: every
frame has a method label, timer_interrupt and tty_write have a pt_regs
frame, the unwind reaches the start of the stack, and the frames CFI
found equal gdb's backtrace of an ELF core file made of the same
registers and stack pages (gdb starting again from each pt_regs frame's
registers, as it does not cross entry code); only the CFI shards of the
frames are fetched; screenshots light and dark at 1280 and 390 px. The
unwind import case imports the minified Linux's -g vmlinux and gets the
same frames as the shipped shards, and after a reload the same CFI rows
from IndexedDB. At each of those pauses the Parameters and locals section
has one entry per unwound frame, every variable shows a value with its
location or the reason it has none, and over the frames CFI found the
values equal gdb's on the same core file (each inlined call and function
with the same variables; a scalar both read is equal; one gdb finds
optimized out the page does not show; gdb's own extra, entry values it
resolves through call sites, are counted); the import gives the same
section as the shipped shards, and the same variables after a reload. `test_dwarf.py --cfi N` compares the row at N random
addresses (half inside FDEs) with the rows `llvm-dwarfdump --debug-frame`
evaluates (local: minified 4,918/4,918, Debian 20,000/20,000 of which 211
with DWARF expressions), and `test_dwarf.py --eh-frame` does the same on
the `.eh_frame` of a small program built with GCC and clang. A last case serves two "deploys" with `Cache-Control: max-age=600` (as
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
