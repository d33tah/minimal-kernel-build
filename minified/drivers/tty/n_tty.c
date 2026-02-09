/* --- 2026-02-06 22:30 --- Gutted: n_tty ldisc is registered but never
 * used since no TTY devices are opened. Keep n_tty_init stub for printk.c. */

#include <linux/tty.h>

static struct tty_ldisc_ops n_tty_ops = {
	.owner = THIS_MODULE,
	.num = N_TTY,
	.name = "n_tty",
};

void __init n_tty_init(void)
{
	tty_register_ldisc(&n_tty_ops);
}
