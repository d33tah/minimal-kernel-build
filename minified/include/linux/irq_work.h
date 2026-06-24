#ifndef _LINUX_IRQ_WORK_H
#define _LINUX_IRQ_WORK_H

#include <linux/smp_types.h>
#include <linux/rcuwait.h>

/*
 * struct irq_work + IRQ_WORK_INIT/__IRQ_WORK_INIT/DEFINE_IRQ_WORK removed:
 * 0 references tree-wide. The 5 includers (rcu/update.c, sched/sched.h,
 * time/timer.c, printk/printk.c, asm/nmi.h) only #include this header and
 * never name struct irq_work or any of its initializers; the irq_work
 * subsystem (kernel/irq_work.c) is absent from this build. Includes kept
 * for transitive consumers.
 */

#endif
