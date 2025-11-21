 
 
 

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/security.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <linux/signal.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>
#include <linux/rcupdate.h>
#include <linux/export.h>
#include <linux/context_tracking.h>
#include <linux/nospec.h>

#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>
#include <asm/fpu/xstate.h>
#include <asm/debugreg.h>
#include <asm/ldt.h>
#include <asm/desc.h>
#include <asm/prctl.h>
#include <asm/proto.h>
#include <asm/hw_breakpoint.h>
#include <asm/traps.h>
#include <asm/syscall.h>
#include <asm/fsgsbase.h>
#include <asm/io_bitmap.h>

#include "tls.h"

enum x86_regset {
	REGSET_GENERAL,
	REGSET_FP,
	REGSET_XFP,
	REGSET_IOPERM64 = REGSET_XFP,
	REGSET_XSTATE,
	REGSET_TLS,
	REGSET_IOPERM32,
};

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r) {.name = #r, .offset = offsetof(struct pt_regs, r)}
#define REG_OFFSET_END {.name = NULL, .offset = 0}

static const struct pt_regs_offset regoffset_table[] = {
	REG_OFFSET_NAME(bx),
	REG_OFFSET_NAME(cx),
	REG_OFFSET_NAME(dx),
	REG_OFFSET_NAME(si),
	REG_OFFSET_NAME(di),
	REG_OFFSET_NAME(bp),
	REG_OFFSET_NAME(ax),
	REG_OFFSET_NAME(ds),
	REG_OFFSET_NAME(es),
	REG_OFFSET_NAME(fs),
	REG_OFFSET_NAME(gs),
	REG_OFFSET_NAME(orig_ax),
	REG_OFFSET_NAME(ip),
	REG_OFFSET_NAME(cs),
	REG_OFFSET_NAME(flags),
	REG_OFFSET_NAME(sp),
	REG_OFFSET_NAME(ss),
	REG_OFFSET_END,
};

 
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

 
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;
	for (roff = regoffset_table; roff->name != NULL; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

 

 
#define FLAG_MASK_32		((unsigned long)			\
				 (X86_EFLAGS_CF | X86_EFLAGS_PF |	\
				  X86_EFLAGS_AF | X86_EFLAGS_ZF |	\
				  X86_EFLAGS_SF | X86_EFLAGS_TF |	\
				  X86_EFLAGS_DF | X86_EFLAGS_OF |	\
				  X86_EFLAGS_RF | X86_EFLAGS_AC))

 
static inline bool invalid_selector(u16 value)
{
	return unlikely(value != 0 && (value & SEGMENT_RPL_MASK) != USER_RPL);
}


#define FLAG_MASK		FLAG_MASK_32

static unsigned long *pt_regs_access(struct pt_regs *regs, unsigned long regno)
{
	BUILD_BUG_ON(offsetof(struct pt_regs, bx) != 0);
	return &regs->bx + (regno >> 2);
}

static u16 get_segment_reg(struct task_struct *task, unsigned long offset)
{
	 
	unsigned int retval;
	if (offset != offsetof(struct user_regs_struct, gs))
		retval = *pt_regs_access(task_pt_regs(task), offset);
	else {
		if (task == current)
			savesegment(gs, retval);
		else
			retval = task->thread.gs;
	}
	return retval;
}

static int set_segment_reg(struct task_struct *task,
			   unsigned long offset, u16 value)
{
	if (WARN_ON_ONCE(task == current))
		return -EIO;

	 
	if (invalid_selector(value))
		return -EIO;

	 
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ss):
		if (unlikely(value == 0))
			return -EIO;
		fallthrough;

	default:
		*pt_regs_access(task_pt_regs(task), offset) = value;
		break;

	case offsetof(struct user_regs_struct, gs):
		task->thread.gs = value;
	}

	return 0;
}


static unsigned long get_flags(struct task_struct *task)
{
	unsigned long retval = task_pt_regs(task)->flags;

	 
	if (test_tsk_thread_flag(task, TIF_FORCED_TF))
		retval &= ~X86_EFLAGS_TF;

	return retval;
}

static int set_flags(struct task_struct *task, unsigned long value)
{
	struct pt_regs *regs = task_pt_regs(task);

	 
	if (value & X86_EFLAGS_TF)
		clear_tsk_thread_flag(task, TIF_FORCED_TF);
	else if (test_tsk_thread_flag(task, TIF_FORCED_TF))
		value |= X86_EFLAGS_TF;

	regs->flags = (regs->flags & ~FLAG_MASK) | (value & FLAG_MASK);

	return 0;
}

static int putreg(struct task_struct *child,
		  unsigned long offset, unsigned long value)
{
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ds):
	case offsetof(struct user_regs_struct, es):
	case offsetof(struct user_regs_struct, fs):
	case offsetof(struct user_regs_struct, gs):
	case offsetof(struct user_regs_struct, ss):
		return set_segment_reg(child, offset, value);

	case offsetof(struct user_regs_struct, flags):
		return set_flags(child, value);

	}

	*pt_regs_access(task_pt_regs(child), offset) = value;
	return 0;
}

static unsigned long getreg(struct task_struct *task, unsigned long offset)
{
	switch (offset) {
	case offsetof(struct user_regs_struct, cs):
	case offsetof(struct user_regs_struct, ds):
	case offsetof(struct user_regs_struct, es):
	case offsetof(struct user_regs_struct, fs):
	case offsetof(struct user_regs_struct, gs):
	case offsetof(struct user_regs_struct, ss):
		return get_segment_reg(task, offset);

	case offsetof(struct user_regs_struct, flags):
		return get_flags(task);

	}

	return *pt_regs_access(task_pt_regs(task), offset);
}

