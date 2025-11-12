// SPDX-License-Identifier: GPL-2.0+
/* Stubbed - logic PIO unused in minimal kernel */
#include <linux/logic_pio.h>
#include <linux/fwnode.h>

int logic_pio_register_range(struct logic_pio_hwaddr *new_range) { return -EINVAL; }

void logic_pio_unregister_range(struct logic_pio_hwaddr *range) {}

struct logic_pio_hwaddr *find_io_range_by_fwnode(struct fwnode_handle *fwnode) { return NULL; }

resource_size_t logic_pio_to_hwaddr(unsigned long pio) { return -1; }

unsigned long logic_pio_trans_hwaddr(struct fwnode_handle *fwnode, resource_size_t addr, resource_size_t size) { return -1; }

unsigned long logic_pio_trans_cpuaddr(resource_size_t addr) { return -1; }
