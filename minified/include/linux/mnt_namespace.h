#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_
#ifdef __KERNEL__
struct mnt_namespace;
struct fs_struct;
struct user_namespace;
struct ns_common;
extern struct mnt_namespace *copy_mnt_ns(unsigned long, struct mnt_namespace *, struct user_namespace *, struct fs_struct *);
extern void put_mnt_ns(struct mnt_namespace *ns);
#endif
#endif
