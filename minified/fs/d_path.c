/* Stub d_path - path to string conversion */
#include <linux/fs.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE2(getcwd, char __user *, buf, unsigned long, size) { return -ENOSYS; }
