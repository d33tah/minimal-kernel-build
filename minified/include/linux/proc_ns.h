#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H

#include <linux/ns_common.h>


enum { PROC_UTS_INIT_INO	= 0xEFFFFFFEU, };


static inline int proc_alloc_inum(unsigned int *inum) {
	*inum = 1;
	return 0; }

static inline int ns_alloc_inum(struct ns_common *ns) {
	return proc_alloc_inum(&ns->inum); }


#endif
