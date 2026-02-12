
#include <linux/linkage.h>
#include <linux/errno.h>

#include <asm/unistd.h>

asmlinkage long sys_ni_syscall(void);

asmlinkage long sys_ni_syscall(void)
{
	return -ENOSYS;
}

/* All COND_SYSCALL entries removed - syscall table reduced to 5 entries,
   only sys_restart_syscall, sys_exit, sys_ni_syscall, and sys_write needed */
