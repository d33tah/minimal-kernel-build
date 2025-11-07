# Minimal Kernel Build - Session Summary

## Current Status
- **bzImage size**: 475KB (target: 400KB, need 75KB reduction)
- **LOC**: 366,417 (target: 320,000, need 46,417 LOC reduction)
- **Boot test**: ✅ PASSES ("Hello, World!" + "Still alive")

## Changes Made This Session

### Successfully Committed & Pushed
1. **Stubbed sched_show_task and dump_cpu_task** (kernel/sched/core.c)
   - Removed ~24 lines of task debugging code
   - Commit: e73e5f0

2. **Stubbed show_iret_regs** (arch/x86/kernel/dumpstack.c)  
   - Removed interrupt register display debug function
   - Commit: 1416db4

### Progress
- Reduced LOC from 366,441 to 366,417 (24 lines saved)
- Kernel size remains at 475KB (LTO/compression limits visible improvements)
- All builds successful, boot tests passing

## Issues Encountered

### Input Subsystem Challenges
Attempted to stub input subsystem files but encountered boot hangs:
- **ff-core.c** (force feedback): Stubbing broke boot, likely due to input_ff_destroy being called unconditionally
- **touchscreen.c**: Even minimal stubbing caused boot failures
- **Conclusion**: Input subsystem is tightly coupled and risky to modify

### Finding Already-Stubbed Code
Many debug functions are already stubbed from previous sessions:
- show_state_filter (kernel/sched/core.c)
- show_one_workqueue, show_all_workqueues (kernel/workqueue.c)
- print_fatal_signal (kernel/signal.c)
- dump_page (mm/debug.c)
- show_free_areas (mm/page_alloc.c)
- show_opcodes, show_ip (arch/x86/kernel/dumpstack.c)

## Key Findings

### What Works
- Stubbing individual debug/show/dump functions in core subsystems
- Functions that print diagnostic information for debugging
- Stack trace and register dump functions

### What Doesn't Work
- Stubbing entire input subsystem files
- Removing functions that are part of cleanup paths (like input_ff_destroy)
- Config-based optimizations (INPUT_VIVALDIFMAP only saved ~0 bytes)

### Where Code Is Located
By subsystem LOC count:
- kernel/: 64,465 lines
- mm/: 57,643 lines  
- drivers/: 47,634 lines
- arch/x86/: 43,695 lines
- fs/: 31,905 lines
- lib/: 29,958 lines

## Next Steps to Reach Goals

### For bzImage (need 75KB reduction)
Challenge: LTO/compression means LOC reductions don't always translate to size reductions
- Look for large data structures or string tables
- Consider more aggressive stubbing of entire subsystems
- May need to disable more CONFIG options

### For LOC (need ~46,000 lines)
More achievable than bzImage reduction:
- Continue finding debug/show/dump functions in:
  - kernel/irq/ subsystem
  - drivers/base/ (but careful with sysfs)
  - More arch/x86 debug functions
- Look for large functions in:
  - kernel/workqueue.c (4,949 lines)
  - kernel/signal.c (4,061 lines)
  - kernel/fork.c (2,795 lines)

### Strategy
1. Search for "show_", "dump_", "print_debug" patterns
2. Verify functions are only for debugging/introspection
3. Stub one at a time, test boot after each
4. Commit frequently to avoid losing progress

## Lessons Learned
- Input subsystem modifications are high-risk
- Many debug functions already stubbed in previous sessions
- Small, incremental changes are safer than large refactorings
- Boot testing after every change is critical
- Git pre-commit hooks run full builds (takes 4-5 minutes)

## Commands for Next Session
```bash
# Find debug functions
find kernel -name "*.c" | xargs grep -n "^void show_\|^void dump_"

# Check current metrics
ls -lh minified/arch/x86/boot/bzImage
cd minified && cloc --quiet . | grep "SUM:"

# Test boot
./vmtest.tcl
```
