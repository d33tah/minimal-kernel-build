#include <linux/syscalls.h>

/* ptrace stubs (merged from ptrace.c) */

void __noreturn make_task_dead(int signr)
{
	panic("make_task_dead(%d)\n", signr);
}

/* Stub: exit syscalls - Hello World doesn't need to exit cleanly */
SYSCALL_DEFINE1(exit, int, error_code)
{
	return 0;
}

/* wait syscalls replaced with COND_SYSCALL */
