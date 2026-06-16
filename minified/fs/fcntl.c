/* Stub fcntl - minimal file control */
#include <linux/fs.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -EINVAL; }
