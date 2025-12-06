
#ifndef _LINUX_SOCKET_H
#define _LINUX_SOCKET_H

#include <asm/socket.h>
/* sockios.h removed - network socket ioctls not used */
#include <linux/uio.h>
#include <linux/types.h>
#include <linux/compiler.h>

/* From uapi/linux/socket.h - inlined */
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

#endif
