#ifndef _ASM_E820_API_H
#define _ASM_E820_API_H

#include <asm/e820/types.h>

extern struct e820_table *e820_table;

extern void e820__range_add(u64 start, u64 size, enum e820_type type);

extern unsigned long e820__end_of_ram_pfn(void);

extern void e820__memblock_setup(void);

extern void e820__memory_setup(void);
extern void e820__memory_setup_default(void);

#endif /* _ASM_E820_API_H */