static int genregs_get(struct task_struct *target,
		       const struct user_regset *regset,
		       struct membuf to)
{
	int reg;

	for (reg = 0; to.left; reg++)
		membuf_store(&to, getreg(target, reg * sizeof(unsigned long)));
	return 0;
}

static int genregs_set(struct task_struct *target,
		       const struct user_regset *regset,
		       unsigned int pos, unsigned int count,
		       const void *kbuf, const void __user *ubuf)
{
	int ret = 0;
	if (kbuf) {
		const unsigned long *k = kbuf;
		while (count >= sizeof(*k) && !ret) {
			ret = putreg(target, pos, *k++);
			count -= sizeof(*k);
			pos += sizeof(*k);
		}
	} else {
		const unsigned long  __user *u = ubuf;
		while (count >= sizeof(*u) && !ret) {
			unsigned long word;
			ret = __get_user(word, u++);
			if (ret)
				break;
			ret = putreg(target, pos, word);
			count -= sizeof(*u);
			pos += sizeof(*u);
		}
	}
	return ret;
}

static void ptrace_triggered(struct perf_event *bp,
			     struct perf_sample_data *data,
			     struct pt_regs *regs)
{
	int i;
	struct thread_struct *thread = &(current->thread);

	 
	for (i = 0; i < HBP_NUM; i++) {
		if (thread->ptrace_bps[i] == bp)
			break;
	}

	thread->virtual_dr6 |= (DR_TRAP0 << i);
}

 

static int ptrace_fill_bp_fields(struct perf_event_attr *attr,
					int len, int type, bool disabled)
{
	int err, bp_len, bp_type;

	err = arch_bp_generic_fields(len, type, &bp_len, &bp_type);
	if (!err) {
		attr->bp_len = bp_len;
		attr->bp_type = bp_type;
		attr->disabled = disabled;
	}

	return err;
}




static int ioperm_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	struct io_bitmap *iobm = target->thread.io_bitmap;

	return iobm ? DIV_ROUND_UP(iobm->max, regset->size) : 0;
}

static int ioperm_get(struct task_struct *target,
		      const struct user_regset *regset,
		      struct membuf to)
{
	struct io_bitmap *iobm = target->thread.io_bitmap;

	if (!iobm)
		return -ENXIO;

	return membuf_write(&to, iobm->bitmap, IO_BITMAP_BYTES);
}

 
void ptrace_disable(struct task_struct *child)
{
	user_disable_single_step(child);
}

static const struct user_regset_view user_x86_32_view;  

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	/* Stub: ptrace operations not needed for minimal kernel */
	return ptrace_request(child, request, addr, data);
}





#define user_regs_struct32	user_regs_struct
#define genregs32_get		genregs_get
#define genregs32_set		genregs_set


static struct user_regset x86_32_regsets[] __ro_after_init = {
	[REGSET_GENERAL] = {
		.core_note_type = NT_PRSTATUS,
		.n = sizeof(struct user_regs_struct32) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.regset_get = genregs32_get, .set = genregs32_set
	},
	[REGSET_FP] = {
		.core_note_type = NT_PRFPREG,
		.n = sizeof(struct user_i387_ia32_struct) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = regset_fpregs_active, .regset_get = fpregs_get, .set = fpregs_set
	},
	[REGSET_XFP] = {
		.core_note_type = NT_PRXFPREG,
		.n = sizeof(struct fxregs_state) / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = regset_xregset_fpregs_active, .regset_get = xfpregs_get, .set = xfpregs_set
	},
	[REGSET_XSTATE] = {
		.core_note_type = NT_X86_XSTATE,
		.size = sizeof(u64), .align = sizeof(u64),
		.active = xstateregs_active, .regset_get = xstateregs_get,
		.set = xstateregs_set
	},
	[REGSET_TLS] = {
		.core_note_type = NT_386_TLS,
		.n = GDT_ENTRY_TLS_ENTRIES, .bias = GDT_ENTRY_TLS_MIN,
		.size = sizeof(struct user_desc),
		.align = sizeof(struct user_desc),
		.active = regset_tls_active,
		.regset_get = regset_tls_get, .set = regset_tls_set
	},
	[REGSET_IOPERM32] = {
		.core_note_type = NT_386_IOPERM,
		.n = IO_BITMAP_BYTES / sizeof(u32),
		.size = sizeof(u32), .align = sizeof(u32),
		.active = ioperm_active, .regset_get = ioperm_get
	},
};

static const struct user_regset_view user_x86_32_view = {
	.name = "i386", .e_machine = EM_386,
	.regsets = x86_32_regsets, .n = ARRAY_SIZE(x86_32_regsets)
};

 
u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

void __init update_regset_xstate_info(unsigned int size, u64 xstate_mask)
{
	x86_32_regsets[REGSET_XSTATE].n = size / sizeof(u64);
	xstate_fx_sw_bytes[USER_XSTATE_XCR0_WORD] = xstate_mask;
}

 
const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
		return &user_x86_32_view;
}

void send_sigtrap(struct pt_regs *regs, int error_code, int si_code)
{
	struct task_struct *tsk = current;

	tsk->thread.trap_nr = X86_TRAP_DB;
	tsk->thread.error_code = error_code;

	 
	force_sig_fault(SIGTRAP, si_code,
			user_mode(regs) ? (void __user *)regs->ip : NULL);
}

void user_single_step_report(struct pt_regs *regs)
{
	send_sigtrap(regs, 0, TRAP_BRKPT);
}
