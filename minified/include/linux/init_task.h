#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/seqlock.h>

#define SECUREBITS_DEFAULT 0x00000000
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <net/net_namespace.h>
#include <linux/sched/rt.h>
#include <linux/mm_types.h>

/* Inlined from livepatch.h - CONFIG_LIVEPATCH not set */
static inline void klp_update_patch_state(struct task_struct *task) {}
static inline void klp_copy_process(struct task_struct *child) {}

#include <asm/thread_info.h>

extern struct files_struct init_files;
extern struct fs_struct init_fs;
extern struct nsproxy init_nsproxy;
extern struct cred init_cred;

#define INIT_PREV_CPUTIME(x)	.prev_cputime = {			\
	.lock = __RAW_SPIN_LOCK_UNLOCKED(x.prev_cputime.lock),		\
},

#define INIT_TASK_COMM "swapper"

#define __init_task_data  

#define __init_thread_info __section(".data..init_thread_info")

#endif
