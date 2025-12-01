#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>
#if defined(CONFIG_HAVE_NMI_WATCHDOG)
#include <asm/nmi.h>
#endif

static inline void lockup_detector_init(void) { }
static inline void lockup_detector_soft_poweroff(void) { }
static inline void lockup_detector_cleanup(void) { }

static inline void touch_softlockup_watchdog_sched(void) { }
static inline void touch_softlockup_watchdog(void) { }
static inline void touch_softlockup_watchdog_sync(void) { }
static inline void touch_all_softlockup_watchdogs(void) { }

#define lockup_detector_online_cpu	NULL
#define lockup_detector_offline_cpu	NULL

static inline void reset_hung_task_detector(void) { }

#define NMI_WATCHDOG_ENABLED_BIT   0
#define SOFT_WATCHDOG_ENABLED_BIT  1
#define NMI_WATCHDOG_ENABLED      (1 << NMI_WATCHDOG_ENABLED_BIT)
#define SOFT_WATCHDOG_ENABLED     (1 << SOFT_WATCHDOG_ENABLED_BIT)

static inline void hardlockup_detector_disable(void) {}

#if defined(CONFIG_HAVE_NMI_WATCHDOG) || defined(CONFIG_HARDLOCKUP_DETECTOR)
# define NMI_WATCHDOG_SYSCTL_PERM	0644
#else
# define NMI_WATCHDOG_SYSCTL_PERM	0444
#endif

static inline void hardlockup_detector_perf_stop(void) { }
static inline void hardlockup_detector_perf_restart(void) { }
static inline void hardlockup_detector_perf_disable(void) { }
static inline void hardlockup_detector_perf_enable(void) { }
static inline void hardlockup_detector_perf_cleanup(void) { }
# if !defined(CONFIG_HAVE_NMI_WATCHDOG)
static inline int hardlockup_detector_perf_init(void) { return -ENODEV; }
static inline void arch_touch_nmi_watchdog(void) {}
# else
static inline int hardlockup_detector_perf_init(void) { return 0; }
# endif


static inline void touch_nmi_watchdog(void)
{
	arch_touch_nmi_watchdog();
	touch_softlockup_watchdog();
}

#ifdef arch_trigger_cpumask_backtrace
static inline bool trigger_all_cpu_backtrace(void)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, false);
	return true;
}

static inline bool trigger_allbutself_cpu_backtrace(void)
{
	arch_trigger_cpumask_backtrace(cpu_online_mask, true);
	return true;
}

static inline bool trigger_cpumask_backtrace(struct cpumask *mask)
{
	arch_trigger_cpumask_backtrace(mask, false);
	return true;
}

static inline bool trigger_single_cpu_backtrace(int cpu)
{
	arch_trigger_cpumask_backtrace(cpumask_of(cpu), false);
	return true;
}

void nmi_trigger_cpumask_backtrace(const cpumask_t *mask,
				   bool exclude_self,
				   void (*raise)(cpumask_t *mask));
bool nmi_cpu_backtrace(struct pt_regs *regs);

#else
static inline bool trigger_all_cpu_backtrace(void)
{
	return false;
}
static inline bool trigger_allbutself_cpu_backtrace(void)
{
	return false;
}
static inline bool trigger_cpumask_backtrace(struct cpumask *mask)
{
	return false;
}
static inline bool trigger_single_cpu_backtrace(int cpu)
{
	return false;
}
#endif


#if defined(CONFIG_HARDLOCKUP_CHECK_TIMESTAMP) && \
    defined(CONFIG_HARDLOCKUP_DETECTOR)
void watchdog_update_hrtimer_threshold(u64 period);
#else
static inline void watchdog_update_hrtimer_threshold(u64 period) { }
#endif

struct ctl_table;
int proc_watchdog(struct ctl_table *, int, void *, size_t *, loff_t *);
int proc_nmi_watchdog(struct ctl_table *, int , void *, size_t *, loff_t *);
int proc_soft_watchdog(struct ctl_table *, int , void *, size_t *, loff_t *);
int proc_watchdog_thresh(struct ctl_table *, int , void *, size_t *, loff_t *);
int proc_watchdog_cpumask(struct ctl_table *, int, void *, size_t *, loff_t *);


#endif
