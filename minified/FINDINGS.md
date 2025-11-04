# Kernel Minification Findings

## Current Status (After Investigation)
- **Baseline bzImage size**: 615,184 bytes (target: <590,000) - Need to reduce by 25,184 bytes
- **Baseline LOC**: 633,022 (assuming target ~380,000 based on previous work) - Need to reduce by ~253,000 lines
- **Boot test**: Not yet tested in this session

## Attempted Approaches

### Attempt 1: Disable CONFIG_PERF_EVENTS

#### What Was Tried
1. Disabled CONFIG_PERF_EVENTS in .config
2. Modified Makefiles (arch/x86/events/Makefile, kernel/events/Makefile) to make perf events conditional
3. Rebuilt kernel

#### Why It Failed
- The kernel's Makefiles had `obj-y` (always compile) for perf events code
- Simply changing to `obj-$(CONFIG_PERF_EVENTS)` triggered build system issues
- Build completed compilation but did not generate final bzImage
- Perf events code was still being compiled despite CONFIG being disabled

### Attempt 2: Disable CONFIG_VT (Virtual Terminal)

#### What Was Tried
1. Disabled CONFIG_VT and CONFIG_VT_CONSOLE in .config
2. Ran make olddefconfig to resolve dependencies
3. Built kernel

#### Why It Failed
- Linker errors: undefined symbols (conswitchp, vga_con, vty_init, unblank_screen, console_driver, fg_console)
- These VT symbols are referenced by other kernel code (setup_arch, tty_init, oops_end, panic, bust_spinlocks, tty_lookup_driver)
- Even with CONFIG_VT disabled, other parts of the kernel still expect these symbols to exist
- **Result**: Cannot disable CONFIG_VT without breaking the build

## Challenges Discovered

### 1. Build System Complexity
The Linux kernel build system has deep interdependencies:
- Changing Makefiles can break the build in unexpected ways
- Some code is always compiled regardless of CONFIG settings
- `obj-y` vs `obj-$(CONFIG_X)` isn't always straightforward

### 2. Tightly Coupled Dependencies
From previous FINDINGS.md (now deleted):
- Core kernel code references perf functions even when CONFIG_PERF_EVENTS is disabled
- VT/console code is referenced by tty_io.c and drivers/char/mem.c
- Filesystem functions are referenced across many subsystems
- Library functions (kfifo, xarray, etc.) are used by many drivers

### 3. Source Stubbing Approach Failed
Previous attempts to stub source files (replacing functions with empty stubs) broke builds because:
- Header files expect certain function signatures
- Link-time dependencies still exist
- Kernel has both compile-time and runtime dependencies

## Conclusions

### What Works
- Disabling CON FIG options in .config (if done correctly and completely)
- The kernel successfully builds with the current minimal tinyconfig

### What Doesn't Work (Without Major Surgery)
- Aggressive source file stubbing - breaks too many dependencies
- Simple Makefile changes - kernel build system is complex
  - Attempted to change `obj-y` to `obj-$(CONFIG_PERF_EVENTS)` in kernel/events/Makefile and arch/x86/events/Makefile
  - Build completed compilation but stopped at archiving stage without creating bzImage
  - **REVERTED**: Makefile changes were reverted as they broke the build
- Disabling CONFIG_PERF_EVENTS alone - requires more comprehensive changes

### Realistic Path Forward
To achieve the size/LOC goals would require:
1. **Comprehensive CONFIG analysis**: Find multiple small CONFIG options that can be safely disabled
2. **Iterative testing**: Test each change individually with build + boot test
3. **Header stub approach**: Instead of stubbing source files, provide proper stub headers that satisfy dependencies
4. **Acceptance**: The targets may be unrealistic for a bootable kernel - current metrics may represent practical minimum

## Recommendations
1. Focus on CONFIG-based reductions (cleaner, safer)
2. Test each change incrementally (build + boot)
3. Document why each CONFIG was disabled
4. Accept that some LOC/size is necessary for basic functionality
5. Consider whether boot testing is passing with current config before making further changes
