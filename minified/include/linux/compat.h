/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
/*
 * These are the type definitions for the architecture specific
 * syscall compatibility layer.
 */

#include <linux/types.h>
#include <linux/time.h>

#include <linux/stat.h>
#include <linux/param.h>	/* for HZ */
#include <linux/sem.h>
#include <linux/socket.h>
#include <linux/if.h>
#include <linux/fs.h>
#include <linux/aio_abi.h>	/* for aio_context_t */
#include <linux/uaccess.h>
#include <linux/unistd.h>

#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

/*
 * It may be useful for an architecture to override the definitions of the
 * COMPAT_SYSCALL_DEFINE0 and COMPAT_SYSCALL_DEFINEx() macros, in particular
 * to use a different calling convention for syscalls. To allow for that,
 + the prototypes for the compat_sys_*() functions below will *not* be included
 * if CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */
#include <asm/syscall_wrapper.h>

#ifndef COMPAT_USE_64BIT_TIME
#define COMPAT_USE_64BIT_TIME 0
#endif

#ifndef __SC_DELOUSE
#define __SC_DELOUSE(t,v) ((__force t)(unsigned long)(v))
#endif

#ifndef COMPAT_SYSCALL_DEFINE0
#define COMPAT_SYSCALL_DEFINE0(name) \
	asmlinkage long compat_sys_##name(void); \
	ALLOW_ERROR_INJECTION(compat_sys_##name, ERRNO); \
	asmlinkage long compat_sys_##name(void)
#endif /* COMPAT_SYSCALL_DEFINE0 */

#define COMPAT_SYSCALL_DEFINE1(name, ...) \
        COMPAT_SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE2(name, ...) \
	COMPAT_SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE3(name, ...) \
	COMPAT_SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE4(name, ...) \
	COMPAT_SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE5(name, ...) \
	COMPAT_SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define COMPAT_SYSCALL_DEFINE6(name, ...) \
	COMPAT_SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

/*
 * The asmlinkage stub is aliased to a function named __se_compat_sys_*() which
 * sign-extends 32-bit ints to longs whenever needed. The actual work is
 * done within __do_compat_sys_*().
 */
#ifndef COMPAT_SYSCALL_DEFINEx
#define COMPAT_SYSCALL_DEFINEx(x, name, ...)					\
	__diag_push();								\
	__diag_ignore(GCC, 8, "-Wattribute-alias",				\
		      "Type aliasing is used to sanitize syscall arguments");\
	asmlinkage long compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))	\
		__attribute__((alias(__stringify(__se_compat_sys##name))));	\
	ALLOW_ERROR_INJECTION(compat_sys##name, ERRNO);				\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\
	asmlinkage long __se_compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	asmlinkage long __se_compat_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{									\
		long ret = __do_compat_sys##name(__MAP(x,__SC_DELOUSE,__VA_ARGS__));\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		return ret;							\
	}									\
	__diag_pop();								\
	static inline long __do_compat_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))
#endif /* COMPAT_SYSCALL_DEFINEx */

struct compat_iovec {
	compat_uptr_t	iov_base;
	compat_size_t	iov_len;
};

#ifndef compat_user_stack_pointer
#define compat_user_stack_pointer() current_user_stack_pointer()
#endif
#ifndef compat_sigaltstack	/* we'll need that for MIPS */
typedef struct compat_sigaltstack {
	compat_uptr_t			ss_sp;
	int				ss_flags;
	compat_size_t			ss_size;
} compat_stack_t;
#endif
#ifndef COMPAT_MINSIGSTKSZ
#define COMPAT_MINSIGSTKSZ	MINSIGSTKSZ
#endif

#define compat_jiffies_to_clock_t(x)	\
		(((unsigned long)(x) * COMPAT_USER_HZ) / HZ)

typedef __compat_uid32_t	compat_uid_t;
typedef __compat_gid32_t	compat_gid_t;

struct compat_sel_arg_struct;
struct rusage;

struct old_itimerval32;

struct compat_tms {
	compat_clock_t		tms_utime;
	compat_clock_t		tms_stime;
	compat_clock_t		tms_cutime;
	compat_clock_t		tms_cstime;
};

#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)

#ifndef compat_sigset_t
typedef struct {
	compat_sigset_word	sig[_COMPAT_NSIG_WORDS];
} compat_sigset_t;
#endif

int set_compat_user_sigmask(const compat_sigset_t __user *umask,
			    size_t sigsetsize);

struct compat_sigaction {
#ifndef __ARCH_HAS_IRIX_SIGACTION
	compat_uptr_t			sa_handler;
	compat_ulong_t			sa_flags;
#else
	compat_uint_t			sa_flags;
	compat_uptr_t			sa_handler;
#endif
#ifdef __ARCH_HAS_SA_RESTORER
	compat_uptr_t			sa_restorer;
#endif
	compat_sigset_t			sa_mask __packed;
};

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

typedef struct compat_siginfo {
	int si_signo;
#ifndef __ARCH_HAS_SWAPPED_SIGINFO
	int si_errno;
	int si_code;
#else
	int si_code;
	int si_errno;
#endif

	union {
		int _pad[128/sizeof(int) - 3];

		/* kill() */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			__compat_uid32_t _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			__compat_uid32_t _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			__compat_uid32_t _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;


		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGTRAP, SIGEMT */
		struct {
			compat_uptr_t _addr;	/* faulting insn/memory ref. */
#define __COMPAT_ADDR_BND_PKEY_PAD  (__alignof__(compat_uptr_t) < sizeof(short) ? \
				     sizeof(short) : __alignof__(compat_uptr_t))
			union {
				/* used on alpha and sparc */
				int _trapno;	/* TRAP # which caused the signal */
				/*
				 * used when si_code=BUS_MCEERR_AR or
				 * used when si_code=BUS_MCEERR_AO
				 */
				short int _addr_lsb;	/* Valid LSB of the reported address. */
				/* used when si_code=SEGV_BNDERR */
				struct {
					char _dummy_bnd[__COMPAT_ADDR_BND_PKEY_PAD];
					compat_uptr_t _lower;
					compat_uptr_t _upper;
				} _addr_bnd;
				/* used when si_code=SEGV_PKUERR */
				struct {
					char _dummy_pkey[__COMPAT_ADDR_BND_PKEY_PAD];
					u32 _pkey;
				} _addr_pkey;
				/* used when si_code=TRAP_PERF */
				struct {
					compat_ulong_t _data;
					u32 _type;
					u32 _flags;
				} _perf;
			};
		} _sigfault;

		/* SIGPOLL */
		struct {
			compat_long_t _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		struct {
			compat_uptr_t _call_addr; /* calling user insn */
			int _syscall;	/* triggering system call number */
			unsigned int _arch;	/* AUDIT_ARCH_* of syscall */
		} _sigsys;
	} _sifields;
} compat_siginfo_t;

struct compat_rlimit {
	compat_ulong_t	rlim_cur;
	compat_ulong_t	rlim_max;
};

#ifdef __ARCH_NEED_COMPAT_FLOCK64_PACKED
#define __ARCH_COMPAT_FLOCK64_PACK	__attribute__((packed))
#else
#define __ARCH_COMPAT_FLOCK64_PACK
#endif

struct compat_flock {
	short			l_type;
	short			l_whence;
	compat_off_t		l_start;
	compat_off_t		l_len;
#ifdef __ARCH_COMPAT_FLOCK_EXTRA_SYSID
	__ARCH_COMPAT_FLOCK_EXTRA_SYSID
#endif
	compat_pid_t		l_pid;
#ifdef __ARCH_COMPAT_FLOCK_PAD
	__ARCH_COMPAT_FLOCK_PAD
#endif
};

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
#ifdef __ARCH_COMPAT_FLOCK64_PAD
	__ARCH_COMPAT_FLOCK64_PAD
#endif
} __ARCH_COMPAT_FLOCK64_PACK;

struct compat_rusage {
	struct old_timeval32 ru_utime;
	struct old_timeval32 ru_stime;
	compat_long_t	ru_maxrss;
	compat_long_t	ru_ixrss;
	compat_long_t	ru_idrss;
	compat_long_t	ru_isrss;
	compat_long_t	ru_minflt;
	compat_long_t	ru_majflt;
	compat_long_t	ru_nswap;
	compat_long_t	ru_inblock;
	compat_long_t	ru_oublock;
	compat_long_t	ru_msgsnd;
	compat_long_t	ru_msgrcv;
	compat_long_t	ru_nsignals;
	compat_long_t	ru_nvcsw;
	compat_long_t	ru_nivcsw;
};

extern int put_compat_rusage(const struct rusage *,
			     struct compat_rusage __user *);

struct compat_siginfo;
struct __compat_aio_sigset;

struct compat_dirent {
	u32		d_ino;
	compat_off_t	d_off;
	u16		d_reclen;
	char		d_name[256];
};

struct compat_ustat {
	compat_daddr_t		f_tfree;
	compat_ino_t		f_tinode;
	char			f_fname[6];
	char			f_fpack[6];
};

#define COMPAT_SIGEV_PAD_SIZE	((SIGEV_MAX_SIZE/sizeof(int)) - 3)

typedef struct compat_sigevent {
	compat_sigval_t sigev_value;
	compat_int_t sigev_signo;
	compat_int_t sigev_notify;
	union {
		compat_int_t _pad[COMPAT_SIGEV_PAD_SIZE];
		compat_int_t _tid;

		struct {
			compat_uptr_t _function;
			compat_uptr_t _attribute;
		} _sigev_thread;
	} _sigev_un;
} compat_sigevent_t;

struct compat_ifmap {
	compat_ulong_t mem_start;
	compat_ulong_t mem_end;
	unsigned short base_addr;
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
};

struct compat_if_settings {
	unsigned int type;	/* Type of physical device or protocol */
	unsigned int size;	/* Size of the data allocated by the caller */
	compat_uptr_t ifs_ifsu;	/* union of pointers */
};

struct compat_ifreq {
	union {
		char	ifrn_name[IFNAMSIZ];    /* if name, e.g. "en0" */
	} ifr_ifrn;
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		struct	sockaddr ifru_netmask;
		struct	sockaddr ifru_hwaddr;
		short	ifru_flags;
		compat_int_t	ifru_ivalue;
		compat_int_t	ifru_mtu;
		struct	compat_ifmap ifru_map;
		char	ifru_slave[IFNAMSIZ];   /* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
		compat_caddr_t	ifru_data;
		struct	compat_if_settings ifru_settings;
	} ifr_ifru;
};

struct compat_ifconf {
	compat_int_t	ifc_len;                /* size of buffer */
	compat_caddr_t  ifcbuf;
};

struct compat_robust_list {
	compat_uptr_t			next;
};

struct compat_robust_list_head {
	struct compat_robust_list	list;
	compat_long_t			futex_offset;
	compat_uptr_t			list_op_pending;
};


struct compat_keyctl_kdf_params {
	compat_uptr_t hashname;
	compat_uptr_t otherinfo;
	__u32 otherinfolen;
	__u32 __spare[8];
};

struct compat_stat;
struct compat_statfs;
struct compat_statfs64;
struct compat_old_linux_dirent;
struct compat_linux_dirent;
struct linux_dirent64;
struct compat_msghdr;
struct compat_mmsghdr;
struct compat_sysinfo;
struct compat_sysctl_args;
struct compat_kexec_segment;
struct compat_mq_attr;
struct compat_msgbuf;

void copy_siginfo_to_external32(struct compat_siginfo *to,
		const struct kernel_siginfo *from);
int copy_siginfo_from_user32(kernel_siginfo_t *to,
		const struct compat_siginfo __user *from);
int __copy_siginfo_to_user32(struct compat_siginfo __user *to,
		const kernel_siginfo_t *from);
#ifndef copy_siginfo_to_user32
#define copy_siginfo_to_user32 __copy_siginfo_to_user32
#endif
int get_compat_sigevent(struct sigevent *event,
		const struct compat_sigevent __user *u_event);

extern int get_compat_sigset(sigset_t *set, const compat_sigset_t __user *compat);

/*
 * Defined inline such that size can be compile time constant, which avoids
 * CONFIG_HARDENED_USERCOPY complaining about copies from task_struct
 */
static inline int
put_compat_sigset(compat_sigset_t __user *compat, const sigset_t *set,
		  unsigned int size)
{
	/* size <= sizeof(compat_sigset_t) <= sizeof(sigset_t) */
	return copy_to_user(compat, set, size) ? -EFAULT : 0;
}

#define unsafe_put_compat_sigset(compat, set, label) do {		\
	compat_sigset_t __user *__c = compat;				\
	const sigset_t *__s = set;					\
									\
	unsafe_copy_to_user(__c, __s, sizeof(*__c), label);		\
} while (0)

#define unsafe_get_compat_sigset(set, compat, label) do {		\
	const compat_sigset_t __user *__c = compat;			\
	sigset_t *__s = set;						\
									\
	unsafe_copy_from_user(__s, __c, sizeof(*__c), label);		\
} while (0)

