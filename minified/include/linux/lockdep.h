/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#include <linux/lockdep_types.h>


static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}


# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, i)			do { } while (0)
# define lockdep_set_class(lock, key)		do { (void)(key); } while (0)
# define lockdep_set_class_and_name(lock, key, name) \
		do { (void)(key); (void)(name); } while (0)

# define lockdep_sys_exit() 			do { } while (0)

extern int lockdep_is_held(const void *);

#define lockdep_assert_held(l)			do { (void)(l); } while (0)

#define lockdep_pin_lock(l)			({ struct pin_cookie cookie = { }; cookie; })
#define lockdep_unpin_lock(l, c)		do { (void)(l); (void)(c); } while (0)

#define lock_acquired(lockdep_map, ip) do {} while (0)

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#define LOCK_CONTENDED_RETURN(_lock, try, lock) \
	lock(_lock)



#define SINGLE_DEPTH_NESTING			1


#define lock_acquire_exclusive(l, s, t, n, i)		lock_acquire(l, s, t, 0, 1, n, i)
#define lock_acquire_shared(l, s, t, n, i)		lock_acquire(l, s, t, 1, 1, n, i)

#define spin_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define spin_release(l, i)			lock_release(l, i)

#define mutex_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define mutex_release(l, i)			lock_release(l, i)

#define rwsem_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define rwsem_acquire_read(l, s, t, i)		lock_acquire_shared(l, s, t, NULL, i)
#define rwsem_release(l, i)			lock_release(l, i)


# define might_lock(lock) do { } while (0)

# define lockdep_assert_irqs_enabled() do { } while (0)
# define lockdep_assert_irqs_disabled() do { } while (0)

# define lockdep_assert_preemption_disabled() do { } while (0)

#endif  
