// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/char/misc.c
 *
 * Generic misc open routine by Johan Myreen
 *
 * Based on code from Linus
 *
 * Teemu Rantanen's Microsoft Busmouse support and Derrick Cole's
 *   changes incorporated into 0.97pl4
 *   by Peter Cervasio (pete%q106fm.uucp@wupost.wustl.edu) (08SEP92)
 *   See busmouse.c for particulars.
 *
 * Made things a lot mode modular - easy to compile in just one or two
 * of the misc drivers, as they are now completely independent. Linus.
 *
 * Support for loadable modules. 8-Sep-95 Philip Blundell <pjb27@cam.ac.uk>
 *
 * Fixed a failing symbol register to free the device registration
 *		Alan Cox <alan@lxorguk.ukuu.org.uk> 21-Jan-96
 *
 * Dynamic minors and /proc/mice by Alessandro Rubini. 26-Mar-96
 *
 * Renamed to misc and miscdevice to be more accurate. Alan Cox 26-Mar-96
 *
 * Handling of mouse minor numbers for kerneld:
 *  Idea by Jacques Gelinas <jack@solucorp.qc.ca>,
 *  adapted by Bjorn Ekwall <bj0rn@blox.se>
 *  corrected by Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * Changes for kmod (from kerneld):
 *	Cyrus Durgin <cider@speakeasy.org>
 *
 * Added devfs support. Richard Gooch <rgooch@atnf.csiro.au>  10-Jan-1998
 */

#include <linux/fs.h>
#include <asm/bug.h>
#include <linux/bitmap.h>
#include <linux/miscdevice.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/gfp.h>

#include "asm-generic/bitops/instrumented-atomic.h"
#include "asm-generic/errno-base.h"
#include "linux/device/class.h"
#include "linux/err.h"
#include "linux/export.h"
#include "linux/kdev_t.h"
#include "linux/list.h"
#include "linux/printk.h"
#include "linux/string.h"
#include "linux/types.h"

/*
 * Head entry for the doubly linked miscdevice list
 */
static LIST_HEAD(misc_list);
static DEFINE_MUTEX(misc_mtx);

/*
 * Assigned numbers, used for dynamic minors
 */
#define DYNAMIC_MINORS 128 /* like dynamic majors */
static DECLARE_BITMAP(misc_minors, DYNAMIC_MINORS);


static int misc_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct miscdevice *c = NULL, *iter;
	int err = -ENODEV;
	const struct file_operations *new_fops = NULL;

	mutex_lock(&misc_mtx);

	list_for_each_entry(iter, &misc_list, list) {
		if (iter->minor != minor)
			continue;
		c = iter;
		new_fops = fops_get(iter->fops);
		break;
	}

	if (!new_fops) {
		mutex_unlock(&misc_mtx);
		request_module("char-major-%d-%d", MISC_MAJOR, minor);
		mutex_lock(&misc_mtx);

		list_for_each_entry(iter, &misc_list, list) {
			if (iter->minor != minor)
				continue;
			c = iter;
			new_fops = fops_get(iter->fops);
			break;
		}
		if (!new_fops)
			goto fail;
	}

	/*
	 * Place the miscdevice in the file's
	 * private_data so it can be used by the
	 * file operations, including f_op->open below
	 */
	file->private_data = c;

	err = 0;
	replace_fops(file, new_fops);
	if (file->f_op->open)
		err = file->f_op->open(inode, file);
fail:
	mutex_unlock(&misc_mtx);
	return err;
}

static struct class *misc_class;

static const struct file_operations misc_fops = {
	.owner		= THIS_MODULE,
	.open		= misc_open,
	.llseek		= noop_llseek,
};

