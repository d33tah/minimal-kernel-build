--- 2025-11-14 18:00 ---

SESSION START (18:00):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC (measured with cloc after mrproper): 261,664 total
  - C: 144,085 LOC
  - C/C++ Headers: 106,199 LOC
  - C+Headers: 250,284 LOC (66 LOC better than previous session)
  - Other (make, asm, scripts, etc): 11,380 LOC
- Gap to 200K: 50,284 LOC (20.1% reduction needed)

Plan: Continue systematic reduction. Previous sessions successfully removed logging
statements (76 LOC total). Need to find bigger targets. Will explore:
1. Large header files that might be unnecessary
2. Unused subsystems that can be stubbed
3. Complex features in VT driver
4. Syscall implementations that are unused

Actions (18:00-):

1. SUCCESS - Removed informational/warning logging (18:00-18:12):
   - drivers/tty/vt/vt.c: Removed console messages (17 LOC)
   - drivers/tty/tty_io.c: Removed TTY warnings (7 LOC)
   - drivers/tty/tty_ldisc.c: Removed ldisc warning (2 LOC, +1 for name var)
   - drivers/base/dd.c: Removed driver warnings (5 LOC)
   - drivers/base/driver.c: Removed driver update warning (4 LOC)
   - kernel/fork.c: Removed deny_write_access warning (1 LOC)
   - kernel/panic.c: Removed warning messages (11 LOC)
   - kernel/params.c: Removed IRQ warning (3 LOC)
   - kernel/reboot.c: Removed reboot/shutdown warnings (2 LOC)
   - kernel/resource.c: Removed device conflict warning (2 LOC)
   - mm/mmap.c: Removed VmData warning (5 LOC)
   - Total: 59 LOC removed, +1 added = 58 net, measured 38 LOC reduction
   - Binary: 375KB (unchanged)
   - Build successful, "Hello, World!" printed ✓
   - Committed & pushed ✓

Current status (18:12):
- LOC: 261,626 (C+headers: 250,227)
- Gap to 200K: 50,227 LOC (20.0% reduction needed)
- Binary: 375KB
- Progress: 38 LOC removed this session

--- 2025-11-14 17:27 ---

SESSION START (17:27):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC (measured with cloc): 275,917 total
  - C: 149,286 LOC
  - C/C++ Headers: 108,668 LOC
  - C+Headers: 257,954 LOC
  - Other (make, asm, scripts, etc): 17,963 LOC
- Gap to 200K: 57,954 LOC (22.5% reduction needed)
- vmlinux size: 660KB text, 181KB data, 1.2MB BSS

Actions (17:27-):

1. ANALYSIS (17:27-17:45):
   - Examined binary structure: BSS is 1.2MB, mostly __brk_pagetables (1MB) for early boot page tables
   - Found keyboard maps taking ~1.6KB BSS (shift_map, ctrl_map, altgr_map, etc - 200 bytes each)
   - Found 246 SYSCALL_DEFINE in codebase, init only uses 2 (write, exit)
   - kernel/sys.c has 498 LOC with 30 syscalls (getpid, setpriority, etc) - all unused by init
   - kernel/sched/ has 9,483 LOC with 13 sched-related syscalls - all unused
   - fs/ layer is 26,375 LOC total (namespace.c 3857, namei.c 3853, dcache.c 2326)
   - 316 CONFIG options enabled, minimal debug/trace configs
   - Only 2 compiler warnings (about generated atomic headers)
   - drivers/tty/vt/defkeymap.c is 165 LOC (generated keyboard map)
   - DIARY notes from 2025-11-12 indicate subsystems are deeply interconnected
   
2. NEXT ATTEMPT - Target: simplify keyboard/console code:
   - Will investigate if keyboard maps can be simplified
   - Check if defkeymap.c generation can be reduced
   - Examine if VT color handling or other VT features can be stubbed

--- 2025-11-14 16:03 ---

SESSION START (16:03):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC: 261,678 total (144,188 C + 106,199 headers = 250,387 C+headers)
- Gap to 200K: 61,678 LOC (23.6% reduction needed)

Previous session removed 29 LOC of unused functions (15:46-16:05).

