#include <linux/semaphore.h>

void down(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	if (likely(sem->count > 0))
		sem->count--;
	else
		BUG();
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

void up(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	sem->count++;
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}
