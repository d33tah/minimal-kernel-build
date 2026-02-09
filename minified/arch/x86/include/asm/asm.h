 
#ifndef _ASM_X86_ASM_H
#define _ASM_X86_ASM_H

#ifdef __ASSEMBLY__
# define __ASM_FORM(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_RAW(x, ...)		x,## __VA_ARGS__
# define __ASM_FORM_COMMA(x, ...)	x,## __VA_ARGS__,
# define __ASM_REGPFX			%
#else
#include <linux/stringify.h>
# define __ASM_FORM(x, ...)		" " __stringify(x,##__VA_ARGS__) " "
# define __ASM_FORM_RAW(x, ...)		    __stringify(x,##__VA_ARGS__)
# define __ASM_FORM_COMMA(x, ...)	" " __stringify(x,##__VA_ARGS__) ","
# define __ASM_REGPFX			%%
#endif

/* _ASM_BYTES removed - unused */
/* 32-bit only kernel */
#define __ASM_SEL(a,b)		__ASM_FORM(a)
#define __ASM_SEL_RAW(a,b)	__ASM_FORM_RAW(a)

#define __ASM_SIZE(inst, ...)	__ASM_SEL(inst##l##__VA_ARGS__, \
					  inst##q##__VA_ARGS__)
#define __ASM_REG(reg)         __ASM_SEL_RAW(e##reg, r##reg)

/* _ASM_PTR, _ASM_ALIGN removed - never used */

/* Keep only used register macros */
#define _ASM_AX		__ASM_REG(ax)
#define _ASM_BX		__ASM_REG(bx)
#define _ASM_CX		__ASM_REG(cx)
#define _ASM_DX		__ASM_REG(dx)
#define _ASM_SP		__ASM_REG(sp)

#define _ASM_RIP(x)	__ASM_SEL_RAW(x, x (__ASM_REGPFX rip))

 
#ifdef __GCC_ASM_FLAG_OUTPUTS__
# define CC_SET(c) "\n\t/* output condition code " #c "*/\n"
# define CC_OUT(c) "=@cc" #c
#else
# define CC_SET(c) "\n\tset" #c " %[_cc_" #c "]\n"
# define CC_OUT(c) [_cc_ ## c] "=qm"
#endif

#ifdef __KERNEL__

/* EX_TYPE/EX_REG/EX_DATA defines from extable_fixup_types.h (assembly-safe) */
#define EX_DATA_REG_SHIFT		8
#define EX_DATA_FLAG_SHIFT		12
#define EX_DATA_IMM_SHIFT		16
#define EX_DATA_REG(reg)		((reg) << EX_DATA_REG_SHIFT)
#define EX_DATA_FLAG(flag)		((flag) << EX_DATA_FLAG_SHIFT)
#define EX_DATA_IMM(imm)		((imm) << EX_DATA_IMM_SHIFT)
#define EX_REG_DS			EX_DATA_REG(8)
#define EX_REG_ES			EX_DATA_REG(9)
#define EX_REG_FS			EX_DATA_REG(10)
#define EX_FLAG_CLEAR_AX		EX_DATA_FLAG(1)
#define EX_FLAG_CLEAR_DX		EX_DATA_FLAG(2)
#define	EX_TYPE_DEFAULT			 1
#define	EX_TYPE_FAULT			 2
#define	EX_TYPE_UACCESS			 3
#define	EX_TYPE_COPY			 4
#define	EX_TYPE_CLEAR_FS		 5
#define	EX_TYPE_FPU_RESTORE		 6
#define	EX_TYPE_WRMSR			 8
#define	EX_TYPE_RDMSR			 9
#define	EX_TYPE_WRMSR_SAFE		10
#define	EX_TYPE_RDMSR_SAFE		11
#define	EX_TYPE_DEFAULT_MCE_SAFE	14
#define	EX_TYPE_FAULT_MCE_SAFE		15
#define	EX_TYPE_POP_REG			16
#define EX_TYPE_POP_ZERO		(EX_TYPE_POP_REG | EX_DATA_IMM(0))
#define	EX_TYPE_IMM_REG			17
#define	EX_TYPE_EFAULT_REG		(EX_TYPE_IMM_REG | EX_DATA_IMM(-14))
#define	EX_TYPE_ZERO_REG		(EX_TYPE_IMM_REG | EX_DATA_IMM(0))
#define	EX_TYPE_UCOPY_LEN		19
#define	EX_TYPE_UCOPY_LEN1		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(1))
#define	EX_TYPE_UCOPY_LEN4		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(4))

 
#ifdef __ASSEMBLY__

# define _ASM_EXTABLE_TYPE(from, to, type)			\
	.pushsection "__ex_table","a" ;				\
	.balign 4 ;						\
	.long (from) - . ;					\
	.long (to) - . ;					\
	.long type ;						\
	.popsection

#  define _ASM_NOKPROBE(entry)

#else  

# define DEFINE_EXTABLE_TYPE_REG \
	".macro extable_type_reg type:req reg:req\n"						\
	".set .Lfound, 0\n"									\
	".set .Lregnr, 0\n"									\
	".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n"		\
	".ifc \\reg, %%\\rs\n"									\
	".set .Lfound, .Lfound+1\n"								\
	".long \\type + (.Lregnr << 8)\n"							\
	".endif\n"										\
	".set .Lregnr, .Lregnr+1\n"								\
	".endr\n"										\
	".set .Lregnr, 0\n"									\
	".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n"	\
	".ifc \\reg, %%\\rs\n"									\
	".set .Lfound, .Lfound+1\n"								\
	".long \\type + (.Lregnr << 8)\n"							\
	".endif\n"										\
	".set .Lregnr, .Lregnr+1\n"								\
	".endr\n"										\
	".if (.Lfound != 1)\n"									\
	".error \"extable_type_reg: bad register argument\"\n"					\
	".endif\n"										\
	".endm\n"

# define UNDEFINE_EXTABLE_TYPE_REG \
	".purgem extable_type_reg\n"

# define _ASM_EXTABLE_TYPE(from, to, type)			\
	" .pushsection \"__ex_table\",\"a\"\n"			\
	" .balign 4\n"						\
	" .long (" #from ") - .\n"				\
	" .long (" #to ") - .\n"				\
	" .long " __stringify(type) " \n"			\
	" .popsection\n"

# define _ASM_EXTABLE_TYPE_REG(from, to, type, reg)				\
	" .pushsection \"__ex_table\",\"a\"\n"					\
	" .balign 4\n"								\
	" .long (" #from ") - .\n"						\
	" .long (" #to ") - .\n"						\
	DEFINE_EXTABLE_TYPE_REG							\
	"extable_type_reg reg=" __stringify(reg) ", type=" __stringify(type) " \n"\
	UNDEFINE_EXTABLE_TYPE_REG						\
	" .popsection\n"

 

 
register unsigned long current_stack_pointer asm(_ASM_SP);
#define ASM_CALL_CONSTRAINT "+r" (current_stack_pointer)
#endif  

#define _ASM_EXTABLE(from, to)					\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_DEFAULT)

#define _ASM_EXTABLE_UA(from, to)				\
	_ASM_EXTABLE_TYPE(from, to, EX_TYPE_UACCESS)

#endif  
#endif  
