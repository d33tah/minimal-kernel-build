#ifndef _LINUX_SIGNAL_TYPES_H
#define _LINUX_SIGNAL_TYPES_H


#include <linux/list.h>
#include <asm/signal.h>
#include <asm/siginfo.h>

typedef struct kernel_siginfo { __SIGINFO; } kernel_siginfo_t;

struct ucounts;


struct sigqueue { struct list_head list; int flags; kernel_siginfo_t info; struct ucounts *ucounts; };

struct sigpending { struct list_head list; sigset_t signal; };

struct sigaction {
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;	 
};

struct k_sigaction { struct sigaction sa; };

/* __ARCH_UAPI_SA_FLAGS + UAPI_SA_FLAGS removed - unused */

#endif
