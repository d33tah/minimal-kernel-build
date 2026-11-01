/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Filesystem superblock creation and reconfiguration context.
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_FS_CONTEXT_H
#define _LINUX_FS_CONTEXT_H

struct cred;
struct dentry;
struct file_system_type;
struct super_block;
struct user_namespace;

/*
 * Filesystem context for holding the parameters used in the creation or
 * reconfiguration of a superblock.
 *
 * Superblock creation fills in ->root whereas reconfiguration begins with this
 * already set.
 *
 * See Documentation/filesystems/mount_api.rst
 */
struct fs_context {
	const struct fs_context_operations *ops;
	struct dentry *root; /* The root and superblock */
	struct user_namespace *user_ns; /* The user namespace for this mount */
	const struct cred *cred; /* The mounter's credentials */
};

struct fs_context_operations {
	int (*get_tree)(struct fs_context *fc);
};

extern struct fs_context *
fs_context_for_mount(struct file_system_type *fs_type);
extern void put_fs_context(struct fs_context *fc);

extern int get_tree_nodev(struct fs_context *fc,
			  int (*fill_super)(struct super_block *sb,
					    struct fs_context *fc));

#endif /* _LINUX_FS_CONTEXT_H */
