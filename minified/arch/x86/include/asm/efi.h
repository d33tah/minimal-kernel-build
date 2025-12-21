 
#ifndef _ASM_X86_EFI_H
#define _ASM_X86_EFI_H

 

#include <linux/build_bug.h>
#include <linux/kernel.h>
#include <asm/tlbflush.h>

#define EFI32_LOADER_SIGNATURE	"EL32"
#define EFI64_LOADER_SIGNATURE	"EL64"

#define ARCH_EFI_IRQ_FLAGS_MASK	X86_EFLAGS_IF


#define arch_ima_efi_boot_mode	\
	({ extern struct boot_params boot_params; boot_params.secure_boot; })

#endif  
