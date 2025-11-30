#!/bin/bash
cd /home/user/minimal-kernel-build/minified

echo "=== uapi/asm-generic headers ==="
for file in bpf_perf_event.h errno-base.h errno.h fcntl.h int-ll64.h ioctl.h mman.h resource.h sockios.h statfs.h termios.h types.h; do
  count=$(grep -r "uapi/asm-generic/$file" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
  echo "$file: $count"
done
