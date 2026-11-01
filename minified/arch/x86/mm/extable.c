#include <linux/extable.h>

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

#include <asm/traps.h>

/*
 * Register numbers of _ASM_EXTABLE_TYPE_REG (asm.h: ax=0 ... di=7) and
 * EX_REG_DS/ES/FS (8-10) to their pt_regs slots. This used to be the
 * instruction decoder's pt_regs_offset(); once that was compiled out, a
 * zero-filled data array of the same name satisfied the link, so every
 * register-type fixup (UCOPY_LEN, ZERO_REG, POP_ZERO) executed .bss as code
 * and a faulting copy_from_user() never returned -EFAULT.
 */
static const int pt_regoff[] = {
	offsetof(struct pt_regs, ax), offsetof(struct pt_regs, cx),
	offsetof(struct pt_regs, dx), offsetof(struct pt_regs, bx),
	offsetof(struct pt_regs, sp), offsetof(struct pt_regs, bp),
	offsetof(struct pt_regs, si), offsetof(struct pt_regs, di),
	offsetof(struct pt_regs, ds), offsetof(struct pt_regs, es),
	offsetof(struct pt_regs, fs), offsetof(struct pt_regs, gs),
};

static inline unsigned long *pt_regs_nr(struct pt_regs *regs, int nr)
{
	static unsigned long __dummy;

	if (WARN_ON_ONCE((unsigned int)nr >= ARRAY_SIZE(pt_regoff)))
		return &__dummy;

	return (unsigned long *)((unsigned long)regs + pt_regoff[nr]);
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

static bool ex_handler_imm_reg(const struct exception_table_entry *fixup,
			       struct pt_regs *regs, int reg, int imm)
{
	*pt_regs_nr(regs, reg) = (long)imm;
	return ex_handler_default(fixup, regs);
}

static bool ex_handler_ucopy_len(const struct exception_table_entry *fixup,
				 struct pt_regs *regs, int trapnr, int reg,
				 int imm)
{
	regs->cx = imm * regs->cx + *pt_regs_nr(regs, reg);
	return ex_handler_uaccess(fixup, regs, trapnr);
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
	case EX_TYPE_POP_REG:
		regs->sp += sizeof(long);
		fallthrough;
	case EX_TYPE_IMM_REG:
		return ex_handler_imm_reg(e, regs, reg, imm);
	case EX_TYPE_UCOPY_LEN:
		return ex_handler_ucopy_len(e, regs, trapnr, reg, imm);
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

fail:

halt_loop:
	while (true)
		halt();
}
