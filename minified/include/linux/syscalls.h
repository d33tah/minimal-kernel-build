/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * syscalls.h - Linux syscall interfaces (non-arch-specific)
 *
 * Copyright (c) 2004 Randy Dunlap
 * Copyright (c) 2004 Open Source Development Labs
 */

#ifndef _LINUX_SYSCALLS_H
#define _LINUX_SYSCALLS_H

struct __aio_sigset;
struct epoll_event;
struct iattr;
struct inode;
struct iocb;
struct io_event;
struct iovec;
struct __kernel_old_itimerval;
struct kexec_segment;
struct linux_dirent;
struct linux_dirent64;
struct list_head;
struct mmap_arg_struct;
struct msgbuf;
struct user_msghdr;
struct mmsghdr;
struct msqid_ds;
struct new_utsname;
struct nfsctl_arg;
struct __old_kernel_stat;
struct oldold_utsname;
struct old_utsname;
struct pollfd;
struct rlimit;
struct rlimit64;
struct rusage;
struct sched_param;
struct sched_attr;
struct sel_arg_struct;
struct semaphore;
struct sembuf;
struct shmid_ds;
struct sockaddr;
struct stat;
struct stat64;
struct statfs;
struct statfs64;
struct statx;
struct sysinfo;
struct timespec;
struct __kernel_old_timeval;
struct __kernel_timex;
struct timezone;
struct tms;
struct utimbuf;
struct mq_attr;
struct compat_stat;
struct old_timeval32;
struct robust_list_head;
struct futex_waitv;
struct getcpu_cache;
struct old_linux_dirent;
struct perf_event_attr;
struct file_handle;
struct sigaltstack;
struct rseq;
union bpf_attr;
struct io_uring_params;
struct clone_args;
struct open_how;
struct mount_attr;
struct landlock_ruleset_attr;
enum landlock_rule_type;

#include <linux/types.h>
#include <linux/aio_abi.h>
#include <linux/capability.h>
#include <linux/signal.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <linux/sem.h>
#include <asm/siginfo.h>
#include <linux/unistd.h>
#include <linux/quota.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <trace/syscall.h>

/*
 * It may be useful for an architecture to override the definitions of the
 * SYSCALL_DEFINE0() and __SYSCALL_DEFINEx() macros, in particular to use a
 * different calling convention for syscalls. To allow for that, the prototypes
 * for the sys_*() functions below will *not* be included if
 * CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */
#include <asm/syscall_wrapper.h>

/*
 * __MAP - apply a macro to syscall arguments
 * __MAP(n, m, t1, a1, t2, a2, ..., tn, an) will expand to
 *    m(t1, a1), m(t2, a2), ..., m(tn, an)
 * The first argument must be equal to the amount of type/name
 * pairs given.  Note that this list of pairs (i.e. the arguments
 * of __MAP starting at the third one) is in the same format as
 * for SYSCALL_DEFINE<n>/COMPAT_SYSCALL_DEFINE<n>
 */
#define __MAP0(m,...)
#define __MAP1(m,t,a,...) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define __SC_DECL(t, a)	t a
#define __TYPE_AS(t, v)	__same_type((__force t)0, v)
#define __TYPE_IS_L(t)	(__TYPE_AS(t, 0L))
#define __TYPE_IS_UL(t)	(__TYPE_AS(t, 0UL))
#define __TYPE_IS_LL(t) (__TYPE_AS(t, 0LL) || __TYPE_AS(t, 0ULL))
#define __SC_LONG(t, a) __typeof(__builtin_choose_expr(__TYPE_IS_LL(t), 0LL, 0L)) a
#define __SC_CAST(t, a)	(__force t) a
#define __SC_ARGS(t, a)	a
#define __SC_TEST(t, a) (void)BUILD_BUG_ON_ZERO(!__TYPE_IS_LL(t) && sizeof(t) > sizeof(long))

