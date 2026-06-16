/* Minimal IO delay implementation for minimal kernel */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

/* Use default 0x80 port IO delay - no DMI detection or command line params */
void native_io_delay(void)
{
	asm volatile ("outb %al, $0x80");
}
