/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMFD_H
#define __LINUX_MEMFD_H

#include <linux/file.h>

static inline long memfd_fcntl(struct file *f, unsigned int c, unsigned long a)
{
	return -EINVAL;
}

#endif /* __LINUX_MEMFD_H */
