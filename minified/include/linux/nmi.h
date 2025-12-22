#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>

static inline void lockup_detector_init(void) { }
static inline void touch_softlockup_watchdog(void) { }
static inline void arch_touch_nmi_watchdog(void) {}

/* arch_touch_nmi_watchdog and touch_softlockup_watchdog are stubs */
static inline void touch_nmi_watchdog(void) { }

static inline bool trigger_all_cpu_backtrace(void)
{
	return false;
}

#endif
