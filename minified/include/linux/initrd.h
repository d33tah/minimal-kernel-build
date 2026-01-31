
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

/* INITRD_MINOR removed - unused */

extern int initrd_below_start_ok;

extern unsigned long initrd_start, initrd_end;
/* free_initrd_mem removed - never called */

/* reserve_initrd_mem, wait_for_initramfs removed - unused */

extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;

/* __initramfs_start, __initramfs_size removed - declared but never used */

void console_on_rootfs(void);

#endif  
