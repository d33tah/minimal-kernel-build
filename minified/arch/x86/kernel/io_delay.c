/* Minimal IO delay implementation for minimal kernel */

#include <linux/kernel.h>
#include <linux/io.h>

/* Use default 0x80 port IO delay - no DMI detection or command line params */
void native_io_delay(void)
{
	asm volatile("outb %al, $0x80");
}

/* io_delay_init removed - was empty stub, call removed from setup.c */
