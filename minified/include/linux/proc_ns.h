#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H
#include <linux/ns_common.h>
enum {
	PROC_UTS_INIT_INO	= 0xEFFFFFFEU,
	PROC_PID_INIT_INO	= 0xEFFFFFFCU,
};
static inline int ns_alloc_inum(struct ns_common *ns) { atomic_long_set(&ns->stashed, 0); ns->inum = 1; return 0; }
#endif
