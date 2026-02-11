#ifndef _LINUX_SIGNAL_TYPES_H
#define _LINUX_SIGNAL_TYPES_H


#include <linux/list.h>
#include <asm/signal.h>
#include <asm-generic/siginfo.h>

/* SS_DISABLE, SS_ONSTACK, SS_AUTODISARM, SS_FLAG_BITS removed - unused */

typedef struct kernel_siginfo {
	__SIGINFO;
} kernel_siginfo_t;

struct ucounts;


struct sigqueue {
	struct list_head list;
	int flags;
	kernel_siginfo_t info;
	struct ucounts *ucounts;
};

/* SIGQUEUE_PREALLOC removed - unused */

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

/* struct old_sigaction removed - unused */
/* struct ksignal removed - unused */

#define SA_IMMUTABLE		0x00800000

#endif  
