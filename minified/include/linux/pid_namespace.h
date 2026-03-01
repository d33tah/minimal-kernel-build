#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H
#include <linux/bug.h>
#include <linux/nsproxy.h>
#include <linux/ns_common.h>
#include <linux/idr.h>
struct pid_namespace {
	struct idr idr;
	unsigned int pid_allocated;
	struct kmem_cache *pid_cachep;
} __randomize_layout;
extern struct pid_namespace init_pid_ns;
#define PIDNS_ADDING (1U << 31)
#include <linux/err.h>
static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns) { return ns; }
void pid_idr_init(void);
#endif
