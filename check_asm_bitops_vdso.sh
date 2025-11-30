#!/bin/bash
cd /home/user/minimal-kernel-build/minified

echo "=== bitops directory ==="
for file in const_hweight.h fls64.h instrumented-atomic.h instrumented-lock.h instrumented-non-atomic.h sched.h; do
  count=$(grep -r "asm-generic/bitops/$file" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
  echo "$file: $count"
done

echo ""
echo "=== vdso directory ==="
count=$(grep -r "asm-generic/vdso/vsyscall.h" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
echo "vsyscall.h: $count"
