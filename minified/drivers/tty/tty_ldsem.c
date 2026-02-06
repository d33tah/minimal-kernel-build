/* --- 2026-02-06 22:30 --- Gutted: ldsem operations are dead since no
 * TTY structs are ever created (tty_io.c gutted). Keep __init_ldsem
 * and stubs to satisfy linker if referenced via tty.h declarations. */

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>

void __init_ldsem(struct ld_semaphore *sem, const char *name,
		  struct lock_class_key *key)
{
	atomic_long_set(&sem->count, 0);
	raw_spin_lock_init(&sem->wait_lock);
	sem->wait_readers = 0;
	INIT_LIST_HEAD(&sem->read_wait);
	INIT_LIST_HEAD(&sem->write_wait);
}

int __sched ldsem_down_read(struct ld_semaphore *sem, long timeout)
{
	return 1;
}

int ldsem_down_read_trylock(struct ld_semaphore *sem)
{
	return 1;
}

int __sched ldsem_down_write(struct ld_semaphore *sem, long timeout)
{
	return 1;
}

void ldsem_up_read(struct ld_semaphore *sem)
{
}

void ldsem_up_write(struct ld_semaphore *sem)
{
}
