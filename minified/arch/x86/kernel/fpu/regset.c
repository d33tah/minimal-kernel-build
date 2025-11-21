// Stubbed FPU register set operations for ptrace
// Original: 242 LOC
// Not needed for minimal boot without ptrace

#include <linux/sched/task_stack.h>
#include <linux/vmalloc.h>

#include <asm/fpu/api.h>
#include <asm/fpu/signal.h>
#include <asm/fpu/regset.h>

#include "context.h"
#include "internal.h"
#include "legacy.h"
#include "xstate.h"

// Stub: no regsets active
int regset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return 0;
}

int regset_xregset_fpregs_active(struct task_struct *target, const struct user_regset *regset)
{
	return 0;
}

// Stub: FPU state get/set operations not supported
int xfpregs_get(struct task_struct *target, const struct user_regset *regset,
		struct membuf to)
{
	return -ENODEV;
}

int xfpregs_set(struct task_struct *target, const struct user_regset *regset,
		unsigned int pos, unsigned int count,
		const void *kbuf, const void __user *ubuf)
{
	return -ENODEV;
}

int xstateregs_get(struct task_struct *target, const struct user_regset *regset,
		   struct membuf to)
{
	return -ENODEV;
}

int xstateregs_set(struct task_struct *target, const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	return -ENODEV;
}

void convert_from_fxsr(struct user_i387_ia32_struct *env, struct task_struct *tsk)
{
	// Stub: no conversion
}

void convert_to_fxsr(struct fxregs_state *fxsave,
		     const struct user_i387_ia32_struct *env)
{
	// Stub: no conversion
}

int fpregs_get(struct task_struct *target, const struct user_regset *regset,
	       struct membuf to)
{
	return -ENODEV;
}

int fpregs_set(struct task_struct *target, const struct user_regset *regset,
	       unsigned int pos, unsigned int count,
	       const void *kbuf, const void __user *ubuf)
{
	return -ENODEV;
}
