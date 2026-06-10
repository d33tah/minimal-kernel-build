
#include <linux/init.h>
#include <linux/tty.h>

/*
 * The /dev/{null,zero,full,mem,port,random,urandom} character devices are
 * never opened on this minimal system (init only does write(2)+exit, and the
 * console is the TTY device, not a mem device), so their file_operations and
 * the whole mem-class registration were dead.  All that remains live here is
 * the tty_init() call that this fs_initcall chains into.
 */
static int __init chr_dev_init(void)
{
	return tty_init();
}

fs_initcall(chr_dev_init);
