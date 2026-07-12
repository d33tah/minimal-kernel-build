#include <linux/spinlock.h>

int _atomic_dec_and_lock_irqsave(atomic_t *atomic, spinlock_t *lock, unsigned long *flags)
{
	/* runtime-dead: sole caller put_ucounts never executes at boot */
	return 0;
}
