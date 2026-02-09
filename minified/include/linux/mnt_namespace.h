#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_
#ifdef __KERNEL__
struct mnt_namespace;
/* copy_mnt_ns removed - create_new_namespaces no longer called */
extern void put_mnt_ns(struct mnt_namespace *ns);
#endif
#endif
