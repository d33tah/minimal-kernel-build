
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

extern unsigned long initrd_start, initrd_end;

extern void wait_for_initramfs(void);

extern phys_addr_t phys_initrd_start;

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

void console_on_rootfs(void);

#endif  
