
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/device.h>

struct backing_dev_info noop_backing_dev_info;

int bdi_init(struct backing_dev_info *bdi)
{
	return 0;
}

