/* Minimal compat.h - COMPAT_32BIT enabled but most code not used */
#ifndef _LINUX_COMPAT_H
#define _LINUX_COMPAT_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/stat.h>
#include <linux/param.h>
#include <linux/sem.h>
#include <linux/uio.h>
#include <linux/compiler.h>

/* --- Inlined from socket.h (2025-12-08 02:05) --- */
#define _K_SS_MAXSIZE	128

typedef unsigned short __kernel_sa_family_t;

struct __kernel_sockaddr_storage {
	union {
		struct {
			__kernel_sa_family_t	ss_family;
			char __data[_K_SS_MAXSIZE - sizeof(unsigned short)];
		};
		void *__align;
	};
};

#define SOCK_SNDBUF_LOCK	1
#define SOCK_RCVBUF_LOCK	2
#define SOCK_BUF_LOCK_MASK (SOCK_SNDBUF_LOCK | SOCK_RCVBUF_LOCK)
#define SOCK_TXREHASH_DEFAULT	255
#define SOCK_TXREHASH_DISABLED	0
#define SOCK_TXREHASH_ENABLED	1

struct file;
struct pid;
struct cred;
struct socket;

typedef __kernel_sa_family_t	sa_family_t;

struct sockaddr {
	sa_family_t	sa_family;
	char		sa_data[14];
};

struct linger {
	int		l_onoff;
	int		l_linger;
};

#define sockaddr_storage __kernel_sockaddr_storage

struct msghdr {
	void		*msg_name;
	int		msg_namelen;
	int		msg_inq;
	struct iov_iter	msg_iter;
	union {
		void		*msg_control;
		void __user	*msg_control_user;
	};
	bool		msg_control_is_user : 1;
	bool		msg_get_inq : 1;
	unsigned int	msg_flags;
	__kernel_size_t	msg_controllen;
	struct kiocb	*msg_iocb;
};

struct user_msghdr {
	void		__user *msg_name;
	int		msg_namelen;
	struct iovec	__user *msg_iov;
	__kernel_size_t	msg_iovlen;
	void		__user *msg_control;
	__kernel_size_t	msg_controllen;
	unsigned int	msg_flags;
};

struct mmsghdr {
	struct user_msghdr  msg_hdr;
	unsigned int        msg_len;
};

struct cmsghdr {
	__kernel_size_t	cmsg_len;
        int		cmsg_level;
        int		cmsg_type;
};

struct ucred {
	__u32	pid;
	__u32	uid;
	__u32	gid;
};

struct scm_timestamping_internal {
	struct timespec64 ts[3];
};
/* --- End inlined socket.h --- */

#include <linux/fs.h>
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

/* Forward declarations */
struct compat_iovec;
struct compat_sigaltstack;
struct compat_tms;
struct compat_sigaction;
struct compat_siginfo;
struct compat_rlimit;

#ifndef compat_sigset_t
typedef struct {
	compat_sigset_word	sig[_COMPAT_NSIG_WORDS];
} compat_sigset_t;
#endif

#define _COMPAT_NSIG_WORDS	(_COMPAT_NSIG / _COMPAT_NSIG_BPW)

#ifndef compat_user_stack_pointer
#define compat_user_stack_pointer() current_user_stack_pointer()
#endif
#ifndef COMPAT_MINSIGSTKSZ
#define COMPAT_MINSIGSTKSZ	MINSIGSTKSZ
#endif

typedef __compat_uid32_t	compat_uid_t;
typedef __compat_gid32_t	compat_gid_t;

#define is_compat_task() (0)
#define in_compat_syscall in_compat_syscall
static inline bool in_compat_syscall(void) { return false; }

#define BITS_PER_COMPAT_LONG    (8*sizeof(compat_long_t))
#define BITS_TO_COMPAT_LONGS(bits) DIV_ROUND_UP(bits, BITS_PER_COMPAT_LONG)

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
