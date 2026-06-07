#ifndef _UAPI_ASM_GENERIC_SIGINFO_H
#define _UAPI_ASM_GENERIC_SIGINFO_H

#include <linux/compiler.h>
#include <linux/types.h>

typedef union sigval {
	int sival_int;
	void __user *sival_ptr;
} sigval_t;

#define SI_MAX_SIZE	128

#ifndef __ARCH_SI_BAND_T
#define __ARCH_SI_BAND_T long
#endif

#ifndef __ARCH_SI_CLOCK_T
#define __ARCH_SI_CLOCK_T __kernel_clock_t
#endif

#ifndef __ARCH_SI_ATTRIBUTES
#define __ARCH_SI_ATTRIBUTES
#endif

union __sifields {
	 
	struct {
		__kernel_pid_t _pid;	 
		__kernel_uid32_t _uid;	 
	} _kill;

	 
	struct {
		__kernel_timer_t _tid;	 
		int _overrun;		 
		sigval_t _sigval;	 
		int _sys_private;        
	} _timer;

	 
	struct {
		__kernel_pid_t _pid;	 
		__kernel_uid32_t _uid;	 
		sigval_t _sigval;
	} _rt;

	 
	struct {
		__kernel_pid_t _pid;	 
		__kernel_uid32_t _uid;	 
		int _status;		 
		__ARCH_SI_CLOCK_T _utime;
		__ARCH_SI_CLOCK_T _stime;
	} _sigchld;

	 
	struct {
		void __user *_addr;  
#ifdef __ia64__
		int _imm;		 
		unsigned int _flags;	 
		unsigned long _isr;	 
#endif

#define __ADDR_BND_PKEY_PAD  (__alignof__(void *) < sizeof(short) ? \
			      sizeof(short) : __alignof__(void *))
		union {
			 
			int _trapno;	 
			 
			short _addr_lsb;  
			 
			struct {
				char _dummy_bnd[__ADDR_BND_PKEY_PAD];
				void __user *_lower;
				void __user *_upper;
			} _addr_bnd;
			 
			struct {
				char _dummy_pkey[__ADDR_BND_PKEY_PAD];
				__u32 _pkey;
			} _addr_pkey;
			/* _perf struct removed - unused */
		};
	} _sigfault;

	 
	struct {
		__ARCH_SI_BAND_T _band;	 
		int _fd;
	} _sigpoll;

	 
	struct {
		void __user *_call_addr;  
		int _syscall;	 
		unsigned int _arch;	 
	} _sigsys;
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
#define si_tid		_sifields._timer._tid
#define si_overrun	_sifields._timer._overrun
#define si_sys_private  _sifields._timer._sys_private
#define si_status	_sifields._sigchld._status
#define si_utime	_sifields._sigchld._utime
#define si_stime	_sifields._sigchld._stime
#define si_value	_sifields._rt._sigval
#define si_int		_sifields._rt._sigval.sival_int
#define si_ptr		_sifields._rt._sigval.sival_ptr
#define si_addr		_sifields._sigfault._addr
#define si_trapno	_sifields._sigfault._trapno
#define si_addr_lsb	_sifields._sigfault._addr_lsb
#define si_lower	_sifields._sigfault._addr_bnd._lower
#define si_upper	_sifields._sigfault._addr_bnd._upper
#define si_pkey		_sifields._sigfault._addr_pkey._pkey
#define si_band		_sifields._sigpoll._band
#define si_fd		_sifields._sigpoll._fd
#define si_call_addr	_sifields._sigsys._call_addr
#define si_syscall	_sifields._sigsys._syscall
#define si_arch		_sifields._sigsys._arch

#define SI_USER		0		 
#define SI_KERNEL	0x80		 
#define SI_QUEUE	-1		 
#define SI_TIMER	-2		 
/* SI_MESGQ, SI_ASYNCIO - unused */
#define SI_SIGIO	-5
#define SI_TKILL	-6
#define SI_DETHREAD	-7
/* SI_ASYNCNL - unused */

#define SI_FROMUSER(siptr)	((siptr)->si_code <= 0)
#define SI_FROMKERNEL(siptr)	((siptr)->si_code > 0)

/* Only keep signal sub-codes actually used */
#define ILL_ILLOPN	2
#define ILL_BADSTK	8
#define NSIGILL		11

/* FPE codes used by x86 FPU */
#define FPE_INTDIV	1
#define FPE_FLTDIV	3
#define FPE_FLTOVF	4
#define FPE_FLTUND	5
#define FPE_FLTRES	6
#define FPE_FLTINV	7
#define NSIGFPE		15

/* SEGV codes used by x86 fault handler */
#define SEGV_MAPERR	1
#define SEGV_ACCERR	2
#define SEGV_PKUERR	4
#define NSIGSEGV	9

/* BUS codes used by x86 */
#define BUS_ADRALN	1
#define BUS_ADRERR	2
#define NSIGBUS		5

/* TRAP codes used by x86 */
#define TRAP_BRKPT	1
#define TRAP_TRACE	2
#define TRAP_HWBKPT     4
#define NSIGTRAP	6

/* CLD codes used by kernel/exit.c, kernel/signal.c */
#define CLD_EXITED	1
#define CLD_KILLED	2
#define CLD_DUMPED	3
#define CLD_TRAPPED	4
#define CLD_STOPPED	5
#define CLD_CONTINUED	6
#define NSIGCHLD	6

/* POLL codes */
#define POLL_OUT	2
#define NSIGPOLL	6

/* SYS codes */
#define SYS_USER_DISPATCH 2
#define NSIGSYS		2

/* EMT_TAGOVF, NSIGEMT - unused */
/* SIGEV_* and sigevent_t - unused */

#endif  