/**
 *	misc_register	-	register a miscellaneous device
 *	@misc: device structure
 *
 *	Register a miscellaneous device with the kernel. If the minor
 *	number is set to %MISC_DYNAMIC_MINOR a minor number is assigned
 *	and placed in the minor field of the structure. For other cases
 *	the minor number requested is used.
 *
 *	The structure passed is linked into the kernel and may not be
 *	destroyed until it has been unregistered. By default, an open()
 *	syscall to the device sets file->private_data to point to the
 *	structure. Drivers don't need open in fops for this.
 *
 *	A zero is returned on success and a negative errno code for
 *	failure.
 */

int misc_register(struct miscdevice *misc)
{
	dev_t dev;
	int err = 0;
	bool is_dynamic = (misc->minor == MISC_DYNAMIC_MINOR);

	INIT_LIST_HEAD(&misc->list);

	mutex_lock(&misc_mtx);

	if (is_dynamic) {
		int i = find_first_zero_bit(misc_minors, DYNAMIC_MINORS);

		if (i >= DYNAMIC_MINORS) {
			err = -EBUSY;
			goto out;
		}
		misc->minor = DYNAMIC_MINORS - i - 1;
		set_bit(i, misc_minors);
	} else {
		struct miscdevice *c;

		list_for_each_entry(c, &misc_list, list) {
			if (c->minor == misc->minor) {
				err = -EBUSY;
				goto out;
			}
		}
	}

	dev = MKDEV(MISC_MAJOR, misc->minor);

	misc->this_device =
		device_create_with_groups(misc_class, misc->parent, dev,
					  misc, misc->groups, "%s", misc->name);
	if (IS_ERR(misc->this_device)) {
		if (is_dynamic) {
			int i = DYNAMIC_MINORS - misc->minor - 1;

			if (i < DYNAMIC_MINORS && i >= 0)
				clear_bit(i, misc_minors);
			misc->minor = MISC_DYNAMIC_MINOR;
		}
		err = PTR_ERR(misc->this_device);
		goto out;
	}

	/*
	 * Add it to the front, so that later devices can "override"
	 * earlier defaults
	 */
	list_add(&misc->list, &misc_list);
 out:
	mutex_unlock(&misc_mtx);
	return err;
}
EXPORT_SYMBOL(misc_register);

/**
 *	misc_deregister - unregister a miscellaneous device
 *	@misc: device to unregister
 *
 *	Unregister a miscellaneous device that was previously
 *	successfully registered with misc_register().
 */

void misc_deregister(struct miscdevice *misc)
{
	int i = DYNAMIC_MINORS - misc->minor - 1;

	if (WARN_ON(list_empty(&misc->list)))
		return;

	mutex_lock(&misc_mtx);
	list_del(&misc->list);
	device_destroy(misc_class, MKDEV(MISC_MAJOR, misc->minor));
	if (i < DYNAMIC_MINORS && i >= 0)
		clear_bit(i, misc_minors);
	mutex_unlock(&misc_mtx);
}
EXPORT_SYMBOL(misc_deregister);

static char *misc_devnode(struct device *dev, umode_t *mode)
{
	struct miscdevice *c = dev_get_drvdata(dev);

	if (mode && c->mode)
		*mode = c->mode;
	if (c->nodename)
		return kstrdup(c->nodename, GFP_KERNEL);
	return NULL;
}

static int __init misc_init(void)
{
	int err;
	struct proc_dir_entry *ret;

	ret = proc_create_seq("misc", 0, NULL, &misc_seq_ops);
	misc_class = class_create(THIS_MODULE, "misc");
	err = PTR_ERR(misc_class);
	if (IS_ERR(misc_class))
		goto fail_remove;

	err = -EIO;
	if (register_chrdev(MISC_MAJOR, "misc", &misc_fops))
		goto fail_printk;
	misc_class->devnode = misc_devnode;
	return 0;

fail_printk:
	pr_err("unable to get major %d for misc devices\n", MISC_MAJOR);
	class_destroy(misc_class);
fail_remove:
	if (ret)
		remove_proc_entry("misc", NULL);
	return err;
}
subsys_initcall(misc_init);
