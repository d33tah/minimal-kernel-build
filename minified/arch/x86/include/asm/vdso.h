 
#ifndef _ASM_X86_VDSO_H
#define _ASM_X86_VDSO_H

#include <asm/page_types.h>
#include <linux/linkage.h>
#include <linux/init.h>

#ifndef __ASSEMBLER__

#include <linux/mm_types.h>

struct vdso_image {
	void *data;
	unsigned long size;    

	unsigned long alt, alt_len;
	unsigned long extable_base, extable_len;
	const void *extable;

	long sym_vvar_start;   

	long sym_vvar_page, sym_pvclock_page, sym_hvclock_page, sym_timens_page, sym_VDSO32_NOTE_MASK, sym___kernel_vsyscall, sym_int80_landing_pad;
};



extern const struct vdso_image vdso_image_32;

extern void __init init_vdso_image(const struct vdso_image *image);

extern bool fixup_vdso_exception(struct pt_regs *regs, int trapnr,
				 unsigned long error_code,
				 unsigned long fault_addr);
#endif  

#endif  
