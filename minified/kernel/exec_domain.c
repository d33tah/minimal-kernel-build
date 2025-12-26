/* Stub: personality syscall not needed for Hello World */
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/personality.h>
SYSCALL_DEFINE1(personality, unsigned int, personality)
{
	return 0;
}
