
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

extern char __initramfs_start[];
extern unsigned long __initramfs_size;

void console_on_rootfs(void);

#endif
