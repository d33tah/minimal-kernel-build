// SPDX-License-Identifier: GPL-2.0
#include <linux/nodemask.h>
#include <linux/module.h>
#include <linux/random.h>

unsigned int __next_node_in(int node, const nodemask_t *srcp)
{
	unsigned int ret = __next_node(node, srcp);

	if (ret == MAX_NUMNODES)
		ret = __first_node(srcp);
	return ret;
}
EXPORT_SYMBOL(__next_node_in);

