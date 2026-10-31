
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

extern unsigned long initrd_start, initrd_end;

extern phys_addr_t phys_initrd_start;

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

void console_on_rootfs(void);

#endif
