 
 

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

#define STACKSLOTS_PER_LINE 8


struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

/* show_opcodes, show_ip removed - unused */
#endif  
