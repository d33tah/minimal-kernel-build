#ifndef _ASM_X86_PGTABLE_AREAS_H
#define _ASM_X86_PGTABLE_AREAS_H

#define VMALLOC_OFFSET (8 * 1024 * 1024)

#define VMALLOC_START ((unsigned long)high_memory + VMALLOC_OFFSET)
#define VMALLOC_END ((FIXADDR_TOT_START - 2 * PAGE_SIZE) & PMD_MASK)

/* MKB_FOOTPRINT: lowmem ends at the LOWMEM_PAGES cap (upstream: VMALLOC_END -
 * PAGE_OFFSET - __VMALLOC_RESERVE); RAM above it is ignored. */
#define MAXMEM (LOWMEM_PAGES << PAGE_SHIFT)

#endif
