#ifndef _LINUX__INIT_TASK_H
#define _LINUX__INIT_TASK_H

#include <linux/rcupdate.h>
#include <linux/irqflags.h>
#include <linux/utsname.h>
#include <linux/lockdep.h>
#include <linux/ipc.h>
#include <linux/pid_namespace.h>
#include <linux/user_namespace.h>
#include <linux/seqlock.h>

/* From uapi/linux/securebits.h - inlined */
#define issecure_mask(X)	(1 << (X))
#define SECUREBITS_DEFAULT 0x00000000
#define SECURE_NOROOT			0
#define SECURE_NOROOT_LOCKED		1
#define SECBIT_NOROOT		(issecure_mask(SECURE_NOROOT))
#define SECBIT_NOROOT_LOCKED	(issecure_mask(SECURE_NOROOT_LOCKED))
#define SECURE_NO_SETUID_FIXUP		2
#define SECURE_NO_SETUID_FIXUP_LOCKED	3
#define SECBIT_NO_SETUID_FIXUP	(issecure_mask(SECURE_NO_SETUID_FIXUP))
#define SECBIT_NO_SETUID_FIXUP_LOCKED \
			(issecure_mask(SECURE_NO_SETUID_FIXUP_LOCKED))
#define SECURE_KEEP_CAPS		4
#define SECURE_KEEP_CAPS_LOCKED		5
#define SECBIT_KEEP_CAPS	(issecure_mask(SECURE_KEEP_CAPS))
#define SECBIT_KEEP_CAPS_LOCKED (issecure_mask(SECURE_KEEP_CAPS_LOCKED))
#define SECURE_NO_CAP_AMBIENT_RAISE		6
#define SECURE_NO_CAP_AMBIENT_RAISE_LOCKED	7
#define SECBIT_NO_CAP_AMBIENT_RAISE (issecure_mask(SECURE_NO_CAP_AMBIENT_RAISE))
#define SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED \
			(issecure_mask(SECURE_NO_CAP_AMBIENT_RAISE_LOCKED))
#define SECURE_ALL_BITS		(issecure_mask(SECURE_NOROOT) | \
				 issecure_mask(SECURE_NO_SETUID_FIXUP) | \
				 issecure_mask(SECURE_KEEP_CAPS) | \
				 issecure_mask(SECURE_NO_CAP_AMBIENT_RAISE))
#define SECURE_ALL_LOCKS	(SECURE_ALL_BITS << 1)
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <net/net_namespace.h>
#include <linux/sched/rt.h>
#include <linux/mm_types.h>

/* Inlined from livepatch.h - CONFIG_LIVEPATCH not set */
static inline bool klp_patch_pending(struct task_struct *task) { return false; }
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
