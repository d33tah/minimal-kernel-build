#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <uapi/asm/bootparam.h>

#define E820_TYPE_RAM 1

#define ISA_END_ADDRESS 0x00100000

extern void e820__memory_setup(void);
extern unsigned long e820__end_of_ram_pfn(void);
extern void e820__memblock_setup(void);

#endif
