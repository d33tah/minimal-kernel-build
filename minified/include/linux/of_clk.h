/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OF clock helpers
 */

#ifndef __LINUX_OF_CLK_H
#define __LINUX_OF_CLK_H

struct device_node;
struct of_device_id;


static inline unsigned int of_clk_get_parent_count(const struct device_node *np)
{
	return 0;
}
static inline const char *of_clk_get_parent_name(const struct device_node *np,
						 int index)
{
	return NULL;
}
static inline void of_clk_init(const struct of_device_id *matches) {}


#endif /* __LINUX_OF_CLK_H */
