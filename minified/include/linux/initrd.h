
#ifndef __LINUX_INITRD_H
#define __LINUX_INITRD_H

extern int initrd_below_start_ok;

extern unsigned long initrd_start, initrd_end;

extern phys_addr_t phys_initrd_start;
extern unsigned long phys_initrd_size;

#endif
