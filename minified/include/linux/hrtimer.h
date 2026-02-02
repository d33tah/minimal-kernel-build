#ifndef _LINUX_HRTIMER_H
#define _LINUX_HRTIMER_H


#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/seqlock.h>
#include <linux/timer.h>
/* timerqueue.h inlined */
struct timerqueue_node { struct rb_node node; ktime_t expires; };
struct timerqueue_head { struct rb_root_cached rb_root; };

struct hrtimer_clock_base;
struct hrtimer_cpu_base;

/* enum hrtimer_mode removed - never used (~20 LOC) */

enum hrtimer_restart {
	HRTIMER_NORESTART,	 
	HRTIMER_RESTART,	 
};

struct hrtimer {
	struct timerqueue_node		node;
	/* _softexpires removed - never accessed (timers are stubbed) */
	enum hrtimer_restart		(*function)(struct hrtimer *);
	struct hrtimer_clock_base	*base;
	u8				state;
	/* is_rel, is_soft, is_hard removed - never accessed */
};

/* __hrtimer_clock_base_align was empty macro, removed */

struct hrtimer_clock_base {
	struct hrtimer_cpu_base	*cpu_base;
	unsigned int		index;
	clockid_t		clockid;
	seqcount_raw_spinlock_t	seq;
	struct hrtimer		*running;
	struct timerqueue_head	active;
	ktime_t			(*get_time)(void);
	ktime_t			offset;
};

enum  hrtimer_base_type {
	HRTIMER_BASE_MONOTONIC,
	HRTIMER_BASE_REALTIME,
	HRTIMER_BASE_BOOTTIME,
	HRTIMER_BASE_TAI,
	/* *_SOFT entries removed - never used by name */
	HRTIMER_MAX_CLOCK_BASES,
};

struct hrtimer_cpu_base {
	raw_spinlock_t			lock;
	unsigned int			cpu;
	/* active_bases, clock_was_set_seq removed - never accessed */
	unsigned int			hres_active		: 1,
					in_hrtirq		: 1;
					/* hang_detected, softirq_activated removed - never accessed */
	ktime_t				expires_next;
	struct hrtimer			*next_timer;
	/* softirq_expires_next, softirq_next_timer removed - never accessed */
	struct hrtimer_clock_base	clock_base[HRTIMER_MAX_CLOCK_BASES];
} ____cacheline_aligned;


/* hrtimer_set_expires_range_ns, hrtimer_get_expires, hrtimer_get_expires_tv64,
   hrtimer_get_softexpires_tv64, hrtimer_cancel_wait_running, hrtimer_resolution removed - unused */

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);


/* hrtimer_init, hrtimer_start_range_ns, hrtimer_cancel, hrtimer_active removed - never called */

/* hrtimer_callback_running removed - no callers */

extern void hrtimer_run_queues(void);

extern void __init hrtimers_init(void);


/* hrtimers_prepare_cpu, hrtimers_dead_cpu removed - CPU hotplug disabled */

#endif
