#!/bin/bash
cd /home/user/minimal-kernel-build/minified
for file in access_ok.h barrier.h bitsperlong.h bug.h cacheflush.h compat.h delay.h early_ioremap.h error-injection.h export.h fixmap.h getorder.h hyperv-tlfs.h int-ll64.h ioctl.h io.h irq_regs.h kmap_size.h kprobes.h local64.h memory_model.h mmiowb.h module.h param.h percpu.h pgalloc.h pgtable-nop4d.h pgtable-nopmd.h pgtable-nopud.h pgtable_uffd.h resource.h rwonce.h sections.h set_memory.h softirq_stack.h statfs.h termios.h tlb.h topology.h unaligned.h; do
  count=$(grep -r "asm-generic/$file" --include="*.c" --include="*.h" . 2>/dev/null | wc -l)
  echo "$file: $count"
done
