#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H
#include <linux/types.h>
/* EPOLLIN, EPOLLERR, EPOLLHUP, EPOLLRDNORM, EPOLLWRNORM removed - unused */
#define EPOLLOUT	(__force __poll_t)0x00000004
#endif