Plan: Focus on larger reduction targets. Top candidates identified:
1. vt.c (3631 LOC) - virtual terminal driver
2. Signal handling (signal.c ~2414 LOC)
3. Memory management files (page_alloc.c, memory.c)
4. Time subsystem
5. Header reduction

Starting systematic reduction efforts...

Actions (16:10-16:30):
1. ANALYSIS - Deep dive into codebase structure (16:10-16:29):
   - Re-verified make vm works, prints "Hello, World!" ✓
   - Binary: 375KB (unchanged)
   - LOC: 261,678 total (gap to 200K: 61,678 LOC = 23.6% reduction needed)

   Top 5 files analysis (16,568 LOC total = 6.3% of codebase):
   * mm/page_alloc.c: 3876 LOC - page allocation, likely core functionality
   * mm/memory.c: 3306 LOC - memory management, core
   * fs/namei.c: 3260 LOC - pathname resolution, needed for init
   * fs/namespace.c: 3093 LOC - mount handling, needed for initramfs
   * drivers/tty/vt/vt.c: 3033 LOC - virtual terminal, mostly needed

   Other large components:
   - Headers: 106,199 LOC (40.6% of total!) with 5281 inline functions
   - kernel/irq/manage.c: 1587 LOC (IRQ management)
   - kernel/sched: ~9K LOC total (schedulers)
   - lib/ large files: bitmap.c, vsprintf.c, iov_iter.c, xarray.c, radix-tree.c
   - Only 96 exported text symbols in vmlinux (already minimal)
   - 266 CONFIG options enabled

   Key insight: Most "obviously removable" code already eliminated in previous sessions.
   Remaining code is tightly integrated core functionality. Headers are 40% of LOC!

   Need new approach: Look for simplifiable functions within large files OR
   attempt header reduction strategy.

2. INVESTIGATION - Signal and time subsystems (16:29-16:32):
   - kernel/signal.c: 3099 LOC with 19 syscalls
   - Signals ARE used internally: page faults (force_sig), file limits (send_sig)
   - Cannot safely stub entire signal subsystem
   - kernel/time: ~7K LOC total, time syscalls likely needed for init

3. INVESTIGATION - Optional subsystems check (16:32-16:34):
   - futex: already removed ✓
   - modules: already removed ✓
   - network: already removed ✓
   - Most optional subsystems already eliminated in previous work

SESSION STATUS (16:34):
- No LOC reduction this session (analysis only)
- Binary: 375KB (unchanged)
- LOC: 261,678 (unchanged, gap to 200K: 61,678)

