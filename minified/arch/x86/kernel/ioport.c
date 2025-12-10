/* Stub IO port syscalls */
#include <linux/syscalls.h>

SYSCALL_DEFINE3(ioperm, unsigned long, from, unsigned long, num, int, turn_on) { return -ENOSYS; }
SYSCALL_DEFINE1(iopl, unsigned int, level) { return -ENOSYS; }
