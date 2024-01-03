// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/ksysfs.c - sysfs attributes in /sys/kernel, which
 * 		     are not related to any other subsystem
 *
 * Copyright (C) 2004 Kay Sievers <kay.sievers@vrfy.org>
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kexec.h>
#include <linux/profile.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/compiler.h>

#include <linux/rcupdate.h>

#define KERNEL_ATTR_RO(_name) static struct kobj_attribute _name ##_attr = __ATTR_RO(_name)

#define KERNEL_ATTR_RW(_name) static struct kobj_attribute _name ##_attr = __ATTR_RW(_name)

/* current uevent sequence number */
static ssize_t uevent_seqnum_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%llu\n", (unsigned long long)uevent_seqnum);
}
KERNEL_ATTR_RO(uevent_seqnum);





/* whether file capabilities are enabled */
static ssize_t fscaps_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", file_caps_enabled);
}
KERNEL_ATTR_RO(fscaps);


/*
 * Make /sys/kernel/notes give the raw contents of our kernel .notes section.
 */
extern const void __start_notes __weak;
extern const void __stop_notes __weak;
#define notes_size (&__stop_notes - &__start_notes)

static ssize_t notes_read(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr,
			  char *buf, loff_t off, size_t count)
{
	memcpy(buf, &__start_notes + off, count);
	return count;
}

static struct bin_attribute notes_attr __ro_after_init  = {
	.attr = {
		.name = "notes",
		.mode = S_IRUGO,
	},
	.read = &notes_read,
};

struct kobject *kernel_kobj;
EXPORT_SYMBOL_GPL(kernel_kobj);

static struct attribute * kernel_attrs[] = {
	&fscaps_attr.attr,
	&uevent_seqnum_attr.attr,
	NULL
};

static const struct attribute_group kernel_attr_group = {
	.attrs = kernel_attrs,
};

static int __init ksysfs_init(void)
{
	int error;

	kernel_kobj = kobject_create_and_add("kernel", NULL);
	if (!kernel_kobj) {
		error = -ENOMEM;
		goto exit;
	}
	error = sysfs_create_group(kernel_kobj, &kernel_attr_group);
	if (error)
		goto kset_exit;

	if (notes_size > 0) {
		notes_attr.size = notes_size;
		error = sysfs_create_bin_file(kernel_kobj, &notes_attr);
		if (error)
			goto group_exit;
	}

	return 0;

group_exit:
	sysfs_remove_group(kernel_kobj, &kernel_attr_group);
kset_exit:
	kobject_put(kernel_kobj);
exit:
	return error;
}

core_initcall(ksysfs_init);
