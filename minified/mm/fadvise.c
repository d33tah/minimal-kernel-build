// SPDX-License-Identifier: GPL-2.0
/*
 * mm/fadvise.c
 *
 * Copyright (C) 2002, Linus Torvalds
 *
 * 11Jan2003	Andrew Morton
 *		Initial version.
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include <linux/pagevec.h>
#include <linux/fadvise.h>
#include <linux/writeback.h>
#include <linux/syscalls.h>
#include <linux/swap.h>

#include <asm/unistd.h>

#include "internal.h"

/*
 * POSIX_FADV_WILLNEED could set PG_Referenced, and POSIX_FADV_NOREUSE could
 * deactivate the pages and clear PG_Referenced.
 */

int generic_fadvise(struct file *file, loff_t offset, loff_t len, int advice)
{
	/* Stubbed for minimization - just validate advice and return success */
	switch (advice) {
	case POSIX_FADV_NORMAL:
	case POSIX_FADV_RANDOM:
	case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_WILLNEED:
	case POSIX_FADV_NOREUSE:
	case POSIX_FADV_DONTNEED:
		return 0;
	default:
		return -EINVAL;
	}
}

int vfs_fadvise(struct file *file, loff_t offset, loff_t len, int advice)
{
	if (file->f_op->fadvise)
		return file->f_op->fadvise(file, offset, len, advice);

	return generic_fadvise(file, offset, len, advice);
}

