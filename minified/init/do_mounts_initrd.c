#include <linux/kernel.h>
#include <linux/initrd.h>

unsigned long initrd_start, initrd_end;

phys_addr_t phys_initrd_start __initdata;
