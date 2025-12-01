
#ifndef __CLK_CONF_H
#define __CLK_CONF_H

#include <linux/types.h>

struct device_node;

static inline int of_clk_set_defaults(struct device_node *node,
				      bool clk_supplier)
{
	return 0;
}

#endif  
