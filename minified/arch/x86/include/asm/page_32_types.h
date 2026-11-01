#ifndef _ASM_X86_PAGE_32_DEFS_H
#define _ASM_X86_PAGE_32_DEFS_H

#define __PAGE_OFFSET_BASE _AC(CONFIG_PAGE_OFFSET, UL)
#define __PAGE_OFFSET __PAGE_OFFSET_BASE

#define THREAD_SIZE_ORDER 1
#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)

#define IRQ_STACK_SIZE THREAD_SIZE

#define __PHYSICAL_MASK_SHIFT 32

#define TASK_SIZE __PAGE_OFFSET
#define TASK_SIZE_MAX TASK_SIZE
#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX STACK_TOP

#define KERNEL_IMAGE_SIZE (512 * 1024 * 1024)

#ifndef __ASSEMBLY__

/*
 * This much address space is reserved for vmalloc() and iomap()
 * as well as fixmap mappings.
 */
extern unsigned int __VMALLOC_RESERVE;

extern void find_low_pfn_range(void);

#endif

#endif
