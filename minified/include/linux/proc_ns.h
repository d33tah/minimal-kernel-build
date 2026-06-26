#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H

#include <linux/ns_common.h>

struct pid_namespace;
struct path;
struct task_struct;
struct inode;

enum {
	PROC_ROOT_INO		= 1,
	PROC_IPC_INIT_INO	= 0xEFFFFFFFU,
	PROC_UTS_INIT_INO	= 0xEFFFFFFEU,
	PROC_USER_INIT_INO	= 0xEFFFFFFDU,
	PROC_PID_INIT_INO	= 0xEFFFFFFCU,
	PROC_CGROUP_INIT_INO	= 0xEFFFFFFBU,
	PROC_TIME_INIT_INO	= 0xEFFFFFFAU,
};


static inline int proc_alloc_inum(unsigned int *inum)
{
	*inum = 1;
	return 0;
}

static inline int ns_alloc_inum(struct ns_common *ns)
{
	atomic_long_set(&ns->stashed, 0);
	return proc_alloc_inum(&ns->inum);
}


#endif
