 
 
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/initrd.h>

#include "do_mounts.h"

unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;

phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

static int __init no_initrd(char *str)
{
	return 1;
}
__setup("noinitrd", no_initrd);

static int __init early_initrdmem(char *p)
{
	return 0;
}
early_param("initrdmem", early_initrdmem);

static int __init early_initrd(char *p)
{
	return early_initrdmem(p);
}
early_param("initrd", early_initrd);

bool __init initrd_load(void)
{
	 
	return false;
}
