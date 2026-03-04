#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/spinlock.h>

struct device;

struct bdi_writeback {
};

struct backing_dev_info {
	u64 id;
	struct list_head bdi_list;

	struct kref refcnt;

	struct bdi_writeback wb;
	struct list_head wb_list;
	wait_queue_head_t wb_waitq;

	struct device *dev;
};
extern struct backing_dev_info noop_backing_dev_info;
#endif
