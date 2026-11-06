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
  slices, so timers fire at the same instruction counts. Debian 6.1:
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
  software breakpoint (a HLT byte at the target, put back when the CPU
  stops on it; a guest that reads its own code there would see it). The
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
  `gen-symbols.py --trim` on the -dbg vmlinux: 47,963 symbols (all code
  and global data with a size; local data objects and export/tracing
  bookkeeping dropped), 5,179,791 bytes, 778,527 gzipped (117,357
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
  There is no disasm.json or shipped C source for imported symbols: the
  listing is disassembled live from guest memory (below). An ELF is read
  in File slices (headers, `.symtab`, `.strtab`), never whole; its DWARF,
  if any, gives file:line (Debug info below). A System.map or a vmlinux
  without `.debug_info` says that it has no line information. The map is remembered in localStorage
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
  C lines of both listings, and in the Graph tab the hover text and the
  detail of a timeline frame ("called from" and "entry", each with its
  chain) and the title of a call-tree name. "no line information" is
  said where the line table has no line (padding, assembly without -g,
  line 0); "reading DWARF…" while a shard or unit loads. Wherever a C
  location or line is shown on the page (both listings, the Debugger's
  source rows, the source panel, the call stack, the Graph detail), the
  file:line or line number has one colour and the C text another: one
  class pair, `.srcloc` and `.srctext` (tokens `--src-loc` and
  `--src-text`, light and dark). Hover titles are plain text drawn by the
  browser and cannot be coloured. Sizes: minified
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
  `name`, `Function::name`, then `.member`, `->member`, `[index]`, a
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
  drawn. `test_dwarf.py --types N` checks N random bytes inside variables:
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
- **Coverage tab.** All the kernel's code as a tree of directories, files
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
  imported vmlinux has no tree (it would need every unit's line table).
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
  (`KCFLAGS=-g` for the minified Linux, `CC="clang -g"` for hello) next to
  the gate kernels and fails unless their code is byte-identical
  (`llvm-objcopy -O binary --remove-section=.notes`: only the 20-byte GNU
  build-id differs, as it hashes the debug info too); `gen-dwarf.js` and
  `gen-src.js` read those. The -g build takes about 11 s locally (9 s
  without -g).
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
  from: v86 computes the running count from the elapsed time), the local
  APIC and IOAPIC (only when v86 runs with ACPI, which the page does not
  enable), the RTC; paging: the mode, a translate box (hex address or
  symbol name -> each level's entry and flags -> physical address and
  symbol) and a page-directory viewer (present PDEs as runs). At the end
  of the minified Linux's boot: CR4 PSE PGE, the GDT's 0x60 is the flat
  32-bit kernel code segment, the master PIC's IMR 0xfa equals the
  kernel's cached_irq_mask, PIT channel 0 reloads 4773 (HZ=250).

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
  and `startup_32`) plus about 1,080 continuation roots (the exact count varies a little between runs), no scheduler code as a
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
    thread function on a strip above the rows (labels that would overlap
    are left out; hover or tap one for the task); switches from inside
    interrupt handling are hidden with "hide interrupts".
    "chains" (on) draws a call whose caller has a single call and
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
- **speed**: full (v86's JIT, not counted) or 10M, 1M, 100k, 10k, 1k,
  100, 10 or 1 instructions per second. The top bar shows the speed
  actually reached, sampled once a second from v86's instruction counter
  (JIT and stepping alike): JavaScript stepping tops out around 5M/s on a
  fast desktop, so 10M/s shows about that; 1M/s and up run in the message
  loop and redraw 10 times a second. Stepped speeds update the screen, the
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
    (dwarf/ and src/: see the workflow's "Debug-info builds" step)
    site/gen-kernels.sh
    python3 site/test_browser.py
    python3 site/test_browser.py --suites debian,dwarf,dwarf_import,types

The suites (`SUITES` in test_browser.py) run in three parallel CI jobs
(the workflow's browser-test matrix), grouped by their measured times;
each prints `suite NAME: N s`.

The tab bar wraps on narrow screens (every tab a 32 px target); the touch
case checks every tab is inside a 390 px viewport, the last one opens,
and a touch swipe outside the graph scrolls the page. A determinism case
boots hello at 1k/s and at full speed and Linux at full speed twice and
at 1M/s, and checks the counts are identical.
A coverage case opens start_kernel's page paused at the reset vector
(nothing tinted, the key says nothing was stepped), then after a counted
boot (`?speed=full`) in both colour schemes at 1280 and 390 px: the first
instruction of start_kernel is not tinted, die() is, no hex row is, and
the page has no horizontal scroll.

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
and dark at 1280 and 390 px. The types import case imports the minified
Linux's and Debian's -g vmlinux and checks the Worker's variables equal
the shipped `types.json`, also after a reload (IndexedDB). The coverage tree case boots the minified Linux counted and checks the
Coverage tab: the root and every directory are the sums of their files and
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
