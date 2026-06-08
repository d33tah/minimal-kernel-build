 
#ifndef _ASM_X86_UNWIND_H
#define _ASM_X86_UNWIND_H

#include <linux/sched.h>
#include <linux/rethook.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

#define IRET_FRAME_OFFSET (offsetof(struct pt_regs, ip))
#define IRET_FRAME_SIZE   (sizeof(struct pt_regs) - IRET_FRAME_OFFSET)

struct unwind_state {
	struct stack_info stack_info;
	unsigned long stack_mask;
	struct task_struct *task;
	int graph_idx;
	bool error;
	unsigned long *sp;
};

bool unwind_next_frame(struct unwind_state *state);
unsigned long unwind_get_return_address(struct unwind_state *state);
unsigned long *unwind_get_return_address_ptr(struct unwind_state *state);

static inline bool unwind_done(struct unwind_state *state)
{
	return state->stack_info.type == STACK_TYPE_UNKNOWN;
}

static inline void unwind_init(void) {}

static inline
unsigned long unwind_recover_rethook(struct unwind_state *state,
				     unsigned long addr, unsigned long *addr_p)
{
	return addr;
}

 
static inline
unsigned long unwind_recover_ret_addr(struct unwind_state *state,
				     unsigned long addr, unsigned long *addr_p)
{
	unsigned long ret;

	ret = ftrace_graph_ret_addr(state->task, &state->graph_idx,
				    addr, addr_p);
	return unwind_recover_rethook(state, ret, addr_p);
}

 
#define READ_ONCE_TASK_STACK(task, x)			\
({							\
	unsigned long val;				\
	if (task == current)				\
		val = READ_ONCE(x);			\
	else						\
		val = READ_ONCE_NOCHECK(x);		\
	val;						\
})

#endif
