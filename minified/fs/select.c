 
 
#include <linux/poll.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/compat.h>

void poll_initwait(struct poll_wqueues *pwq) { }

void poll_freewait(struct poll_wqueues *pwq) { }

struct sel_arg_struct { unsigned long n; fd_set __user *inp, *outp, *exp; struct timeval __user *tvp; };

SYSCALL_DEFINE5(select, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct __kernel_old_timeval __user *, tvp) { return -ENOSYS; }
SYSCALL_DEFINE6(pselect6, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct __kernel_timespec __user *, tsp,
		void __user *, sig) { return -ENOSYS; }
SYSCALL_DEFINE1(old_select, struct sel_arg_struct __user *, arg) { return -ENOSYS; }
SYSCALL_DEFINE3(poll, struct pollfd __user *, ufds, unsigned int, nfds, int, timeout_msecs) { return -ENOSYS; }
SYSCALL_DEFINE5(ppoll, struct pollfd __user *, ufds, unsigned int, nfds,
		struct __kernel_timespec __user *, tsp, const sigset_t __user *, sigmask,
		size_t, sigsetsize) { return -ENOSYS; }
