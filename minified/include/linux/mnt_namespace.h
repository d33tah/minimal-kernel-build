#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_
#ifdef __KERNEL__

struct mnt_namespace;
struct fs_struct;
struct user_namespace;
struct ns_common;

extern struct mnt_namespace *copy_mnt_ns(unsigned long, struct mnt_namespace *,
		struct user_namespace *, struct fs_struct *);
extern void put_mnt_ns(struct mnt_namespace *ns);
/* from_mnt_ns, proc_mounts_operations, proc_mountinfo_operations, proc_mountstats_operations removed - never called */

#endif
#endif
