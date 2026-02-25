#ifndef _LINUX_SIGNAL_H
#define _LINUX_SIGNAL_H

#include <linux/bug.h>
#include <linux/list.h>
#include <asm/signal.h>
#include <asm-generic/siginfo.h>
#include <linux/string.h>

#ifndef _LINUX_SIGNAL_TYPES_INLINED
#define _LINUX_SIGNAL_TYPES_INLINED

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;
};

struct k_sigaction {
	struct sigaction sa;
};


#endif /* _LINUX_SIGNAL_TYPES_INLINED */

struct task_struct;

#ifndef __HAVE_ARCH_SIG_SETOPS

static inline void sigemptyset(sigset_t *set)
{
	set->sig[1] = 0;
	set->sig[0] = 0;
}

#endif  

static inline void init_sigpending(struct sigpending *sig)
{
	sigemptyset(&sig->signal);
	INIT_LIST_HEAD(&sig->list);
}

enum pid_type;

extern void __set_current_blocked(const sigset_t *);

#endif