#ifdef CONFIG_FTRACE_SYSCALLS
#define __SC_STR_ADECL(t, a)	#a
#define __SC_STR_TDECL(t, a)	#t

extern struct trace_event_class event_class_syscall_enter;
extern struct trace_event_class event_class_syscall_exit;
extern struct trace_event_functions enter_syscall_print_funcs;
extern struct trace_event_functions exit_syscall_print_funcs;

#define SYSCALL_TRACE_ENTER_EVENT(sname)				\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct trace_event_call __used				\
	  event_enter_##sname = {					\
		.class			= &event_class_syscall_enter,	\
		{							\
			.name                   = "sys_enter"#sname,	\
		},							\
		.event.funcs            = &enter_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags                  = TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct trace_event_call __used				\
	  __section("_ftrace_events")					\
	 *__event_enter_##sname = &event_enter_##sname;

#define SYSCALL_TRACE_EXIT_EVENT(sname)					\
	static struct syscall_metadata __syscall_meta_##sname;		\
	static struct trace_event_call __used				\
	  event_exit_##sname = {					\
		.class			= &event_class_syscall_exit,	\
		{							\
			.name                   = "sys_exit"#sname,	\
		},							\
		.event.funcs		= &exit_syscall_print_funcs,	\
		.data			= (void *)&__syscall_meta_##sname,\
		.flags                  = TRACE_EVENT_FL_CAP_ANY,	\
	};								\
	static struct trace_event_call __used				\
	  __section("_ftrace_events")					\
	*__event_exit_##sname = &event_exit_##sname;

#define SYSCALL_METADATA(sname, nb, ...)			\
	static const char *types_##sname[] = {			\
		__MAP(nb,__SC_STR_TDECL,__VA_ARGS__)		\
	};							\
	static const char *args_##sname[] = {			\
		__MAP(nb,__SC_STR_ADECL,__VA_ARGS__)		\
	};							\
	SYSCALL_TRACE_ENTER_EVENT(sname);			\
	SYSCALL_TRACE_EXIT_EVENT(sname);			\
	static struct syscall_metadata __used			\
	  __syscall_meta_##sname = {				\
		.name 		= "sys"#sname,			\
		.syscall_nr	= -1,	/* Filled in at boot */	\
		.nb_args 	= nb,				\
		.types		= nb ? types_##sname : NULL,	\
		.args		= nb ? args_##sname : NULL,	\
		.enter_event	= &event_enter_##sname,		\
		.exit_event	= &event_exit_##sname,		\
		.enter_fields	= LIST_HEAD_INIT(__syscall_meta_##sname.enter_fields), \
	};							\
	static struct syscall_metadata __used			\
	  __section("__syscalls_metadata")			\
	 *__p_syscall_meta_##sname = &__syscall_meta_##sname;

static inline int is_syscall_trace_event(struct trace_event_call *tp_event)
{
	return tp_event->class == &event_class_syscall_enter ||
	       tp_event->class == &event_class_syscall_exit;
}

#else
#define SYSCALL_METADATA(sname, nb, ...)

static inline int is_syscall_trace_event(struct trace_event_call *tp_event)
{
	return 0;
}
#endif

#ifndef SYSCALL_DEFINE0
#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);				\
	asmlinkage long sys_##sname(void);			\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);		\
	asmlinkage long sys_##sname(void)
#endif /* SYSCALL_DEFINE0 */

#define SYSCALL_DEFINE1(name, ...) SYSCALL_DEFINEx(1, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE2(name, ...) SYSCALL_DEFINEx(2, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE3(name, ...) SYSCALL_DEFINEx(3, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE4(name, ...) SYSCALL_DEFINEx(4, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE5(name, ...) SYSCALL_DEFINEx(5, _##name, __VA_ARGS__)
#define SYSCALL_DEFINE6(name, ...) SYSCALL_DEFINEx(6, _##name, __VA_ARGS__)

