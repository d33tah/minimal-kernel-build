#ifndef __LINUX_SMP_TYPES_H
#define __LINUX_SMP_TYPES_H

#include <linux/llist.h>

/*
 * CSD and IRQ_WORK flag enum removed: every member (CSD_FLAG_LOCK,
 * IRQ_WORK_PENDING, IRQ_WORK_BUSY, IRQ_WORK_LAZY, IRQ_WORK_HARD_IRQ,
 * IRQ_WORK_CLAIMED, CSD_TYPE_ASYNC, CSD_TYPE_SYNC, CSD_TYPE_IRQ_WORK,
 * CSD_TYPE_TTWU, CSD_FLAG_TYPE_MASK) is 0-ref tree-wide. The smp/irq_work
 * cross-CPU call machinery (kernel/smp.c, kernel/irq_work.c) is absent on
 * this !SMP build. The two includers (smp.h, irq_work.h) name none of these.
 * Kept the llist.h include for transitive consumers.
 */

#endif