Key conclusion: We've hit a plateau. Most "easy" reductions done. Remaining options:
1. Attempt fine-grained function stubbing within large files (high risk)
2. Header reduction (complex, needs careful analysis of what's actually used)
3. Simplify core algorithms (page allocator, scheduler, etc) - very risky
4. Accept that 200K LOC goal may require breaking functionality

Next session should either:
- Take calculated risks with targeted subsystem simplification, OR
- Focus on header cleanup/reduction strategy

--- 2025-11-14 15:09 ---

SESSION START (15:09):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC: 261,650 total (144,205 C + 106,199 headers = 250,404 C+headers)
- Gap to 200K: 50,650 LOC (19.4% reduction needed)

Actions (15:09-ongoing):
1. Committed FIXUP.md documentation from previous session
2. Investigating reduction targets:
   - Headers are 106K LOC (40% of total) - biggest opportunity
   - Looked at scheduler files (deadline.c, rt.c) - tightly integrated, risky
   - Checked CONFIG_PERF_EVENTS - disabled but hw_breakpoint files still present (176 LOC only)

3. FAILED - Attempted to stub lib/xarray.c (15:15-15:20):
   - Observation: nm vmlinux showed no "xa_" symbols (all local 't' symbols)
   - Hypothesis: xarray functions optimized away by LTO, could be stubbed
   - Action: Stubbed entire file to ~30 LOC
   - Result: Linker errors! Functions ARE used: xa_erase, xa_load, __xa_insert, xas_set_mark, etc.
   - Used by: page writeback (tag_pages_for_writeback), folio management, IDA allocator, vmalloc
   - Reverted changes via git checkout
   - Conclusion: LTO inlines functions but they're still needed. nm output misleading.

4. Current challenge (15:20):
   - Need 50K LOC reduction (19.4%)
   - Most obvious targets already eliminated in previous sessions
   - Core subsystems (MM, VFS, TTY, schedulers) are tightly integrated
   - Risky to stub without thorough understanding of dependencies

SESSION STATUS (15:23):
Current: 261,650 LOC total (144,205 C + 106,199 headers = 250,404 C+headers)
Binary: 375KB
Gap to 200K: 50,650 LOC (19.4% reduction needed)

This session's accomplishments:
- No successful LOC reductions
- Documented failed xarray.c stubbing attempt
- Learned that LTO makes it difficult to determine which code is actually needed
- Confirmed that nm output showing only local symbols doesn't mean code is unused

Key insight:
With LTO (Link Time Optimization), most functions are inlined or made local ('t' symbols),
but they're still needed. The linker will fail if we remove them. Need different strategy
than checking symbol visibility.

Recommendations for next session:
- Focus on identifying truly unused code paths via CONFIG analysis
- Look for subsystems that can be disabled entirely in .config
- Consider header consolidation (106K LOC = 42% of code base)
- Try incremental function stubbing with immediate testing rather than whole-file approaches

--- 2025-11-14 14:53 ---

Actions (14:53-15:08):
1. FAILED - Attempted additional string_helpers.c reductions (14:53-15:01):
   - Stubbed string_get_size() - simplified to basic formatter
   - Stubbed kstrdup_quotable functions to use simple kstrdup
   - Result: Build succeeded but kernel hung - didn't print "Hello, World!"
   - Reverted changes via git checkout
   - Conclusion: These functions must be used somewhere in boot path
   - Previous commit with escape/unescape stubs is safe and works

2. Analysis of other reduction targets (15:01-15:08):
   - Examined scheduler files: deadline.c (1279), rt.c (1074) - risky to remove
   - Examined lib files: bitmap.c (1350), iov_iter.c (1431), xarray.c (1234)
   - Examined drivers: tty/ (1.2M), base/ (976K), char/mem.c (693)
   - char/mem.c implements /dev/null, /dev/zero etc - might be reducible
   - vsprintf.c still at 1744 LOC - may have more reduction opportunities

SESSION STATUS (15:08):
Current: 261,585 LOC total (144,205 C + 106,199 headers = 250,404 C+headers)
Binary: 375KB
Gap to 200K: 50,404 LOC (20.1% reduction needed)

This session's accomplishments:
- 1 successful commit (string_helpers escape/unescape stubbing: 180 LOC saved)
- Total: 261,745 → 261,585 LOC (160 lines saved)
- Identified that string_get_size/kstrdup_quotable are critical for boot

Recommendations for next session:
- Try stubbing functions in char/mem.c (/dev/null, /dev/zero implementations)
- Look for more vsprintf.c reduction opportunities
- Consider more aggressive header reduction (106K LOC = 42% of total)
- Investigate if any driver subsystems can be removed entirely

--- 2025-11-14 14:39 ---

SESSION START (14:39):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC: 261,585 total (144,205 C + 106,199 headers = 250,404 C+headers)
- Gap to 200K: 50,404 LOC (20.1% reduction needed)

Actions (14:39-14:53):
1. SUCCESS - Stubbed string escape/unescape functions in lib/string_helpers.c (14:39-14:53):
   - Stubbed 7 complex string formatting helper functions:
     * unescape_space, unescape_octal, unescape_hex, unescape_special (4 functions)
     * escape_space, escape_special, escape_null, escape_octal, escape_hex (5 functions)
   - These handle escaping/unescaping special characters for debug logging and string formatting
   - Not needed for basic console output in minimal Hello World kernel
   - Result: Build successful, make vm prints "Hello, World!" ✓
   - lib/string_helpers.c: 955 → 775 lines (180 lines / 18.8% reduction)
   - Binary: 375KB (unchanged)
   - Total: 261,745 → 261,585 LOC (160 lines saved after mrproper)
   - C code: 144,365 → 144,205 (160 lines saved)
   - Committed & pushed ✓

SESSION STATUS (14:53):
Current: 261,585 LOC total (144,205 C + 106,199 headers = 250,404 C+headers)
Binary: 375KB
Gap to 200K: 50,404 LOC (20.1% reduction needed)

Session progress:
- 1 commit made (string_helpers stubbing)
- lib/string_helpers.c reduced from 955 → 775 lines (180 lines / 18.8% reduction)
- Total: 261,745 → 261,585 LOC (160 lines saved)
- C code: 144,365 → 144,205 (160 lines saved)
- Binary: 375KB (unchanged)

Next targets to consider:
- More lib/ reductions: iov_iter.c (1431), bitmap.c (1350), xarray.c (1234)
- VT code reduction: drivers/tty/vt/vt.c (3631) - many features not needed for Hello World
- Scheduler simplification: deadline.c (1279), rt.c (1074) - specialized schedulers
- Time subsystem: timekeeping.c (1577), timer.c (1497), clocksource.c (1277)
- Continue with lib/string_helpers.c - may have more functions to stub

--- 2025-11-14 14:21 ---

SESSION START (14:21):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC: 261,714 total (144,365 C + 106,199 headers = 250,564 C+headers)
- Gap to 200K: 50,564 LOC (20.2% reduction needed)

Actions (14:21-14:31):
1. SUCCESS - Removed unused IP address formatting functions from vsprintf.c (14:21-14:31):
   - Removed 7 unused socket address formatting functions (143 lines):
     * ip4_string, ip6_compressed_string, ip6_string
     * ip6_addr_string, ip4_addr_string
     * ip6_addr_string_sa, ip4_addr_string_sa
   - These functions were only used for network address formatting, not needed for Hello World
   - Simplified no_hash_pointers_enable warning message (14 lines)
   - Total removed: 157 lines of code
   - Result: Build successful, make vm prints "Hello, World!" ✓
   - vsprintf.c: 1895 → 1744 lines (151 lines / 8.0% reduction)
   - Binary: 375KB (unchanged)
   - Total: 261,849 → 261,714 LOC (135 lines saved after mrproper)
   - C code: 144,500 → 144,365 (135 lines saved)
   - Committed & pushed ✓

SESSION STATUS (14:31):
Current: 261,714 LOC total (144,365 C + 106,199 headers = 250,564 C+headers)
Binary: 375KB
Gap to 200K: 50,564 LOC (20.2% reduction needed)

Progress this session:
- 1 commit made (vsprintf.c IP address formatting removal)
- vsprintf.c reduced from 1895 → 1744 lines (151 lines / 8.0% reduction)
- Total: 261,849 → 261,714 LOC (135 lines saved)

--- 2025-11-14 14:18 ---
SESSION START (14:18):

Current status:
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 375KB (meets 400KB goal ✓)
- LOC: 271,033 total (C+headers)
- Gap to 200K: 71,033 LOC (26.2% reduction needed)

Actions (14:18):
1. SUCCESS - Stubbed IP address formatters in vsprintf.c (14:08-14:18):
   - Identified remaining IP/MAC formatting functions in vsprintf.c
   - Stubbed 3 IP address formatting functions:
     * ip4_string - IPv4 address formatting (47 lines → 4 lines)
     * ip6_compressed_string - IPv6 compressed format (80 lines → 4 lines)
     * ip6_string - IPv6 string format (14 lines → 4 lines)
   - These formatters are not needed for minimal "Hello World" kernel
   - Result: Build successful, make vm prints "Hello, World!" ✓
   - vsprintf.c: 2020 → 1895 lines (125 lines saved)
   - Total: 271,145 → 271,033 LOC (112 lines saved after cloc)
   - Binary: 375KB (unchanged)
   - Committed & pushed ✓

SESSION STATUS (14:18):
Current: 271,033 LOC total
Binary: 375KB
Gap to 200K: 71,033 LOC (26.2% reduction needed)

Plan (14:18):
Continue reducing - targeting larger files/subsystems. Need to find ~70K LOC to remove.
Focus areas:
- Large C files: page_alloc.c (5158), memory.c (4061), namespace.c (3857), namei.c (3853), vt.c (3631)
- Scheduler: deadline.c (1279), rt.c (1074) - specialized schedulers probably not needed
- Time: timekeeping.c (1577), timer.c (1497), clocksource.c (1277)
- Headers: 108,607 LOC total - probably can reduce significantly

--- 2025-11-14 08:36 ---
SESSION END (08:17-08:36):

Achievements:
1. Successfully stubbed cpufreq.h
   - Reduced from 801 LOC to 60 LOC
   - Created minimal stub with only essential types and stubs
   - Added necessary includes (cpumask.h, errno.h)
   - Implemented cpufreq_suspend(), cpufreq_resume(), cpufreq_scale()
   - Build: PASSES ✓
   - make vm: PASSES ✓ 
   - Hello World: PRINTS ✓
   - Binary: 390KB (unchanged)
   - Committed: a192a76

2. Explored other reduction opportunities:
   - PCI headers (pci.h: 1636 + pci_regs.h: 1106 = 2742 LOC)
     - CONFIG_PCI disabled but 9 .c files still include pci.h
     - Complex due to many interdependencies, needs careful analysis
   - mod_devicetable.h (914 LOC) - included by 6 headers, used by scripts/mod
   - kfifo.h (893 LOC) - only lib/kfifo.c includes, but kfifo functions are in vmlinux
   - page-flags.h (858 LOC) - included by 6 headers, important for MM
   - EFI/tracing headers also investigated

Current status after session:
- LOC: 267,173 (down from 267,569 at start, saved ~396 LOC via cloc)
- Binary: 390KB (meets 400KB goal ✓)
- Gap to 200K: 67,173 LOC (25.1% reduction still needed)
- make vm: PASSES ✓
- Hello World: PRINTS ✓

Next session opportunities:
- Continue stubbing large headers where CONFIG is disabled
- Focus on PCI headers (2742 LOC potential with careful work)
- Look at EFI headers (1249 LOC)
- Consider more aggressive MM/VFS reductions
- Identify and remove unused inline functions from large headers

--- 2025-11-14 08:31 ---
SESSION PROGRESS (08:17-08:31):

Successfully stubbed cpufreq.h:
- Reduced from 801 LOC to 60 LOC
- Savings: 741 LOC
- Build: PASSES ✓
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 390KB (unchanged)
- Committed and pushed: a192a76

Next target: Investigating PCI headers (pci.h: 1636 LOC + pci_regs.h: 1106 LOC = 2742 LOC potential)
- CONFIG_PCI is disabled
- However, 9 .c files include pci.h and all are compiled
- Need to determine if pci.h can be stubbed while keeping necessary types

Current LOC: ~267,569 - 741 = ~266,828
Gap to 200K: ~66,828 LOC (24.9% reduction needed)

--- 2025-11-14 08:03 ---
SESSION START:

Current status at session start (08:03):
- make vm: PASSES ✓
- Hello World: PRINTS ✓
- Binary: 390KB (meets 400KB goal ✓)
- LOC: 276,585 total
- Gap to 200K: 76,585 LOC (27.7% reduction needed)

Note: The FIXUP.md at parent level showed 267,497 LOC but actual cloc in minified shows 276,585.
Previous attempts (per parent FIXUP.md):
- lib/xz removal failed (needed for boot decompression)
- scripts/mod removal failed (build system dependency)
- Large files (signal.c, n_tty.c, page_alloc.c) are actively used in binary

Strategy for this session:
- Focus on header file reduction (1213 header files, 110,629 LOC = 40% of total!)
- Look for large headers that can be trimmed or removed
- Consider scripts/ directory reduction (if safe)
- Look for subsystems that can be further stubbed

Starting investigation...

Investigation (08:03-08:10):
LOC breakdown by directory:
- include: 81,038 LOC (29.3%)
- arch: 54,195 LOC (19.6%)
- kernel: 33,907 LOC (12.3%)
- mm: 29,164 LOC (10.5%)
- fs: 20,467 LOC (7.4%)
- drivers: 19,758 LOC (7.1%)
- scripts: 18,096 LOC (6.5%)
- lib: 15,215 LOC (5.5%)

Largest files:
- mm/page_alloc.c: 5158 LOC
- mm/memory.c: 4061 LOC
- drivers/tty/vt/vt.c: 3914 LOC
- fs/namespace.c: 3857 LOC
- fs/namei.c: 3853 LOC
- include/linux/atomic/atomic-arch-fallback.h: 2456 LOC
- include/linux/fs.h: 2192 LOC
- include/linux/atomic/atomic-instrumented.h: 2086 LOC

Attempted perf_event.h stub (1395 LOC) - FAILED: struct perf_event_attr needs many fields (bp_addr, bp_type, etc) for hw_breakpoint.h

Focus areas:
1. Headers are biggest opportunity (81K LOC = 29%)
2. arch/x86 has large files (fpu: 2154 LOC, insn-eval: 1575 LOC, etc)
3. Most C files are actually compiled and linked
4. Scripts directory is 18K LOC but needed for build system

Next: Try to identify and remove/stub large unnecessary subsystems

Additional investigation (08:10-08:15):
- Largest object files: vt.o (82K), namespace.o (82K), namei.o (67K), page_alloc.o (103K)
- Only 96 global 'T' symbols in final vmlinux (LTO is very aggressive)
- 538 headers in include/linux/ directory alone
- lib/math (306 LOC) and lib/crypto (26 LOC) already minimal
- DIARY.md from Nov 12 noted 316K LOC was "near-optimal", now at 276K (40K improvement!)
- arch/x86/fpu: 2154 LOC compiled for FPU support

Session is challenging - most low-hanging fruit already picked. Need to consider:
1. Major subsystem stubbing (VT console - 3914 LOC, 82K object)
2. Filesystem complexity reduction (namespace.c, namei.c - 7710 LOC combined)
3. Memory management simplification (page_alloc.c - 5158 LOC)
4. Aggressive header trimming or removal

Current challenge: 27.7% reduction needed, but previous session concluded this was "infeasible without architectural changes". However, we've already achieved 13% since then (316K→276K).

Session conclusion (08:15):
After extensive investigation, identified the main remaining opportunities:
1. Headers (81K LOC, 29% of codebase) - largest category but highly interconnected
2. Large subsystems with potential for stubbing:
   - drivers/tty/vt/vt.c (3914 LOC, 82K object) - console code
   - fs/namespace.c + fs/namei.c (7710 LOC combined) - mount/path resolution
   - mm/page_alloc.c (5158 LOC, 103K object) - page allocator

Key insight: init program only uses syscall 4 (write) and syscall 1 (exit), yet we have full VFS, mount handling, and complex MM. However, kernel itself needs these to boot and mount initramfs.

The gap from 276K to 200K (76K LOC, 27.7%) requires either:
- Major refactoring of core subsystems (MM, VFS, console)
- Removal of build infrastructure (scripts/ 18K LOC)
- Aggressive header consolidation/trimming

No changes committed this session - investigation and documentation only.
Progress since Nov 12: 316K → 276K = 40K LOC reduction (13% improvement)

NEXT STEPS (05:39):
Successfully reduced 775 LOC through CONFIG analysis. Current: 269,701 LOC, Goal: 200K, Gap: 69,701 LOC.

Strategy for continued reduction:
1. Look for more CONFIG-disabled features with remaining code
2. Analyze lib/ directory for unnecessary library functions
3. Consider stubbing out large subsystems (e.g., simplify scheduler, reduce MM complexity)
4. Check for unused drivers beyond CPU vendors

The CONFIG approach is productive - will continue analyzing for more opportunities.


Session progress (09:20-09:30):
Investigation conducted:
- Analyzed largest files: page_alloc.c (5158), memory.c (4061), vt.c (3914)
- Checked subsystems: crypto (removed), net (removed), ipc (removed), sound (removed)
- Found device.h has 42 inline functions - ALL appear unused in .c files (need header check)
- kernel/sys.c has 30 syscalls, many likely unnecessary for Hello World
- Most major reduction opportunities (headers, large files) require careful analysis
  
Challenges:
- Need to distinguish between truly unused code and code used indirectly
- Large files (page_alloc, memory, vt) are core functionality, risky to reduce
- Most "easy" removals have been done in previous sessions

Next steps:
- Focus on methodical header cleanup (device.h, cpumask.h, wait.h candidates)
- Consider stubbing less-critical syscalls if safe
- May need to accept slower progress - at 267K LOC, goal is 200K (25% reduction needed)

