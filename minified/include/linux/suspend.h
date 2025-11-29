 
#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H

#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/mm.h>
#include <linux/freezer.h>
#include <asm/errno.h>

extern void pm_set_vt_switch(int);

static inline void pm_prepare_console(void)
{
}

static inline void pm_restore_console(void)
{
}

typedef int __bitwise suspend_state_t;

#define PM_SUSPEND_ON		((__force suspend_state_t) 0)
#define PM_SUSPEND_TO_IDLE	((__force suspend_state_t) 1)
#define PM_SUSPEND_STANDBY	((__force suspend_state_t) 2)
#define PM_SUSPEND_MEM		((__force suspend_state_t) 3)
#define PM_SUSPEND_MIN		PM_SUSPEND_TO_IDLE
#define PM_SUSPEND_MAX		((__force suspend_state_t) 4)

enum suspend_stat_step {
	SUSPEND_FREEZE = 1,
};

struct suspend_stats {
	int	success;
	int	fail;
	int	failed_freeze;
	int	failed_prepare;
	int	failed_suspend;
	int	failed_suspend_late;
	int	failed_suspend_noirq;
	int	failed_resume;
	int	failed_resume_early;
	int	failed_resume_noirq;
#define	REC_FAILED_NUM	2
	int	last_failed_dev;
	char	failed_devs[REC_FAILED_NUM][40];
	int	last_failed_errno;
	int	errno[REC_FAILED_NUM];
	int	last_failed_step;
	enum suspend_stat_step	failed_steps[REC_FAILED_NUM];
};

extern struct suspend_stats suspend_stats;

static inline void dpm_save_failed_dev(const char *name)
{
	strlcpy(suspend_stats.failed_devs[suspend_stats.last_failed_dev],
		name,
		sizeof(suspend_stats.failed_devs[0]));
	suspend_stats.last_failed_dev++;
	suspend_stats.last_failed_dev %= REC_FAILED_NUM;
}

static inline void dpm_save_failed_errno(int err)
{
	suspend_stats.errno[suspend_stats.last_failed_errno] = err;
	suspend_stats.last_failed_errno++;
	suspend_stats.last_failed_errno %= REC_FAILED_NUM;
}

static inline void dpm_save_failed_step(enum suspend_stat_step step)
{
	suspend_stats.failed_steps[suspend_stats.last_failed_step] = step;
	suspend_stats.last_failed_step++;
	suspend_stats.last_failed_step %= REC_FAILED_NUM;
}

 
struct platform_suspend_ops {
	int (*valid)(suspend_state_t state);
	int (*begin)(suspend_state_t state);
	int (*prepare)(void);
	int (*prepare_late)(void);
	int (*enter)(suspend_state_t state);
	void (*wake)(void);
	void (*finish)(void);
	bool (*suspend_again)(void);
	void (*end)(void);
	void (*recover)(void);
};

struct platform_s2idle_ops {
	int (*begin)(void);
	int (*prepare)(void);
	int (*prepare_late)(void);
	bool (*wake)(void);
	void (*restore_early)(void);
	void (*restore)(void);
	void (*end)(void);
};

#define suspend_valid_only_mem	NULL

static inline void pm_suspend_clear_flags(void) {}

static inline int pm_suspend(suspend_state_t state) { return -ENOSYS; }
static inline bool sync_on_suspend_enabled(void) { return true; }
static inline bool idle_should_enter_s2idle(void) { return false; }

 
struct pbe {
	void *address;		 
	void *orig_address;	 
	struct pbe *next;
};

 
extern void mark_free_pages(struct zone *zone);

 
struct platform_hibernation_ops {
	int (*begin)(pm_message_t stage);
	void (*end)(void);
	int (*pre_snapshot)(void);
	void (*finish)(void);
	int (*prepare)(void);
	int (*enter)(void);
	void (*leave)(void);
	int (*pre_restore)(void);
	void (*restore_cleanup)(void);
	void (*recover)(void);
};

static inline void register_nosave_region(unsigned long b, unsigned long e) {}

static inline int hibernate(void) { return -ENOSYS; }
static inline bool system_entering_hibernation(void) { return false; }
static inline bool hibernation_available(void) { return false; }


#define PM_HIBERNATION_PREPARE	0x0001  
#define PM_POST_HIBERNATION	0x0002  
#define PM_SUSPEND_PREPARE	0x0003  
#define PM_POST_SUSPEND		0x0004  
#define PM_RESTORE_PREPARE	0x0005  
#define PM_POST_RESTORE		0x0006  

extern struct mutex system_transition_mutex;


static inline int register_pm_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int unregister_pm_notifier(struct notifier_block *nb)
{
	return 0;
}

#define pm_notifier(fn, pri)	do { (void)(fn); } while (0)

static inline bool pm_wakeup_pending(void) { return false; }

static inline void lock_system_sleep(void) {}
static inline void unlock_system_sleep(void) {}


#define pm_print_times_enabled	(false)
#define pm_debug_messages_on	(false)

#include <linux/printk.h>

#define __pm_pr_dbg(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define __pm_deferred_pr_dbg(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

 
#define pm_pr_dbg(fmt, ...) \
	__pm_pr_dbg(fmt, ##__VA_ARGS__)

#define pm_deferred_pr_dbg(fmt, ...) \
	__pm_deferred_pr_dbg(fmt, ##__VA_ARGS__)


static inline void queue_up_suspend_work(void) {}


#endif  
