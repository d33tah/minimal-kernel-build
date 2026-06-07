#ifndef _SYSCALL_USER_DISPATCH_H
#define _SYSCALL_USER_DISPATCH_H

#include <linux/thread_info.h>


struct syscall_user_dispatch {
	char __user	*selector;
	unsigned long	offset;
	unsigned long	len;
	bool		on_dispatch;
};

int set_syscall_user_dispatch(unsigned long mode, unsigned long offset,
			      unsigned long len, char __user *selector);

#define clear_syscall_work_syscall_user_dispatch(tsk) \
	clear_task_syscall_work(tsk, SYSCALL_USER_DISPATCH)


#endif  
