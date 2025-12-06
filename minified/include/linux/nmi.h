#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>
#if defined(CONFIG_HAVE_NMI_WATCHDOG)
#include <asm/nmi.h>
#endif

static inline void lockup_detector_init(void) { }
static inline void touch_softlockup_watchdog(void) { }

#define lockup_detector_online_cpu	NULL
#define lockup_detector_offline_cpu	NULL

# if !defined(CONFIG_HAVE_NMI_WATCHDOG)
static inline void arch_touch_nmi_watchdog(void) {}
# endif


static inline void touch_nmi_watchdog(void)
{
	arch_touch_nmi_watchdog();
	touch_softlockup_watchdog();
}

static inline bool trigger_all_cpu_backtrace(void)
{
	return false;
}


#endif
