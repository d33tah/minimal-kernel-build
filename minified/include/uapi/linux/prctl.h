/* Minimal prctl.h - only syscall user dispatch constants needed */
#ifndef _LINUX_PRCTL_H
#define _LINUX_PRCTL_H

#include <linux/types.h>

/* Only keep constants that are actually used */
#define PR_SET_SYSCALL_USER_DISPATCH	59
# define PR_SYS_DISPATCH_OFF		0
# define PR_SYS_DISPATCH_ON		1
# define SYSCALL_DISPATCH_FILTER_ALLOW	0
# define SYSCALL_DISPATCH_FILTER_BLOCK	1

#endif /* _LINUX_PRCTL_H */
