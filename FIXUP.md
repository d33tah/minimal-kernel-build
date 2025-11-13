--- 2025-11-13 12:35 ---
SESSION UPDATE - perf_event.h reduction attempt

ATTEMPT: Stub uapi/linux/perf_event.h (1395 -> 96 lines, 1299 LOC reduction)
Tried to create minimal stub with only essential types (perf_event_attr, etc.)
Added back PERF_COUNT_SW_PAGE_FAULTS after initial build error.
Result: BUILD PASSES but VM HANGS - "Hello, World!" does not print
Root cause: Stub was too aggressive, removed types/defines that are needed at runtime

REVERTED: Back to full 1395-line version
Build: PASSES, make vm: PASSES, Hello World: PRINTS

Current state unchanged:
- LOC: 269,133 total (157,737 C + 111,396 headers)  
- Goal: 200,000 LOC
- Gap: 69,133 LOC (25.7% reduction needed)

Lesson: uapi/linux/perf_event.h needs more careful analysis of what's actually used.
The header is included by hw_breakpoint.h which is needed for breakpoint support.

Next strategy: Look for other medium-sized reduction opportunities.
Will try different files or take a more incremental approach to header reduction.

ANALYSIS (12:40):
Examined other potential targets:
- Generated headers (atomic-arch-fallback.h, atomic-instrumented.h) cannot be edited directly
- EFI header (1285 lines) - EFI is disabled but header still full, complex to stub safely
- compat.h (556 lines) - COMPAT_32 enabled, likely needed
- lib files (siphash.c 451, string_helpers.c 972) - used by core code
- arch/x86/kernel/dumpstack.c (446 lines) - error handling, risky to stub

Challenge: Need 69,133 LOC reduction (25.7%). Most large remaining files are either:
1. Core infrastructure (mm, fs, sched) that's heavily interdependent
2. Headers that are complex to stub without runtime issues
3. Generated files that can't be directly edited

Potential approaches for next session:
1. CONFIG-level changes to disable entire subsystems (may require careful Kconfig work)
2. Systematic header trimming with incremental testing
3. Focus on accumulating many small reductions (50-200 LOC each)
4. Dead code analysis to find unused functions/exports
