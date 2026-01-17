 
#ifndef _ASM_X86_BOOTPARAM_UTILS_H
#define _ASM_X86_BOOTPARAM_UTILS_H

#include <asm/bootparam.h>

 

 

/* sizeof_mbr, BOOT_PARAM_PRESERVE, boot_params_to_save removed - unused */

static void sanitize_boot_params(struct boot_params *boot_params)
{
	/* Stubbed - trust boot loader, save 4KB BSS */
	(void)boot_params;
}

#endif  
