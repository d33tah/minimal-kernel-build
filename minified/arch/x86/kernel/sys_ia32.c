// SPDX-License-Identifier: GPL-2.0
/*
 * sys_ia32.c: Conversion between 32bit and 64bit native syscalls. Based on
 *             sys_sparc32
 *
 * Copyright (C) 2000		VA Linux Co
 * Copyright (C) 2000		Don Dugger <n0ano@valinux.com>
 * Copyright (C) 1999		Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 1997,1998	Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997		David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 2000		Hewlett-Packard Co.
 * Copyright (C) 2000		David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000,2001,2002	Andi Kleen, SuSE Labs (x86-64 port)
 *
 * These routines maintain argument size conversion between 32bit and 64bit
 * environment. In 2.5 most of this should be moved to a generic directory.
 *
 * This file assumes that there is a hole at the end of user address space.
 *
 * Some of the functions are LE specific currently. These are
 * hopefully all marked.  This should be fixed.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/signal.h>
#include <linux/syscalls.h>
#include <linux/times.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <linux/uio.h>

#include <linux/personality.h>
#include <linux/stat.h>
#include <linux/rwsem.h>
#include <linux/compat.h>
#include <linux/vfs.h>
#include <linux/ptrace.h>
#include <linux/highuid.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <asm/mman.h>
#include <asm/types.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <asm/vgtod.h>
#include <asm/ia32.h>

#define AA(__x)		((unsigned long)(__x))

/* Stub ia32_truncate64 - 32-bit compatibility not needed for minimal kernel */
asmlinkage long sys_ia32_truncate64(const char __user *filename,
				   unsigned long offset_low, unsigned long offset_high)
{
	return -ENOSYS; /* Not implemented */
}

SYSCALL_DEFINE3(ia32_ftruncate64, unsigned int, fd,
		unsigned long, offset_low, unsigned long, offset_high)
{
	return ksys_ftruncate(fd, ((loff_t) offset_high << 32) | offset_low);
}

/* warning: next two assume little endian */
SYSCALL_DEFINE5(ia32_pread64, unsigned int, fd, char __user *, ubuf,
		u32, count, u32, poslo, u32, poshi)
{
	return ksys_pread64(fd, ubuf, count,
			    ((loff_t)AA(poshi) << 32) | AA(poslo));
}

SYSCALL_DEFINE5(ia32_pwrite64, unsigned int, fd, const char __user *, ubuf,
		u32, count, u32, poslo, u32, poshi)
{
	return ksys_pwrite64(fd, ubuf, count,
			     ((loff_t)AA(poshi) << 32) | AA(poslo));
}


/*
 * Some system calls that need sign extended arguments. This could be
 * done by a generic wrapper.
 */
SYSCALL_DEFINE6(ia32_fadvise64_64, int, fd, __u32, offset_low,
		__u32, offset_high, __u32, len_low, __u32, len_high,
		int, advice)
{
	return ksys_fadvise64_64(fd,
				 (((u64)offset_high)<<32) | offset_low,
				 (((u64)len_high)<<32) | len_low,
				 advice);
}

SYSCALL_DEFINE4(ia32_readahead, int, fd, unsigned int, off_lo,
		unsigned int, off_hi, size_t, count)
{
	return ksys_readahead(fd, ((u64)off_hi << 32) | off_lo, count);
}

SYSCALL_DEFINE6(ia32_sync_file_range, int, fd, unsigned int, off_low,
		unsigned int, off_hi, unsigned int, n_low,
		unsigned int, n_hi, int, flags)
{
	return ksys_sync_file_range(fd,
				    ((u64)off_hi << 32) | off_low,
				    ((u64)n_hi << 32) | n_low, flags);
}

SYSCALL_DEFINE5(ia32_fadvise64, int, fd, unsigned int, offset_lo,
		unsigned int, offset_hi, size_t, len, int, advice)
{
	return ksys_fadvise64_64(fd, ((u64)offset_hi << 32) | offset_lo,
				 len, advice);
}

SYSCALL_DEFINE6(ia32_fallocate, int, fd, int, mode,
		unsigned int, offset_lo, unsigned int, offset_hi,
		unsigned int, len_lo, unsigned int, len_hi)
{
	return ksys_fallocate(fd, mode, ((u64)offset_hi << 32) | offset_lo,
			      ((u64)len_hi << 32) | len_lo);
}

