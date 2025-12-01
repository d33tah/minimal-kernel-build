#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <uapi/linux/eventpoll.h>



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
