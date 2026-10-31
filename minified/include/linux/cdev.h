#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

#include <linux/kdev_t.h>
#include <linux/list.h>

struct file_operations;

struct cdev {
	const struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
} __randomize_layout;

void cdev_init(struct cdev *, const struct file_operations *);

int cdev_add(struct cdev *, dev_t, unsigned);

#endif
