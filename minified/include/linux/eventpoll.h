#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H
#include <linux/fcntl.h>
#include <linux/types.h>
#define EPOLL_CLOEXEC O_CLOEXEC
/* EPOLL_CTL_ADD/DEL/MOD removed - unused (epoll stubbed) */
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
struct file;
static inline void eventpoll_release(struct file *file) {}
#endif
