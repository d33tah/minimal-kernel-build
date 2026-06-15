
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




#define IOMEM_ERR_PTR(err) (__force void __iomem *)ERR_PTR(err)




#ifndef arch_has_dev_port
#define arch_has_dev_port()     (1)
#endif

#endif
