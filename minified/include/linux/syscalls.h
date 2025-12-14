
#ifndef _LINUX_SYSCALLS_H
#define _LINUX_SYSCALLS_H

/* Removed many unused forward declarations:
   __aio_sigset, epoll_event, iocb, io_event, __kernel_old_itimerval,
   kexec_segment, mmap_arg_struct, msgbuf, user_msghdr, mmsghdr, msqid_ds,
   nfsctl_arg, __old_kernel_stat, oldold_utsname, old_utsname,
   rlimit64, old_timeval32, getcpu_cache, bpf_attr, open_how */
struct iattr;
struct inode;
struct iovec;
struct linux_dirent;
struct linux_dirent64;
struct list_head;
struct new_utsname;
struct pollfd;
struct rlimit;
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
struct sigaltstack;
struct rseq;
struct clone_args;
struct open_how;
struct mount_attr;

#include <linux/types.h>
#include <linux/capability.h>
#include <linux/signal.h>
#include <linux/list.h>
#include <linux/bug.h>
#include <asm/siginfo.h>
#include <linux/unistd.h>
#include <linux/quota.h>
#include <linux/key.h>
#include <linux/personality.h>
#include <linux/fcntl.h>

/* Inlined from trace/syscall.h */
static inline void syscall_tracepoint_update(struct task_struct *p) {}

#include <asm/syscall_wrapper.h>
#include <asm/syscall.h>

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

#define SYSCALL_METADATA(sname, nb, ...)

#ifndef SYSCALL_DEFINE0
#define __MAP0(m,...)
#define __MAP1(m,t,a) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define __SC_ARGS(t, a) t a
#define __SC_DECL(t, a) t a

#define SYSCALL_DEFINE0(sname)					\
	SYSCALL_METADATA(_##sname, 0);				\
	asmlinkage long sys_##sname(void);			\
	ALLOW_ERROR_INJECTION(sys_##sname, ERRNO);		\
	asmlinkage long sys_##sname(void)
#endif  

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
#endif  

#ifdef __LITTLE_ENDIAN
#define SC_ARG64(name) u32, name##_lo, u32, name##_hi
#else
#define SC_ARG64(name) u32, name##_hi, u32, name##_lo
#endif
#define SC_VAL64(type, name) ((type) name##_hi << 32 | name##_lo)

#define SYSCALL32_DEFINE1 SYSCALL_DEFINE1
#define SYSCALL32_DEFINE2 SYSCALL_DEFINE2
#define SYSCALL32_DEFINE3 SYSCALL_DEFINE3
#define SYSCALL32_DEFINE4 SYSCALL_DEFINE4
#define SYSCALL32_DEFINE5 SYSCALL_DEFINE5
#define SYSCALL32_DEFINE6 SYSCALL_DEFINE6

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



void ksys_sync(void);
int ksys_sync_file_range(int fd, loff_t offset, loff_t nbytes,
			 unsigned int flags);
ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count,
		     loff_t pos);
ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos);
int ksys_fallocate(int fd, int mode, loff_t offset, loff_t len);
static inline int ksys_fadvise64_64(int fd, loff_t offset, loff_t len,
				    int advice)
{
	return -EINVAL;
}
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff);
ssize_t ksys_readahead(int fd, loff_t offset, size_t count);

/* do_fchownat declaration removed - made static */

extern long do_sys_ftruncate(unsigned int fd, loff_t length, int small);

static inline long ksys_ftruncate(unsigned int fd, loff_t length)
{
	return do_sys_ftruncate(fd, length, 1);
}


#endif
