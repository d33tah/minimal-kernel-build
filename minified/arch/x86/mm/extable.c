#include <linux/extable.h>
#include <linux/uaccess.h>
#include <linux/sched/debug.h>

/* Inlined from bitfield.h */
#include <linux/build_bug.h>
#include <asm/byteorder.h>

#define __bf_shf(x) (__builtin_ffsll(x) - 1)

#define __scalar_type_to_unsigned_cases(type) \
	unsigned type : (unsigned type)0, signed type : (unsigned type)0

#define __unsigned_scalar_typeof(x)                                \
	typeof(_Generic((x),                                       \
		       char: (unsigned char)0,                     \
		       __scalar_type_to_unsigned_cases(char),      \
		       __scalar_type_to_unsigned_cases(short),     \
		       __scalar_type_to_unsigned_cases(int),       \
		       __scalar_type_to_unsigned_cases(long),      \
		       __scalar_type_to_unsigned_cases(long long), \
		       default: (x)))

#define __bf_cast_unsigned(type, x) ((__unsigned_scalar_typeof(type))(x))

#define __BF_FIELD_CHECK(_mask, _reg, _val, _pfx)                         \
	({                                                                \
		BUILD_BUG_ON_MSG(!__builtin_constant_p(_mask),            \
				 _pfx "mask is not constant");            \
		BUILD_BUG_ON_MSG((_mask) == 0, _pfx "mask is zero");      \
		BUILD_BUG_ON_MSG(__builtin_constant_p(_val) ?             \
					 ~((_mask) >> __bf_shf(_mask)) &  \
						 (_val) :                 \
					 0,                               \
				 _pfx "value too large for the field");   \
		BUILD_BUG_ON_MSG(__bf_cast_unsigned(_mask, _mask) >       \
					 __bf_cast_unsigned(_reg, ~0ull), \
				 _pfx "type of reg too small for mask");  \
		__BUILD_BUG_ON_NOT_POWER_OF_2((_mask) +                   \
					      (1ULL << __bf_shf(_mask))); \
	})

#define FIELD_GET(_mask, _reg)                                          \
	({                                                              \
		__BF_FIELD_CHECK(_mask, _reg, 0U, "FIELD_GET: ");       \
		(typeof(_mask))(((_reg) & (_mask)) >> __bf_shf(_mask)); \
	})

#include <asm/fpu/api.h>
/* asm/sev.h include removed - file is stub, nothing used */
#include <asm/traps.h>
#include <asm/kdebug.h>
/* pt_regs_offset inlined - always returns -1, so pt_regs_nr always returns &__dummy */

static inline unsigned long *pt_regs_nr(struct pt_regs *regs, int nr)
{
	static unsigned long __dummy;
	/* Always returns &__dummy since pt_regs_offset was always -1 */
	WARN_ON_ONCE(true);
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

static bool ex_handler_fault(const struct exception_table_entry *fixup,
			     struct pt_regs *regs, int trapnr)
{
	regs->ax = trapnr;
	return ex_handler_default(fixup, regs);
}

/* ex_handler_sgx removed - no SGX in minimal kernel */
/* ex_handler_fprestore inlined into fixup_exception */

static bool ex_handler_uaccess(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int trapnr)
{
	WARN_ONCE(
		trapnr == X86_TRAP_GP,
		"General protection fault in user access. Non-canonical address?");
	return ex_handler_default(fixup, regs);
}

/* ex_handler_copy inlined into fixup_exception */

static bool ex_handler_msr(const struct exception_table_entry *fixup,
			   struct pt_regs *regs, bool wrmsr, bool safe, int reg)
{
	if (!safe && wrmsr &&
	    pr_warn_once(
		    "unchecked MSR access error: WRMSR to 0x%x (tried to write 0x%08x%08x) at rIP: 0x%lx (%pS)\n",
		    (unsigned int)regs->cx, (unsigned int)regs->dx,
		    (unsigned int)regs->ax, regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	if (!safe && !wrmsr &&
	    pr_warn_once(
		    "unchecked MSR access error: RDMSR from 0x%x at rIP: 0x%lx (%pS)\n",
		    (unsigned int)regs->cx, regs->ip, (void *)regs->ip))
		show_stack_regs(regs);

	if (!wrmsr) {
		regs->ax = 0;
		regs->dx = 0;
	}

	if (safe)
		*pt_regs_nr(regs, reg) = -EIO;

	return ex_handler_default(fixup, regs);
}

/* ex_handler_clear_fs inlined into fixup_exception */
/* ex_handler_imm_reg inlined into fixup_exception */
/* ex_handler_ucopy_len inlined into fixup_exception */
/* ex_get_fixup_type removed - never called */

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
	case EX_TYPE_DEFAULT_MCE_SAFE:
		return ex_handler_default(e, regs);
	case EX_TYPE_FAULT:
	case EX_TYPE_FAULT_MCE_SAFE:
		return ex_handler_fault(e, regs, trapnr);
	case EX_TYPE_UACCESS:
		return ex_handler_uaccess(e, regs, trapnr);
	case EX_TYPE_COPY:
		/* ex_handler_copy inlined */
		WARN_ONCE(
			trapnr == X86_TRAP_GP,
			"General protection fault in user access. Non-canonical address?");
		return ex_handler_fault(e, regs, trapnr);
	case EX_TYPE_CLEAR_FS:
		/* ex_handler_clear_fs inlined */
		if (static_cpu_has(X86_BUG_NULL_SEG))
			asm volatile("mov %0, %%fs" : : "rm"(__USER_DS));
		asm volatile("mov %0, %%fs" : : "rm"(0));
		return ex_handler_default(e, regs);
	case EX_TYPE_FPU_RESTORE:
		/* ex_handler_fprestore inlined */
		regs->ip = ex_fixup_addr(e);
		WARN_ONCE(
			1,
			"Bad FPU state detected at %pB, reinitializing FPU registers.",
			(void *)instruction_pointer(regs));
		fpu_reset_from_exception_fixup();
		return true;
	/* EX_TYPE_BPF removed - never used in this minimal kernel */
	case EX_TYPE_WRMSR:
		return ex_handler_msr(e, regs, true, false, reg);
	case EX_TYPE_RDMSR:
		return ex_handler_msr(e, regs, false, false, reg);
	case EX_TYPE_WRMSR_SAFE:
		return ex_handler_msr(e, regs, true, true, reg);
	case EX_TYPE_RDMSR_SAFE:
		return ex_handler_msr(e, regs, false, true, reg);
	/* EX_TYPE_WRMSR_IN_MCE, EX_TYPE_RDMSR_IN_MCE removed - never used */
	case EX_TYPE_POP_REG:
		regs->sp += sizeof(long);
		fallthrough;
	case EX_TYPE_IMM_REG:
		/* ex_handler_imm_reg inlined */
		*pt_regs_nr(regs, reg) = (long)imm;
		return ex_handler_default(e, regs);
	/* EX_TYPE_FAULT_SGX removed - no SGX in minimal kernel */
	case EX_TYPE_UCOPY_LEN:
		/* ex_handler_ucopy_len inlined */
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
	/* early_printk removed - function is empty stub */
	show_regs(regs);

halt_loop:
	while (true)
		halt();
}
