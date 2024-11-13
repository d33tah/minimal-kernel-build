/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 */

#ifndef __CLK_CONF_H
#define __CLK_CONF_H

#include <linux/types.h>

struct device_node;

static inline int of_clk_set_defaults(struct device_node *node,
				      bool clk_supplier)
{
	return 0;
}

#endif /* __CLK_CONF_H */