extern int compat_ptrace_request(struct task_struct *child,
				 compat_long_t request,
				 compat_ulong_t addr, compat_ulong_t data);

extern long compat_arch_ptrace(struct task_struct *child, compat_long_t request,
			       compat_ulong_t addr, compat_ulong_t data);

struct epoll_event;	/* fortunately, this one is fixed-layout */

int compat_restore_altstack(const compat_stack_t __user *uss);
int __compat_save_altstack(compat_stack_t __user *, unsigned long);
#define unsafe_compat_save_altstack(uss, sp, label) do { \
	compat_stack_t __user *__uss = uss; \
	struct task_struct *t = current; \
	unsafe_put_user(ptr_to_compat((void __user *)t->sas_ss_sp), \
			&__uss->ss_sp, label); \
	unsafe_put_user(t->sas_ss_flags, &__uss->ss_flags, label); \
	unsafe_put_user(t->sas_ss_size, &__uss->ss_size, label); \
} while (0);

/*
 * These syscall function prototypes are kept in the same order as
 * include/uapi/asm-generic/unistd.h. Deprecated or obsolete system calls
 * go below.
 *
 * Please note that these prototypes here are only provided for information
 * purposes, for static analysis, and for linking from the syscall table.
 * These functions should not be called elsewhere from kernel code.
 *
 * As the syscall calling convention may be different from the default
 * for architectures overriding the syscall calling convention, do not
 * include the prototypes if CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */

