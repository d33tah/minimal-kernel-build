
#include <linux/backing-dev.h>

struct backing_dev_info noop_backing_dev_info;

int bdi_init(struct backing_dev_info *bdi)
{
	bdi->dev = NULL;
	kref_init(&bdi->refcnt);
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->wb_list);
	init_waitqueue_head(&bdi->wb_waitq);
	return 0;
}
