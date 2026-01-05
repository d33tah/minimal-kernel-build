#ifndef _LINUX_HRTIMER_H
#define _LINUX_HRTIMER_H


#include <linux/rbtree.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/percpu.h>
#include <linux/seqlock.h>
#include <linux/timer.h>
#include <linux/timerqueue.h>

struct hrtimer_clock_base;
struct hrtimer_cpu_base;

enum hrtimer_mode {
	HRTIMER_MODE_ABS	= 0x00,
	HRTIMER_MODE_REL	= 0x01,
	HRTIMER_MODE_PINNED	= 0x02,
	HRTIMER_MODE_SOFT	= 0x04,
	HRTIMER_MODE_HARD	= 0x08,

	HRTIMER_MODE_ABS_PINNED = HRTIMER_MODE_ABS | HRTIMER_MODE_PINNED,
	HRTIMER_MODE_REL_PINNED = HRTIMER_MODE_REL | HRTIMER_MODE_PINNED,

	HRTIMER_MODE_ABS_SOFT	= HRTIMER_MODE_ABS | HRTIMER_MODE_SOFT,
	HRTIMER_MODE_REL_SOFT	= HRTIMER_MODE_REL | HRTIMER_MODE_SOFT,

	HRTIMER_MODE_ABS_PINNED_SOFT = HRTIMER_MODE_ABS_PINNED | HRTIMER_MODE_SOFT,
	HRTIMER_MODE_REL_PINNED_SOFT = HRTIMER_MODE_REL_PINNED | HRTIMER_MODE_SOFT,

	HRTIMER_MODE_ABS_HARD	= HRTIMER_MODE_ABS | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_HARD	= HRTIMER_MODE_REL | HRTIMER_MODE_HARD,

	HRTIMER_MODE_ABS_PINNED_HARD = HRTIMER_MODE_ABS_PINNED | HRTIMER_MODE_HARD,
	HRTIMER_MODE_REL_PINNED_HARD = HRTIMER_MODE_REL_PINNED | HRTIMER_MODE_HARD,
};

enum hrtimer_restart {
	HRTIMER_NORESTART,	 
	HRTIMER_RESTART,	 
};

struct hrtimer {
	struct timerqueue_node		node;
	ktime_t				_softexpires;
	enum hrtimer_restart		(*function)(struct hrtimer *);
	struct hrtimer_clock_base	*base;
	u8				state;
	/* is_rel, is_soft, is_hard removed - never accessed */
};

# define __hrtimer_clock_base_align

struct hrtimer_clock_base {
	struct hrtimer_cpu_base	*cpu_base;
	unsigned int		index;
	clockid_t		clockid;
	seqcount_raw_spinlock_t	seq;
	struct hrtimer		*running;
	struct timerqueue_head	active;
	ktime_t			(*get_time)(void);
	ktime_t			offset;
} __hrtimer_clock_base_align;

enum  hrtimer_base_type {
	HRTIMER_BASE_MONOTONIC,
	HRTIMER_BASE_REALTIME,
	HRTIMER_BASE_BOOTTIME,
	HRTIMER_BASE_TAI,
	HRTIMER_BASE_MONOTONIC_SOFT,
	HRTIMER_BASE_REALTIME_SOFT,
	HRTIMER_BASE_BOOTTIME_SOFT,
	HRTIMER_BASE_TAI_SOFT,
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
   hrtimer_get_softexpires_tv64, hrtimer_cancel_wait_running removed - unused */

#define hrtimer_resolution	(unsigned int)LOW_RES_NSEC

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);


/* hrtimer_init, hrtimer_start_range_ns, hrtimer_cancel, hrtimer_active removed - never called */

/* hrtimer_callback_running removed - no callers */

extern void hrtimer_run_queues(void);

extern void __init hrtimers_init(void);


int hrtimers_prepare_cpu(unsigned int cpu);
#define hrtimers_dead_cpu	NULL

#endif