#define SYSCALL_DEFINE_MAXARGS	6

#define SYSCALL_DEFINEx(x, sname, ...)				\
	SYSCALL_METADATA(sname, x, __VA_ARGS__)			\
	__SYSCALL_DEFINEx(x, sname, __VA_ARGS__)

#define __PROTECT(...) asmlinkage_protect(__VA_ARGS__)

/*
 * The asmlinkage stub is aliased to a function named __se_sys_*() which
 * sign-extends 32-bit ints to longs whenever needed. The actual work is
 * done within __do_sys_*().
 */
#ifndef __SYSCALL_DEFINEx
#define __SYSCALL_DEFINEx(x, name, ...)					\
	__diag_push();							\
	__diag_ignore(GCC, 8, "-Wattribute-alias",			\
		      "Type aliasing is used to sanitize syscall arguments");\
	asmlinkage long sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))	\
		__attribute__((alias(__stringify(__se_sys##name))));	\
	ALLOW_ERROR_INJECTION(sys##name, ERRNO);			\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));\
	asmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__));	\
	asmlinkage long __se_sys##name(__MAP(x,__SC_LONG,__VA_ARGS__))	\
	{								\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));\
		__MAP(x,__SC_TEST,__VA_ARGS__);				\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));	\
		return ret;						\
	}								\
	__diag_pop();							\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))
#endif /* __SYSCALL_DEFINEx */

/* For split 64-bit arguments on 32-bit architectures */
#ifdef __LITTLE_ENDIAN
#define SC_ARG64(name) u32, name##_lo, u32, name##_hi
#else
#define SC_ARG64(name) u32, name##_hi, u32, name##_lo
#endif
#define SC_VAL64(type, name) ((type) name##_hi << 32 | name##_lo)

#ifdef CONFIG_COMPAT
#define SYSCALL32_DEFINE1 COMPAT_SYSCALL_DEFINE1
#define SYSCALL32_DEFINE2 COMPAT_SYSCALL_DEFINE2
#define SYSCALL32_DEFINE3 COMPAT_SYSCALL_DEFINE3
#define SYSCALL32_DEFINE4 COMPAT_SYSCALL_DEFINE4
#define SYSCALL32_DEFINE5 COMPAT_SYSCALL_DEFINE5
#define SYSCALL32_DEFINE6 COMPAT_SYSCALL_DEFINE6
#else
#define SYSCALL32_DEFINE1 SYSCALL_DEFINE1
#define SYSCALL32_DEFINE2 SYSCALL_DEFINE2
#define SYSCALL32_DEFINE3 SYSCALL_DEFINE3
#define SYSCALL32_DEFINE4 SYSCALL_DEFINE4
#define SYSCALL32_DEFINE5 SYSCALL_DEFINE5
#define SYSCALL32_DEFINE6 SYSCALL_DEFINE6
#endif

/*
 * Called before coming back to user-mode. Returning to user-mode with an
 * address limit different than USER_DS can allow to overwrite kernel memory.
 */
static inline void addr_limit_user_check(void)
{
#ifdef TIF_FSCHECK
	if (!test_thread_flag(TIF_FSCHECK))
		return;
#endif

#ifdef TIF_FSCHECK
	clear_thread_flag(TIF_FSCHECK);
#endif
}

/*
 * These syscall function prototypes are kept in the same order as
 * include/uapi/asm-generic/unistd.h. Architecture specific entries go below,
 * followed by deprecated or obsolete system calls.
 *
 * Please note that these prototypes here are only provided for information
 * purposes, for static analysis, and for linking from the syscall table.
 * These functions should not be called elsewhere from kernel code.
 *
 * As the syscall calling convention may be different from the default
 * for architectures overriding the syscall calling convention, do not
 * include the prototypes if CONFIG_ARCH_HAS_SYSCALL_WRAPPER is enabled.
 */


