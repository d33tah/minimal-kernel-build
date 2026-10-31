/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}

#define lock_acquire(l, s, t, r, c, n, i) \
	do {                              \
	} while (0)

#define lockdep_sys_exit() \
	do {               \
	} while (0)

extern int lockdep_is_held(const void *);

#define lockdep_assert_held(l) \
	do {                   \
		(void)(l);     \
	} while (0)

#define lockdep_assert_irqs_enabled() \
	do {                          \
	} while (0)
#define lockdep_assert_irqs_disabled() \
	do {                           \
	} while (0)

#define lockdep_assert_preemption_disabled() \
	do {                                 \
	} while (0)

#endif
