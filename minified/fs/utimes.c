#include <linux/syscalls.h>
#include <linux/fs.h>

SYSCALL_DEFINE4(utimensat, int, dfd, const char __user *, filename,
		struct __kernel_timespec __user *, utimes, int, flags) { return 0; }
