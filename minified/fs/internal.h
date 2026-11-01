/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fs/ internal definitions
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

struct path;

extern void __init mnt_init(void);

/*
 * file_table.c
 */
extern struct file *alloc_empty_file(int, const struct cred *);

/*
 * open.c
 */
struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode, lookup_flags;
};
extern struct file *do_filp_open(struct filename *pathname,
				 const struct open_flags *op);

extern int vfs_open(const struct path *, struct file *);
