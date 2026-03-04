#include <linux/extable.h>
#include <asm/extable.h>

#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(_mask, _reg) \
	((typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask)))

#include <asm/fpu/api.h>
#include <asm/traps.h>
#include <asm/kdebug.h>

static inline unsigned long *pt_regs_nr(struct pt_regs *regs, int nr)
{
	static unsigned long __dummy;
	return &__dummy;
}

static inline unsigned long ex_fixup_addr(const struct exception_table_entry *x)
{
	return (unsigned long)&x->fixup + x->fixup;
}

static bool ex_handler_default(const struct exception_table_entry *e,
			       struct pt_regs *regs)
{
	if (e->data & EX_FLAG_CLEAR_AX)
		regs->ax = 0;
	if (e->data & EX_FLAG_CLEAR_DX)
		regs->dx = 0;

	regs->ip = ex_fixup_addr(e);
	return true;
}

static bool ex_handler_uaccess(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int trapnr)
{
	return ex_handler_default(fixup, regs);
}

int fixup_exception(struct pt_regs *regs, int trapnr, unsigned long error_code,
		    unsigned long fault_addr)
{
	const struct exception_table_entry *e;
	int type, reg, imm;

	e = search_exception_tables(regs->ip);
	if (!e)
		return 0;

	type = FIELD_GET(EX_DATA_TYPE_MASK, e->data);
	reg = FIELD_GET(EX_DATA_REG_MASK, e->data);
	imm = FIELD_GET(EX_DATA_IMM_MASK, e->data);

	switch (type) {
	case EX_TYPE_DEFAULT:
		return ex_handler_default(e, regs);
	case EX_TYPE_UACCESS:
		return ex_handler_uaccess(e, regs, trapnr);
	case EX_TYPE_FPU_RESTORE:
		regs->ip = ex_fixup_addr(e);
		fpu_reset_from_exception_fixup();
		return true;
	case EX_TYPE_POP_REG:
		regs->sp += sizeof(long);
		fallthrough;
	case EX_TYPE_IMM_REG:
		*pt_regs_nr(regs, reg) = (long)imm;
		return ex_handler_default(e, regs);
	case EX_TYPE_UCOPY_LEN:
		regs->cx = imm * regs->cx + *pt_regs_nr(regs, reg);
		return ex_handler_uaccess(e, regs, trapnr);
	}
	BUG();
}

extern unsigned int early_recursion_flag;

void __init early_fixup_exception(struct pt_regs *regs, int trapnr)
{
	if (trapnr == X86_TRAP_NMI)
		return;

	if (early_recursion_flag > 2)
		goto halt_loop;

	if (regs->cs != __KERNEL_CS)
		goto fail;

	if (fixup_exception(regs, trapnr, regs->orig_ax, 0))
		return;

	if (trapnr == X86_TRAP_UD) {
		if (report_bug(regs->ip, regs) == BUG_TRAP_TYPE_WARN) {
			regs->ip += LEN_UD2;
			return;
		}
	}

fail:
halt_loop:
	while (true)
		asm volatile("hlt" : : : "memory");
}
