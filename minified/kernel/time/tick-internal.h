 
 
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include "timekeeping.h"

/* --- 2025-12-08 02:15 --- Inlined from tick-sched.h */
enum tick_device_mode {
	TICKDEV_MODE_PERIODIC,
	TICKDEV_MODE_ONESHOT,
};

struct tick_device {
	struct clock_event_device *evtdev;
	enum tick_device_mode mode;
};

enum tick_nohz_mode {
	NOHZ_MODE_INACTIVE,
	NOHZ_MODE_LOWRES,
	NOHZ_MODE_HIGHRES,
};

struct tick_sched {
	struct hrtimer			sched_timer;
	unsigned long			check_clocks;
	enum tick_nohz_mode		nohz_mode;

	unsigned int			inidle		: 1;
	unsigned int			tick_stopped	: 1;
	unsigned int			idle_active	: 1;
	unsigned int			do_timer_last	: 1;
	unsigned int			got_idle_tick	: 1;

	ktime_t				last_tick;
	ktime_t				next_tick;
	unsigned long			idle_jiffies;
	unsigned long			idle_calls;
	unsigned long			idle_sleeps;
	ktime_t				idle_entrytime;
	ktime_t				idle_waketime;
	ktime_t				idle_exittime;
	ktime_t				idle_sleeptime;
	ktime_t				iowait_sleeptime;
	unsigned long			last_jiffies;
	u64				timer_expires;
	u64				timer_expires_base;
	u64				next_timer;
	ktime_t				idle_expires;
	atomic_t			tick_dep_mask;
	unsigned long			last_tick_jiffies;
	unsigned int			stalled_jiffies;
};

extern struct tick_sched *tick_get_tick_sched(int cpu);

extern void tick_setup_sched_timer(void);
static inline void tick_cancel_sched_timer(int cpu) { }

static inline int
__tick_broadcast_oneshot_control(enum tick_broadcast_state state)
{
	return -EBUSY;
}


# define TICK_DO_TIMER_NONE	-1
# define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern int tick_do_timer_cpu __read_mostly;

extern void tick_setup_periodic(struct clock_event_device *dev, int broadcast);
extern void tick_handle_periodic(struct clock_event_device *dev);
extern void tick_check_new_device(struct clock_event_device *dev);
extern void tick_shutdown(unsigned int cpu);
/* tick_suspend, tick_resume removed - unused */
extern bool tick_check_replacement(struct clock_event_device *curdev,
				   struct clock_event_device *newdev);
extern void tick_install_replacement(struct clock_event_device *dev);
extern struct tick_device *tick_get_device(int cpu);

extern int clockevents_tick_resume(struct clock_event_device *dev);
 
static inline int tick_device_is_functional(struct clock_event_device *dev)
{
	return !(dev->features & CLOCK_EVT_FEAT_DUMMY);
}

static inline enum clock_event_state clockevent_get_state(struct clock_event_device *dev)
{
	return dev->state_use_accessors;
}

static inline void clockevent_set_state(struct clock_event_device *dev,
					enum clock_event_state state)
{
	dev->state_use_accessors = state;
}

extern void clockevents_shutdown(struct clock_event_device *dev);
extern void clockevents_exchange_device(struct clock_event_device *old,
					struct clock_event_device *new);
extern void clockevents_switch_state(struct clock_event_device *dev,
				     enum clock_event_state state);
extern int clockevents_program_event(struct clock_event_device *dev,
				     ktime_t expires, bool force);
extern void clockevents_handle_noop(struct clock_event_device *dev);
extern ssize_t sysfs_get_uname(const char *buf, char *dst, size_t cnt);

 
static inline void tick_install_broadcast_device(struct clock_event_device *dev, int cpu) { }
static inline int tick_is_broadcast_device(struct clock_event_device *dev) { return 0; }
static inline int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu) { return 0; }
/* Removed uncalled: tick_do_periodic_broadcast, tick_suspend_broadcast,
 * tick_resume_broadcast, tick_resume_check_broadcast, tick_broadcast_update_freq */
static inline void tick_broadcast_init(void) { }

 
static inline void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast)
{
	dev->event_handler = tick_handle_periodic;
}


 
static inline
void tick_setup_oneshot(struct clock_event_device *newdev,
			void (*handler)(struct clock_event_device *),
			ktime_t nextevt) { BUG(); }
/* Removed uncalled: tick_resume_oneshot */
static inline int tick_program_event(ktime_t expires, int force) { return 0; }
static inline void tick_oneshot_notify(void) { }
/* Removed uncalled: tick_oneshot_possible */
static inline int tick_oneshot_mode_active(void) { return 0; }
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }

/* Removed uncalled: tick_broadcast_switch_to_oneshot, tick_check_oneshot_broadcast_this_cpu,
 * tick_broadcast_oneshot_available, tick_broadcast_offline */
static inline int tick_broadcast_oneshot_active(void) { return 0; }

static inline void tick_nohz_init(void) { }
/* Removed uncalled: timers_update_nohz */
#define tick_nohz_active (0)

DECLARE_PER_CPU(struct hrtimer_cpu_base, hrtimer_bases);

extern u64 get_next_timer_interrupt(unsigned long basej, u64 basem);
void timer_clear_idle(void);

#define CLOCK_SET_WALL							\
	(BIT(HRTIMER_BASE_REALTIME) | BIT(HRTIMER_BASE_REALTIME_SOFT) |	\
	 BIT(HRTIMER_BASE_TAI) | BIT(HRTIMER_BASE_TAI_SOFT))

#define CLOCK_SET_BOOT							\
	(BIT(HRTIMER_BASE_BOOTTIME) | BIT(HRTIMER_BASE_BOOTTIME_SOFT))

void clock_was_set(unsigned int bases);
void clock_was_set_delayed(void);
/* hrtimers_resume_local removed - unused */

 
#if HZ < 34
#define JIFFIES_SHIFT	6
#elif HZ < 67
#define JIFFIES_SHIFT	7
#else
#define JIFFIES_SHIFT	8
#endif
