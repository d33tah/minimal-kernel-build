/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <asm/e820/types.h>

extern struct e820_table *e820_table;

extern unsigned long e820__end_of_ram_pfn(void);

extern void e820__memblock_setup(void);

extern void e820__memory_setup(void);

#endif /* _ASM_E820_API_H */
