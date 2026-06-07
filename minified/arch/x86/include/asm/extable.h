 
#ifndef _ASM_X86_EXTABLE_H
#define _ASM_X86_EXTABLE_H

#ifndef EX_DATA_REG_SHIFT /* guard - may be defined in asm/asm.h */
#define EX_DATA_TYPE_MASK		((int)0x000000FF)
#define EX_DATA_REG_MASK		((int)0x00000F00)
#define EX_DATA_IMM_MASK		((int)0xFFFF0000)

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
#define	EX_TYPE_UACCESS			 3
#define	EX_TYPE_FPU_RESTORE		 6

#define	EX_TYPE_POP_REG			16
#define EX_TYPE_POP_ZERO		(EX_TYPE_POP_REG | EX_DATA_IMM(0))

#define	EX_TYPE_IMM_REG			17
#define	EX_TYPE_ZERO_REG		(EX_TYPE_IMM_REG | EX_DATA_IMM(0))

#define	EX_TYPE_UCOPY_LEN		19
#define	EX_TYPE_UCOPY_LEN1		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(1))
#define	EX_TYPE_UCOPY_LEN4		(EX_TYPE_UCOPY_LEN | EX_DATA_IMM(4))

#endif /* EX_DATA_REG_SHIFT */

struct exception_table_entry {
	int insn, fixup, data;
};
struct pt_regs;

#define ARCH_HAS_RELATIVE_EXTABLE

#define swap_ex_entry_fixup(a, b, tmp, delta)			\
	do {							\
		(a)->fixup = (b)->fixup + (delta);		\
		(b)->fixup = (tmp).fixup - (delta);		\
		(a)->data = (b)->data;				\
		(b)->data = (tmp).data;				\
	} while (0)

extern int fixup_exception(struct pt_regs *regs, int trapnr,
			   unsigned long error_code, unsigned long fault_addr);
extern void early_fixup_exception(struct pt_regs *regs, int trapnr);

#endif
