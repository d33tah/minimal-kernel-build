#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H
#include <linux/types.h>
#define EPOLLIN		(__force __poll_t)0x00000001
#define EPOLLOUT	(__force __poll_t)0x00000004
#define EPOLLERR	(__force __poll_t)0x00000008
#define EPOLLHUP	(__force __poll_t)0x00000010
#define EPOLLRDNORM	(__force __poll_t)0x00000040
#define EPOLLWRNORM	(__force __poll_t)0x00000100
struct file;
static inline void eventpoll_release(struct file *file) {}
#endif
