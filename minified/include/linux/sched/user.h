 
#ifndef _LINUX_SCHED_USER_H
#define _LINUX_SCHED_USER_H

#include <linux/uidgid.h>
#include <linux/atomic.h>
#include <linux/percpu_counter.h>
#include <linux/refcount.h>
#include <linux/ratelimit.h>

 
struct user_struct {
	refcount_t __count;	 
	unsigned long unix_inflight;	 
	atomic_long_t pipe_bufs;   

	 
	struct hlist_node uidhash_node;
	kuid_t uid;

#if defined(CONFIG_PERF_EVENTS) || defined(CONFIG_BPF_SYSCALL) || \
    defined(CONFIG_NET) || defined(CONFIG_IO_URING)
	atomic_long_t locked_vm;
#endif

	 
	struct ratelimit_state ratelimit;
};

extern int uids_sysfs_init(void);

extern struct user_struct *find_user(kuid_t);

extern struct user_struct root_user;
#define INIT_USER (&root_user)


 
extern struct user_struct * alloc_uid(kuid_t);
static inline struct user_struct *get_uid(struct user_struct *u)
{
	refcount_inc(&u->__count);
	return u;
}
extern void free_uid(struct user_struct *);

#endif  
