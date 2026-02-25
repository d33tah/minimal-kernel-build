#ifndef _UAPI_ASM_GENERIC_SIGINFO_H
#define _UAPI_ASM_GENERIC_SIGINFO_H

#include <linux/compiler.h>
#include <linux/types.h>

#define SI_MAX_SIZE	128

#ifndef __ARCH_SI_ATTRIBUTES
#define __ARCH_SI_ATTRIBUTES
#endif

union __sifields {
	struct {
		void __user *_addr;
	} _sigfault;
};

#define __SIGINFO 			\
struct {				\
	int si_signo;			\
	int si_errno;			\
	int si_code;			\
	union __sifields _sifields;	\
}

typedef struct siginfo {
	union {
		__SIGINFO;
		int _si_pad[SI_MAX_SIZE/sizeof(int)];
	};
} __ARCH_SI_ATTRIBUTES siginfo_t;

/* SEGV codes used by x86 fault handler */
#define SEGV_MAPERR	1
#define SEGV_ACCERR	2

/* BUS codes used by x86 */
#define BUS_ADRERR	2

#endif  
