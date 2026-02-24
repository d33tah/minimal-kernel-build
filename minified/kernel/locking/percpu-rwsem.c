/* --- 2026-02-08 05:55 --- */
#include <linux/percpu-rwsem.h>

int __percpu_init_rwsem(struct percpu_rw_semaphore *sem, const char *name,
			struct lock_class_key *key)
{
	sem->read_count = alloc_percpu(int);
	if (unlikely(!sem->read_count))
		return -ENOMEM;

	rcu_sync_init(&sem->rss);
	sem->writer.task = NULL;
	init_waitqueue_head(&sem->waiters);
	atomic_set(&sem->block, 0);
	return 0;
}

void percpu_free_rwsem(struct percpu_rw_semaphore *sem)
{
	if (!sem->read_count)
		return;
	rcu_sync_dtor(&sem->rss);
	sem->read_count = NULL;
}
