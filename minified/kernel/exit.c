#include <linux/syscalls.h>

/* ptrace stubs (merged from ptrace.c) */

void __noreturn make_task_dead(int signr)
{
	panic("make_task_dead(%d)\n", signr);
}

/* do_exit gutted - Hello World kernel never exits processes.
 * init loops forever, kthreadd loops forever, no kthreads created.
 * Only reachable via make_task_dead (fatal errors). */
void __noreturn do_exit(long code)
{
	panic("do_exit called with code %ld\n", code);
}

/* Stub: exit syscalls - Hello World doesn't need to exit cleanly */
SYSCALL_DEFINE1(exit, int, error_code)
{
	return 0;
}

/* wait syscalls replaced with COND_SYSCALL */
