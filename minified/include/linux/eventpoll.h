/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  include/linux/eventpoll.h ( Efficient event polling implementation )
 *  Copyright (C) 2001,...,2006	 Davide Libenzi
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 */
#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <uapi/linux/eventpoll.h>
#include <uapi/linux/kcmp.h>


/* Forward declarations to avoid compiler errors */
struct file;



static inline void eventpoll_release(struct file *file) {}


#if defined(CONFIG_ARM) && defined(CONFIG_OABI_COMPAT)
/* ARM OABI has an incompatible struct layout and needs a special handler */
extern struct epoll_event __user *
epoll_put_uevent(__poll_t revents, __u64 data,
		 struct epoll_event __user *uevent);
#else
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

#endif /* #ifndef _LINUX_EVENTPOLL_H */
