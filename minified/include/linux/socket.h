
#ifndef _LINUX_SOCKET_H
#define _LINUX_SOCKET_H

#include <asm/socket.h>
#include <linux/sockios.h>
#include <linux/uio.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <uapi/linux/socket.h>

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
