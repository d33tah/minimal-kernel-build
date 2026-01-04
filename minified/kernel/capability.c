#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/capability.h>
#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/fs.h>
/* __cap_empty_set removed - never used (cap_clear macro never called) */
/* ns_capable always returns true - simplified */
bool capable_wrt_inode_uidgid(struct user_namespace *mnt_userns,
			      const struct inode *inode, int cap)
{
	struct user_namespace *ns = current_user_ns();
	return kuid_has_mapping(ns, i_uid_into_mnt(mnt_userns, inode)) &&
	       kgid_has_mapping(ns, i_gid_into_mnt(mnt_userns, inode));
}
