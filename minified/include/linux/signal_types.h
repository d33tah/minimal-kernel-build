#ifndef _LINUX_SIGNAL_TYPES_H
#define _LINUX_SIGNAL_TYPES_H


#include <linux/list.h>
#include <asm/signal.h>
#include <asm/siginfo.h>

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

struct sigpending {
	struct list_head list;
	sigset_t signal;
};

struct sigaction {
#ifndef __ARCH_HAS_IRIX_SIGACTION
	__sighandler_t	sa_handler;
	unsigned long	sa_flags;
#else
	unsigned int	sa_flags;
	__sighandler_t	sa_handler;
#endif
#ifdef __ARCH_HAS_SA_RESTORER
	__sigrestore_t sa_restorer;
#endif
	sigset_t	sa_mask;	 
};

struct k_sigaction {
	struct sigaction sa;
#ifdef __ARCH_HAS_KA_RESTORER
	__sigrestore_t ka_restorer;
#endif
};

/* __ARCH_UAPI_SA_FLAGS + UAPI_SA_FLAGS removed - unused */

#endif
