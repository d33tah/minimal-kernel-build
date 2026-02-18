#ifndef _UAPI_ASM_GENERIC_SIGINFO_H
#define _UAPI_ASM_GENERIC_SIGINFO_H

#include <linux/compiler.h>
#include <linux/types.h>

#define SI_MAX_SIZE	128

#ifndef __ARCH_SI_ATTRIBUTES
#define __ARCH_SI_ATTRIBUTES
#endif

union __sifields {
	/* _kill: used by si_pid, si_uid */
	struct {
		__kernel_pid_t _pid;
		__kernel_uid32_t _uid;
	} _kill;

	struct {
		void __user *_addr;
	} _sigfault;

	/* _sigsys: used by si_call_addr, si_syscall, si_arch */
	struct {
		void __user *_call_addr;
		int _syscall;
		unsigned int _arch;
	} _sigsys;

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

/* Only keep si_ accessors that are actually used */
#define si_pid		_sifields._kill._pid
#define si_uid		_sifields._kill._uid
#define si_addr		_sifields._sigfault._addr
#define si_call_addr	_sifields._sigsys._call_addr
#define si_syscall	_sifields._sigsys._syscall
#define si_arch		_sifields._sigsys._arch
#define SI_KERNEL	0x80

/* Only keep signal sub-codes actually used */
#define ILL_ILLOPN	2
#define ILL_BADSTK	8

/* FPE codes used by x86 FPU */
#define FPE_INTDIV	1

/* SEGV codes used by x86 fault handler */
#define SEGV_MAPERR	1
#define SEGV_ACCERR	2

/* BUS codes used by x86 */
#define BUS_ADRALN	1
#define BUS_ADRERR	2

/* TRAP codes used by x86 */
#define TRAP_BRKPT	1
#define TRAP_TRACE	2
#define TRAP_HWBKPT     4

/* SYS codes */

#endif  
