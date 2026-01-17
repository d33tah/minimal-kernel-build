#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/seqlock.h>

/* SECUREBITS_DEFAULT removed - never used */
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <net/net_namespace.h>
#include <linux/sched/rt.h>
#include <linux/mm_types.h>

#include <asm/thread_info.h>

extern struct files_struct init_files;
extern struct fs_struct init_fs;
extern struct nsproxy init_nsproxy;
extern struct cred init_cred;

/* INIT_PREV_CPUTIME removed - prev_cputime removed from task_struct */

#define INIT_TASK_COMM "swapper"

/* __init_task_data, __init_thread_info removed - unused */

#endif