/*
 * Kernel code should not call syscalls (i.e., sys_xyzyyz()) directly.
 * Instead, use one of the functions which work equivalently, such as
 * the ksys_xyzyyz() functions prototyped below.
 */
ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count);
int ksys_fchown(unsigned int fd, uid_t user, gid_t group);
ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count);
void ksys_sync(void);
int ksys_unshare(unsigned long unshare_flags);
int ksys_setsid(void);
int ksys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
			 unsigned int flags);
ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count,
		     loff_t pos);
ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos);
int ksys_fallocate(int fd, int mode, loff_t offset, loff_t len);
#ifdef CONFIG_ADVISE_SYSCALLS
int ksys_fadvise64_64(int fd, loff_t offset, loff_t len, int advice);
#else
static inline int ksys_fadvise64_64(int fd, loff_t offset, loff_t len,
				    int advice)
{
	return -EINVAL;
}
#endif
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff);
ssize_t ksys_readahead(int fd, loff_t offset, size_t count);
int ksys_ipc(unsigned int call, int first, unsigned long second,
	unsigned long third, void __user * ptr, long fifth);
int compat_ksys_ipc(u32 call, int first, int second,
	u32 third, u32 ptr, u32 fifth);

/*
 * The following kernel syscall equivalents are just wrappers to fs-internal
 * functions. Therefore, provide stubs to be inlined at the callsites.
 */
extern int do_fchownat(int dfd, const char __user *filename, uid_t user,
		       gid_t group, int flag);

static inline long ksys_chown(const char __user *filename, uid_t user,
			      gid_t group)
{
	return do_fchownat(AT_FDCWD, filename, user, group, 0);
}

static inline long ksys_lchown(const char __user *filename, uid_t user,
			       gid_t group)
{
	return do_fchownat(AT_FDCWD, filename, user, group,
			     AT_SYMLINK_NOFOLLOW);
}

extern long do_sys_ftruncate(unsigned int fd, loff_t length, int small);

static inline long ksys_ftruncate(unsigned int fd, loff_t length)
{
	return do_sys_ftruncate(fd, length, 1);
}

extern long do_sys_truncate(const char __user *pathname, loff_t length);

static inline long ksys_truncate(const char __user *pathname, loff_t length)
{
	return do_sys_truncate(pathname, length);
}

static inline unsigned int ksys_personality(unsigned int personality)
{
	unsigned int old = current->personality;

	if (personality != 0xffffffff)
		set_personality(personality);

	return old;
}

/* for __ARCH_WANT_SYS_IPC */
long ksys_semtimedop(int semid, struct sembuf __user *tsops,
		     unsigned int nsops,
		     const struct __kernel_timespec __user *timeout);
long ksys_semget(key_t key, int nsems, int semflg);
long ksys_old_semctl(int semid, int semnum, int cmd, unsigned long arg);
long ksys_msgget(key_t key, int msgflg);
long ksys_old_msgctl(int msqid, int cmd, struct msqid_ds __user *buf);
long ksys_msgrcv(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 long msgtyp, int msgflg);
long ksys_msgsnd(int msqid, struct msgbuf __user *msgp, size_t msgsz,
		 int msgflg);
long ksys_shmget(key_t key, size_t size, int shmflg);
long ksys_shmdt(char __user *shmaddr);
long ksys_old_shmctl(int shmid, int cmd, struct shmid_ds __user *buf);
long compat_ksys_semtimedop(int semid, struct sembuf __user *tsems,
			    unsigned int nsops,
			    const struct old_timespec32 __user *timeout);
long __do_semtimedop(int semid, struct sembuf *tsems, unsigned int nsops,
		     const struct timespec64 *timeout,
		     struct ipc_namespace *ns);

int __sys_getsockopt(int fd, int level, int optname, char __user *optval,
		int __user *optlen);
int __sys_setsockopt(int fd, int level, int optname, char __user *optval,
		int optlen);
#endif
