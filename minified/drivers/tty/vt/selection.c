// SPDX-License-Identifier: GPL-2.0
/*
 * This module exports the functions:
 *
 *     'int set_selection_user(struct tiocl_selection __user *,
 *			       struct tty_struct *)'
 *     'int set_selection_kernel(struct tiocl_selection *, struct tty_struct *)'
 *     'void clear_selection(void)'
 *     'int paste_selection(struct tty_struct *)'
 *     'int sel_loadlut(char __user *)'
 *
 * Now that /dev/vcs exists, most of this can disappear again.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>
#include <linux/tiocl.h>
#include <linux/console.h>
#include <linux/tty_flip.h>

#include <linux/sched/signal.h>

/* Stubbed for minification - console selection not needed for minimal boot */

void clear_selection(void)
{
	/* Stubbed */
}
EXPORT_SYMBOL_GPL(clear_selection);

bool vc_is_sel(struct vc_data *vc)
{
	return false;
}

int sel_loadlut(char __user *p)
{
	return 0;
}

int set_selection_user(const struct tiocl_selection __user *sel,
		       struct tty_struct *tty)
{
	return 0;
}

int set_selection_kernel(struct tiocl_selection *v, struct tty_struct *tty)
{
	return 0;
}
EXPORT_SYMBOL_GPL(set_selection_kernel);

int paste_selection(struct tty_struct *tty)
{
	return 0;
}
EXPORT_SYMBOL_GPL(paste_selection);
