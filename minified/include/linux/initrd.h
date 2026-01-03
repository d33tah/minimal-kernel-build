
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

/* INITRD_MINOR removed - unused */

extern int initrd_below_start_ok;

extern unsigned long initrd_start, initrd_end;
extern void free_initrd_mem(unsigned long, unsigned long);

/* reserve_initrd_mem, wait_for_initramfs removed - unused */

extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

void console_on_rootfs(void);

#endif  
