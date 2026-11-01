/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

struct dentry;
struct user_namespace;

struct vfsmount {
	struct dentry *mnt_root; /* root of the mounted tree */
	struct user_namespace *mnt_userns;
} __randomize_layout;

static inline struct user_namespace *mnt_user_ns(const struct vfsmount *mnt)
{
	/* Pairs with smp_store_release() in do_idmap_mount(). */
	return smp_load_acquire(&mnt->mnt_userns);
}

#endif /* _LINUX_MOUNT_H */
