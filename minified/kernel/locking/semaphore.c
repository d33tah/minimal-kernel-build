

#include <linux/kernel.h>
#include <linux/semaphore.h>

void down(struct semaphore *sem)
{
	unsigned long flags;

	might_sleep();
	raw_spin_lock_irqsave(&sem->lock, flags);
	sem->count--;
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}

void up(struct semaphore *sem)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&sem->lock, flags);
	sem->count++;
	raw_spin_unlock_irqrestore(&sem->lock, flags);
}
