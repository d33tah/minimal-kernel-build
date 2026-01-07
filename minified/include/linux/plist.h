#ifndef _LINUX_PLIST_H_
#define _LINUX_PLIST_H_

#include <linux/container_of.h>
#include <linux/list.h>
#include <linux/types.h>

#include <asm/bug.h>

struct plist_head {
	struct list_head node_list;
};

struct plist_node {
	int			prio;
	struct list_head	prio_list;
	struct list_head	node_list;
};

#endif
