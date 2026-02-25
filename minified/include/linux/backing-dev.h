#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/percpu_counter.h>
#include <linux/workqueue.h>

struct device;

struct bdi_writeback {
};

struct backing_dev_info {
	u64 id;
	struct list_head bdi_list;
	unsigned long ra_pages;
	unsigned long io_pages;

	struct kref refcnt;

	struct bdi_writeback wb;
	struct list_head wb_list;
	wait_queue_head_t wb_waitq;

	struct device *dev;
	char dev_name[64];
	struct device *owner;

};
extern struct backing_dev_info noop_backing_dev_info;
int bdi_init(struct backing_dev_info *bdi);
#endif
