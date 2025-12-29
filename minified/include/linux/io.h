
#ifndef _LINUX_IO_H
#define _LINUX_IO_H

#include <linux/types.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/err.h>
#include <asm/io.h>
#include <asm/page.h>

struct device;
struct resource;


int ioremap_page_range(unsigned long addr, unsigned long end,
		       phys_addr_t phys_addr, pgprot_t prot);


/* IOMEM_ERR_PTR removed - unused */




#ifndef arch_has_dev_port
#define arch_has_dev_port()     (1)
#endif

enum {
	MEMREMAP_WB = 1 << 0,
	MEMREMAP_WT = 1 << 1,
	MEMREMAP_WC = 1 << 2,
	MEMREMAP_ENC = 1 << 3,
	MEMREMAP_DEC = 1 << 4,
};

/* memremap, memunmap removed - declared but never called */

#endif  
