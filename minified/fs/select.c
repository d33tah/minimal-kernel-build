#include <linux/poll.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/compat.h>


SYSCALL_DEFINE6(pselect6, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct __kernel_timespec __user *, tsp,
		void __user *, sig) { return -ENOSYS; }
SYSCALL_DEFINE5(ppoll, struct pollfd __user *, ufds, unsigned int, nfds,
		struct __kernel_timespec __user *, tsp, const sigset_t __user *, sigmask,
		size_t, sigsetsize) { return -ENOSYS; }
