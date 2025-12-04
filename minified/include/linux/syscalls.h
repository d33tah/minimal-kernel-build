
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
#include <linux/fcntl.h>
#include <trace/syscall.h>

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
/* is_syscall_trace_event removed - unused */

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
static inline int ksys_fadvise64_64(int fd, loff_t offset, loff_t len,
				    int advice)
{
	return -EINVAL;
}
unsigned long ksys_mmap_pgoff(unsigned long addr, unsigned long len,
			      unsigned long prot, unsigned long flags,
			      unsigned long fd, unsigned long pgoff);
ssize_t ksys_readahead(int fd, loff_t offset, size_t count);
/* ksys_ipc and compat_ksys_ipc removed - unused */

extern int do_fchownat(int dfd, const char __user *filename, uid_t user,
		       gid_t group, int flag);

/* ksys_chown, ksys_lchown removed - unused */

extern long do_sys_ftruncate(unsigned int fd, loff_t length, int small);

static inline long ksys_ftruncate(unsigned int fd, loff_t length)
{
	return do_sys_ftruncate(fd, length, 1);
}

/* ksys_truncate, ksys_personality removed - unused */

/* IPC ksys_* declarations removed - unused */

int __sys_getsockopt(int fd, int level, int optname, char __user *optval,
		int __user *optlen);
int __sys_setsockopt(int fd, int level, int optname, char __user *optval,
		int optlen);
#endif
