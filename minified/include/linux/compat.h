 
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H
 

#include <linux/types.h>
#include <linux/time.h>

#include <linux/stat.h>
#include <linux/param.h>	 
#include <linux/sem.h>
#include <linux/socket.h>
#include <linux/if.h>
#include <linux/fs.h>
#include <linux/aio_abi.h>	 
#include <linux/uaccess.h>
#include <linux/unistd.h>

#include <asm/compat.h>
#include <asm/siginfo.h>
#include <asm/signal.h>

 
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
#endif  

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
#endif  

struct compat_iovec {
	compat_uptr_t	iov_base;
	compat_size_t	iov_len;
};

#ifndef compat_user_stack_pointer
#define compat_user_stack_pointer() current_user_stack_pointer()
#endif
#ifndef compat_sigaltstack	 
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

		 
		struct {
			compat_pid_t _pid;	 
			__compat_uid32_t _uid;	 
		} _kill;

		 
		struct {
			compat_timer_t _tid;	 
			int _overrun;		 
			compat_sigval_t _sigval;	 
		} _timer;

		 
		struct {
			compat_pid_t _pid;	 
			__compat_uid32_t _uid;	 
			compat_sigval_t _sigval;
		} _rt;

		 
		struct {
			compat_pid_t _pid;	 
			__compat_uid32_t _uid;	 
			int _status;		 
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;


		 
		struct {
			compat_uptr_t _addr;	 
#define __COMPAT_ADDR_BND_PKEY_PAD  (__alignof__(compat_uptr_t) < sizeof(short) ? \
				     sizeof(short) : __alignof__(compat_uptr_t))
			union {
				 
				int _trapno;	 
				 
				short int _addr_lsb;	 
				 
				struct {
					char _dummy_bnd[__COMPAT_ADDR_BND_PKEY_PAD];
					compat_uptr_t _lower;
					compat_uptr_t _upper;
				} _addr_bnd;
				 
				struct {
					char _dummy_pkey[__COMPAT_ADDR_BND_PKEY_PAD];
					u32 _pkey;
				} _addr_pkey;
				 
				struct {
					compat_ulong_t _data;
					u32 _type;
					u32 _flags;
				} _perf;
			};
		} _sigfault;

		 
		struct {
			compat_long_t _band;	 
			int _fd;
		} _sigpoll;

		struct {
			compat_uptr_t _call_addr;  
			int _syscall;	 
			unsigned int _arch;	 
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

struct compat_flock;
struct compat_flock64;
struct compat_rusage;
struct compat_siginfo;
struct __compat_aio_sigset;
struct compat_dirent;
struct compat_ustat;

struct compat_sigevent;
typedef struct compat_sigevent compat_sigevent_t;
struct compat_ifmap;
struct compat_if_settings;
struct compat_ifreq;
struct compat_ifconf;
struct compat_robust_list;
struct compat_robust_list_head;
struct compat_keyctl_kdf_params;

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

struct epoll_event;	 

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



int kcompat_sys_statfs64(const char __user * pathname, compat_size_t sz,
		     struct compat_statfs64 __user * buf);
int kcompat_sys_fstatfs64(unsigned int fd, compat_size_t sz,
			  struct compat_statfs64 __user * buf);


#define is_compat_task() (0)
 
#define in_compat_syscall in_compat_syscall
static inline bool in_compat_syscall(void) { return false; }


#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))

#define BITS_TO_COMPAT_LONGS(bits) DIV_ROUND_UP(bits, BITS_PER_COMPAT_LONG)

long compat_get_bitmap(unsigned long *mask, const compat_ulong_t __user *umask,
		       unsigned long bitmap_size);
long compat_put_bitmap(compat_ulong_t __user *umask, unsigned long *mask,
		       unsigned long bitmap_size);

 
#ifndef compat_need_64bit_alignment_fixup
#define compat_need_64bit_alignment_fixup()		false
#endif

 
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

#endif  
