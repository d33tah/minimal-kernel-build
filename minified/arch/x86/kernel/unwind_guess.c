#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/bitops.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

unsigned long unwind_get_return_address(struct unwind_state *state)
{
	unsigned long addr;

	if (unwind_done(state))
		return 0;

	addr = READ_ONCE_NOCHECK(*state->sp);

	return unwind_recover_ret_addr(state, addr, state->sp);
}

unsigned long *unwind_get_return_address_ptr(struct unwind_state *state)
{
	return NULL;
}

bool unwind_next_frame(struct unwind_state *state)
{
	struct stack_info *info = &state->stack_info;

	if (unwind_done(state))
		return false;

	do {
		for (state->sp++; state->sp < info->end; state->sp++) {
			unsigned long addr = READ_ONCE_NOCHECK(*state->sp);

			if (__kernel_text_address(addr))
				return true;
		}

		state->sp = PTR_ALIGN(info->next_sp, sizeof(long));

	} while (!get_stack_info(state->sp, state->task, info,
				 &state->stack_mask));

	return false;
}

