#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/export.h>
#include <linux/syscalls.h>

/* All non-syscall xattr functions removed - unused in minimal kernel */

SYSCALL_DEFINE5(setxattr, const char __user *, pathname, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE5(lsetxattr, const char __user *, pathname, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE5(fsetxattr, int, fd, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(getxattr, const char __user *, pathname, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(lgetxattr, const char __user *, pathname, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(fgetxattr, int, fd, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(listxattr, const char __user *, pathname, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(llistxattr, const char __user *, pathname, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(flistxattr, int, fd, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(removexattr, const char __user *, pathname, const char __user *, name) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(lremovexattr, const char __user *, pathname, const char __user *, name) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(fremovexattr, int, fd, const char __user *, name) { return -EOPNOTSUPP; }
