 
 

#ifndef _ASM_X86_STACKTRACE_H
#define _ASM_X86_STACKTRACE_H

#include <linux/uaccess.h>
#include <linux/ptrace.h>

#include <asm/cpu_entry_area.h>
#include <asm/switch_to.h>

enum stack_type {
	STACK_TYPE_UNKNOWN,
	STACK_TYPE_TASK,
	STACK_TYPE_IRQ,
	STACK_TYPE_SOFTIRQ,
	STACK_TYPE_ENTRY,
	STACK_TYPE_EXCEPTION,
	STACK_TYPE_EXCEPTION_LAST = STACK_TYPE_EXCEPTION + N_EXCEPTION_STACKS-1,
};

struct stack_info {
	enum stack_type type;
	unsigned long *begin, *end, *next_sp;
};

bool in_task_stack(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info);

bool in_entry_stack(unsigned long *stack, struct stack_info *info);

int get_stack_info(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask);
bool get_stack_info_noinstr(unsigned long *stack, struct task_struct *task,
			    struct stack_info *info);

/* get_stack_guard_info, on_stack, get_frame_pointer, get_stack_pointer, stack_type_name removed - never used */

/* STACKSLOTS_PER_LINE removed - never used */

#endif