/**
 * ns_to_old_timeval32 - Compat version of ns_to_timeval
 * @nsec:	the nanoseconds value to be converted
 *
 * Returns the old_timeval32 representation of the nsec parameter.
 */
static inline struct old_timeval32 ns_to_old_timeval32(s64 nsec)
{
	struct __kernel_old_timeval tv;
	struct old_timeval32 ctv;

	tv = ns_to_kernel_old_timeval(nsec);
	ctv.tv_sec = tv.tv_sec;
	ctv.tv_usec = tv.tv_usec;

	return ctv;
}

/*
 * Kernel code should not call compat syscalls (i.e., compat_sys_xyzyyz())
 * directly.  Instead, use one of the functions which work equivalently, such
 * as the kcompat_sys_xyzyyz() functions prototyped below.
 */

int kcompat_sys_statfs64(const char __user * pathname, compat_size_t sz,
		     struct compat_statfs64 __user * buf);
int kcompat_sys_fstatfs64(unsigned int fd, compat_size_t sz,
			  struct compat_statfs64 __user * buf);


#define is_compat_task() (0)
/* Ensure no one redefines in_compat_syscall() under !CONFIG_COMPAT */
#define in_compat_syscall in_compat_syscall
static inline bool in_compat_syscall(void) { return false; }


#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))

#define BITS_TO_COMPAT_LONGS(bits) DIV_ROUND_UP(bits, BITS_PER_COMPAT_LONG)

long compat_get_bitmap(unsigned long *mask, const compat_ulong_t __user *umask,
		       unsigned long bitmap_size);
long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size);

/*
 * Some legacy ABIs like the i386 one use less than natural alignment for 64-bit
 * types, and will need special compat treatment for that.  Most architectures
 * don't need that special handling even for compat syscalls.
 */
#ifndef compat_need_64bit_alignment_fixup
#define compat_need_64bit_alignment_fixup()		false
#endif

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately converted them already.
 */
#ifndef compat_ptr
static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}
#endif

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

#endif /* _LINUX_COMPAT_H */
