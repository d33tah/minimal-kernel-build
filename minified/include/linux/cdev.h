#ifndef _LINUX_CDEV_H
#define _LINUX_CDEV_H

#include <linux/kdev_t.h>
#include <linux/device.h>

struct file_operations;
struct inode;
struct module;

struct cdev {
	struct kobject kobj;
	struct module *owner;
	const struct file_operations *ops;
	struct list_head list;
	dev_t dev;
	unsigned int count;
} __randomize_layout;

void cdev_put(struct cdev *p);

void cd_forget(struct inode *);

#endif
