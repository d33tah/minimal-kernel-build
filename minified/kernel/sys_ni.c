
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

asmlinkage long sys_ni_syscall(void);

asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}
