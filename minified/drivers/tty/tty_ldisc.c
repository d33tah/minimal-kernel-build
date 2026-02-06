/* --- 2026-02-06 22:30 --- Gutted: all ldisc operations are dead since
 * no TTY devices are created (tty_io.c gutted). Keep tty_register_ldisc
 * for n_tty_init called from printk.c. */

#include <linux/tty.h>

static struct tty_ldisc_ops *tty_ldiscs[NR_LDISCS];

int tty_register_ldisc(struct tty_ldisc_ops *new_ldisc)
{
	unsigned long flags;

	if (new_ldisc->num < N_TTY || new_ldisc->num >= NR_LDISCS)
		return -EINVAL;

	local_irq_save(flags);
	tty_ldiscs[new_ldisc->num] = new_ldisc;
	local_irq_restore(flags);

	return 0;
}
