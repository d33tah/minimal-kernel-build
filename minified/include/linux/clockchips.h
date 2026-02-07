#ifndef _LINUX_CLOCKCHIPS_H
#define _LINUX_CLOCKCHIPS_H


# include <linux/clocksource.h>
# include <linux/cpumask.h>
# include <linux/ktime.h>
# include <linux/notifier.h>

struct clock_event_device;
struct module;

enum clock_event_state {
	CLOCK_EVT_STATE_DETACHED,
	CLOCK_EVT_STATE_SHUTDOWN,
	CLOCK_EVT_STATE_PERIODIC,
	CLOCK_EVT_STATE_ONESHOT,
	CLOCK_EVT_STATE_ONESHOT_STOPPED,
};

# define CLOCK_EVT_FEAT_PERIODIC	0x000001
# define CLOCK_EVT_FEAT_ONESHOT		0x000002
# define CLOCK_EVT_FEAT_KTIME		0x000004
/* CLOCK_EVT_FEAT_C3STOP removed - unused */
# define CLOCK_EVT_FEAT_DUMMY		0x000010

struct clock_event_device {
	/* event_handler callback removed - never invoked */
	int			(*set_next_event)(unsigned long evt, struct clock_event_device *);
	int			(*set_next_ktime)(ktime_t expires, struct clock_event_device *);
	ktime_t			next_event;
	u64			max_delta_ns;
	u64			min_delta_ns;
	u32			mult;
	u32			shift;
	enum clock_event_state	state_use_accessors;
	unsigned int		features;
	/* retries removed - only incremented, never read */
	int			(*set_state_periodic)(struct clock_event_device *);
	int			(*set_state_oneshot)(struct clock_event_device *);
	int			(*set_state_oneshot_stopped)(struct clock_event_device *);
	int			(*set_state_shutdown)(struct clock_event_device *);
	/* tick_resume, broadcast, suspend, resume removed - never called */
	/* min_delta_ticks, max_delta_ticks removed - only set by clockevents_config_and_register which was removed */

	const char		*name;
	int			rating;
	int			irq;
	/* bound_on removed - never accessed */
	const struct cpumask	*cpumask;
	struct list_head	list;
	struct module		*owner;
} ____cacheline_aligned;

/* clockevent_state_detached, clockevent_state_shutdown,
   clockevent_state_periodic, clockevent_state_oneshot,
   clockevents_register_device, clockevents_config_and_register,
   clockevents_calc_mult_shift removed - entire clockevents subsystem dead */



#endif  
