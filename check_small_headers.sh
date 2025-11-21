#!/bin/bash
cd /home/user/minimal-kernel-build/minified

echo "=== Small linux headers usage ==="
for file in compiler-version.h static_key.h user.h hidden.h task_io_accounting.h aio_abi.h blkzoned.h membarrier.h ppp-ioctl.h rseq.h utime.h cgroupstats.h securebits.h auxvec.h gcd.h kbd_diacr.h screen_info.h elfnote-lto.h elf-randomize.h lcm.h edd.h mqueue.h clocksource_ids.h errname.h errseq.h hugetlb_inline.h panic_notifier.h dirent.h fault-inject-usercopy.h; do
  count=$(grep -r "linux/$file" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
  if [ "$count" -eq 0 ]; then
    lines=$(wc -l < "include/linux/$file")
    echo "$file: $count uses ($lines lines) ***UNUSED***"
  fi
done
