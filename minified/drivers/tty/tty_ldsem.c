
#include <linux/sched/debug.h>
#include <linux/tty.h>


/*
 * BITS_PER_LONG is unconditionally 32 in this x86-32 build
 * (arch/x86/include/uapi/asm/bitsperlong.h), so the `#if BITS_PER_LONG == 64`
 * arm (LDSEM_ACTIVE_MASK 0xffffffffL) was statically dead. Emit the 32-bit
 * mask unconditionally.
 */
#define LDSEM_ACTIVE_MASK	0x0000ffffL

#define LDSEM_UNLOCKED		0L
#define LDSEM_ACTIVE_BIAS	1L
#define LDSEM_WAIT_BIAS		(-LDSEM_ACTIVE_MASK-1)
#define LDSEM_READ_BIAS		LDSEM_ACTIVE_BIAS
#define LDSEM_WRITE_BIAS	(LDSEM_WAIT_BIAS + LDSEM_ACTIVE_BIAS)

void __init_ldsem(struct ld_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, LDSEM_UNLOCKED);
}

/*
 * Contention slow paths removed: this is a uniprocessor (SMP=n) non-preemptible
 * (PREEMPTION=n) kernel and the tty ldisc_sem is only taken serially around the
 * single console, so the read/write acquire fast paths never fail. The
 * down_read_failed / down_write_failed wait machinery, the ldsem_waiter queue,
 * and the ldsem_wake wakeup helpers were therefore unreachable and are gone.
 */

static int __ldsem_down_read_nested(struct ld_semaphore *sem,
					   int subclass, long timeout)
{
	rwsem_acquire_read(&sem->dep_map, subclass, 0, _RET_IP_);
	atomic_long_add_return(LDSEM_READ_BIAS, &sem->count);
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}

static int __ldsem_down_write_nested(struct ld_semaphore *sem,
					    int subclass, long timeout)
{
	rwsem_acquire(&sem->dep_map, subclass, 0, _RET_IP_);
	atomic_long_add_return(LDSEM_WRITE_BIAS, &sem->count);
	lock_acquired(&sem->dep_map, _RET_IP_);
	return 1;
}


int __sched ldsem_down_read(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_read_nested(sem, 0, timeout);
}

int __sched ldsem_down_write(struct ld_semaphore *sem, long timeout)
{
	might_sleep();
	return __ldsem_down_write_nested(sem, 0, timeout);
}

void ldsem_up_read(struct ld_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	atomic_long_add_return(-LDSEM_READ_BIAS, &sem->count);
}

void ldsem_up_write(struct ld_semaphore *sem)
{
	rwsem_release(&sem->dep_map, _RET_IP_);
	atomic_long_add_return(-LDSEM_WRITE_BIAS, &sem->count);
}


