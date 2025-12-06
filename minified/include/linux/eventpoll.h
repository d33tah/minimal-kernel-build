#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <linux/fcntl.h>
#include <linux/types.h>

/* Inlined from uapi/linux/eventpoll.h */
#define EPOLL_CLOEXEC O_CLOEXEC
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

#define EPOLLIN		(__force __poll_t)0x00000001
#define EPOLLPRI	(__force __poll_t)0x00000002
#define EPOLLOUT	(__force __poll_t)0x00000004
#define EPOLLERR	(__force __poll_t)0x00000008
#define EPOLLHUP	(__force __poll_t)0x00000010
#define EPOLLNVAL	(__force __poll_t)0x00000020
#define EPOLLRDNORM	(__force __poll_t)0x00000040
#define EPOLLRDBAND	(__force __poll_t)0x00000080
#define EPOLLWRNORM	(__force __poll_t)0x00000100
#define EPOLLWRBAND	(__force __poll_t)0x00000200
#define EPOLLMSG	(__force __poll_t)0x00000400
#define EPOLLRDHUP	(__force __poll_t)0x00002000
#define EPOLLEXCLUSIVE	((__force __poll_t)(1U << 28))
#define EPOLLWAKEUP	((__force __poll_t)(1U << 29))
#define EPOLLONESHOT	((__force __poll_t)(1U << 30))
#define EPOLLET		((__force __poll_t)(1U << 31))

struct epoll_event {
	__poll_t events;
	__u64 data;
};

static inline void ep_take_care_of_epollwakeup(struct epoll_event *epev)
{
	epev->events &= ~EPOLLWAKEUP;
}

struct file;



static inline void eventpoll_release(struct file *file) {}


static inline struct epoll_event __user *
epoll_put_uevent(__poll_t revents, __u64 data,
		 struct epoll_event __user *uevent)
{
	if (__put_user(revents, &uevent->events) ||
	    __put_user(data, &uevent->data))
		return NULL;

	return uevent+1;
}

#endif  
