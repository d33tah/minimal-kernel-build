/* Stub readdir syscalls */
#include <linux/fs.h>
#include <linux/syscalls.h>

SYSCALL_DEFINE3(getdents64, unsigned int, fd,
		struct linux_dirent64 __user *, dirent, unsigned int, count) { return 0; }
SYSCALL_DEFINE3(old_readdir, unsigned int, fd,
		struct old_linux_dirent __user *, dirent, unsigned int, count) { return 0; }
SYSCALL_DEFINE3(getdents, unsigned int, fd,
		struct linux_dirent __user *, dirent, unsigned int, count) { return 0; }
