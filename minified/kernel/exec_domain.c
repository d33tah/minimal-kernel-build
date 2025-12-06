/* Minimal includes for personality syscall */
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/personality.h>

SYSCALL_DEFINE1(personality, unsigned int, personality)
{
	unsigned int old = current->personality;

	if (personality != 0xffffffff)
		set_personality(personality);

	return old;
}
