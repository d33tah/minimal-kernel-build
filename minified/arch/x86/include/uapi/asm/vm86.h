/* Minimal vm86 header - VM86 syscall is stubbed */
#ifndef _UAPI_ASM_X86_VM86_H
#define _UAPI_ASM_X86_VM86_H

#define VM86_SIGNAL	0

/* Minimal structs needed for asm/vm86.h struct vm86 definition */
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

#endif
