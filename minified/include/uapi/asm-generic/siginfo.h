#ifndef _UAPI_ASM_GENERIC_SIGINFO_H
#define _UAPI_ASM_GENERIC_SIGINFO_H

#include <linux/compiler.h>
#include <linux/types.h>

#define SI_MAX_SIZE	128

#ifndef __ARCH_SI_ATTRIBUTES
#define __ARCH_SI_ATTRIBUTES
#endif

/* Trimmed to the union members actually read in this build: _kill (_pid/_uid)
 * and _sigfault._addr. siginfo_t is padded to SI_MAX_SIZE and both consumers
 * (copy_siginfo/clear_siginfo) use sizeof(self), so the layout is self-consistent. */
union __sifields {
	struct {
		__kernel_pid_t _pid;
		__kernel_uid32_t _uid;
	} _kill;

	struct {
		void __user *_addr;
	} _sigfault;
};

#ifndef __ARCH_HAS_SWAPPED_SIGINFO
#define __SIGINFO 			\
struct {				\
	int si_signo;			\
	int si_errno;			\
	int si_code;			\
	union __sifields _sifields;	\
}
#else
#define __SIGINFO 			\
struct {				\
	int si_signo;			\
	int si_code;			\
	int si_errno;			\
	union __sifields _sifields;	\
}
#endif  

typedef struct siginfo {
	union {
		__SIGINFO;
		int _si_pad[SI_MAX_SIZE/sizeof(int)];
	};
} __ARCH_SI_ATTRIBUTES siginfo_t;

#define si_pid		_sifields._kill._pid
#define si_uid		_sifields._kill._uid
#define si_addr		_sifields._sigfault._addr

/* SI_USER removed - 0-ref in this build */
#define SI_KERNEL	0x80

/* Only keep signal sub-codes actually used */
#define ILL_ILLOPN	2
#define ILL_BADSTK	8

/* FPE code used by x86 (only FPE_INTDIV is referenced) */
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

#endif  
