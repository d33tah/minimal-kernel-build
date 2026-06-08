 
#ifndef _UAPI_ASM_X86_VM86_H
#define _UAPI_ASM_X86_VM86_H

 

#include <asm/processor-flags.h>

struct vm86_regs {
 
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	long __null_ds;
	long __null_es;
	long __null_fs;
	long __null_gs;
	long orig_eax;
	long eip;
	unsigned short cs, __csh;
	long eflags;
	long esp;
	unsigned short ss, __ssh;
 
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

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
	struct vm86_regs regs;
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
};


#endif  
