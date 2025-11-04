# Minimal Kernel Build - Current Status

## Measurements (Nov 4, 2025)

### bzImage Size
- **Current**: 606,176 bytes
- **Target**: <600,000 bytes
- **Remaining**: 6,176 bytes (1.03% over)
- **Status**: 🟡 Very close to target

### Lines of Code (using cloc)
- **Current**: 394,974 lines
- **Target**: ≤380,000 lines  
- **Remaining**: 14,974 lines (3.9% over)
- **Status**: 🟡 Very close to target

### Boot Test
- **Status**: ✅ PASSES
- **Output**: "Hello, World!" and "Still alive" displayed correctly

## Progress Summary

### Optimizations Applied
1. ✅ Removed blake2s test vectors (577 LOC, ~5KB bzImage saved)
2. ✅ Previously stubbed debug functions (dump_page, etc.)
3. ✅ Configured with tinyconfig + LTO_CLANG_FULL

### Key Findings
1. **PERF_EVENTS cannot be disabled** - hardcoded requirement in arch/x86/Kconfig
   - kernel/events/core.c alone: 7,869 LOC
   - Attempting to disable causes link errors for required symbols

2. **Header dependencies** prevent easy subsystem deletion
   - include/net, include/trace, include/kunit all needed by core headers
   - Cannot simply delete unused directories

3. **Most code is essential** for minimal boot
   - mm/, kernel/, fs/, drivers/tty/ all actively used
   - Aggressive stubbing risks breaking boot functionality

## Breakdown by Component (cloc)

```
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C                              528          49251          83944         222547
C/C++ Header                  1437          38420          74539         168984
Assembly                        39           1043           2668           3443
-------------------------------------------------------------------------------
SUM:                          2004          88714         161151         394974
-------------------------------------------------------------------------------
```

## Largest Files (LOC via cloc)

1. kernel/events/core.c - 7,869 lines (perf events, cannot disable)
2. drivers/tty/vt/vt.c - 3,398 lines (VT console, needed for output)
3. fs/namei.c - 3,338 lines (path lookup, core filesystem)
4. kernel/workqueue.c - 2,550 lines (work queues, core kernel)
5. lib/vsprintf.c - 2,280 lines (printf formatting)
6. drivers/tty/vt/keyboard.c - 1,601 lines (keyboard input)
7. lib/iov_iter.c - 1,596 lines (I/O vector iteration)
8. lib/xarray.c - 1,253 lines (extensible arrays)

## Next Steps to Reach Goals

### For bzImage (need 6-12KB reduction)
- Find more test data or debug strings
- Stub non-essential formatting in lib/vsprintf.c
- Minimize defkeymap.c key tables if safe
- Look for unused config options to disable

### For LOC (need ~15,000 lines)
- More aggressive stubbing of debug/ioctl functions
- Minimize VT console features (vt_ioctl.c - 735 LOC)
- Reduce lib/vsprintf.c complexity
- Consider stubbing less-used keyboard handlers

## Build Configuration

```bash
cd minified
make LLVM=1 tinyconfig
make LLVM=1 -j$(nproc)
```

**Important**: Must use LLVM=1 (clang) - GCC builds fail to boot

## Test Command

```bash
./vmtest.tcl
```

Expected output: "Hello, World!" and "Still alive" messages

