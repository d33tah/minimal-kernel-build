/* Most of initramfs.c removed - parsing functions unused after populate_rootfs removal */

#include <linux/init.h>

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

/* free_initrd_mem provided by arch/x86/mm/init.c */
