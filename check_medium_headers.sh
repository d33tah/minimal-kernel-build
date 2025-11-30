#!/bin/bash
cd /home/user/minimal-kernel-build/minified

echo "=== Medium-sized linux headers usage (50-150 lines) ==="
for file in iocontext.h objtool.h tty_flip.h pageblock-flags.h kfifo.h selection.h sizes.h task_io_accounting_ops.h delayacct.h futex.h cache.h freezer.h profile.h timerqueue.h tracepoint-defs.h kernel_read_file.h page_table_check.h shrinker.h vm_event_item.h alarmtimer.h debug_locks.h dev_printk.h io_uring.h ratelimit.h rculist_bl.h tty_buffer.h acct.h; do
  count=$(grep -r "linux/$file" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
  if [ "$count" -eq 0 ]; then
    lines=$(wc -l < "include/linux/$file")
    echo "$file: $count uses ($lines lines) ***UNUSED***"
  fi
done
