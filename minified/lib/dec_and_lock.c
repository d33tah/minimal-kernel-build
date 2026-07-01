#include <linux/spinlock.h>

int _atomic_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock,
				 unsigned long *flags)
{
	 
	if (atomic_add_unless(atomic, -1, 1))
		return 0;

	 
	spin_lock_irqsave(lock, *flags);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock_irqrestore(lock, *flags);
	return 0;
}
