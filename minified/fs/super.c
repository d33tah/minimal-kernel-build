// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/super.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  super.c contains code to handle: - mount structures
 *                                   - super-block tables
 *                                   - filesystem drivers list
 *                                   - mount system call
 *                                   - umount system call
 *                                   - ustat system call
 *
 * GK 2/5/95  -  Changed to support mounting the root fs via NFS
 *
 *  Added kerneld support: Jacques Gelinas and Bjorn Ekwall
 *  Added change_root: Werner Almesberger & Hans Lermen, Feb '96
 *  Added options to /proc/mounts:
 *    Torbjörn Lindh (torbjorn.lindh@gopta.se), April 14, 1996.
 *  Added devfs support: Richard Gooch <rgooch@atnf.csiro.au>, 13-JAN-1998
 *  Heavily rewritten for 'one fs - one tree' dcache architecture. AV, Mar 2000
 */

#include <linux/fs.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>

/*
 * Each mount gets its own superblock and nothing is ever unmounted, so
 * there is no superblock list, active count, anon dev number or kill_sb
 * teardown. The only filesystem (ramfs, as rootfs) is mounted once.
 */
int get_tree_nodev(struct fs_context *fc,
		   int (*fill_super)(struct super_block *sb,
				     struct fs_context *fc))
{
	struct super_block *s = kzalloc(sizeof(struct super_block), GFP_USER);
	int err;

	if (!s)
		return -ENOMEM;

	s->s_user_ns = get_user_ns(fc->user_ns);
	INIT_HLIST_BL_HEAD(&s->s_roots);
	s->s_maxbytes = MAX_NON_LFS;

	err = fill_super(s, fc);
	if (err)
		return err;

	fc->root = dget(s->s_root);
	return 0;
}
