/* Minimal lockdep.h - stubs for !CONFIG_LOCKDEP */
#ifndef __LINUX_LOCKDEP_H
#define __LINUX_LOCKDEP_H

#include <linux/lockdep_types.h>

struct task_struct;


static inline void lockdep_init_task(struct task_struct *task)
{
}

static inline void lockdep_off(void)
{
}

static inline void lockdep_on(void)
{
}


# define lock_acquire(l, s, t, r, c, n, i)	do { } while (0)
# define lock_release(l, i)			do { } while (0)
# define lockdep_init()				do { } while (0)
# define lockdep_init_map_type(lock, name, key, sub, inner, outer, type) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_init_map_waits(lock, name, key, sub, inner, outer) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_init_map_wait(lock, name, key, sub, inner) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_init_map(lock, name, key, sub) \
		do { (void)(name); (void)(key); } while (0)
# define lockdep_set_class(lock, key)		do { (void)(key); } while (0)
# define lockdep_set_class_and_name(lock, key, name) \
		do { (void)(key); (void)(name); } while (0)
/* lockdep_set_subclass removed - unused */

#define lockdep_set_novalidate_class(lock) do { } while (0)


# define lockdep_sys_exit() 			do { } while (0)

/* lock_is_held, lockdep_is_held are never defined but used in
   RCU_LOCKDEP_WARN conditions (which are no-ops but still need valid syntax) */
#define lock_is_held(l)			(1)
#define lockdep_is_held(l)		(1)
#define lockdep_is_held_type(l, r)		(1)

/* lockdep_assert removed - unused */

#define lockdep_assert_held(l)			do { (void)(l); } while (0)
#define lockdep_assert_not_held(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_write(l)		do { (void)(l); } while (0)
#define lockdep_assert_held_read(l)		do { (void)(l); } while (0)

#define lockdep_pin_lock(l)			({ struct pin_cookie cookie = { }; cookie; })
#define lockdep_repin_lock(l, c)		do { (void)(l); (void)(c); } while (0)
#define lockdep_unpin_lock(l, c)		do { (void)(l); (void)(c); } while (0)

/* STATIC_LOCKDEP_MAP_INIT removed - unused */

#define lock_contended(lockdep_map, ip) do {} while (0)
#define lock_acquired(lockdep_map, ip) do {} while (0)

#define LOCK_CONTENDED(_lock, try, lock) \
	lock(_lock)

#define LOCK_CONTENDED_RETURN(_lock, try, lock) \
	lock(_lock)

#define SINGLE_DEPTH_NESTING			1


#define lock_acquire_exclusive(l, s, t, n, i)		lock_acquire(l, s, t, 0, 1, n, i)
#define lock_acquire_shared(l, s, t, n, i)		lock_acquire(l, s, t, 1, 1, n, i)
#define lock_acquire_shared_recursive(l, s, t, n, i)	lock_acquire(l, s, t, 2, 1, n, i)

#define spin_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define spin_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define spin_release(l, i)			lock_release(l, i)

/* rwlock_acquire, rwlock_acquire_read, rwlock_release removed - never called */

#define seqcount_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
/* seqcount_acquire_read removed - never called */
#define seqcount_release(l, i)			lock_release(l, i)

#define mutex_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
#define mutex_acquire_nest(l, s, t, n, i)	lock_acquire_exclusive(l, s, t, n, i)
#define mutex_release(l, i)			lock_release(l, i)

#define rwsem_acquire(l, s, t, i)		lock_acquire_exclusive(l, s, t, NULL, i)
/* rwsem_acquire_nest removed - never called */
#define rwsem_acquire_read(l, s, t, i)		lock_acquire_shared(l, s, t, NULL, i)
#define rwsem_release(l, i)			lock_release(l, i)

/* lock_map_acquire, lock_map_acquire_read, lock_map_release removed - never called */

# define might_lock(lock) do { } while (0)

# define lockdep_assert_irqs_enabled() do { } while (0)
# define lockdep_assert_irqs_disabled() do { } while (0)

# define lockdep_assert_preemption_enabled() do { } while (0)
# define lockdep_assert_preemption_disabled() do { } while (0)

#endif  
