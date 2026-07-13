#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <linux/types.h>

/* Inlined from uapi/linux/eventpoll.h (only EPOLLOUT is used by poll users) */
#define EPOLLOUT	(__force __poll_t)0x00000004

#endif
