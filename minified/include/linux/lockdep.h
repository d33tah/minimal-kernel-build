/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#define lock_acquire(l, s, t, r, c, n, i) \
	do {                              \
	} while (0)

extern int lockdep_is_held(const void *);

#define lockdep_assert_held(l) \
	do {                   \
		(void)(l);     \
	} while (0)

#endif
