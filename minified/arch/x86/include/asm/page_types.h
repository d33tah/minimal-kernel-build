 
#ifndef _ASM_X86_PAGE_DEFS_H
#define _ASM_X86_PAGE_DEFS_H

#include <linux/const.h>
#include <linux/types.h>
#include <linux/mem_encrypt.h>

#define PAGE_SHIFT		12
#define PAGE_SIZE		(_AC(1,UL) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE-1))

#define PMD_PAGE_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_PAGE_MASK		(~(PMD_PAGE_SIZE-1))

#define PHYSICAL_PAGE_MASK	(((signed long)PAGE_MASK) & __PHYSICAL_MASK)
#define PHYSICAL_PMD_PAGE_MASK	(((signed long)PMD_PAGE_MASK) & __PHYSICAL_MASK)

#define PAGE_OFFSET		((unsigned long)__PAGE_OFFSET)

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_EXEC

#ifndef _ASM_X86_PAGE_32_DEFS_H
#define _ASM_X86_PAGE_32_DEFS_H

#define __PAGE_OFFSET_BASE	_AC(CONFIG_PAGE_OFFSET, UL)
#define __PAGE_OFFSET		__PAGE_OFFSET_BASE

#define THREAD_SIZE_ORDER	1
#define THREAD_SIZE		(PAGE_SIZE << THREAD_SIZE_ORDER)

#define IRQ_STACK_SIZE		THREAD_SIZE

#define __PHYSICAL_MASK_SHIFT	32

#define TASK_SIZE		__PAGE_OFFSET
#define TASK_SIZE_LOW		TASK_SIZE
#define TASK_SIZE_MAX		TASK_SIZE
#define DEFAULT_MAP_WINDOW	TASK_SIZE
#define STACK_TOP		TASK_SIZE
#define STACK_TOP_MAX		STACK_TOP

#define KERNEL_IMAGE_SIZE	(512 * 1024 * 1024)

#ifndef __ASSEMBLY__

extern unsigned int __VMALLOC_RESERVE;

extern void find_low_pfn_range(void);

#endif

#endif /* _ASM_X86_PAGE_32_DEFS_H */
#define IOREMAP_MAX_ORDER       (PMD_SHIFT)

#ifndef __ASSEMBLY__

#define __PHYSICAL_MASK		((phys_addr_t)((1ULL << __PHYSICAL_MASK_SHIFT) - 1))

extern unsigned long max_low_pfn_mapped;
extern unsigned long max_pfn_mapped;

static inline phys_addr_t get_max_mapped(void)
{
	return (phys_addr_t)max_pfn_mapped << PAGE_SHIFT;
}

extern void initmem_init(void);

#endif	 

#endif	 
