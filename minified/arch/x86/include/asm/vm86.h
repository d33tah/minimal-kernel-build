#ifndef _ASM_X86_VM86_H
#define _ASM_X86_VM86_H

#include <asm/ptrace.h>

/* Inlined from uapi/asm/vm86.h */
struct revectored_struct {
	unsigned long __map[8];
};
struct vm86plus_info_struct {
	unsigned long force_return_for_pic:1;
	unsigned long vm86dbg_active:1;
	unsigned long vm86dbg_TFpendig:1;
	unsigned long unused:28;
	unsigned long is_vm86pus:1;
	unsigned char vm86dbg_intxxtab[32];
};
struct vm86plus_struct {
	unsigned long flags;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
};

struct kernel_vm86_regs {
	struct pt_regs pt;
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct vm86 {
	struct vm86plus_struct __user *user_vm86;
	struct pt_regs regs32;
	unsigned long veflags;
	unsigned long veflags_mask;
	unsigned long saved_sp0;
	unsigned long flags;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
};

#define handle_vm86_fault(a, b)

#endif
