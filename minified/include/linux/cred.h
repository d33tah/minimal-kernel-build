
#ifndef _LINUX_CRED_H
#define _LINUX_CRED_H

#include <linux/capability.h>
#include <linux/init.h>
#include <linux/atomic.h>
#include <linux/uidgid.h>
#include <linux/sched.h>
#include <linux/sched/user.h>

struct cred;
/* struct inode forward decl removed - unused */

struct group_info {
	atomic_t	usage;
	int		ngroups;
	kgid_t		gid[];
} __randomize_layout;

static inline struct group_info *get_group_info(struct group_info *gi)
{
	atomic_inc(&gi->usage);
	return gi;
}

#define put_group_info(group_info)			\
do {							\
	if (atomic_dec_and_test(&(group_info)->usage))	\
		groups_free(group_info);		\
} while (0)

static inline void groups_free(struct group_info *group_info)
{
}

static inline int in_group_p(kgid_t grp)
{
        return 1;
}


struct cred {
	atomic_t	usage;
	kuid_t		uid;
	kgid_t		gid;
	/* suid, sgid removed - write-only, never read */
	kuid_t		euid;
	kgid_t		egid;
	kuid_t		fsuid;
	kgid_t		fsgid;
	/* securebits, cap_inheritable, cap_effective, cap_bset, cap_ambient removed - never read */
	kernel_cap_t	cap_permitted;
	struct user_struct *user;
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct group_info *group_info;

	union {
		int non_rcu;
		struct rcu_head	rcu;
	};
} __randomize_layout;

extern void __put_cred(struct cred *);
extern void exit_creds(struct task_struct *);
extern int copy_creds(struct task_struct *, unsigned long);
extern struct cred *prepare_creds(void);
extern struct cred *prepare_exec_creds(void);
extern int commit_creds(struct cred *);
extern void abort_creds(struct cred *);
extern void __init cred_init(void);
extern int set_cred_ucounts(struct cred *);

/* validate_creds, validate_creds_for_do_exit, validate_process_creds removed - empty stubs, no callers */

static inline struct cred *get_new_cred(struct cred *cred)
{
	atomic_inc(&cred->usage);
	return cred;
}

static inline const struct cred *get_cred(const struct cred *cred)
{
	struct cred *nonconst_cred = (struct cred *) cred;
	if (!cred)
		return cred;
	/* validate_creds call removed - empty stub */
	nonconst_cred->non_rcu = 0;
	return get_new_cred(nonconst_cred);
}

static inline void put_cred(const struct cred *_cred)
{
	struct cred *cred = (struct cred *) _cred;

	if (cred) {
		/* validate_creds call removed - empty stub */
		if (atomic_dec_and_test(&(cred)->usage))
			__put_cred(cred);
	}
}

#define current_cred() \
	rcu_dereference_protected(current->cred, 1)

/* current_real_cred removed - never used */

#define __task_cred(task)	\
	rcu_dereference((task)->real_cred)

#define get_current_cred()				\
	(get_cred(current_cred()))

#define task_cred_xxx(task, xxx)			\
({							\
	__typeof__(((struct cred *)NULL)->xxx) ___val;	\
	rcu_read_lock();				\
	___val = __task_cred((task))->xxx;		\
	rcu_read_unlock();				\
	___val;						\
})

#define task_ucounts(task)	(task_cred_xxx((task), ucounts))

#define current_cred_xxx(xxx)			\
({						\
	current_cred()->xxx;			\
})

#define current_uid()		(current_cred_xxx(uid))
#define current_gid()		(current_cred_xxx(gid))
#define current_euid()		(current_cred_xxx(euid))
#define current_egid()		(current_cred_xxx(egid))
#define current_fsuid() 	(current_cred_xxx(fsuid))
#define current_fsgid() 	(current_cred_xxx(fsgid))
/* current_ucounts removed - never used */
/* init_user_ns extern from uidgid.h */
static inline struct user_namespace *current_user_ns(void)
{
	return &init_user_ns;
}

#endif  
