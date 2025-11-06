// SPDX-License-Identifier: GPL-2.0
/* Stubbed char_dev.c */
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/export.h>
#include <linux/device.h>

int register_chrdev_region(dev_t from, unsigned count, const char *name) { return 0; }
EXPORT_SYMBOL(register_chrdev_region);

void unregister_chrdev_region(dev_t from, unsigned count) { }
EXPORT_SYMBOL(unregister_chrdev_region);

int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
			const char *name) { return -ENODEV; }
EXPORT_SYMBOL(alloc_chrdev_region);

void cdev_init(struct cdev *cdev, const struct file_operations *fops) { }
EXPORT_SYMBOL(cdev_init);

struct cdev *cdev_alloc(void) { return NULL; }
EXPORT_SYMBOL(cdev_alloc);

void cdev_del(struct cdev *p) { }
EXPORT_SYMBOL(cdev_del);

int cdev_add(struct cdev *p, dev_t dev, unsigned count) { return 0; }
EXPORT_SYMBOL(cdev_add);

void cdev_set_parent(struct cdev *p, struct kobject *kobj) { }
EXPORT_SYMBOL(cdev_set_parent);

int cdev_device_add(struct cdev *cdev, struct device *dev) { return 0; }
EXPORT_SYMBOL(cdev_device_add);

void cdev_device_del(struct cdev *cdev, struct device *dev) { }
EXPORT_SYMBOL(cdev_device_del);

int __register_chrdev(unsigned int major, unsigned int baseminor,
		      unsigned int count, const char *name,
		      const struct file_operations *fops) { return 0; }
EXPORT_SYMBOL(__register_chrdev);

void __unregister_chrdev(unsigned int major, unsigned int baseminor,
			 unsigned int count, const char *name) { }
EXPORT_SYMBOL(__unregister_chrdev);

void __init chrdev_init(void) { }

void cdev_put(struct cdev *p) { }
void cd_forget(struct inode *inode) { }

const struct file_operations def_chr_fops = { };
