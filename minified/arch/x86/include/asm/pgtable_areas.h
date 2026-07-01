#ifndef _ASM_X86_PGTABLE_AREAS_H
#define _ASM_X86_PGTABLE_AREAS_H

/* --- 2025-12-07 20:22 --- Inlined pgtable_32_areas.h */
#include <asm/cpu_entry_area.h>

#define VMALLOC_OFFSET	(8 * 1024 * 1024)

#ifndef __ASSEMBLY__
extern bool __vmalloc_start_set;
#endif

#define VMALLOC_START	((unsigned long)high_memory + VMALLOC_OFFSET)
#define CPU_ENTRY_AREA_PAGES		(NR_CPUS * DIV_ROUND_UP(sizeof(struct cpu_entry_area), PAGE_SIZE))
#define CPU_ENTRY_AREA_BASE	\
	((FIXADDR_TOT_START - PAGE_SIZE*(CPU_ENTRY_AREA_PAGES+1)) & PMD_MASK)
#define LDT_BASE_ADDR		\
	((CPU_ENTRY_AREA_BASE - PAGE_SIZE) & PMD_MASK)
# define VMALLOC_END	(LDT_BASE_ADDR - 2 * PAGE_SIZE)

#define CPU_ENTRY_AREA_RO_IDT		CPU_ENTRY_AREA_BASE
#define CPU_ENTRY_AREA_PER_CPU		(CPU_ENTRY_AREA_RO_IDT + PAGE_SIZE)

#define CPU_ENTRY_AREA_RO_IDT_VADDR	((void *)CPU_ENTRY_AREA_RO_IDT)

#define CPU_ENTRY_AREA_MAP_SIZE		(CPU_ENTRY_AREA_PER_CPU + CPU_ENTRY_AREA_ARRAY_SIZE - CPU_ENTRY_AREA_BASE)

#endif  
