#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/initrd.h>

#include "do_mounts.h"

unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;

phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

/* no_initrd, early_initrdmem, early_initrd and handlers removed (~16 LOC) */

bool __init initrd_load(void)
{
	 
	return false;
}
