 
#ifndef _ASM_X86_BOOT_H
#define _ASM_X86_BOOT_H


#include <asm/pgtable_types.h>

/* Inlined from uapi/asm/boot.h */
#define NORMAL_VGA	0xffff

 
#define LOAD_PHYSICAL_ADDR ((CONFIG_PHYSICAL_START \
				+ (CONFIG_PHYSICAL_ALIGN - 1)) \
				& ~(CONFIG_PHYSICAL_ALIGN - 1))

 
# define MIN_KERNEL_ALIGN_LG2	(PAGE_SHIFT + THREAD_SIZE_ORDER)
#define MIN_KERNEL_ALIGN	(_AC(1, UL) << MIN_KERNEL_ALIGN_LG2)

#if (CONFIG_PHYSICAL_ALIGN & (CONFIG_PHYSICAL_ALIGN-1)) || \
	(CONFIG_PHYSICAL_ALIGN < MIN_KERNEL_ALIGN)
# error "Invalid value for CONFIG_PHYSICAL_ALIGN"
#endif

# define BOOT_HEAP_SIZE		 0x1000  /* Reduced from 64KB to 4KB */

# define BOOT_STACK_SIZE	0x800  /* Reduced from 4KB to 2KB */

#endif  
