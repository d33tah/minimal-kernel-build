 
 
#include <linux/hrtimer.h>
#include <linux/tick.h>

#include "timekeeping.h"
#include "tick-sched.h"


# define TICK_DO_TIMER_NONE	-1
# define TICK_DO_TIMER_BOOT	-2

DECLARE_PER_CPU(struct tick_device, tick_cpu_device);
extern ktime_t tick_next_period;
extern int tick_do_timer_cpu __read_mostly;

extern void tick_setup_periodic(struct clock_event_device *dev, int broadcast);
extern void tick_handle_periodic(struct clock_event_device *dev);
extern void tick_check_new_device(struct clock_event_device *dev);
extern void tick_shutdown(unsigned int cpu);
extern void tick_suspend(void);
extern void tick_resume(void);
extern bool tick_check_replacement(struct clock_event_device *curdev,
				   struct clock_event_device *newdev);
extern void tick_install_replacement(struct clock_event_device *dev);
extern int tick_is_oneshot_available(void);
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
extern int __clockevents_update_freq(struct clock_event_device *dev, u32 freq);
extern ssize_t sysfs_get_uname(const char *buf, char *dst, size_t cnt);

 
static inline void tick_install_broadcast_device(struct clock_event_device *dev, int cpu) { }
static inline int tick_is_broadcast_device(struct clock_event_device *dev) { return 0; }
static inline int tick_device_uses_broadcast(struct clock_event_device *dev, int cpu) { return 0; }
static inline void tick_do_periodic_broadcast(struct clock_event_device *d) { }
static inline void tick_suspend_broadcast(void) { }
static inline void tick_resume_broadcast(void) { }
static inline bool tick_resume_check_broadcast(void) { return false; }
static inline void tick_broadcast_init(void) { }
static inline int tick_broadcast_update_freq(struct clock_event_device *dev, u32 freq) { return -ENODEV; }

 
static inline void tick_set_periodic_handler(struct clock_event_device *dev, int broadcast)
{
	dev->event_handler = tick_handle_periodic;
}


 
static inline
void tick_setup_oneshot(struct clock_event_device *newdev,
			void (*handler)(struct clock_event_device *),
			ktime_t nextevt) { BUG(); }
static inline void tick_resume_oneshot(void) { BUG(); }
static inline int tick_program_event(ktime_t expires, int force) { return 0; }
static inline void tick_oneshot_notify(void) { }
static inline bool tick_oneshot_possible(void) { return false; }
static inline int tick_oneshot_mode_active(void) { return 0; }
static inline void tick_clock_notify(void) { }
static inline int tick_check_oneshot_change(int allow_nohz) { return 0; }

 
static inline void tick_broadcast_switch_to_oneshot(void) { }
static inline int tick_broadcast_oneshot_active(void) { return 0; }
static inline void tick_check_oneshot_broadcast_this_cpu(void) { }
static inline bool tick_broadcast_oneshot_available(void) { return tick_oneshot_possible(); }

static inline void tick_broadcast_offline(unsigned int cpu) { }

 
static inline void tick_nohz_init(void) { }

static inline void timers_update_nohz(void) { }
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

void hrtimers_resume_local(void);

 
#if HZ < 34
#define JIFFIES_SHIFT	6
#elif HZ < 67
#define JIFFIES_SHIFT	7
#else
#define JIFFIES_SHIFT	8
#endif
