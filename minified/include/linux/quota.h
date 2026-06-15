
#ifndef _LINUX_QUOTA_
#define _LINUX_QUOTA_

#include <linux/rwsem.h>

/* Minimal quota.h stub - CONFIG_QUOTA is disabled */

#define MAXQUOTAS 3

struct mem_dqinfo {
	void *dqi_priv;
};

struct quota_format_ops;

/* Only needed because super_block embeds this struct */
struct quota_info {
	unsigned int flags;
	struct rw_semaphore dqio_sem;
	struct inode *files[MAXQUOTAS];
	struct mem_dqinfo info[MAXQUOTAS];
	const struct quota_format_ops *ops[MAXQUOTAS];
};

struct dquot_operations;
struct quotactl_ops;

#endif /* _LINUX_QUOTA_ */
