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

#define PLIST_HEAD_INIT(head)				\
{							\
	.node_list = LIST_HEAD_INIT((head).node_list)	\
}

#define PLIST_HEAD(head) \
	struct plist_head head = PLIST_HEAD_INIT(head)

#define PLIST_NODE_INIT(node, __prio)			\
{							\
	.prio  = (__prio),				\
	.prio_list = LIST_HEAD_INIT((node).prio_list),	\
	.node_list = LIST_HEAD_INIT((node).node_list),	\
}

#endif
