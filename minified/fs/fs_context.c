// SPDX-License-Identifier: GPL-2.0-or-later
/* Provide a way to create a superblock configuration context within the kernel
 * that allows a superblock to be set up prior to mounting.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/user_namespace.h>

/* No mount options are ever passed: there is no option parser. */
struct fs_context *fs_context_for_mount(struct file_system_type *fs_type)
{
	struct fs_context *fc;
	int ret;

	fc = kzalloc(sizeof(struct fs_context), GFP_KERNEL_ACCOUNT);
	if (!fc)
		return ERR_PTR(-ENOMEM);

	fc->cred = get_current_cred();
	fc->user_ns = get_user_ns(fc->cred->user_ns);

	ret = fs_type->init_fs_context(fc);
	if (ret < 0) {
		put_fs_context(fc);
		return ERR_PTR(ret);
	}
	return fc;
}

/**
 * put_fs_context - Dispose of a superblock configuration context.
 * @fc: The context to dispose of.
 */
void put_fs_context(struct fs_context *fc)
{
	dput(fc->root);
	put_cred(fc->cred);
	kfree(fc);
}
