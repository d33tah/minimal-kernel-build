 
#ifndef _ASM_X86_BOOTPARAM_UTILS_H
#define _ASM_X86_BOOTPARAM_UTILS_H

#include <asm/bootparam.h>

 

 

#define sizeof_mbr(type, member) ({ sizeof(((type *)0)->member); })

#define BOOT_PARAM_PRESERVE(struct_member)				\
	{								\
		.start = offsetof(struct boot_params, struct_member),	\
		.len   = sizeof_mbr(struct boot_params, struct_member),	\
	}

struct boot_params_to_save {
	unsigned int start;
	unsigned int len;
};

static void sanitize_boot_params(struct boot_params *boot_params)
{
	/* Stubbed - trust boot loader, save 4KB BSS */
	(void)boot_params;
}

#endif  
