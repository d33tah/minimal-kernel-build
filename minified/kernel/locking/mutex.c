#include <linux/mutex.h>
#include <linux/sched/debug.h>
#include <linux/spinlock.h>

#include "mutex.h"

# define MUTEX_WARN_ON(cond)

void __mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key) {
	atomic_long_set(&lock->owner, 0);
	raw_spin_lock_init(&lock->wait_lock);
	INIT_LIST_HEAD(&lock->wait_list);

	debug_mutex_init(lock, name, key); }

#define MUTEX_FLAG_HANDOFF	0x02
#define MUTEX_FLAG_PICKUP	0x04

#define MUTEX_FLAGS		0x07

static inline struct task_struct *__owner_task(unsigned long owner) {
	return (struct task_struct *)(owner & ~MUTEX_FLAGS); }

static inline unsigned long __owner_flags(unsigned long owner) {
	return owner & MUTEX_FLAGS; }

static inline struct task_struct *__mutex_trylock_common(struct mutex *lock, bool handoff) {
	unsigned long owner, curr = (unsigned long)current;

	owner = atomic_long_read(&lock->owner);
	for (;;) {
		unsigned long flags = __owner_flags(owner);
		unsigned long task = owner & ~MUTEX_FLAGS;

		if (task) {
			if (flags & MUTEX_FLAG_PICKUP) {
				if (task != curr)
					break;
				flags &= ~MUTEX_FLAG_PICKUP;
			} else if (handoff) {
				if (flags & MUTEX_FLAG_HANDOFF)
					break;
				flags |= MUTEX_FLAG_HANDOFF;
			} else {
				break; }
		} else {
			MUTEX_WARN_ON(flags & (MUTEX_FLAG_HANDOFF | MUTEX_FLAG_PICKUP));
			task = curr; }

		if (atomic_long_try_cmpxchg_acquire(&lock->owner, &owner, task | flags)) {
			if (task == curr)
				return NULL;
			break; } }

	return __owner_task(owner); }

static inline bool __mutex_trylock(struct mutex *lock) {
	return !__mutex_trylock_common(lock, false); }


static __always_inline bool __mutex_trylock_fast(struct mutex *lock) {
	unsigned long curr = (unsigned long)current;
	unsigned long zero = 0UL;

	return atomic_long_try_cmpxchg_acquire(&lock->owner, &zero, curr); }

static __always_inline bool __mutex_unlock_fast(struct mutex *lock) {
	unsigned long curr = (unsigned long)current;

	return atomic_long_try_cmpxchg_release(&lock->owner, &curr, 0UL); }

void __sched mutex_lock(struct mutex *lock) {
	might_sleep();

	__mutex_trylock_fast(lock); }

void __sched mutex_unlock(struct mutex *lock) {
	__mutex_unlock_fast(lock); }

int __sched mutex_lock_interruptible(struct mutex *lock) {
	might_sleep();

	__mutex_trylock_fast(lock);
	return 0; }


int __sched mutex_lock_killable(struct mutex *lock) {
	might_sleep();

	__mutex_trylock_fast(lock);
	return 0; }

int __sched mutex_trylock(struct mutex *lock) {
	bool locked;

	MUTEX_WARN_ON(lock->magic != lock);

	locked = __mutex_trylock(lock);
	if (locked)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return locked; }
