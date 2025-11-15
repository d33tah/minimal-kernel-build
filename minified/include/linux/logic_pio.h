 
 

#ifndef __LINUX_LOGIC_PIO_H
#define __LINUX_LOGIC_PIO_H

#include <linux/fwnode.h>

enum {
	LOGIC_PIO_INDIRECT,		 
	LOGIC_PIO_CPU_MMIO,		 
};

struct logic_pio_hwaddr {
	struct list_head list;
	struct fwnode_handle *fwnode;
	resource_size_t hw_start;
	resource_size_t io_start;
	resource_size_t size;  
	unsigned long flags;

	void *hostdata;
	const struct logic_pio_host_ops *ops;
};

struct logic_pio_host_ops {
	u32 (*in)(void *hostdata, unsigned long addr, size_t dwidth);
	void (*out)(void *hostdata, unsigned long addr, u32 val,
		    size_t dwidth);
	u32 (*ins)(void *hostdata, unsigned long addr, void *buffer,
		   size_t dwidth, unsigned int count);
	void (*outs)(void *hostdata, unsigned long addr, const void *buffer,
		     size_t dwidth, unsigned int count);
};

#define PIO_INDIRECT_SIZE 0
#define MMIO_UPPER_LIMIT (IO_SPACE_LIMIT - PIO_INDIRECT_SIZE)

struct logic_pio_hwaddr *find_io_range_by_fwnode(struct fwnode_handle *fwnode);
unsigned long logic_pio_trans_hwaddr(struct fwnode_handle *fwnode,
			resource_size_t hw_addr, resource_size_t size);
int logic_pio_register_range(struct logic_pio_hwaddr *newrange);
void logic_pio_unregister_range(struct logic_pio_hwaddr *range);
resource_size_t logic_pio_to_hwaddr(unsigned long pio);
unsigned long logic_pio_trans_cpuaddr(resource_size_t hw_addr);

#endif  
